#include <Arduino.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <AsyncWiFiManager.h>
#include <EspSNTPTimeSync.h>
#ifndef DS1302
#include <EspRTCTimeSync.h>
#else
#include <EspDS1302TimeSync.h>
#endif
#include <ConfigItem.h>
#include <EEPROMConfig.h>
#include <ImprovWiFi.h>
#include <eSPI_Menu.h>

#include "TFTs.h"
#include "IPSClock.h"
#include "Backlights.h"
#include "GPIOButton.h"
#include "WSHandler.h"
#include "WSMenuHandler.h"
#include "WSConfigHandler.h"
#include "WSInfoHandler.h"
#include "ImageUnpacker.h"
#include "LiveImageApi.h"
#include "IRAMPtrArray.h"
#include "Uptime.h"

//#define DEBUG(...) { Serial.println(__VA_ARGS__); }
#ifndef DEBUG
#define DEBUG(...) {  }
#endif

#define SMALL_FONT  2
#define ITEM_FONT   2
#define TITLE_FONT  4

IRAMPtrArray<const char*> manifest {
#if defined(HARDWARE_PunkCyber_CLOCK)
	"PCBWay RGB Glow Tube Clock Firmware",
#elif defined(HARDWARE_Elekstube_CLOCK)
	"EleksTubeIPS V1 Live Image Firmware",
#elif defined(HARDWARE_Elekstube_CLOCK_V2)
	"EleksTubeIPS V2 Live Image Firmware",
#elif defined(HARDWARE_NovelLife_SE_CLOCK)
	"NovelLife SE Live Image Firmware",
#elif defined(HARDWARE_SI_HAI_CLOCK)
	"Si Hai Clock Live Image Firmware",
#elif defined(HARDWARE_IPSTube_CLOCK)
	"IPSTube Clock Live Image Firmware",
#elif defined(HARDWARE_IPSTube_DIM_CLOCK)
	"IPSTube Clock Live Image Firmware",
#else
	"Unknown clock hardware",
#endif
	"1.9.5-live-image",
	"ESP32",
	"IPS Clock"
};

String getChipId(void);
void setWiFiAP(bool on);
void infoCallback();
String wifiCallback();
String clockFacesCallback();
void broadcastUpdate(String msg);
void broadcastUpdate(const BaseConfigItem& item);
void setFace(const char *menuLabel);
void initFacesMenu();

TFTs *tfts = NULL;
eSPIMenu::Menu *menu;
Backlights *backlights = NULL;
IPSClock *ipsClock = NULL;
ImageUnpacker *imageUnpacker = NULL;

#if defined(BUTTON_MODE_PIN) && defined(BUTTON_RIGHT_PIN) && defined(BUTTON_LEFT_PIN)
#define BUTTON_MENU_PINS
#endif
#ifdef BUTTON_MENU_PINS
GPIOButton *modeButton;
GPIOButton *rightButton;
GPIOButton *leftButton;
#endif
#ifdef BUTTON_POWER_PIN
GPIOButton *powerButton;
#endif

AsyncWebServer *server = new AsyncWebServer(80);
AsyncWebSocket *ws = new AsyncWebSocket("/ws");
DNSServer *dns = new DNSServer();
AsyncWiFiManager *wifiManager = new AsyncWiFiManager(server, dns);
TimeSync *timeSync;
#ifndef DS1302
RTCTimeSync *rtcTimeSync;
#else
DS1302TimeSync *rtcTimeSync;
#endif

Uptime uptime;

TaskHandle_t wifiManagerTask;
TaskHandle_t clockTask;
TaskHandle_t improvTask;
TaskHandle_t ledTask;
TaskHandle_t commitEEPROMTask;

SemaphoreHandle_t wsMutex;
SemaphoreHandle_t memMutex;

AsyncWiFiManagerParameter *hostnameParam;
String ssid("EleksTubeIPS");
String chipId = getChipId();

