#include "TFTs.h"
#include "IPSClock.h"
#include <LittleFS.h>

extern void broadcastUpdate(const BaseConfigItem& item);
extern void broadcastFSChange();

namespace {
    const char *LIVE_IMAGE_DIR = "/ips/live";
    const char *LIVE_IMAGE_CACHE_PREFIX = "/ips/cache/live";
    const uint32_t INVALID_LIVE_SLOT_VERSION = 0xffffffff;

    uint32_t liveSlotVersion[IPSClock::LIVE_SLOT_COUNT] = {0, 0, 0, 0, 0, 0};
    uint32_t cachedLiveSlotVersion[IPSClock::LIVE_SLOT_COUNT] = {
        INVALID_LIVE_SLOT_VERSION,
        INVALID_LIVE_SLOT_VERSION,
        INVALID_LIVE_SLOT_VERSION,
        INVALID_LIVE_SLOT_VERSION,
        INVALID_LIVE_SLOT_VERSION,
        INVALID_LIVE_SLOT_VERSION
    };

    const uint8_t LEFT_TO_RIGHT_DIGITS[IPSClock::LIVE_SLOT_COUNT] = {
        HOURS_TENS,
        HOURS_ONES,
        MINUTES_TENS,
        MINUTES_ONES,
        SECONDS_TENS,
        SECONDS_ONES
    };

    bool copyFile(const String& srcPath, const String& dstPath) {
        fs::File src = LittleFS.open(srcPath, "r");
        if (!src) {
            LittleFS.remove(dstPath);
            return false;
        }

        fs::File dst = LittleFS.open(dstPath, "w", true);
        if (!dst) {
            src.close();
            return false;
        }

        uint8_t buffer[512];
        while (src.available()) {
            size_t readLen = src.read(buffer, sizeof(buffer));
            if (readLen == 0) {
                break;
            }
            if (dst.write(buffer, readLen) != readLen) {
                src.close();
                dst.close();
                LittleFS.remove(dstPath);
                return false;
            }
        }

        src.close();
        dst.close();
        return true;
    }

    void drawLiveSlot(uint8_t physicalDigit, uint8_t liveSlot, TFTs::show_t show = TFTs::yes) {
        if (!IPSClock::isValidLiveSlot(liveSlot)) {
            return;
        }

        IPSClock::ensureLiveSlotCache(liveSlot);

        char name[8];
        snprintf(name, sizeof(name), "live%u", liveSlot);
        tfts->setDigit(physicalDigit, name, show);
    }
}

IRAMPtrArray<const char*> IPSClock::digitToName {
    "0",
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9"
};

IPSClock::IPSClock() {
}

const char* IPSClock::getLiveImageDir() {
    return LIVE_IMAGE_DIR;
}

String IPSClock::getLiveSlotPath(uint8_t slot) {
    return String(LIVE_IMAGE_DIR) + "/" + String(slot) + ".bmp";
}

String IPSClock::getLiveSlotCachePath(uint8_t slot) {
    return String(LIVE_IMAGE_CACHE_PREFIX) + String(slot) + ".bmp";
}

bool IPSClock::ensureLiveImageDir(fs::FS& fs) {
    if (!fs.exists("/ips")) {
        fs.mkdir("/ips");
    }
    if (!fs.exists(LIVE_IMAGE_DIR)) {
        return fs.mkdir(LIVE_IMAGE_DIR);
    }
    return true;
}

uint32_t IPSClock::getLiveSlotVersion(uint8_t slot) {
    if (!isValidLiveSlot(slot)) {
        return 0;
    }
    return liveSlotVersion[slot];
}

void IPSClock::markLiveSlotDirty(uint8_t slot) {
    if (!isValidLiveSlot(slot)) {
        return;
    }

    liveSlotVersion[slot]++;
    if (liveSlotVersion[slot] == 0) {
        liveSlotVersion[slot] = 1;
    }
    cachedLiveSlotVersion[slot] = INVALID_LIVE_SLOT_VERSION;
}

bool IPSClock::ensureLiveSlotCache(uint8_t slot) {
    if (!isValidLiveSlot(slot)) {
        return false;
    }

    String srcPath = getLiveSlotPath(slot);
    String dstPath = getLiveSlotCachePath(slot);

    if (!LittleFS.exists(srcPath)) {
        LittleFS.remove(dstPath);
        cachedLiveSlotVersion[slot] = liveSlotVersion[slot];
        return false;
    }

    if (cachedLiveSlotVersion[slot] == liveSlotVersion[slot] && LittleFS.exists(dstPath)) {
        return true;
    }

    bool copied = copyFile(srcPath, dstPath);
    if (copied) {
        cachedLiveSlotVersion[slot] = liveSlotVersion[slot];
    }
    return copied;
}

