#include "LiveImageApi.h"

#include <LittleFS.h>

#include "IPSClock.h"
#include "TFTs.h"

extern void broadcastUpdate(const BaseConfigItem& item);
extern void broadcastFSChange();

namespace {
const size_t MAX_LIVE_IMAGE_BYTES = 256 * 1024;
bool uploadFailed = false;
uint32_t uploadWritten = 0;
uint8_t uploadSlot = 255;

bool parseSlot(AsyncWebServerRequest *request, uint8_t &slot) {
    int parsed = request->pathArg(0).toInt();
    if (parsed < 0 || parsed >= IPSClock::LIVE_SLOT_COUNT) {
        return false;
    }
    slot = static_cast<uint8_t>(parsed);
    return true;
}

void sendError(AsyncWebServerRequest *request, int code, const char *error) {
    String body = "{\"ok\":false,\"error\":\"";
    body += error;
    body += "\"}";
    request->send(code, "application/json", body);
}

bool isBmpHeaderValid(const String& path) {
    fs::File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }

    uint8_t h[30];
    size_t readLen = file.read(h, sizeof(h));
    file.close();
    if (readLen < sizeof(h) || h[0] != 'B' || h[1] != 'M') {
        return false;
    }

    int32_t width = static_cast<int32_t>(h[18]) | (static_cast<int32_t>(h[19]) << 8) |
                    (static_cast<int32_t>(h[20]) << 16) | (static_cast<int32_t>(h[21]) << 24);
    int32_t height = static_cast<int32_t>(h[22]) | (static_cast<int32_t>(h[23]) << 8) |
                     (static_cast<int32_t>(h[24]) << 16) | (static_cast<int32_t>(h[25]) << 24);
    uint16_t planes = static_cast<uint16_t>(h[26]) | (static_cast<uint16_t>(h[27]) << 8);
    uint16_t bitDepth = static_cast<uint16_t>(h[28]) | (static_cast<uint16_t>(h[29]) << 8);
    height = height < 0 ? -height : height;

    return planes == 1 && width == TFT_WIDTH && height == TFT_HEIGHT &&
           (bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8 || bitDepth == 16 || bitDepth == 24);
}

void invalidateLiveDisplay(uint8_t slot) {
    IPSClock::markLiveSlotDirty(slot);
    if (tfts == nullptr) {
        return;
    }
    if (IPSClock::getTimeOrDate().value == IPSClock::LIVE_IMAGES ||
        (IPSClock::getTimeOrDate().value == IPSClock::TIME &&
         IPSClock::getFourDigitDisplay().value == IPSClock::FOUR_WITH_TWO_LIVE_IMAGES && slot >= 4)) {
        tfts->claim();
        tfts->invalidateAllDigits();
        tfts->release();
    }
}

void sendPreset(AsyncWebServerRequest *request) {
    String body = "{\"ok\":true,\"time_or_date\":";
    body += String(IPSClock::getTimeOrDate().value);
    body += ",\"four_digit_display\":";
    body += String(IPSClock::getFourDigitDisplay().value);
    body += '}';
    request->send(200, "application/json", body);
}

void handleGetSlots(AsyncWebServerRequest *request) {
    String body = "{\"ok\":true,\"slots\":[";
    for (uint8_t slot = 0; slot < IPSClock::LIVE_SLOT_COUNT; slot++) {
        if (slot != 0) body += ',';
        body += "{\"slot\":";
        body += String(slot);
        body += ",\"exists\":";
        body += LittleFS.exists(IPSClock::getLiveSlotPath(slot)) ? "true" : "false";
        body += ",\"version\":";
        body += String(IPSClock::getLiveSlotVersion(slot));
        body += '}';
    }
    body += "]}";
    request->send(200, "application/json", body);
}

void handleDeleteImage(AsyncWebServerRequest *request) {
    uint8_t slot;
    if (!parseSlot(request, slot)) {
        sendError(request, 400, "invalid_slot");
        return;
    }

    LittleFS.remove(IPSClock::getLiveSlotPath(slot));
    invalidateLiveDisplay(slot);
    broadcastFSChange();
    request->send(200, "application/json", "{\"ok\":true}");
}

void handleUploadBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    uint8_t slot;
    if (!parseSlot(request, slot)) {
        sendError(request, 400, "invalid_slot");
        return;
    }

    String tmpPath = String(IPSClock::getLiveImageDir()) + "/live" + String(slot) + ".tmp";
    String finalPath = IPSClock::getLiveSlotPath(slot);

    if (index == 0) {
        IPSClock::ensureLiveImageDir(LittleFS);
        uploadFailed = false;
        uploadWritten = 0;
        uploadSlot = slot;
        LittleFS.remove(tmpPath);
    }

    if (uploadSlot != slot) {
        uploadFailed = true;
    }

    if (!uploadFailed) {
        if (uploadWritten + len > MAX_LIVE_IMAGE_BYTES) {
            uploadFailed = true;
        } else {
            fs::File file = LittleFS.open(tmpPath, index == 0 ? "w" : "a", true);
            if (!file || (len > 0 && file.write(data, len) != len)) {
                uploadFailed = true;
            }
            if (file) file.close();
        }
    }
    uploadWritten += len;
    if (index + len < total) return;

    if (uploadFailed || !isBmpHeaderValid(tmpPath)) {
        LittleFS.remove(tmpPath);
        sendError(request, 400, "invalid_bmp");
        return;
    }

    LittleFS.remove(finalPath);
    if (!LittleFS.rename(tmpPath, finalPath)) {
        LittleFS.remove(tmpPath);
        sendError(request, 500, "rename_failed");
        return;
    }

    invalidateLiveDisplay(slot);
    broadcastFSChange();
    request->send(200, "application/json", "{\"ok\":true}");
}

void handleSetPresetBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index + len < total) return;

    String body;
    body.reserve(len);
    for (size_t i = 0; i < len; i++) body += static_cast<char>(data[i]);
    const char *preset = nullptr;
    if (body.indexOf("HHMM_WITH_TWO_LIVE_IMAGES") >= 0) {
        preset = "HHMM_WITH_TWO_LIVE_IMAGES";
    } else if (body.indexOf("SIX_LIVE_IMAGES") >= 0) {
        preset = "SIX_LIVE_IMAGES";
    }

    if (preset == nullptr || !IPSClock::setDisplayPreset(String(preset))) {
        sendError(request, 400, "invalid_preset");
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
    if (server == nullptr) return;
    server->on("/api/live/slots", HTTP_GET, handleGetSlots);
    server->on("^\\/api\\/live\\/slots\\/([0-5])\\/image$", HTTP_DELETE, handleDeleteImage);
    server->on("^\\/api\\/live\\/slots\\/([0-5])\\/image$", HTTP_PUT, [](AsyncWebServerRequest *request) {}, nullptr, handleUploadBody);
    server->on("/api/display/preset", HTTP_GET, sendPreset);
    server->on("/api/display/preset", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr, handleSetPresetBody);
}