#if defined(HARDWARE_PunkCyber_CLOCK)
StringConfigItem hostName("hostname", 63, "punkcyber");
#elif defined(HARDWARE_Elekstube_CLOCK)
StringConfigItem hostName("hostname", 63, "elekstubeips");
#elif defined(HARDWARE_Elekstube_CLOCK_V2)
StringConfigItem hostName("hostname", 63, "elekstubeipsv2");
#elif defined(HARDWARE_NovelLife_SE_CLOCK)
StringConfigItem hostName("hostname", 63, "novellifese");
#elif defined(HARDWARE_SI_HAI_CLOCK)
StringConfigItem hostName("hostname", 63, "sihai");
#elif defined(HARDWARE_IPSTube_CLOCK) || defined (HARDWARE_IPSTube_DIM_CLOCK)
StringConfigItem hostName("hostname", 63, "ipstube");
#else
StringConfigItem hostName("hostname", 63, "ipsclock");
#endif

IRAMPtrArray<BaseConfigItem*> clockSet {
	&IPSClock::getDateFormat(),
	&IPSClock::getTimeOrDate(),
	&IPSClock::getSlideTransition(),
	&IPSClock::getHourFormat(),
	&IPSClock::getLeadingZero(),
	&IPSClock::getFourDigitDisplay(),
	&IPSClock::getDisplayOn(),
	&IPSClock::getDisplayOff(),
	&IPSClock::getClockFace(),
	&IPSClock::getDimming(),
	&IPSClock::getBrightnessConfig(),
	&IPSClock::getTimeZone(),
	0
};
CompositeConfigItem clockConfig("clock", 0, clockSet);

IRAMPtrArray<BaseConfigItem*> ledSet {
	&Backlights::getLEDPattern(),
	&Backlights::getLEDHue(),
	&Backlights::getLEDSaturation(),
	&Backlights::getLEDValue(),
	&Backlights::getBreathPerMin(),
	&Backlights::getHuePerLed(),
	0
};
CompositeConfigItem ledConfig("leds", 0, ledSet);

StringConfigItem *fileSet = new StringConfigItem("file_set", 10, "faces");
StringConfigItem *slidesSet = new StringConfigItem("slide_show", 25, "anime_female");
String *oldSlidesSet = new String("anime_female");

IRAMPtrArray<BaseConfigItem*> faceSet {
	&IPSClock::getClockFace(),
	slidesSet,
	fileSet,
	0
};
CompositeConfigItem facesConfig("faces", 0, faceSet);

IRAMPtrArray<BaseConfigItem*> configSetGlobal = {
	&hostName,
	0
};
CompositeConfigItem globalConfig("global", 0, configSetGlobal);

IRAMPtrArray<BaseConfigItem*> configSetRoot {
	&globalConfig,
	&clockConfig,
	&ledConfig,
	&facesConfig,
	0
};
CompositeConfigItem rootConfig("root", 0, configSetRoot);

EEPROMConfig config(rootConfig);

void asyncTimeSetCallback(String time) {
	DEBUG(time);
	tfts->setStatus("NTP time received...");
#ifndef DS1302
	rtcTimeSync->enabled(false);
	rtcTimeSync->setDevice();
#else
	rtcTimeSync->enabled(false);
	rtcTimeSync->setDevice();
#endif
}

void asyncTimeErrorCallback(String msg) {
	DEBUG(msg);
#ifndef DS1302
	rtcTimeSync->enabled(true);
#else
	rtcTimeSync->enabled(true);
#endif
}

void onTimezoneChanged(ConfigItem<String> &tzItem) {
	timeSync->setTz(tzItem);
	timeSync->sync();
}

void broadcastFSChange() {
	String freeSpace = String(LittleFS.totalBytes() - LittleFS.usedBytes());
	String msg = "{\"type\":\"sv.update\",\"value\":{\"fs_free\":" + freeSpace + ",\"fs_size\":" + String(LittleFS.totalBytes()) + "}}";
	broadcastUpdate(msg);
}