const char* IPSClock::getTimeOrDateName() {
    switch (getTimeOrDate().value) {
        case TIME: return "TIME";
        case DATE: return "DATE";
        case WEATHER: return "WEATHER";
        case SLIDE_SHOW: return "SLIDE_SHOW";
        case LIVE_IMAGES: return "LIVE_IMAGES";
        default: return "UNKNOWN";
    }
}

const char* IPSClock::getFourDigitDisplayName() {
    switch (getFourDigitDisplay().value) {
        case SIX: return "SIX";
        case FOUR: return "FOUR";
        case FOUR_WITH_WEATHER: return "FOUR_WITH_WEATHER";
        case FOUR_WITH_SLIDESHOW: return "FOUR_WITH_SLIDESHOW";
        case FOUR_WITH_TWO_LIVE_IMAGES: return "FOUR_WITH_TWO_LIVE_IMAGES";
        default: return "UNKNOWN";
    }
}

const char* IPSClock::getDisplayPresetName() {
    switch (getTimeOrDate().value) {
        case TIME:
            switch (getFourDigitDisplay().value) {
                case SIX: return "TIME_SIX";
                case FOUR: return "TIME_FOUR";
                case FOUR_WITH_WEATHER: return "TIME_FOUR_WITH_WEATHER";
                case FOUR_WITH_SLIDESHOW: return "TIME_FOUR_WITH_SLIDESHOW";
                case FOUR_WITH_TWO_LIVE_IMAGES: return "HHMM_WITH_TWO_LIVE_IMAGES";
                default: return "TIME_UNKNOWN";
            }
        case DATE: return "DATE";
        case WEATHER: return "WEATHER";
        case SLIDE_SHOW: return "SLIDE_SHOW";
        case LIVE_IMAGES: return "SIX_LIVE_IMAGES";
        default: return "UNKNOWN";
    }
}

bool IPSClock::setDisplayPreset(const String& preset) {
    if (preset == "HHMM_WITH_TWO_LIVE_IMAGES") {
        getTimeOrDate().value = TIME;
        getFourDigitDisplay().value = FOUR_WITH_TWO_LIVE_IMAGES;
    } else if (preset == "SIX_LIVE_IMAGES") {
        getTimeOrDate().value = LIVE_IMAGES;
    } else if (preset == "TIME_SIX") {
        getTimeOrDate().value = TIME;
        getFourDigitDisplay().value = SIX;
    } else if (preset == "TIME_FOUR") {
        getTimeOrDate().value = TIME;
        getFourDigitDisplay().value = FOUR;
    } else if (preset == "TIME_FOUR_WITH_WEATHER") {
        getTimeOrDate().value = TIME;
        getFourDigitDisplay().value = FOUR_WITH_WEATHER;
    } else if (preset == "TIME_FOUR_WITH_SLIDESHOW") {
        getTimeOrDate().value = TIME;
        getFourDigitDisplay().value = FOUR_WITH_SLIDESHOW;
    } else if (preset == "DATE") {
        getTimeOrDate().value = DATE;
    } else if (preset == "WEATHER") {
        getTimeOrDate().value = WEATHER;
    } else if (preset == "SLIDE_SHOW" || preset == "SLIDESHOW") {
        getTimeOrDate().value = SLIDE_SHOW;
    } else {
        return false;
    }

    getTimeOrDate().put();
    getFourDigitDisplay().put();
    return true;
}

void IPSClock::init() {
	displayTimer.init(millis(), 0);
    oldClockFace = getClockFace().value;
    ensureLiveImageDir(LittleFS);
}

bool IPSClock::clockOn() {
    if (millis() - onOverride <= 10000) {
        return true;
    }
    
	struct tm now;
	suseconds_t uSec;
    bool scheduledOn = false;

	if (getDisplayOn().value == getDisplayOff().value) {
		scheduledOn = true;
	} else if (pTimeSync) {
        pTimeSync->getLocalTime(&now, &uSec);

        if (getDisplayOn().value < getDisplayOff().value) {
			scheduledOn = now.tm_hour >= getDisplayOn().value && now.tm_hour < getDisplayOff().value;
		} else if (getDisplayOn().value > getDisplayOff().value) {
			scheduledOn = !(now.tm_hour >= getDisplayOff().value && now.tm_hour < getDisplayOn().value);
		}
    }

    if (temporaryOverride) {
        if (prevScheduleOn == scheduledOn) {
            scheduledOn = !scheduledOn;
        } else {
            temporaryOverride = false;
        }
    }

	return scheduledOn;
}

