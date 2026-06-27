#ifndef _IPS_CLOCK_H
#define _IPS_CLOCK_H

#include <ConfigItem.h>
#include <TimeSync.h>
#include <FS.h>

#include "ClockTimer.h"
#include "ImageUnpacker.h"
#include "IRAMPtrArray.h"

class IPSClock {
public:
    enum TimeOrDate {
        TIME = 0,
        DATE,
        WEATHER,
        SLIDE_SHOW,
        LIVE_IMAGES
    };

    enum Dimming {
        BLANK = 0,
        DIM,
        MATRIX
    };

    enum DateFormat {
        EURO = 0,
        USA,
        ISO
    };

    enum Display {
        SIX = 0,
        FOUR,
        FOUR_WITH_WEATHER,
        FOUR_WITH_SLIDESHOW,
        FOUR_WITH_TWO_LIVE_IMAGES
    };

    static const uint8_t LIVE_SLOT_COUNT = 6;

    IPSClock();

    static IntConfigItem& getTimeOrDate() { static IntConfigItem time_or_date("time_or_date", TIME); return time_or_date; }
    static ByteConfigItem& getDateFormat() { static ByteConfigItem date_format("date_format", USA); return date_format; }
    static ByteConfigItem& getSlideTransition() { static ByteConfigItem slide_transition("slide_transition", 0); return slide_transition; }
    static BooleanConfigItem& getHourFormat() { static BooleanConfigItem hour_format("hour_format", true); return hour_format; }
    static ByteConfigItem& getFourDigitDisplay() { static ByteConfigItem four_digit_display("four_digit_display", FOUR_WITH_WEATHER); return four_digit_display; }
    static BooleanConfigItem& getLeadingZero() { static BooleanConfigItem leading_zero("leading_zero", true); return leading_zero; }
    static ByteConfigItem& getDisplayOn() { static ByteConfigItem display_on("display_on", 0); return display_on; }
    static ByteConfigItem& getDisplayOff() { static ByteConfigItem display_off("display_off", 24); return display_off; }
    static StringConfigItem& getClockFace() { static StringConfigItem clock_face("clock_face", 25, "original"); return clock_face; }
    static StringConfigItem& getTimeZone() { static StringConfigItem time_zone("time_zone", 63, "EST5EDT,M3.2.0,M11.1.0"); return time_zone; }
    static IntConfigItem& getDimming() { static IntConfigItem dimming("dimming", MATRIX); return dimming; }
    static ByteConfigItem& getBrightnessConfig() { static ByteConfigItem brightness_config("brightness_config", 255); return brightness_config; }
    static StringConfigItem& getCustomData() { static StringConfigItem custom_data("custom_data", 10, ""); return custom_data; }

    static const char* getLiveImageDir();
    static String getLiveSlotPath(uint8_t slot);
    static String getLiveSlotCachePath(uint8_t slot);
    static bool ensureLiveImageDir(fs::FS& fs);
    static bool isValidLiveSlot(uint8_t slot) { return slot < LIVE_SLOT_COUNT; }
    static uint32_t getLiveSlotVersion(uint8_t slot);
    static void markLiveSlotDirty(uint8_t slot);
    static bool ensureLiveSlotCache(uint8_t slot);
    static bool setDisplayPreset(const String& preset);

    void init();
    void loop();
    void checkIconPack();
    void setTimeSync(TimeSync *pTimeSync) { this->pTimeSync = pTimeSync; }
    void setImageUnpacker(ImageUnpacker *imageUnpacker) { this->imageUnpacker = imageUnpacker; }

    bool clockOn();
    void setOnOverride() { onOverride = millis(); };
    void overrideUntilNextChange() { prevScheduleOn = clockOn(); temporaryOverride = true; }
    void setBrightness(byte brightness) { this->brightness = brightness; }
    uint8_t getBrightness() { return getDimming() == DIM && !clockOn() ? (brightness / 6) : brightness; }
private:
    static IRAMPtrArray<const char*> digitToName;

    void drawHHMMWithTwoLiveImages(struct tm& now);
    void drawSixLiveImages();

    byte brightness = 255;
    ClockTimer::Timer displayTimer;
    String oldClockFace;
	TimeSync *pTimeSync = 0;
    ImageUnpacker *imageUnpacker;
    unsigned long onOverride = 0;
    bool temporaryOverride = false;
    bool prevScheduleOn = false;
};

#endif