void onFileSetChanged(ConfigItem<String> &item) {
	if (fileSet->value != "faces" && fileSet->value != "slides") {
		fileSet->value = "faces";
		fileSet->put();
	}

	String msg = "{\"type\":\"sv.update\",\"value\":{\"clock_face\":\""
		 + IPSClock::getClockFace().value
		 + "\",\"slide_show\":\""
		 + slidesSet->value
		 + "\",";
	msg += clockFacesCallback();
	msg += "}}";

	broadcastUpdate(msg);
}

void onDisplayChanged(ConfigItem<int> &item) {
	tfts->invalidateAllDigits();
}

void onBrightnessChanged(ConfigItem<byte> &item) {
	tfts->invalidateAllDigits();
}

bool menuDrawn = false;

void onButtonEvent(const Button *button, Button::Event evt) {
#ifdef BUTTON_POWER_PIN
	if (!ipsClock->clockOn()) {
		ipsClock->setOnOverride();
		return;
	}

	if (button == powerButton && evt == Button::long_press) {
		ipsClock->overrideUntilNextChange();
		return;
	}
#endif

#ifdef BUTTON_MENU_PINS
	if (button == rightButton && evt == Button::button_clicked && menuDrawn) {
		menu->down();
		tfts->getSprite().pushSprite(0, 0);
	}

	if (button == leftButton && evt == Button::button_clicked  && menuDrawn) {
		menu->up();
		tfts->getSprite().pushSprite(0, 0);
	}

	if (button == modeButton) {
		if (evt == Button::long_press) {
			if (!menuDrawn) {
				tfts->clear();
				initFacesMenu();
				menu->show();
				tfts->getSprite().pushSprite(0, 0);
				menuDrawn = true;
			} else {
				tfts->invalidateAllDigits();
				menuDrawn = false;
			}
		}

		if (evt == Button::button_clicked) {
			if (menuDrawn) {
				setFace(menu->getSelectedText());
				tfts->fillScreen(TFT_BLACK);
				tfts->invalidateAllDigits();
				menuDrawn = false;
			} else {
				IntConfigItem &dateOrTime = IPSClock::getTimeOrDate();
				dateOrTime.value = (dateOrTime.value + 1) % 4;
				if (dateOrTime.value == IPSClock::WEATHER) {
					dateOrTime.value = IPSClock::SLIDE_SHOW;
				}
				dateOrTime.put();
				broadcastUpdate(dateOrTime);
				dateOrTime.notify();
				tfts->invalidateAllDigits();
			}
		}
	}
#endif
}

#define DEFAULT_MAIN_SLEEP (pdMS_TO_TICKS(1))

void clockTaskFn(void *pArg) {
	TickType_t toSleep = DEFAULT_MAIN_SLEEP;

	imageUnpacker = new ImageUnpacker();
	fileSet->setCallback(onFileSetChanged);

	ipsClock = new IPSClock();
	ipsClock->init();
	ipsClock->setImageUnpacker(imageUnpacker);
	ipsClock->setTimeSync(timeSync);
	ipsClock->getTimeOrDate().setCallback(onDisplayChanged);
	ipsClock->getBrightnessConfig().setCallback(onBrightnessChanged);

	*oldSlidesSet = slidesSet->value;

#ifdef BUTTON_MENU_PINS
	modeButton = new GPIOButton(BUTTON_MODE_PIN, false);
	rightButton = new GPIOButton(BUTTON_RIGHT_PIN, false);
	leftButton = new GPIOButton(BUTTON_LEFT_PIN, false);

	leftButton->setCallback(onButtonEvent);
	modeButton->setCallback(onButtonEvent);
	rightButton->setCallback(onButtonEvent);
#endif
#ifdef BUTTON_POWER_PIN
	powerButton = new GPIOButton(BUTTON_POWER_PIN, false);
	powerButton->setCallback(onButtonEvent);
#endif

	while (true) {
		delay(toSleep);
		uptime.loop();

#ifdef BUTTON_MENU_PINS
		leftButton->getEvent();
		rightButton->getEvent();
		modeButton->getEvent();
#endif
#ifdef BUTTON_POWER_PIN
		powerButton->getEvent();
#endif
		tfts->checkStatus();

		if (menuDrawn) {
			continue;
		}

		ipsClock->setBrightness(ipsClock->getBrightnessConfig());

		xSemaphoreTake(memMutex, portMAX_DELAY);

		tfts->setShowDigits(IPSClock::getTimeOrDate());
		if (slidesSet->value != *oldSlidesSet) {
			slidesSet->value = imageUnpacker->unpackImages("/ips/slides/", "/ips/slides_cache", *slidesSet, *oldSlidesSet);
			slidesSet->put();
			broadcastUpdate(*slidesSet);
			broadcastFSChange();
			*oldSlidesSet = slidesSet->value;
			tfts->claim();
			tfts->invalidateAllDigits();
			tfts->release();
		}
		ipsClock->checkIconPack();

		if (timeSync->initialized() || rtcTimeSync->initialized()) {
			ipsClock->loop();
		}

		xSemaphoreGive(memMutex);
	}
}

