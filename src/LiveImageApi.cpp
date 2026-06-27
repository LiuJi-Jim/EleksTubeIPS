#include "LiveImageApi.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "IPSClock.h"
#include "TFTs.h"

extern AsyncWebServer *server;
extern void broadcastUpdate(const BaseConfigItem& item);
extern void broadcastFSChange();

namespace {
const size_t MAX_LIVE_IMAGE_BYTES = 256 * 1024;

bool uploadInProgress[IPSClock::LIVE_SLOT_COUNT] = {false, false, false, false, false, false};
bool uploadFailed[IPSClock::LIVE_SLOT_COUNT] = {false, false, false, false, false, false};
size_t uploadWritten[IPSClock::LIVE_SLOT_COUNT] = {0, 0, 0, 0, 0, 0};

bool parseSlot(AsyncWebServerRequest *request, uint8_t &slot) {
    if (request->pathArg(0).length() == 0) {
        return false;
    }
    int parsed = request->pathArg(0).toInt();
    if (parsed < 0 || parsed >= IPSClock::LIVE_SLOT_COUNT) {
        return false;
    }
    slot = static_cast<uint8_t>(parsed);
    return true;
}

void sendJson(AsyncWebServerRequest *request, int code, JsonDocument &doc) {
    String body;
    serializeJson(doc, body);
    request->send(code, "application/json", body);
}

void sendError(AsyncWebServerRequest *request, int code, const char *error, const char *message) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = error;
    doc["message"] = message;
    sendJson(request, code, doc);
}

size_t fileSize(const String& path) {
    fs::File file = LittleFS.open(path, "r");
    if (!file) {
        return 0;
    }
    size_t size = file.size();
    file.close();
    return size;
}

bool isBmpHeaderValid(const String& path) {
    fs::File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }

    uint8_t header[30];
    size_t readLen = file.read(header, sizeof(header));
    file.close();
    if (readLen < sizeof(header)) {
        return false;
    }

    if (header[0] != 'B' || header[1] != 'M') {
        return false;
    }

    int32_t width = static_cast<int32_t>(header[18]) |
                    (static_cast<int32_t>(header[19]) << 8) |
                    (static_cast<int32_t>(header[20]) << 16) |
                    (static_cast<int32_t>(header[21]) << 24);
    int32_t height = static_cast<int32_t>(header[22]) |
                     (static_cast<int32_t>(header[23]) << 8) |
                     (static_cast<int32_t>(header[24]) << 16) |
                     (static_cast<int32_t>(header[25]) << 24);
    uint16_t planes = static_cast<uint16_t>(header[26]) |
                      (static_cast<uint16_t>(header[27]) << 8);
    uint16_t bitDepth = static_cast<uint16_t>(header[28]) |
                        (static_cast<uint16_t>(header[29]) << 8);

    if (planes != 1) {
        return false;
    }
    int32_t absoluteHeight = height < 0 ? -height : height;
    if (absoluteHeight != TFT_HEIGHT || width != TFT_WIDTH) {
        return false;
    }
    return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8 || bitDepth == 16 || bitDepth == 24;
}

void invalidateLiveDisplay(uint8_t slot) {
    IPSClock::markLiveSlotDirty(slot);
    if (tfts == nullptr) {
        return;
    }

    bool visible = IPSClock::getTimeOrDate().value == IPSClock::LIVE_IMAGES ||
        (IPSClock::getTimeOrDate().value == IPSClock::TIME &&
         IPSClock::getFourDigitDisplay().value == IPSClock::FOUR_WITH_TWO_LIVE_IMAGES &&
         slot >= 4);

    if (visible) {
        tfts->claim();
        tfts->invalidateAllDigits();
        tfts->release();
    }
}

void handleGetSlots(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["ok"] = true;
    JsonArray slots = doc["slots"].to<JsonArray>();
    for (uint8_t slot = 0; slot < IPSClock::LIVE_SLOT_COUNT; slot++) {
        String path = IPSClock::getLiveSlotPath(slot);
        JsonObject item = slots.add<JsonObject>();
        item["slot"] = slot;
        bool exists = LittleFS.exists(path);
        item["exists"] = exists;
        item["size"] = exists ? fileSize(path) : 0;
        item["version"] = IPSClock::getLiveSlotVersion(slot);
    }
    sendJson(request, 200, doc);
}

void handleGetImage(AsyncWebServerRequest *request) {
    uint8_t slot;
    if (!parseSlot(request, slot)) {
        sendError(request, 400, "invalid_slot", "slot must be 0..5");
        return;
    }

    String path = IPSClock::getLiveSlotPath(slot);
    if (!LittleFS.exists(path)) {
        sendError(request, 404, "not_found", "live image slot is empty");
        return;
    }

    request->send(LittleFS, path, "image/bmp");
}

