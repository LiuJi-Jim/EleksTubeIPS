#include "TFTs.h"

#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <TFT_eSprite.h>
#include <GLOBAL_DEFINES.h>
#include <SPI_Display.h>
#include <IPSClock.h>

// Global TFT display object pointer
TFTs *tfts = NULL;

// The original file is large; this replacement was intentionally avoided.