void IPSClock::checkIconPack() {
    if (getClockFace().value != oldClockFace) {
        getClockFace() = imageUnpacker->unpackImages("/ips/faces/", "/ips/cache", getClockFace(), oldClockFace);
        getClockFace().put();
        broadcastUpdate(getClockFace());
        broadcastFSChange();
        oldClockFace = getClockFace();
        for (uint8_t slot = 0; slot < LIVE_SLOT_COUNT; slot++) {
            cachedLiveSlotVersion[slot] = INVALID_LIVE_SLOT_VERSION;
        }
        tfts->claim();
        tfts->invalidateAllDigits();
        tfts->release();
    }
}

void IPSClock::drawHHMMWithTwoLiveImages(struct tm& now) {
    uint8_t hour = now.tm_hour;

    if (getHourFormat()) {  // true = 12 hour display
        if (now.tm_hour > 12) {
            hour = now.tm_hour - 12;
        } else if (now.tm_hour == 0) {
            hour = 12;
        }
    }

    if (hour < 10 && !getLeadingZero().value) {
        tfts->setDigit(HOURS_TENS, "space", TFTs::yes);
    } else {
        tfts->setDigit(HOURS_TENS, digitToName[hour / 10], TFTs::yes);
    }
    tfts->setDigit(HOURS_ONES, digitToName[hour % 10], TFTs::yes);
    tfts->setDigit(MINUTES_TENS, digitToName[now.tm_min / 10], TFTs::yes);
    tfts->setDigit(MINUTES_ONES, digitToName[now.tm_min % 10], TFTs::yes);
    drawLiveSlot(SECONDS_TENS, 4, TFTs::yes);
    drawLiveSlot(SECONDS_ONES, 5, TFTs::yes);
}

void IPSClock::drawSixLiveImages() {
    for (uint8_t slot = 0; slot < LIVE_SLOT_COUNT; slot++) {
        drawLiveSlot(LEFT_TO_RIGHT_DIGITS[slot], slot, TFTs::yes);
    }
}