void handleDeleteImage(AsyncWebServerRequest *request) {
    uint8_t slot;
    if (!parseSlot(request, slot)) {
        sendError(request, 400, "invalid_slot", "slot must be 0..5");
        return;
    }

    String path = IPSClock::getLiveSlotPath(slot);
    String cachePath = IPSClock::getLiveSlotCachePath(slot);
    LittleFS.remove(path);
    LittleFS.remove(cachePath);
    invalidateLiveDisplay(slot);
    broadcastFSChange();

    JsonDocument doc;
    doc["ok"] = true;
    doc["slot"] = slot;
    doc["exists"] = false;
    doc["version"] = IPSClock::getLiveSlotVersion(slot);
    sendJson(request, 200, doc);
}

void handleUploadBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    uint8_t slot;
    if (!parseSlot(request, slot)) {
        sendError(request, 400, "invalid_slot", "slot must be 0..5");
        return;
    }

    String tmpPath = String(IPSClock::getLiveImageDir()) + "/" + String(slot) + ".tmp";
    String finalPath = IPSClock::getLiveSlotPath(slot);

    if (index == 0) {
        IPSClock::ensureLiveImageDir(LittleFS);
        uploadInProgress[slot] = true;
        uploadFailed[slot] = false;
        uploadWritten[slot] = 0;
        LittleFS.remove(tmpPath);
    }

    if (!uploadInProgress[slot]) {
        uploadFailed[slot] = true;
    }

    if (!uploadFailed[slot]) {
        if (uploadWritten[slot] + len > MAX_LIVE_IMAGE_BYTES) {
            uploadFailed[slot] = true;
        } else {
            fs::File file = LittleFS.open(tmpPath, index == 0 ? "w" : "a", true);
            if (!file) {
                uploadFailed[slot] = true;
            } else {
                if (len > 0 && file.write(data, len) != len) {
                    uploadFailed[slot] = true;
                }
                file.close();
            }
        }
    }

    uploadWritten[slot] += len;

    if (index + len < total) {
        return;
    }

    bool ok = !uploadFailed[slot] && isBmpHeaderValid(tmpPath);
    uploadInProgress[slot] = false;

    if (!ok) {
        LittleFS.remove(tmpPath);
        sendError(request, 400, "invalid_bmp", "expected 135x240 BMP under 256KB");
        return;
    }

    LittleFS.remove(finalPath);
    if (!LittleFS.rename(tmpPath, finalPath)) {
        LittleFS.remove(tmpPath);
        sendError(request, 500, "rename_failed", "failed to replace live image");
        return;
    }

    invalidateLiveDisplay(slot);
    broadcastFSChange();

    JsonDocument doc;
    doc["ok"] = true;
    doc["slot"] = slot;
    doc["size"] = uploadWritten[slot];
    doc["version"] = IPSClock::getLiveSlotVersion(slot);
    sendJson(request, 200, doc);
}

void handleGetPreset(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["ok"] = true;
    doc["preset"] = IPSClock::getDisplayPresetName();
    doc["time_or_date"] = IPSClock::getTimeOrDateName();
    doc["four_digit_display"] = IPSClock::getFourDigitDisplayName();
    sendJson(request, 200, doc);
}

void handleSetPresetBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index + len < total) {
        return;
    }

    JsonDocument body;
    DeserializationError error = deserializeJson(body, data, len);
    if (error || !body["preset"].is<const char*>()) {
        sendError(request, 400, "invalid_json", "expected JSON body with preset");
        return;
    }

    String preset = body["preset"].as<String>();
    if (!IPSClock::setDisplayPreset(preset)) {
        sendError(request, 400, "invalid_preset", "unknown display preset");
        return;
    }

    broadcastUpdate(IPSClock::getTimeOrDate());
    broadcastUpdate(IPSClock::getFourDigitDisplay());
    IPSClock::getTimeOrDate().notify();
    IPSClock::getFourDigitDisplay().notify();
    if (tfts != nullptr) {
        tfts->claim();
        tfts->invalidateAllDigits();
        tfts->release();
    }

    JsonDocument doc;
    doc["ok"] = true;
    doc["preset"] = IPSClock::getDisplayPresetName();
    doc["time_or_date"] = IPSClock::getTimeOrDateName();
    doc["four_digit_display"] = IPSClock::getFourDigitDisplayName();
    sendJson(request, 200, doc);
}
}

void configureLiveImageApi(AsyncWebServer *server) {
    if (server == nullptr) {
        return;
    }
    server->on("/api/live/slots", HTTP_GET, handleGetSlots);
    server->on("^\\/api\\/live\\/slots\\/([0-5])\\/image$", HTTP_GET, handleGetImage);
    server->on("^\\/api\\/live\\/slots\\/([0-5])\\/image$", HTTP_DELETE, handleDeleteImage);
    server->on("^\\/api\\/live\\/slots\\/([0-5])\\/image$", HTTP_PUT, [](AsyncWebServerRequest *request) {}, nullptr, handleUploadBody);
    server->on("^\\/api\\/live\\/slots\\/([0-5])\\/image$", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr, handleUploadBody);
    server->on("/api/display/preset", HTTP_GET, handleGetPreset);
    server->on("/api/display/preset", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr, handleSetPresetBody);
}

void initVariant() {
    configureLiveImageApi(server);
}
