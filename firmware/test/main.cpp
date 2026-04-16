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
// Simulated sensor values (edit the #defines below to tweak the test data):
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#include "./src/screens/battery/battery_screen.h"
#include "./src/screens/boost/boost_screen.h"
#include "./src/screens/egt/egt_screen.h"

// =============================================================================
// Simulated sensor values — tweak these to test different display states
// =============================================================================
#define SIM_BAT1_V      12.6f   // Battery 1 voltage (V)
#define SIM_BAT2_V      13.1f   // Battery 2 voltage (V)

#define SIM_BOOST_PSI    8.5f   // Boost pressure (PSI)

#define SIM_EGT_C      450.0f   // Exhaust gas temperature (°C)

// =============================================================================
// Screen selection
// =============================================================================
enum ScreenID {
    SCREEN_BATTERY = 1,
    SCREEN_BOOST   = 2,
    SCREEN_EGT     = 3,
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
    Serial.println("============================================");
    Serial.println("  Pajero Gauges — Screen Test Harness");
    Serial.println("============================================");
    Serial.println("  Select a screen to preview:");
    Serial.println("    1 — Battery voltage");
    Serial.println("    2 — Boost gauge");
    Serial.println("    3 — EGT gauge");
    Serial.println();
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
    if (choice >= SCREEN_BATTERY && choice <= SCREEN_EGT) {
        return (ScreenID)choice;
    }

    Serial.printf("No valid input — loading default screen (%d)\n", DEFAULT_SCREEN);
    return DEFAULT_SCREEN;
}

static void initDisplay() {
    Wire.begin(OLED_SDA, OLED_SCL);
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
static void runBatteryScreen() {
    Serial.printf("BATTERY SCREEN  BAT1=%.1fV  BAT2=%.1fV\n",
                  SIM_BAT1_V, SIM_BAT2_V);

    batteryScreen_init(&display);

    while (true) {
        batteryScreen_update(SIM_BAT1_V, SIM_BAT2_V);
        delay(500);
    }
}

static void runBoostScreen() {
    Serial.printf("BOOST SCREEN  boost=%.1f PSI\n", SIM_BOOST_PSI);

    float baselineKpa = 101.3f;   // sea-level reference for the splash animation
    boostScreen_init(&display, baselineKpa);

    while (true) {
        boostScreen_update(SIM_BOOST_PSI);
        delay(50);
    }
}

static void runEgtScreen() {
    Serial.printf("EGT SCREEN  egt=%.0f C\n", SIM_EGT_C);

    egtScreen_init(&display);

    while (true) {
        egtScreen_update(SIM_EGT_C);
        delay(50);
    }
}

// =============================================================================
// Arduino entry points
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);   // let the serial monitor connect before printing the menu

    initDisplay();

    ScreenID screen = waitForSelection();

    switch (screen) {
        case SCREEN_BATTERY: runBatteryScreen(); break;
        case SCREEN_BOOST:   runBoostScreen();   break;
        case SCREEN_EGT:     runEgtScreen();     break;
    }
}

void loop() {
    // All screen runners loop internally — nothing to do here.
}
