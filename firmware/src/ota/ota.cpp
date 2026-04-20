// =============================================================================
// ota.cpp — Triple-reset OTA mode with ArduinoOTA + WebSerial
//
// Uses ESP32 NVS (flash) to count consecutive resets within the detection
// window.  Three rapid resets (off-on-off-on-off-on) enters OTA mode.
// Normal ignition cycling (off → accessory → start) causes at most one or
// two power blips, so triple-reset avoids false triggers.
//
// NVS storage survives all reset types including full power loss — more
// reliable than RTC_NOINIT memory which can be wiped by some reset circuits.
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
// NVS storage — survives all resets including full power loss
// =============================================================================
static Preferences _prefs;

// =============================================================================
// Module state
// =============================================================================
static bool _otaMode = false;
static AsyncWebServer _webServer(80);
static DNSServer      _dnsServer;

static const char *OTA_HOSTNAME = "pajero-gauges";

// Diagnostic streaming state
static OtaDiagCallback _diagCallback = nullptr;
static String _diagMode = ""; // "" = off, else "battery"/"boost"/"egt"/"status"
static unsigned long _diagLastMs = 0;
#define DIAG_INTERVAL_MS  1000


// =============================================================================
// Public API
// =============================================================================

bool ota_checkAndStart() {
    _prefs.begin("ota", false);                  // read-write
    uint8_t count = _prefs.getUChar("rstcnt", 0);
    count++;
    _prefs.putUChar("rstcnt", count);
    _prefs.end();

    Serial.printf("Reset count: %d/%d\n", count, RESETS_NEEDED);

    if (count >= RESETS_NEEDED) {
        // Triple-reset detected — clear counter and enter OTA mode
        ota_clearResetCounter();

        Serial.println(">>> TRIPLE-RESET DETECTED — entering OTA mode <<<");
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

        // mDNS — allows http://pajero-gauges.local/webserial
        MDNS.begin(OTA_HOSTNAME);
        MDNS.addService("http", "tcp", 80);

        // Captive portal — resolve ALL DNS queries to our IP so any URL
        // typed in a browser (or the auto-popup) lands on the ESP32.
        _dnsServer.start(53, "*", WiFi.softAPIP());

        // Redirect root to /webserial for convenience
        _webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
            req->redirect("/webserial");
        });
        // Android captive portal detection
        _webServer.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *req) {
            req->redirect("/webserial");
        });
        // iOS / macOS captive portal detection
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
            } else if (cmd == "battery" || cmd == "bat") {
                if (!_diagCallback) { WebSerial.println("No sensor callback registered"); return; }
                _diagMode = "battery";
                _diagLastMs = 0;
                WebSerial.println("Streaming battery... (type 'stop' to end)");
            } else if (cmd == "boost" || cmd == "map") {
                if (!_diagCallback) { WebSerial.println("No sensor callback registered"); return; }
                _diagMode = "boost";
                _diagLastMs = 0;
                WebSerial.println("Streaming boost... (type 'stop' to end)");
            } else if (cmd == "egt" || cmd == "temp") {
                if (!_diagCallback) { WebSerial.println("No sensor callback registered"); return; }
                _diagMode = "egt";
                _diagLastMs = 0;
                WebSerial.println("Streaming EGT... (type 'stop' to end)");
            } else if (cmd == "status" || cmd == "all") {
                if (!_diagCallback) { WebSerial.println("No sensor callback registered"); return; }
                _diagMode = "status";
                _diagLastMs = 0;
                WebSerial.println("Streaming all sensors... (type 'stop' to end)");
            } else if (cmd == "stop" || cmd == "s") {
                _diagMode = "";
                WebSerial.println("Streaming stopped.");
            } else if (cmd == "help" || cmd == "?") {
                WebSerial.println("Commands:");
                WebSerial.println("  battery — stream ADC + voltage");
                WebSerial.println("  boost   — stream MAP kPa + PSI");
                WebSerial.println("  egt     — stream thermocouple C");
                WebSerial.println("  status  — stream all sensors");
                WebSerial.println("  stop    — stop streaming");
                WebSerial.println("  info    — chip/memory/uptime");
                WebSerial.println("  reboot  — restart ESP32");
                WebSerial.println("  help    — this message");
            } else {
                WebSerial.printf("Unknown command: '%s' (type 'help')\n", cmd.c_str());
            }
        });
        _webServer.begin();
        Serial.printf("WebSerial: http://%s.local/webserial\n", OTA_HOSTNAME);
        Serial.println("Waiting for OTA upload... (turn off ignition to exit)");
        return true;
    }

    // Not enough resets yet — keep counting
    return false;
}


void ota_handle() {
    if (!_otaMode) return;
    ArduinoOTA.handle();
    _dnsServer.processNextRequest();   // captive portal DNS

    // Diagnostic streaming at ~1 Hz
    if (_diagMode.length() > 0 && _diagCallback) {
        unsigned long now = millis();
        if ((now - _diagLastMs) >= DIAG_INTERVAL_MS) {
            _diagLastMs = now;
            _diagCallback(_diagMode.c_str());
        }
    }
}


bool ota_isActive() {
    return _otaMode;
}


void ota_setDiagCallback(OtaDiagCallback cb) {
    _diagCallback = cb;
}


void ota_clearResetCounter() {
    _prefs.begin("ota", false);
    _prefs.putUChar("rstcnt", 0);
    _prefs.end();
}


void ota_forceStart() {
    _prefs.begin("ota", false);
    _prefs.putUChar("rstcnt", RESETS_NEEDED - 1);
    _prefs.end();
    ota_checkAndStart();
}
