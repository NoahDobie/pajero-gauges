// =============================================================================
// test/main.cpp — Single-display test harness with always-on OTA
//
// Target hardware : Any bare ESP32 dev board + 1× 1.3" SH1106 OLED
//
// Wiring (no TCA9548A mux required):
//   OLED VCC  → 3V3
//   OLED GND  → GND
//   OLED SDA  → GPIO21
//   OLED SCL  → GPIO22
//
// Build & flash:
//   pio run -e test --target upload --target monitor
//
// Usage:
//   OTA WiFi is always active — connect to "PajeroGauges" AP and open
//   http://192.168.4.1/webserial for the WebSerial console.
//
//   Switch screens via Serial monitor OR WebSerial:
//     battery / boost / egt / afr / off   — select screen
//     help                                — list commands
//
//   Each screen gets fake random sensor values at its normal update rate.
//   All updates are logged to both Serial and WebSerial.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WebSerial.h>

#include "ota/ota.h"
#include "screens/battery/battery_screen.h"
#include "screens/boost/boost_screen.h"
#include "screens/egt/egt_screen.h"
#include "screens/afr/afr_screen.h"

// =============================================================================
// Display — single SH1106 at 0x3C, no mux
// =============================================================================
#define OLED_SDA    21
#define OLED_SCL    22
#define OLED_ADDR   0x3C

Adafruit_SH1106G display(128, 64, &Wire, -1);

// =============================================================================
// Active screen state
// =============================================================================
enum ScreenID { SCREEN_OFF = 0, SCREEN_BATTERY, SCREEN_BOOST, SCREEN_EGT, SCREEN_AFR };

static ScreenID _activeScreen  = SCREEN_OFF;
static ScreenID _pendingScreen = SCREEN_OFF;
static unsigned long _lastUpdateMs = 0;

// Update interval — all screens update once per second in test
#define SCREEN_INTERVAL_MS   1000

// =============================================================================
// Helpers
// =============================================================================
static float randomFloat(float lo, float hi) {
    return lo + (hi - lo) * (random(0, 10000) / 10000.0f);
}

// Log to both Serial and WebSerial
static void dualLog(const char *fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.print(buf);
    if (ota_isActive()) WebSerial.print(buf);
}

// =============================================================================
// Screen lifecycle
// =============================================================================
static void initScreen(ScreenID id) {
    display.clearDisplay();
    display.display();

    switch (id) {
        case SCREEN_BATTERY:
            batteryScreen_init(&display);
            dualLog("Screen: BATTERY (random 11.8–14.8 V)\n");
            break;
        case SCREEN_BOOST:
            boostScreen_init(&display);
            dualLog("Screen: BOOST (random -5–18 PSI)\n");
            break;
        case SCREEN_EGT:
            egtScreen_init(&display);
            dualLog("Screen: EGT (random 200–700 °C)\n");
            break;
        case SCREEN_AFR:
            afrScreen_init(&display);
            dualLog("Screen: AFR (random 10–20)\n");
            break;
        case SCREEN_OFF:
            display.clearDisplay();
            display.setTextColor(SH110X_WHITE);
            display.setTextSize(1);
            display.setCursor(10, 28);
            display.print("Type 'help' in");
            display.setCursor(10, 40);
            display.print("Serial or WebSerial");
            display.display();
            dualLog("Screen: OFF\n");
            break;
    }
    _lastUpdateMs = millis();
}

static void updateScreen() {
    unsigned long now = millis();

    if (_activeScreen == SCREEN_OFF) return;
    if ((now - _lastUpdateMs) < SCREEN_INTERVAL_MS) return;
    _lastUpdateMs = now;

    switch (_activeScreen) {
        case SCREEN_BATTERY: {
            float b1 = randomFloat(10.0f, 14.8f);
            float b2 = randomFloat(10.0f, 14.8f);
            batteryScreen_update(b1, b2);
            dualLog("BAT1=%.1fV  BAT2=%.1fV\n", b1, b2);
            break;
        }
        case SCREEN_BOOST: {
            float psi = randomFloat(0.0f, 18.0f);
            boostScreen_update(psi);
            dualLog("Boost=%.1f PSI\n", psi);
            break;
        }
        case SCREEN_EGT: {
            float egt = randomFloat(200.0f, 700.0f);
            egtScreen_update(egt);
            dualLog("EGT=%.0f °C\n", egt);
            break;
        }
        case SCREEN_AFR: {
            float afr = randomFloat(10.0f, 20.0f);
            afrScreen_update(afr);
            dualLog("AFR=%.1f\n", afr);
            break;
        }
        default: break;
    }
}

// =============================================================================
// Command processing — shared between Serial and WebSerial
// =============================================================================
static void printHelp() {
    dualLog("Commands:\n");
    dualLog("  battery — show battery screen\n");
    dualLog("  boost   — show boost screen\n");
    dualLog("  egt     — show EGT screen\n");
    dualLog("  afr     — show AFR screen\n");
    dualLog("  off     — blank screen\n");
    dualLog("  help    — this message\n");
    dualLog("  (OTA commands also available)\n");
}

static void handleCommand(const String &raw) {
    String cmd = raw;
    cmd.trim();
    cmd.toLowerCase();
    if (cmd.length() == 0) return;

    if (cmd == "battery" || cmd == "bat" || cmd == "1") {
        _pendingScreen = SCREEN_BATTERY;
    } else if (cmd == "boost" || cmd == "map" || cmd == "2") {
        _pendingScreen = SCREEN_BOOST;
    } else if (cmd == "egt" || cmd == "temp" || cmd == "3") {
        _pendingScreen = SCREEN_EGT;
    } else if (cmd == "afr" || cmd == "4") {
        _pendingScreen = SCREEN_AFR;
    } else if (cmd == "off" || cmd == "0") {
        _pendingScreen = SCREEN_OFF;
    } else if (cmd == "help" || cmd == "?") {
        printHelp();
    } else {
        dualLog("Unknown: '%s' (type 'help')\n", cmd.c_str());
    }
}

// =============================================================================
// Arduino entry points
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    // Always start OTA — no triple-reset needed in test mode
    ota_forceStart();
    ota_clearResetCounter();

    // Forward unrecognised WebSerial commands to our screen handler
    ota_setCmdCallback([](const char *cmd) { handleCommand(String(cmd)); });

    // Init display
    Wire.begin(OLED_SDA, OLED_SCL, 400000);
    if (!display.begin(OLED_ADDR, false)) {
        Serial.println("ERROR: OLED not found!");
        while (true) delay(1000);
    }

    Serial.println("\n============================================");
    Serial.println("Pajero Gauges — Test Harness");
    Serial.println("OTA WiFi active");
    Serial.printf("WebSerial: http://%s/webserial\n",
                  WiFi.softAPIP().toString().c_str());
    Serial.println("Type 'help' for commands");
    Serial.println("============================================\n");

    // Start with screen off — user picks via command
    _pendingScreen = SCREEN_OFF;
}

void loop() {
    // Service OTA (WiFi, ArduinoOTA, WebSerial, DNS)
    ota_handle();

    // Apply pending screen switch
    if (_pendingScreen != _activeScreen) {
        _activeScreen = _pendingScreen;
        initScreen(_activeScreen);
    }

    // Update active screen with fake data
    updateScreen();

    // Read Serial input (line-buffered)
    static String serialBuf;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (serialBuf.length() > 0) {
                handleCommand(serialBuf);
                serialBuf = "";
            }
        } else {
            serialBuf += c;
        }
    }
}