String getChipId(void) {
	uint8_t macid[6];
	esp_efuse_mac_get_default(macid);
	String chipId = String((uint32_t)(macid[5] + (((uint32_t)(macid[4])) << 8) + (((uint32_t)(macid[3])) << 16)), HEX);
	chipId.toUpperCase();
	return chipId;
}

void createSSID() {
	ssid = (chipId + hostName).substring(0, 31);
}

IRAMPtrArray<const char*> items {
	WSMenuHandler::clockMenu,
	WSMenuHandler::ledsMenu,
	WSMenuHandler::facesMenu,
	WSMenuHandler::liveImagesMenu,
	WSMenuHandler::networkMenu,
	WSMenuHandler::infoMenu,
	0
};

WSMenuHandler wsMenuHandler(items);
WSConfigHandler wsClockHandler(rootConfig, "clock");
WSConfigHandler wsLEDHandler(rootConfig, "leds");
WSConfigHandler wsFacesHandler(rootConfig, "faces", clockFacesCallback);
WSConfigHandler wsNetworkHandler(rootConfig, "network", wifiCallback);
WSInfoHandler wsInfoHandler(infoCallback);

IRAMPtrArray<WSHandler*> wsHandlers {
	&wsMenuHandler,
	&wsClockHandler,
	&wsLEDHandler,
	&wsFacesHandler,
	NULL,
	&wsInfoHandler,
	&wsNetworkHandler,
	NULL,
	NULL,
	NULL
};

void infoCallback() {
	wsInfoHandler.setSsid(ssid);
	wsInfoHandler.setDescription(manifest[0]);
	wsInfoHandler.setRevision(manifest[1]);
	wsInfoHandler.setUptime(uptime.uptime());

	wsInfoHandler.setFSSize(String(LittleFS.totalBytes()));
	wsInfoHandler.setFSFree(String(LittleFS.totalBytes() - LittleFS.usedBytes()));
	TimeSync::SyncStats &syncStats = timeSync->getStats();

	wsInfoHandler.setFailedCount(syncStats.failedCount);
	wsInfoHandler.setLastFailedMessage(syncStats.lastFailedMessage);
	wsInfoHandler.setLastUpdateTime(syncStats.lastUpdateTime);
	wsInfoHandler.setHostname(hostName);
}

void broadcastUpdate(String msg) {
	xSemaphoreTake(wsMutex, portMAX_DELAY);
	ws->textAll(msg);
	xSemaphoreGive(wsMutex);
}

void broadcastUpdate(const BaseConfigItem& item) {
	xSemaphoreTake(wsMutex, portMAX_DELAY);

	JsonDocument doc;
	JsonObject root = doc.to<JsonObject>();
	root["type"] = "sv.update";

	JsonVariant value = root.createNestedObject("value");
	String rawJSON = item.toJSON();
	value[item.name] = serialized(rawJSON.c_str());

	size_t len = measureJson(root);
	AsyncWebSocketMessageBuffer * buffer = ws->makeBuffer(len);
	if (buffer) {
		serializeJson(root, (char *)buffer->get(), len);
		ws->textAll(buffer);
	}

	xSemaphoreGive(wsMutex);
}

