// =============================================================================
// test/ota/ota.cpp — Test-environment OTA (always-on WiFi + screen commands)
//
// Stripped-down OTA for the test harness:
//   - No diagnostic sensor streaming (test has no real sensors)
//   - Command callback forwards unrecognised WebSerial commands to the
//     test harness for screen switching
//   - Core OTA functionality identical to production
// =============================================================================

#include "ota.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebSerial.h>
#include <ESPAsyncWebServer.h>

// =============================================================================
// Build info
// =============================================================================
#ifndef BUILD_VERSION
#define BUILD_VERSION "test-dev"
#endif
#ifndef BUILD_TIMESTAMP
#define BUILD_TIMESTAMP __DATE__ " " __TIME__
#endif

// =============================================================================
// Configuration
// =============================================================================
#define OTA_AP_SSID     "PajeroGauges"
#define OTA_AP_PASS     "noahspajero"
#define RESETS_NEEDED   3

// =============================================================================
// Module state
// =============================================================================
static Preferences    _prefs;
static bool           _otaMode = false;
static AsyncWebServer _webServer(80);
static DNSServer      _dnsServer;
static OtaCmdCallback _cmdCallback = nullptr;

static const char *OTA_HOSTNAME = "pajero-gauges";

// Performance tracking
static unsigned long _loopCount = 0;
static unsigned long _maxLoopTimeUs = 0;
static unsigned long _lastLoopUs = 0;

// Screen state tracking (set by test harness via callback)
static String _currentScreen = "off";


// =============================================================================
// Helpers
// =============================================================================
static String formatUptime(unsigned long ms) {
    unsigned long secs = ms / 1000;
    unsigned long mins = secs / 60;
    unsigned long hrs  = mins / 60;
    unsigned long days = hrs / 24;

    if (days > 0) {
        return String(days) + "d " + String(hrs % 24) + "h " + String(mins % 60) + "m";
    } else if (hrs > 0) {
        return String(hrs) + "h " + String(mins % 60) + "m " + String(secs % 60) + "s";
    } else if (mins > 0) {
        return String(mins) + "m " + String(secs % 60) + "s";
    } else {
        return String(secs) + "s";
    }
}

static void logBoth(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.print(buf);
    if (_otaMode) WebSerial.print(buf);
}


