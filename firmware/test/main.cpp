// =============================================================================
// test/main.cpp — Single-display screen layout test harness
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
//   pio run -e test --target upload
//
// Usage:
//   Open the serial monitor (115200 baud).  At startup a menu is printed —
//   type the number of the screen you want to test and press Enter.
//   If no input arrives within 3 s, the default screen (battery) is loaded.
//
//   To switch screens, press the EN/RST button on the board and choose again.
//
// Each screen runner picks random values within a realistic range each update.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>

#include "ota/ota.h"
#include "screens/battery/battery_screen.h"
#include "screens/boost/boost_screen.h"
#include "screens/egt/egt_screen.h"
#include "screens/afr/afr_screen.h"

// =============================================================================
// Screen selection
// =============================================================================
enum ScreenID {
    SCREEN_BATTERY = 1,
    SCREEN_BOOST   = 2,
    SCREEN_EGT     = 3,
    SCREEN_AFR     = 4,
    SCREEN_OTA     = 5,
};

#define DEFAULT_SCREEN  SCREEN_BATTERY   // ← loaded if no input within timeout

// =============================================================================
// Display — single SH1106 at 0x3C, no mux
// =============================================================================
#define OLED_SDA    21
#define OLED_SCL    22
#define OLED_ADDR   0x3C

Adafruit_SH1106G display(128, 64, &Wire, -1);

// =============================================================================
// Helpers
// =============================================================================
static void printMenu() {
    Serial.println();
    Serial.println();
    Serial.println();
    Serial.println();
    Serial.println("============================================");
    Serial.println("  Pajero Gauges — Screen Test Harness");
    Serial.println("============================================");
    Serial.println("  Select a screen to preview:");
    Serial.println("    1 — Battery voltage");
    Serial.println("    2 — Boost gauge");
    Serial.println("    3 — EGT gauge");
    Serial.println("    4 — AFR gauge");
    Serial.println("    5 — OTA WiFi mode");
    Serial.println();
    Serial.println("  (or press EN/RST 3x rapidly for OTA)");
    Serial.printf("  (default: %d in 3 s if no input)\n", DEFAULT_SCREEN);
    Serial.println("============================================");
    Serial.print("> ");
}

static ScreenID waitForSelection() {
    printMenu();

    const unsigned long deadline = millis() + 5000;
    String input = "";

    while (millis() < deadline) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (input.length() > 0) break;
            } else {
                input += c;
                Serial.print(c);  // echo
            }
        }
    }

    Serial.println();

    int choice = input.toInt();
    if (choice >= SCREEN_BATTERY && choice <= SCREEN_OTA) {
        return (ScreenID)choice;
    }

    Serial.printf("No valid input — loading default screen (%d)\n", DEFAULT_SCREEN);
    return DEFAULT_SCREEN;
}

static void initDisplay() {
    Wire.begin(OLED_SDA, OLED_SCL, 400000);   // 400 kHz Fast Mode
    if (!display.begin(OLED_ADDR, false)) {
        Serial.println("ERROR: OLED not found — check wiring and address!");
        while (true) delay(1000);
    }
    display.clearDisplay();
    display.display();
}

// =============================================================================
// Screen runners
// =============================================================================
static float randomFloat(float lo, float hi) {
    return lo + (hi - lo) * (random(0, 10000) / 10000.0f);
}

static void runBatteryScreen() {
    Serial.println("BATTERY SCREEN — random 11.8–14.8 V");

    batteryScreen_init(&display);

    while (true) {
        float bat1 = randomFloat(11.8f, 14.8f);
        float bat2 = randomFloat(11.8f, 14.8f);
        Serial.printf("Updated BAT1=%.1fV  BAT2=%.1fV\n", bat1, bat2);
        batteryScreen_update(bat1, bat2);
        delay(500);
    }
}

static void runBoostScreen() {
    Serial.println("BOOST SCREEN — random 0–18 PSI");

    boostScreen_init(&display);

    while (true) {
        float boost = randomFloat(0.0f, 18.0f);
        Serial.printf("Updated Boost=%.1f PSI\n", boost);
        boostScreen_update(boost);
        delay(50);
    }
}

static void runEgtScreen() {
    Serial.println("EGT SCREEN — random 200–700 C");

    egtScreen_init(&display);

    while (true) {
        float egt = randomFloat(200.0f, 700.0f);
        Serial.printf("Updated EGT=%.0f C\n", egt);
        egtScreen_update(egt);
        delay(50);
    }
}

static void runAfrScreen() {
    Serial.println("AFR SCREEN — random 15.0–50.0");

    afrScreen_init(&display);

    while (true) {
        float afr = randomFloat(15.0f, 50.0f);
        Serial.printf("Updated AFR=%.1f\n", afr);
        afrScreen_update(afr);
        delay(50);
    }
}

static void runOtaScreen() {
    Serial.println("OTA WIFI MODE");
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(4, 0);
    display.print("OTA WiFi Mode");
    display.setCursor(4, 30);
    display.printf("AP: %s", "PajeroGauges");
    display.setCursor(4, 46);
    display.printf("IP: %s", WiFi.softAPIP().toString().c_str());
    display.display();

    while (true) {
        ota_handle();
        delay(10);
    }
}

// =============================================================================
// Arduino entry points
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);   // let the serial monitor connect before printing the menu

    // Triple-reset detection — press EN button 3x rapidly for OTA
    if (ota_checkAndStart()) {
        initDisplay();
        runOtaScreen();
        // never returns
    }

    initDisplay();

    ScreenID screen = waitForSelection();

    // Clear reset counter now that we're past the selection window
    ota_clearResetCounter();

    switch (screen) {
        case SCREEN_BATTERY: runBatteryScreen(); break;
        case SCREEN_BOOST:   runBoostScreen();   break;
        case SCREEN_EGT:     runEgtScreen();     break;
        case SCREEN_AFR:     runAfrScreen();     break;
        case SCREEN_OTA:     ota_forceStart(); runOtaScreen(); break;
    }
}

void loop() {
    // All screen runners loop internally — nothing to do here.
}
