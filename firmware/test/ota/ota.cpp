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
    ArduinoOTA.onStart([]() { Serial.println("OTA: start"); });
    ArduinoOTA.onEnd([]()   { Serial.println("OTA: done — rebooting"); });
    ArduinoOTA.onProgress([](unsigned int pct, unsigned int total) {
        Serial.printf("OTA: %u%%\r", pct * 100 / total);
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("OTA error: %u\n", e);
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
            WebSerial.println("Rebooting...");
            delay(500);
            ESP.restart();
        } else if (cmd == "info") {
            WebSerial.printf("Chip: %s  Rev: %d\n", ESP.getChipModel(), ESP.getChipRevision());
            WebSerial.printf("Flash: %uKB  Free heap: %uKB\n",
                             ESP.getFlashChipSize() / 1024,
                             ESP.getFreeHeap() / 1024);
            WebSerial.printf("Sketch: %uKB / %uKB\n",
                             ESP.getSketchSize() / 1024,
                             ESP.getFreeSketchSpace() / 1024);
            WebSerial.printf("Uptime: %lus\n", millis() / 1000);
            WebSerial.printf("WiFi AP: %s  IP: %s\n", OTA_AP_SSID,
                             WiFi.softAPIP().toString().c_str());
            WebSerial.printf("Clients: %d\n", WiFi.softAPgetStationNum());
        } else if (cmd == "help" || cmd == "?") {
            WebSerial.println("Commands:");
            WebSerial.println("  battery — show battery screen");
            WebSerial.println("  boost   — show boost screen");
            WebSerial.println("  egt     — show EGT screen");
            WebSerial.println("  afr     — show AFR screen");
            WebSerial.println("  off     — blank screen");
            WebSerial.println("  info    — chip/memory/uptime");
            WebSerial.println("  reboot  — restart ESP32");
            WebSerial.println("  help    — this message");
        } else {
            // Forward to test harness for screen switching
            if (_cmdCallback) {
                _cmdCallback(cmd.c_str());
            } else {
                WebSerial.printf("Unknown command: '%s' (type 'help')\n", cmd.c_str());
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
    ArduinoOTA.handle();
    _dnsServer.processNextRequest();
}


bool ota_isActive() {
    return _otaMode;
}


void ota_setCmdCallback(OtaCmdCallback cb) {
    _cmdCallback = cb;
}


void ota_clearResetCounter() {
    _prefs.begin("ota", false);
    _prefs.putUChar("rstcnt", 0);
    _prefs.end();
}
