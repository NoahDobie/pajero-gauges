// =============================================================================
// Pajero Gauges — ESP32 Firmware [DEV BUILD]
// Target hardware : Custom PCB rev 1, 30-pin ELEGOO ESP32 dev board
// Vehicle         : 1993 Mitsubishi Pajero 4D56T Diesel
//
// Main orchestrator — owns hardware (ADC, I2C, display) and delegates
// screen rendering to individual screen modules.
//
// Active inputs:
//   GPIO35  MAP sensor 1  (10k / 20k divider, 1k series resistor) — intake manifold
//   GPIO34  MAP sensor 2  (10k / 20k divider, 1k series resistor) — barometric reference
//   GPIO33  Battery 1     (27k / 5.1k divider) — primary battery
//   GPIO32  Battery 2     (27k / 5.1k divider) — auxiliary battery
//
// Displays (via TCA9548A mux at 0x70 on GPIO21 SDA / GPIO22 SCL):
//   Channel 0 — Boost gauge OLED   (SH1106 128×64 at 0x3C)
//   Channel 1 — Battery voltage OLED (SH1106 128×64 at 0x3C)
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_SSD1306.h>          // TEMP — for 0.96" test display

#include "screens/boost/boost_screen.h"
#include "screens/battery/battery_screen.h"

// =============================================================================
// Pin definitions
// =============================================================================
#define PIN_MAP1    35      // MAP sensor 1 — intake manifold
#define PIN_MAP2    34      // MAP sensor 2 — barometric reference

#define PIN_BAT1    33      // Battery 1 voltage sense
#define PIN_BAT2    32      // Battery 2 voltage sense

#define I2C_SDA     21      // I2C SDA to TCA9548A mux
#define I2C_SCL     22      // I2C SCL to TCA9548A mux

// =============================================================================
// TCA9548A I2C multiplexer
// =============================================================================
#define MUX_ADDR      0x70  // TCA9548A default I2C address
#define MUX_CH_BOOST    0   // Mux channel for the boost OLED
#define MUX_CH_BATTERY  1   // Mux channel for the battery OLED

// =============================================================================
// ADC configuration
// =============================================================================
#define ADC_MAX_RAW     4095.0f
#define ADC_VREF        3.3f
#define ADC_SAMPLES     16

// =============================================================================
// MAP voltage divider: top = 10k, bottom = 20k  →  Vsensor = Vadc × 1.5
// =============================================================================
#define MAP_TOP_R       10.0f
#define MAP_BOT_R       20.0f
#define MAP_DIVIDER     ((MAP_TOP_R + MAP_BOT_R) / MAP_BOT_R)

// =============================================================================
// Battery voltage divider: top = 27k, bottom = 5.1k  →  Vbat = Vadc × 6.294
// =============================================================================
#define BAT_TOP_R       27.0f
#define BAT_BOT_R        5.1f
#define BAT_DIVIDER     ((BAT_TOP_R + BAT_BOT_R) / BAT_BOT_R)

// =============================================================================
// GM 3-bar MAP sensor calibration (per datasheet)
//
// Linear transfer function:
//   0.0 V →   1.1 kPa
//   5.0 V → 315.5 kPa
// =============================================================================
#define MAP_V_LOW       0.0f
#define MAP_V_HIGH      5.0f
#define MAP_KPA_LOW     1.1f
#define MAP_KPA_HIGH  315.5f

// =============================================================================
// Display objects — one per physical OLED, each with its own framebuffer
// =============================================================================
Adafruit_SH1106G boostDisplay(128, 64, &Wire, -1);
Adafruit_SSD1306 batteryDisplay(128, 64, &Wire, -1);  // TEMP — SSD1306 for 0.96" test display

// =============================================================================
// Loop timing
// =============================================================================
#define UPDATE_INTERVAL_MS  50      // ~20 Hz — fast enough for smooth bar graph