// =============================================================================
// Internal — start WiFi AP + OTA + WebSerial
// =============================================================================
static void startOta() {
    _otaMode = true;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(OTA_AP_SSID, OTA_AP_PASS);
    Serial.printf("WiFi AP: %s  IP: %s\n", OTA_AP_SSID,
                  WiFi.softAPIP().toString().c_str());

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.onStart([]() {
        logBoth("OTA: Starting %s update...\n",
                ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
    });
    ArduinoOTA.onEnd([]() {
        logBoth("\nOTA: Complete! Rebooting...\n");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static int lastPct = -1;
        int pct = (progress * 100) / total;
        if (pct != lastPct && pct % 10 == 0) {
            logBoth("OTA: %d%%\n", pct);
            lastPct = pct;
        }
    });
    ArduinoOTA.onError([](ota_error_t e) {
        const char *errStr = "Unknown";
        switch (e) {
            case OTA_AUTH_ERROR:    errStr = "Auth failed"; break;
            case OTA_BEGIN_ERROR:   errStr = "Begin failed"; break;
            case OTA_CONNECT_ERROR: errStr = "Connect failed"; break;
            case OTA_RECEIVE_ERROR: errStr = "Receive failed"; break;
            case OTA_END_ERROR:     errStr = "End failed"; break;
        }
        logBoth("OTA ERROR: %s (code %u)\n", errStr, e);
    });
    ArduinoOTA.begin();

    MDNS.begin(OTA_HOSTNAME);
    MDNS.addService("http", "tcp", 80);

    // Captive portal
    _dnsServer.start(53, "*", WiFi.softAPIP());

    _webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->redirect("/webserial");
    });
    _webServer.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->redirect("/webserial");
    });
    _webServer.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->redirect("/webserial");
    });

    WebSerial.begin(&_webServer);
    WebSerial.onMessage([](uint8_t *data, size_t len) {
        String cmd = String((char *)data, len);
        cmd.trim();
        cmd.toLowerCase();

        if (cmd == "reboot" || cmd == "restart" || cmd == "reset" || cmd == "rst") {
            WebSerial.println("Rebooting in 1 second...");
            delay(1000);
            ESP.restart();

        } else if (cmd == "info") {
            WebSerial.println("=== System Info ===");
            WebSerial.printf("Mode: TEST HARNESS\n");
            WebSerial.printf("Version: %s\n", BUILD_VERSION);
            WebSerial.printf("Built: %s\n", BUILD_TIMESTAMP);
            WebSerial.printf("Chip: %s Rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
            WebSerial.printf("CPU: %d MHz\n", ESP.getCpuFreqMHz());
            WebSerial.printf("Flash: %uKB @ %uMHz\n",
                             ESP.getFlashChipSize() / 1024,
                             ESP.getFlashChipSpeed() / 1000000);
            WebSerial.printf("Sketch: %uKB / %uKB (%.1f%%)\n",
                             ESP.getSketchSize() / 1024,
                             ESP.getFreeSketchSpace() / 1024,
                             100.0f * ESP.getSketchSize() / (ESP.getSketchSize() + ESP.getFreeSketchSpace()));
            WebSerial.printf("Uptime: %s\n", formatUptime(millis()).c_str());
            WebSerial.printf("Current screen: %s\n", _currentScreen.c_str());

        } else if (cmd == "mem" || cmd == "memory" || cmd == "heap") {
            WebSerial.println("=== Memory ===");
            WebSerial.printf("Free heap: %uKB\n", ESP.getFreeHeap() / 1024);
            WebSerial.printf("Min free heap: %uKB\n", ESP.getMinFreeHeap() / 1024);
            WebSerial.printf("Largest free block: %uKB\n", ESP.getMaxAllocHeap() / 1024);
            WebSerial.printf("PSRAM: %s\n", psramFound() ? "Yes" : "No");

        } else if (cmd == "wifi" || cmd == "net" || cmd == "network") {
            WebSerial.println("=== WiFi ===");
            WebSerial.printf("Mode: AP (always-on for test)\n");
            WebSerial.printf("SSID: %s\n", OTA_AP_SSID);
            WebSerial.printf("IP: %s\n", WiFi.softAPIP().toString().c_str());
            WebSerial.printf("MAC: %s\n", WiFi.softAPmacAddress().c_str());
            WebSerial.printf("Clients connected: %d\n", WiFi.softAPgetStationNum());
            WebSerial.printf("Hostname: %s.local\n", OTA_HOSTNAME);

        } else if (cmd == "perf" || cmd == "timing" || cmd == "loop") {
            WebSerial.println("=== Performance ===");
            WebSerial.printf("Loop count: %lu\n", _loopCount);
            WebSerial.printf("Max loop time: %lu us\n", _maxLoopTimeUs);
            float avgHz = (_loopCount > 0) ? (1000000.0f * _loopCount / millis()) : 0;
            WebSerial.printf("Avg loop rate: %.1f Hz\n", avgHz);

        } else if (cmd == "screen") {
            WebSerial.printf("Current screen: %s\n", _currentScreen.c_str());
            WebSerial.println("Available: battery, boost, egt, afr, off");

        } else if (cmd == "help" || cmd == "?") {
            WebSerial.println("=== Test Harness Commands ===");
            WebSerial.println("Screens (fake sensor data):");
            WebSerial.println("  battery  — show battery screen");
            WebSerial.println("  boost    — show boost screen");
            WebSerial.println("  egt      — show EGT screen");
            WebSerial.println("  afr      — show AFR screen");
            WebSerial.println("  off      — blank screen");
            WebSerial.println("  screen   — show current screen");
            WebSerial.println("System:");
            WebSerial.println("  info     — version/chip/flash info");
            WebSerial.println("  mem      — heap/memory stats");
            WebSerial.println("  wifi     — network info");
            WebSerial.println("  perf     — loop timing stats");
            WebSerial.println("  reboot   — restart ESP32");
            WebSerial.println("OTA:");
            WebSerial.println("  Upload via PlatformIO or Arduino IDE");
            WebSerial.printf("  IP: %s  Port: 3232\n", WiFi.softAPIP().toString().c_str());

        } else {
            // Forward to test harness for screen switching
            if (_cmdCallback) {
                _cmdCallback(cmd.c_str());
            } else {
                WebSerial.printf("Unknown: '%s' (type 'help')\n", cmd.c_str());
            }
        }
    });

    _webServer.begin();
    Serial.printf("WebSerial: http://%s.local/webserial\n", OTA_HOSTNAME);
}


// =============================================================================
// Public API
// =============================================================================

bool ota_checkAndStart() {
    _prefs.begin("ota", false);
    uint8_t count = _prefs.getUChar("rstcnt", 0);
    count++;
    _prefs.putUChar("rstcnt", count);
    _prefs.end();

    Serial.printf("Reset count: %d/%d\n", count, RESETS_NEEDED);

    if (count >= RESETS_NEEDED) {
        ota_clearResetCounter();
        Serial.println(">>> TRIPLE-RESET DETECTED — entering OTA mode <<<");
        startOta();
        return true;
    }
    return false;
}


void ota_forceStart() {
    if (!_otaMode) {
        ota_clearResetCounter();
        startOta();
    }
}


void ota_handle() {
    if (!_otaMode) return;

    // Track loop timing
    unsigned long nowUs = micros();
    if (_lastLoopUs > 0) {
        unsigned long elapsed = nowUs - _lastLoopUs;
        if (elapsed > _maxLoopTimeUs) _maxLoopTimeUs = elapsed;
    }
    _lastLoopUs = nowUs;
    _loopCount++;

    ArduinoOTA.handle();
    _dnsServer.processNextRequest();
}


bool ota_isActive() {
    return _otaMode;
}


void ota_setCmdCallback(OtaCmdCallback cb) {
    _cmdCallback = cb;
}


void ota_setCurrentScreen(const char *screen) {
    _currentScreen = String(screen);
}


void ota_clearResetCounter() {
    _prefs.begin("ota", false);
    _prefs.putUChar("rstcnt", 0);
    _prefs.end();
}