void updateValue(int screen, String pair) {
	int index = pair.indexOf(':');
	if (index < 0) {
		return;
	}

	String _key = pair.substring(0, index);
	const char* key = _key.c_str();
	String value = pair.substring(index+1);
	BaseConfigItem *item = rootConfig.get(key);
	if (item != 0) {
		item->fromString(value);
		item->put();
		broadcastUpdate(*item);
		item->notify();
		if (_key == "hostname") {
			config.commit();
			ESP.restart();
		}
	} else if (_key == "wifi_ap") {
		setWiFiAP(value == "true" ? true : false);
	}
}

void handleWSMsg(AsyncWebSocketClient *client, char *data) {
	String wholeMsg(data);
	int colon = wholeMsg.indexOf(':');
	int code = wholeMsg.substring(0, colon).toInt();

	if (code < 9) {
		if (code < wsHandlers.length() && wsHandlers[code] != NULL) {
			wsHandlers[code]->handle(client, data);
		}
		return;
	}

	String message = colon >= 0 ? wholeMsg.substring(colon + 1) : "";
	if (message.length() == 0) {
		return;
	}
	int messageColon = message.indexOf(':');
	if (messageColon < 0) {
		return;
	}
	int screen = message.substring(0, messageColon).toInt();
	String pair = message.substring(messageColon + 1);
	updateValue(screen, pair);
}

void wsHandler(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
	switch (type) {
	case WS_EVT_CONNECT:
		DEBUG("WS connected");
		break;
	case WS_EVT_DISCONNECT:
		DEBUG("WS disconnected");
		break;
	case WS_EVT_ERROR:
		DEBUG("WS error");
		break;
	case WS_EVT_PONG:
		DEBUG("WS pong");
		break;
	case WS_EVT_DATA:
		{
			AwsFrameInfo * info = (AwsFrameInfo*) arg;
			if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
				data[len] = 0;
				handleWSMsg(client, (char *) data);
			}
		}
		break;
	}
}

void mainHandler(AsyncWebServerRequest *request) {
	request->send(LittleFS, "/index.html");
}

void timeHandler(AsyncWebServerRequest *request) {
	String wifiTime = request->getParam("time", true, false)->value();
	timeSync->setTime(wifiTime);
#ifndef DS1302
	rtcTimeSync->setTime(wifiTime);
#else
	rtcTimeSync->setTime(wifiTime);
#endif
	request->send(LittleFS, "/time.html");
}

void sendFavicon(AsyncWebServerRequest *request) {
	request->send(LittleFS, "/assets/favicon-32x32.png", "image/png");
}

void handleDelete(AsyncWebServerRequest *request) {
	String filename = request->pathArg(0);
	if (filename.length() > 0) {
		if (LittleFS.remove("/ips/" + fileSet->value + "/" + filename)) {
			request->send(200, "text/plain", "File deleted");
			wsFacesHandler.broadcast(*ws, 0);
			return;
		}
	}
	request->send(500, "text/plain", "Delete failed");
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
	if (!filename.endsWith(".tar.gz")) {
		request->send(415, "text/plain", "Invalid file type");
		return;
	}

	if (!index) {
		request->_tempFile = LittleFS.open("/ips/" + fileSet->value + "/" + filename, "wb", true);
	}
	if (len) {
		request->_tempFile.write(data, len);
	}
	if (final) {
		request->_tempFile.close();
		request->send(200, "text/plain", "File uploaded");
		wsFacesHandler.broadcast(*ws, 0);
	}
}