// =============================================================================
// ADC helper
// =============================================================================
static float readADCVoltage(uint8_t pin) {
    uint32_t sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        sum += analogRead(pin);
        delayMicroseconds(100);
    }
    return ((float)(sum / ADC_SAMPLES) / ADC_MAX_RAW) * ADC_VREF;
}

// =============================================================================
// Conversion helpers
// =============================================================================

// Divided ADC voltage → absolute MAP pressure in kPa.
// The sensor outputs 0–5 V linearly; constrain guards against out-of-range noise.
static float adcToMapKpa(float adcV) {
    float sensorV = constrain(adcV * MAP_DIVIDER, MAP_V_LOW, MAP_V_HIGH);
    return (sensorV - MAP_V_LOW) / (MAP_V_HIGH - MAP_V_LOW)
           * (MAP_KPA_HIGH - MAP_KPA_LOW) + MAP_KPA_LOW;
}

// Altitude-compensated gauge boost in PSI
static float relativeBoostPsi(float absKpa, float refKpa) {
    return (absKpa - refKpa) * 0.14504f;
}

// Divided ADC voltage → actual battery voltage (recover through divider)
static float adcToBatVoltage(float adcV) {
    return adcV * BAT_DIVIDER;
}


// =============================================================================
// TCA9548A channel selector
//
// Writes a single byte to the mux — each bit enables one downstream channel.
// Only one channel is opened at a time; writing 0 would close all channels.
// =============================================================================
static void tcaSelect(uint8_t channel) {
    Wire.beginTransmission(MUX_ADDR);
    Wire.write(1 << channel);
    Wire.endTransmission();
}


// =============================================================================
// Setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Pajero Gauges - DEV BUILD ===");

    // ADC
    analogSetPinAttenuation(PIN_MAP1, ADC_11db);
    analogSetPinAttenuation(PIN_MAP2, ADC_11db);
    analogSetPinAttenuation(PIN_BAT1, ADC_11db);
    analogSetPinAttenuation(PIN_BAT2, ADC_11db);
    pinMode(PIN_MAP1, INPUT);
    pinMode(PIN_MAP2, INPUT);
    pinMode(PIN_BAT1, INPUT);
    pinMode(PIN_BAT2, INPUT);

    // I2C + mux + displays
    Wire.begin(I2C_SDA, I2C_SCL);

    tcaSelect(MUX_CH_BOOST);
    boostDisplay.begin(0x3C, false);
    boostDisplay.clearDisplay();
    boostDisplay.display();

    tcaSelect(MUX_CH_BATTERY);
    batteryDisplay.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // TEMP — SSD1306 init
    batteryDisplay.clearDisplay();
    batteryDisplay.display();

    // Hand each display to its screen module.
    // The boost splash animation runs inside this call.
    tcaSelect(MUX_CH_BOOST);
    float baselineKpa = 0.0f;
    boostScreen_init(&boostDisplay, baselineKpa);

    tcaSelect(MUX_CH_BATTERY);
    batteryScreen_init(&batteryDisplay);

    Serial.println("Ready.\n");
}


// =============================================================================
// Main loop
// =============================================================================
void loop() {
    // --- Read sensors ---
    float map1Kpa  = adcToMapKpa(readADCVoltage(PIN_MAP1));
    float map2Kpa  = adcToMapKpa(readADCVoltage(PIN_MAP2));
    float boostPsi = relativeBoostPsi(map1Kpa, map2Kpa);

    float bat1V = adcToBatVoltage(readADCVoltage(PIN_BAT1));
    float bat2V = adcToBatVoltage(readADCVoltage(PIN_BAT2));

    Serial.printf("MAP1: %.1f kPa | MAP2: %.1f kPa | Boost: %.1f PSI | BAT1: %.1fV | BAT2: %.1fV\n",
                  map1Kpa, map2Kpa, boostPsi, bat1V, bat2V);

    // --- Update displays ---
    tcaSelect(MUX_CH_BOOST);
    boostScreen_update(boostPsi);

    tcaSelect(MUX_CH_BATTERY);
    batteryScreen_update(bat1V, bat2V);

    delay(UPDATE_INTERVAL_MS);
}
