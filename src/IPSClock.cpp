#include "TFTs.h"
#include "IPSClock.h"
#include <LittleFS.h>

extern void broadcastUpdate(const BaseConfigItem& item);
extern void broadcastFSChange();

namespace {
    const char *LIVE_IMAGE_DIR = "/ips/cache";

    const uint8_t LEFT_TO_RIGHT_DIGITS[IPSClock::LIVE_SLOT_COUNT] = {
        HOURS_TENS,
        HOURS_ONES,
        MINUTES_TENS,
        MINUTES_ONES,
        SECONDS_TENS,
        SECONDS_ONES
    };

    void drawLiveSlot(uint8_t physicalDigit, uint8_t liveSlot, TFTs::show_t show = TFTs::yes) {
        if (!IPSClock::isValidLiveSlot(liveSlot)) {
            return;
        }

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
    return String(LIVE_IMAGE_DIR) + "/live" + String(slot) + ".bmp";
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
    return 0;
}

void IPSClock::markLiveSlotDirty(uint8_t slot) {
}

bool IPSClock::setDisplayPreset(const String& preset) {
    if (preset == "HHMM_WITH_TWO_LIVE_IMAGES") {
        getTimeOrDate().value = TIME;
        getFourDigitDisplay().value = FOUR_WITH_TWO_LIVE_IMAGES;
    } else if (preset == "SIX_LIVE_IMAGES") {
        getTimeOrDate().value = LIVE_IMAGES;
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
        tfts->claim();
        tfts->invalidateAllDigits();
        tfts->release();
    }
}

void IPSClock::drawHHMMWithTwoLiveImages(struct tm& now) {
    uint8_t hour = now.tm_hour;

    if (getHourFormat()) {
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

    if (displayTimer.expired(nowMs)) {
        struct tm now;
        suseconds_t uSec;
        pTimeSync->getLocalTime(&now, &uSec);
        suseconds_t realms = uSec / 1000;
        if (realms > 1000) {
            realms = realms % 1000;
        }
        unsigned long tDelay = 1000 - realms;

        if (clockOn() || (getDimming() == DIM)) {
            tfts->claim();
            tfts->setDimming(getBrightness());
            tfts->setImageJustification(TFTs::MIDDLE_CENTER);
            tfts->setBox(tfts->width(), tfts->height());
            tfts->checkStatus();
            tfts->enableAllDisplays();

            uint8_t customDataLength = getCustomData().value.length();
            if (customDataLength > 0) {
                for (uint8_t i = 0; i < NUM_DIGITS; i++) {
                    char name[10];
                    if (i >= customDataLength) {
                        strcpy(name, "space");
                    } else {
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
                        } else {
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
            } else if (getTimeOrDate().value == TIME) {
                uint8_t hour = now.tm_hour;

                if (getFourDigitDisplay() == FOUR_WITH_TWO_LIVE_IMAGES) {
                    drawHHMMWithTwoLiveImages(now);
                } else if (getFourDigitDisplay() == SIX) {
                    tfts->setDigit(SECONDS_ONES, digitToName[now.tm_sec % 10], TFTs::yes);
                    tfts->setDigit(SECONDS_TENS, digitToName[now.tm_sec / 10], TFTs::yes);
                    tfts->setDigit(MINUTES_ONES, digitToName[now.tm_min % 10], TFTs::yes);
                    tfts->setDigit(MINUTES_TENS, digitToName[now.tm_min / 10], TFTs::yes);
                } else {
                    if (getFourDigitDisplay() == FOUR || getFourDigitDisplay() == FOUR_WITH_WEATHER) {
                        if (getHourFormat()) {
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
                    if (getHourFormat()) {
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
            } else if (getTimeOrDate().value == DATE) {
                uint8_t day = now.tm_mday;
                uint8_t month = now.tm_mon;
                uint8_t year = now.tm_year;

                switch (getDateFormat().value) {
                case EURO:
                    break;
                case USA:
                    day = now.tm_mon;
                    month = now.tm_mday;
                    break;
                default:
                    day = now.tm_year;
                    year = now.tm_mday;
                    break;
                }

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