void configureWebServer() {
	server->serveStatic("/", LittleFS, "/");
	server->on("/", HTTP_GET, mainHandler).setFilter(ON_STA_FILTER);
	server->on("/t", HTTP_POST, timeHandler).setFilter(ON_AP_FILTER);
	server->on("/assets/favicon-32x32.png", HTTP_GET, sendFavicon);
	server->on("/upload_face", HTTP_POST, [](AsyncWebServerRequest *request) {
		request->send(200);
	}, handleUpload);
	server->on("^\\/delete_face\\/(.*\\.tar\\.gz)$", HTTP_DELETE, handleDelete);
	server->serveStatic("/assets", LittleFS, "/assets");
	configureLiveImageApi(server);

	ws->onEvent(wsHandler);
	server->addHandler(ws);
	server->begin();
	ws->enable(true);
}

void setFace(const char *menuLabel) {
	if (IPSClock::getTimeOrDate() == IPSClock::SLIDE_SHOW) {
		slidesSet->fromString(menuLabel);
	} else {
		ipsClock->getClockFace().fromString(menuLabel);
	}
}

#define MENU_TEXT_TITLE 0xDF1C
#define MENU_TEXT_ITEM 0xA5B7
#define MENU_TEXT_HILIGHT 0xDF1C
#define MENU_TEXT_DISABLED 0x3B92
#define MENU_ITEM_BACKGROUND 0x00E5
#define MENU_TITLE_BACKGROUND 0x0a2d
#define MENU_ITEM_HILIGHT_BACKGROUND 0x0062
#define ITEM_BORDER_COLOR 0xFFFF
#define TITLE_BORDER_COLOR 0xFFFF

void initFacesMenu() {
	String postfix(".tar.gz");
	int display = IPSClock::getTimeOrDate();

	menu->reset();
	const char *displayTitle = display == IPSClock::SLIDE_SHOW ? "Slide Show" : "Clock Face";
	menu->setTitle(displayTitle);
	eSPIMenu::Spec& itemSpec = menu->getItemSpec();
	itemSpec.setFont(TITLE_FONT);
	itemSpec.setItemColors(MENU_ITEM_BACKGROUND, MENU_TEXT_ITEM, MENU_ITEM_HILIGHT_BACKGROUND, MENU_TEXT_HILIGHT, MENU_ITEM_BACKGROUND, MENU_TEXT_DISABLED);
	itemSpec.setMargins(1, 1, 2, 2);
	itemSpec.setBorder(0, 2, 0, 0);
	itemSpec.setBorderColors(MENU_ITEM_BACKGROUND, ITEM_BORDER_COLOR, MENU_ITEM_BACKGROUND);

	eSPIMenu::Spec& titleSpec = menu->getTitleSpec();
	titleSpec.setFont(TITLE_FONT);
	titleSpec.setItemColors(MENU_TITLE_BACKGROUND, MENU_TEXT_TITLE, MENU_ITEM_HILIGHT_BACKGROUND, MENU_TEXT_HILIGHT, MENU_ITEM_BACKGROUND, MENU_TEXT_DISABLED);
	titleSpec.setMargins(2, 2, 2, 2);
	titleSpec.setBorder(0, 0, 1, 0);
	titleSpec.setBorderColors(TITLE_BORDER_COLOR, TITLE_BORDER_COLOR, TITLE_BORDER_COLOR);

	String dirName("/ips/");
	dirName += display == IPSClock::SLIDE_SHOW ? "slides" : "faces";

	fs::File dir = LittleFS.open(dirName);
	String name = dir.getNextFileName();
	int optIndex = 0;
	while(name.length() > 0 && optIndex < ESPI_MENU_MAX_ITEMS){
		String fileName = name.substring(dirName.length() + 1, name.length());
		String option = fileName.substring(0, fileName.lastIndexOf(postfix));
		eSPIMenu::State state = option == ipsClock->getClockFace() ? eSPIMenu::selected : eSPIMenu::none;
		if (display == IPSClock::SLIDE_SHOW) {
			state = option == slidesSet->value ? eSPIMenu::selected : eSPIMenu::none;
		}
		menu->addItem(option.c_str(), state);
		name = dir.getNextFileName();
		optIndex++;
	}

	tfts->fillScreen(MENU_ITEM_BACKGROUND);
}

