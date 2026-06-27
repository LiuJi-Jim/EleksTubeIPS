#include "LiveImageApi.h"

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

void sendError(AsyncWebServerRequest *request, int code, const char *error, const char *message) {
    String body = "{\"ok\":false,\"error\":\"";
    body += error;
    body += "\",\"message\":\"";
    body += message;
    body += "\"}";
    request->send(code, "application/json", body);
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

void sendPreset(AsyncWebServerRequest *request) {
    String body = "{\"ok\":true,\"preset\":\"";
    body += IPSClock::getDisplayPresetName();
    body += "\",\"time_or_date\":\"";
    body += IPSClock::getTimeOrDateName();
    body += "\",\"four_digit_display\":\"";
    body += IPSClock::getFourDigitDisplayName();
    body += "\"}";
    request->send(200, "application/json", body);
}

void handleGetSlots(AsyncWebServerRequest *request) {
    String body = "{\"ok\":true,\"slots\":[";
    for (uint8_t slot = 0; slot < IPSClock::LIVE_SLOT_COUNT; slot++) {
        if (slot != 0) {
            body += ',';
        }
        String path = IPSClock::getLiveSlotPath(slot);
        bool exists = LittleFS.exists(path);
        body += "{\"slot\":";
        body += String(slot);
        body += ",\"exists\":";
        body += exists ? "true" : "false";
        body += ",\"size\":";
        body += String(exists ? fileSize(path) : 0);
        body += ",\"version\":";
        body += String(IPSClock::getLiveSlotVersion(slot));
        body += '}';
    }
    body += "]}";
    request->send(200, "application/json", body);
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

    String body = "{\"ok\":true,\"slot\":";
    body += String(slot);
    body += ",\"exists\":false,\"version\":";
    body += String(IPSClock::getLiveSlotVersion(slot));
    body += '}';
    request->send(200, "application/json", body);
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

    String body = "{\"ok\":true,\"slot\":";
    body += String(slot);
    body += ",\"size\":";
    body += String(uploadWritten[slot]);
    body += ",\"version\":";
    body += String(IPSClock::getLiveSlotVersion(slot));
    body += '}';
    request->send(200, "application/json", body);
}

void handleSetPresetBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index + len < total) {
        return;
    }

    String body;
    for (size_t i = 0; i < len; i++) {
        body += static_cast<char>(data[i]);
    }

    const char *preset = nullptr;
    if (body.indexOf("HHMM_WITH_TWO_LIVE_IMAGES") >= 0) {
        preset = "HHMM_WITH_TWO_LIVE_IMAGES";
    } else if (body.indexOf("SIX_LIVE_IMAGES") >= 0) {
        preset = "SIX_LIVE_IMAGES";
    } else if (body.indexOf("TIME_SIX") >= 0) {
        preset = "TIME_SIX";
    } else if (body.indexOf("TIME_FOUR_WITH_WEATHER") >= 0) {
        preset = "TIME_FOUR_WITH_WEATHER";
    } else if (body.indexOf("TIME_FOUR_WITH_SLIDESHOW") >= 0) {
        preset = "TIME_FOUR_WITH_SLIDESHOW";
    } else if (body.indexOf("TIME_FOUR") >= 0) {
        preset = "TIME_FOUR";
    } else if (body.indexOf("DATE") >= 0) {
        preset = "DATE";
    } else if (body.indexOf("WEATHER") >= 0) {
        preset = "WEATHER";
    } else if (body.indexOf("SLIDE_SHOW") >= 0 || body.indexOf("SLIDESHOW") >= 0) {
        preset = "SLIDE_SHOW";
    }

    if (preset == nullptr || !IPSClock::setDisplayPreset(String(preset))) {
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

    sendPreset(request);
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
    server->on("/api/display/preset", HTTP_GET, sendPreset);
    server->on("/api/display/preset", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr, handleSetPresetBody);
}

void initVariant() {
    configureLiveImageApi(server);
}