void IPSClock::loop() {
    unsigned long nowMs = millis();

    // display refresh
    if (displayTimer.expired(nowMs)) {
        struct tm now;
        suseconds_t uSec;
        pTimeSync->getLocalTime(&now, &uSec);
        suseconds_t realms = uSec / 1000;
        if (realms > 1000) {
            realms = realms % 1000;	// Something went wrong so pick a safe number for 1000 - realms...
        }
        unsigned long tDelay = 1000 - realms;

        if (clockOn() || (getDimming() == DIM)) {
            tfts->claim();
            tfts->setDimming(getBrightness());
            tfts->setImageJustification(TFTs::MIDDLE_CENTER);
            tfts->setBox(tfts->width(), tfts->height());
            tfts->checkStatus();
            tfts->enableAllDisplays();
            // tfts->invalidateAllDigits();

            // Display custom data if available: 
            uint8_t customDataLength = getCustomData().value.length();
            if (customDataLength > 0) {
                for (uint8_t i = 0; i < NUM_DIGITS; i++) {
                    char name[10];
                    // no letter found for this digit -> use space
                    if (i >= customDataLength) {
                        strcpy(name, "space");
                    }
                    else 
                    {
                        char value = getCustomData().value[i];
                        if (value == '_' or value == ' ') {
                            strcpy(name, "space");
                        } else if (value == ':') {
                            strcpy(name, "colon");
                        } else if (value == 'a') {
                            strcpy(name, "am");
                        } else if (value == 'p') {
                            strcpy(name, "pm"); 
                        } else if (value >= '0' && value <= '9') {
                            name[0] = value;
                            name[1] = 0;
                        } 
                        // not a digit colon or space -> show as space
                        else {
                            strcpy(name, "space");
                        }

                    }
                    uint8_t DIGITS[NUM_DIGITS] = {
                        HOURS_TENS,
                        HOURS_ONES,
                        MINUTES_TENS,
                        MINUTES_ONES,
                        SECONDS_TENS,
                        SECONDS_ONES
                    };
                    tfts->setDigit(DIGITS[i], name, TFTs::yes);
                }
            }
            // Display time: 
            else if (getTimeOrDate().value == TIME) {
                uint8_t hour = now.tm_hour;

                if (getFourDigitDisplay() == FOUR_WITH_TWO_LIVE_IMAGES) {
                    drawHHMMWithTwoLiveImages(now);
                }
                // refresh starting on seconds
                else if (getFourDigitDisplay() == SIX) {
                    tfts->setDigit(SECONDS_ONES, digitToName[now.tm_sec % 10], TFTs::yes);
                    tfts->setDigit(SECONDS_TENS, digitToName[now.tm_sec / 10], TFTs::yes);
                    tfts->setDigit(MINUTES_ONES, digitToName[now.tm_min % 10], TFTs::yes);
                    tfts->setDigit(MINUTES_TENS, digitToName[now.tm_min / 10], TFTs::yes);
                } else {
                    if (getFourDigitDisplay() == FOUR) {
                        if (getHourFormat()) {  // true == show am/pm indicator
                            tfts->setDigit(SECONDS_ONES, hour < 12 ? "am" : "pm", TFTs::yes);
                        } else {
                            tfts->setDigit(SECONDS_ONES, "space", TFTs::yes);
                        }
                    } else if (getFourDigitDisplay() == FOUR_WITH_SLIDESHOW && now.tm_sec % 10 == 3) {
                        tfts->setShowDigits(SLIDE_SHOW);
                        tfts->setDigit(SECONDS_ONES, digitToName[random(10)], TFTs::yes);
                        tfts->setShowDigits(TIME);
                    }
                    tfts->setDigit(SECONDS_TENS, digitToName[now.tm_min % 10], TFTs::yes);
                    tfts->setDigit(MINUTES_ONES, digitToName[now.tm_min / 10], TFTs::yes);
                    if (now.tm_sec % 2 == 0) {
                        tfts->setDigit(MINUTES_TENS, "space", TFTs::yes);
                    } else {
                        tfts->setDigit(MINUTES_TENS, "colon", TFTs::yes);
                    }
                }

                if (getFourDigitDisplay() != FOUR_WITH_TWO_LIVE_IMAGES) {
                    if (getHourFormat()) {  // true = 12 hour display
                        if (now.tm_hour > 12) {
                            hour = now.tm_hour - 12;
                        } else if (now.tm_hour == 0) {
                            hour = 12;
                        }
                    }

                    tfts->setDigit(HOURS_ONES, digitToName[hour % 10], TFTs::yes);
                    tfts->setDigit(HOURS_ONES, digitToName[hour % 10], TFTs::yes);
                    if (hour < 10 && !getLeadingZero().value) {
                        tfts->setDigit(HOURS_TENS, "space", TFTs::yes);
                    } else {
                        tfts->setDigit(HOURS_TENS, digitToName[hour / 10], TFTs::yes);
                    }
                }
            } 
            // Display Date: 
            else if (getTimeOrDate().value == DATE) {
                uint8_t day = now.tm_mday;
                uint8_t month = now.tm_mon;
                uint8_t year = now.tm_year;

                switch (getDateFormat().value) {
                case EURO:	// DD-MM-YY
                    break;
                case USA: // MM-DD-YY
                    day = now.tm_mon;
                    month = now.tm_mday;
                    break;
                default: // YY-MM-DD
                    day = now.tm_year;
                    year = now.tm_mday;
                    break;
                }

                // refresh starting on 'seconds'
                tfts->setDigit(SECONDS_ONES, digitToName[year % 10], TFTs::yes);
                tfts->setDigit(SECONDS_TENS, digitToName[year / 10], TFTs::yes);
                tfts->setDigit(MINUTES_ONES, digitToName[month % 10], TFTs::yes);
                tfts->setDigit(MINUTES_TENS, digitToName[month / 10], TFTs::yes);
                tfts->setDigit(HOURS_ONES, digitToName[day % 10], TFTs::yes);
                tfts->setDigit(HOURS_TENS, digitToName[day / 10], TFTs::yes);
            } else if (getTimeOrDate().value == LIVE_IMAGES) {
                drawSixLiveImages();
            } else if (getTimeOrDate().value == SLIDE_SHOW) {
                if (strcmp(TFTs::INVALID_DIGIT, tfts->getDigitName(SECONDS_ONES)) == 0) {
                    tfts->setDigit(SECONDS_ONES, digitToName[0], TFTs::yes);
                    tfts->setDigit(SECONDS_TENS, digitToName[1], TFTs::yes);
                    tfts->setDigit(MINUTES_ONES, digitToName[2], TFTs::yes);
                    tfts->setDigit(MINUTES_TENS, digitToName[3], TFTs::yes);
                    tfts->setDigit(HOURS_ONES, digitToName[4], TFTs::yes);
                    tfts->setDigit(HOURS_TENS, digitToName[5], TFTs::yes);
                }
                if (now.tm_sec % 10 == 0) {
                    tfts->setDigit(random(6), digitToName[random(10)], TFTs::yes);
                }
            } else {
                Serial.println("Bad display state for clock");
            }
        } else {
            tfts->disableAllDisplays();
        }

        tfts->release();

        displayTimer.init(nowMs, tDelay);
    }
}