String wifiCallback() {
	String wifiStatus = "\"wifi_ap\":";
	wifiStatus += ((WiFi.getMode() & WIFI_MODE_AP) != 0) ? "true" : "false";
	wifiStatus += ",\"hostname\":\"";
	wifiStatus += hostName.value;
	wifiStatus += "\"";
	return wifiStatus;
}

String clockFacesCallback() {
	const String postfix(".tar.gz");
	const String quote("\"");
	const String quoteColonQuote("\":\"");
	const String comma_quote(",\"");

	if (fileSet->value != "faces" && fileSet->value != "slides") {
		fileSet->value = "faces";
	}

	String freeSpace = String(LittleFS.totalBytes() - LittleFS.usedBytes());
	String options = "\"fs_free\":" + freeSpace + comma_quote;
	options += "face_files\":{";
	String sep = quote;
	String dirName = "/ips/" + fileSet->value;
	fs::File dir = LittleFS.open(dirName);
	String name = dir.getNextFileName();
	while(name.length() > 0){
		String fileName = name.substring(dirName.length() + 1, name.length());
		String option = fileName.substring(0, fileName.lastIndexOf(postfix));
		options += sep;
		options += option;
		options += quoteColonQuote;
		options += fileName;
		options += quote;
		sep = comma_quote;
		name = dir.getNextFileName();
	}
	dir.close();

	options += "}";
	return options;
}

void ledTaskFn(void *pArg) {
	backlights = new Backlights();
	backlights->begin();

	while (true) {
		if (ipsClock != NULL) {
			bool backlightOn = ipsClock->clockOn() || IPSClock::getDimming() == IPSClock::DIM;
			backlights->setOn(backlightOn);
			backlights->setBrightness(ipsClock->getBrightness());
			backlights->loop();
		}
		delay(16);
	}
}

void setWiFiCredentials(const char *ssid, const char *password) {
	WiFi.disconnect();
	wifiManager->setRouterCredentials(ssid, password);
	wifiManager->connect();
}

void improvTaskFn(void *pArg) {
	ImprovWiFi improvWiFi(
		manifest[0],
		manifest[1],
		manifest[2],
		manifest[3]
	);

	improvWiFi.setInfoCallback([](const char *msg) {tfts->setStatus(msg);});
	improvWiFi.setWiFiCallback(setWiFiCredentials);

	while (true) {
		xSemaphoreTake(wsMutex, portMAX_DELAY);
		improvWiFi.loop();
		xSemaphoreGive(wsMutex);
		delay(10);
	}
}

void commitEEPROMTaskFn(void *pArg) {
	while(true) {
		delay(60000);
		config.commit();
	}
}

void initFromEEPROM() {
	config.init();
	rootConfig.get();

	if (IPSClock::getTimeOrDate().value == IPSClock::WEATHER) {
		IPSClock::getTimeOrDate().value = IPSClock::TIME;
		IPSClock::getTimeOrDate().put();
	}
	if (IPSClock::getFourDigitDisplay().value == IPSClock::FOUR_WITH_WEATHER) {
		IPSClock::getFourDigitDisplay().value = IPSClock::FOUR;
		IPSClock::getFourDigitDisplay().put();
	}
	if (fileSet->value != "faces" && fileSet->value != "slides") {
		fileSet->value = "faces";
		fileSet->put();
	}

	hostnameParam = new AsyncWiFiManagerParameter("Hostname", "clock host name", hostName.value.c_str(), 63);
}

void connectedHandler() {
	tfts->setStatus(WiFi.localIP().toString());
	MDNS.end();
	MDNS.begin(hostName.value.c_str());
	MDNS.addService("http", "tcp", 80);
}

void apChange(AsyncWiFiManager *wifiManager) {
	if (wifiManager->isAP()) {
		tfts->setStatus(ssid);
	} else {
		tfts->setStatus("AP Destroyed...");
	}
}

void setWiFiAP(bool on) {
	if (on) {
		wifiManager->startConfigPortal(ssid.c_str(), "secretsauce");
	} else {
		wifiManager->stopConfigPortal();
	}
}

void SetupServer() {
	hostName = String(hostnameParam->getValue());
	hostName.put();
	config.commit();
	createSSID();
	wifiManager->setAPCredentials(ssid.c_str(), "secretsauce");
	MDNS.begin(hostName.value.c_str());
	MDNS.addService("http", "tcp", 80);
}

void wifiManagerTaskFn(void *pArg) {
	while(true) {
		xSemaphoreTake(wsMutex, portMAX_DELAY);
		wifiManager->loop();
		xSemaphoreGive(wsMutex);
		delay(50);
	}
}

void setup() {
	Serial.begin(115200);
	Serial.setDebugOutput(false);

	wsMutex = xSemaphoreCreateMutex();
	memMutex = xSemaphoreCreateMutex();
	tfts = new TFTs();

	LittleFS.begin();

	tfts->begin(LittleFS);
	tfts->fillScreen(TFT_BLACK);
	tfts->setTextColor(TFT_WHITE, TFT_BLACK);
	tfts->setCursor(0, 0, 2);
	tfts->setStatus("setup...");

	menu = new eSPIMenu::Menu(&tfts->getSprite());

	createSSID();

	EEPROM.begin(2048);
	initFromEEPROM();

	timeSync = new EspSNTPTimeSync(IPSClock::getTimeZone().value, asyncTimeSetCallback, asyncTimeErrorCallback);
	timeSync->init();

#ifndef DS1302
	rtcTimeSync = new EspRTCTimeSync(RTC_SDA_PIN, RTC_SCL_PIN);
	rtcTimeSync->init();
	rtcTimeSync->enabled(true);
#else
	rtcTimeSync = new EspDS1302TimeSync(DS1302_IO, DS1302_SCLK, DS1302_CE);
	rtcTimeSync->init();
	rtcTimeSync->enabled(true);
#endif

	IPSClock::getTimeZone().setCallback(onTimezoneChanged);

	xTaskCreatePinnedToCore(commitEEPROMTaskFn, "Commit EEPROM task", 2048, NULL, tskIDLE_PRIORITY, &commitEEPROMTask, xPortGetCoreID());
	xTaskCreatePinnedToCore(ledTaskFn, "led task", 1500, NULL, tskIDLE_PRIORITY + 2, &ledTask, 1);
	xTaskCreatePinnedToCore(clockTaskFn, "Clock task", 5000, NULL, tskIDLE_PRIORITY + 1, &clockTask, 0);
	xTaskCreatePinnedToCore(improvTaskFn, "Improv task", 2048, NULL, tskIDLE_PRIORITY + 1, &improvTask, 0);

	tfts->setStatus("Connecting...");

	wifiManager->setDebugOutput(false);
	wifiManager->setHostname(hostName.value.c_str());
	wifiManager->setCustomOptionsHTML("<br><form action='/t' name='time_form' method='post'><button name='time' onClick=\"{var now=new Date();this.value=now.getFullYear()+','+(now.getMonth()+1)+','+now.getDate()+','+now.getHours()+','+now.getMinutes()+','+now.getSeconds();} return true;\">Set Clock Time</button></form><br><form action=\"/app.html\" method=\"get\"><button>Configure Clock</button></form>");
	wifiManager->addParameter(hostnameParam);
	wifiManager->setSaveConfigCallback(SetupServer);
	wifiManager->setConnectedCallback(connectedHandler);
	wifiManager->setConnectTimeout(5000);
	wifiManager->setAPCallback(apChange);
	wifiManager->setAPCredentials(ssid.c_str(), "secretsauce");
	wifiManager->start();

	configureWebServer();
	esp_wifi_set_ps(WIFI_PS_NONE);
	xTaskCreatePinnedToCore(wifiManagerTaskFn, "WiFi Manager task", 3000, NULL, tskIDLE_PRIORITY + 2, &wifiManagerTask, 0);

	Serial.print("setup() running on core ");
	Serial.println(xPortGetCoreID());
	Serial.println(getCpuFrequencyMhz());

	vTaskDelete(NULL);
}

void loop() {
}
