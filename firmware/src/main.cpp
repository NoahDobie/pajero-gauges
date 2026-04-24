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
//   MAX31855 K-type thermocouple (SCK=IO19, SO=IO18, CS=IO17) — exhaust gas temp
//
// Displays (via TCA9548A mux at 0x70 on GPIO21 SDA / GPIO22 SCL):
//   Channel 0 — Battery voltage OLED (SH1106 128x64 at 0x3C)
//   Channel 1 — AFR gauge OLED       (SH1106 128x64 at 0x3C)
//   Channel 2 — EGT gauge OLED       (SH1106 128x64 at 0x3C)
//   Channel 3 — Boost gauge OLED     (SH1106 128x64 at 0x3C)
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_MAX31855.h>
#include <SPI.h>
#include <WebSerial.h>

#include "ota/ota.h"
#include "screens/boost/boost_screen.h"
#include "screens/battery/battery_screen.h"
#include "screens/egt/egt_screen.h"
#include "screens/afr/afr_screen.h"

// =============================================================================
// Screen enable flags — set to true to activate a screen in the main loop.
// Disabled screens still get init'd (static chrome) but are not updated.
// =============================================================================
#define ENABLE_BOOST    true
#define ENABLE_BATTERY  true
#define ENABLE_EGT      false
#define ENABLE_AFR      false

// =============================================================================
// Pin definitions
// =============================================================================
#define PIN_MAP1    35
#define PIN_MAP2    34
#define PIN_BAT1    33
#define PIN_BAT2    32

#define PIN_TC_SCK  19
#define PIN_TC_SO   18
#define PIN_TC_CS   17

#define I2C_SDA     21
#define I2C_SCL     22

// =============================================================================
// TCA9548A I2C multiplexer
// =============================================================================
#define MUX_ADDR      0x70
#define MUX_CH_BATTERY  0
#define MUX_CH_AFR      1
#define MUX_CH_EGT      2
#define MUX_CH_BOOST    3

// =============================================================================
// ADC configuration
// =============================================================================
#define ADC_SAMPLES     16

// =============================================================================
// Voltage divider ratios
// =============================================================================
#define MAP_TOP_R       10.0f
#define MAP_BOT_R       20.0f
#define MAP_DIVIDER     ((MAP_TOP_R + MAP_BOT_R) / MAP_BOT_R)

#define BAT_TOP_R       27.0f
#define BAT_BOT_R        5.1f
#define BAT_DIVIDER     ((BAT_TOP_R + BAT_BOT_R) / BAT_BOT_R)

// =============================================================================
// GM 3-bar MAP sensor calibration
// =============================================================================
#define MAP_V_LOW       0.0f
#define MAP_V_HIGH      5.0f
#define MAP_KPA_LOW     1.1f
#define MAP_KPA_HIGH  315.5f

#define MAP_ATMOSPHERE_KPA  101.3f
#define MAP_REF_MIN_KPA      50.0f

// =============================================================================
// Display objects
// =============================================================================
Adafruit_SH1106G boostDisplay(128, 64, &Wire, -1);
Adafruit_SH1106G batteryDisplay(128, 64, &Wire, -1);
Adafruit_SH1106G egtDisplay(128, 64, &Wire, -1);
Adafruit_SH1106G afrDisplay(128, 64, &Wire, -1);

// =============================================================================
// MAX31855 thermocouple interface (software SPI)
// =============================================================================
Adafruit_MAX31855 thermocouple(PIN_TC_SCK, PIN_TC_CS, PIN_TC_SO);

// =============================================================================
// Loop timing — non-blocking
// =============================================================================
#define UPDATE_INTERVAL_MS      50    // ~20 Hz main loop
#define BATTERY_UPDATE_FRAMES   10    // ~500 ms
#define MAP2_UPDATE_FRAMES     100    // ~5 s for barometric ref averaging
#define EGT_UPDATE_FRAMES        4    // ~200 ms (MAX31855 has ~100ms conversion)

// =============================================================================
// ADC helper — fast, no delays
// =============================================================================
static float readADCVoltage(uint8_t pin) {
    uint32_t sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        sum += analogReadMilliVolts(pin);
    }
    return (float)(sum / ADC_SAMPLES) / 1000.0f;
}

// =============================================================================
// Conversion helpers
// =============================================================================
static float adcToMapKpa(float adcV) {
    float sensorV = constrain(adcV * MAP_DIVIDER, MAP_V_LOW, MAP_V_HIGH);
    return (sensorV - MAP_V_LOW) / (MAP_V_HIGH - MAP_V_LOW)
           * (MAP_KPA_HIGH - MAP_KPA_LOW) + MAP_KPA_LOW;
}

static float relativeBoostPsi(float absKpa, float refKpa) {
    return (absKpa - refKpa) * 0.14504f;
}

static float adcToBatVoltage(float adcV) {
    return adcV * BAT_DIVIDER;
}

// =============================================================================
// TCA9548A channel selector
// =============================================================================
static void tcaSelect(uint8_t channel) {
    Wire.beginTransmission(MUX_ADDR);
    Wire.write(1 << channel);
    Wire.endTransmission();
}

// =============================================================================
// Sensor baseline state
// =============================================================================
static float _baselineRefKpa = MAP_ATMOSPHERE_KPA;

// =============================================================================
// Diagnostic streaming callback
// =============================================================================
static void diagHandler(const char *mode) {
    String m(mode);

    if (m == "battery" || m == "status") {
        float adc1 = readADCVoltage(PIN_BAT1);
        float adc2 = readADCVoltage(PIN_BAT2);
        WebSerial.printf("BAT1: ADC=%.3fV  V=%.2f | BAT2: ADC=%.3fV  V=%.2f\n",
                         adc1, adcToBatVoltage(adc1),
                         adc2, adcToBatVoltage(adc2));
    }
    if (m == "boost" || m == "status") {
        float adc1 = readADCVoltage(PIN_MAP1);
        float adc2 = readADCVoltage(PIN_MAP2);
        float kpa1 = adcToMapKpa(adc1);
        float kpa2 = adcToMapKpa(adc2);
        WebSerial.printf("MAP1: ADC=%.3fV  kPa=%.1f  PSI=%.2f | MAP2 (Ref): ADC=%.3fV  kPa=%.1f\n",
                         adc1, kpa1, relativeBoostPsi(kpa1, kpa2),
                         adc2, kpa2);
    }
    if (m == "egt" || m == "status") {
        float egtC = thermocouple.readCelsius();
        float cjC  = thermocouple.readInternal();
        WebSerial.printf("EGT: %.1f°C  CJ: %.1f°C%s\n",
                         isnan(egtC) ? 0.0f : egtC, cjC,
                         isnan(egtC) ? "  [FAULT]" : "");
    }
}

// =============================================================================
// Setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Pajero Gauges ===");

    bool otaActive = ota_checkAndStart();
    if (otaActive) {
        Serial.println("OTA mode active — WiFi running alongside gauges");
    }

    // ADC setup
    analogSetPinAttenuation(PIN_MAP1, ADC_11db);
    analogSetPinAttenuation(PIN_MAP2, ADC_11db);
    analogSetPinAttenuation(PIN_BAT1, ADC_11db);
    analogSetPinAttenuation(PIN_BAT2, ADC_11db);
    pinMode(PIN_MAP1, INPUT);
    pinMode(PIN_MAP2, INPUT);
    pinMode(PIN_BAT1, INPUT);
    pinMode(PIN_BAT2, INPUT);

    // 3-second sensor preload
    const unsigned long preloadStart = millis();
    float map2Sum = 0.0f, bat1Sum = 0.0f, bat2Sum = 0.0f;
    int sampleCount = 0;

    while ((millis() - preloadStart) < 3000) {
        float m2 = adcToMapKpa(readADCVoltage(PIN_MAP2));
        float b1 = adcToBatVoltage(readADCVoltage(PIN_BAT1));
        float b2 = adcToBatVoltage(readADCVoltage(PIN_BAT2));
        map2Sum += m2;
        bat1Sum += b1;
        bat2Sum += b2;
        sampleCount++;
        delay(20);
    }

    float initBat1V = 0.0f, initBat2V = 0.0f;
    if (sampleCount > 0) {
        float avgMap2 = map2Sum / sampleCount;
        _baselineRefKpa = (avgMap2 >= MAP_REF_MIN_KPA) ? avgMap2 : MAP_ATMOSPHERE_KPA;
        initBat1V = bat1Sum / sampleCount;
        initBat2V = bat2Sum / sampleCount;
    }

    ota_clearResetCounter();

    Serial.printf("Preload: %d samples, ref=%.1f kPa, bat1=%.1fV, bat2=%.1fV\n",
                  sampleCount, _baselineRefKpa, initBat1V, initBat2V);

    // I2C + mux + displays
    Wire.begin(I2C_SDA, I2C_SCL, 400000);

    tcaSelect(MUX_CH_BOOST);
    boostDisplay.begin(0x3C, false);
    boostDisplay.clearDisplay();
    boostDisplay.display();

    tcaSelect(MUX_CH_BATTERY);
    batteryDisplay.begin(0x3C, false);
    batteryDisplay.clearDisplay();
    batteryDisplay.display();

    tcaSelect(MUX_CH_EGT);
    egtDisplay.begin(0x3C, false);
    egtDisplay.clearDisplay();
    egtDisplay.display();

    tcaSelect(MUX_CH_AFR);
    afrDisplay.begin(0x3C, false);
    afrDisplay.clearDisplay();
    afrDisplay.display();

    // Init all screens
    tcaSelect(MUX_CH_BOOST);
    boostScreen_init(&boostDisplay);

    tcaSelect(MUX_CH_EGT);
    egtScreen_init(&egtDisplay);

    tcaSelect(MUX_CH_AFR);
    afrScreen_init(&afrDisplay);

    tcaSelect(MUX_CH_BATTERY);
    batteryScreen_init(&batteryDisplay);
    batteryScreen_update(initBat1V, initBat2V);

    // OTA indicator
    if (otaActive) {
        tcaSelect(MUX_CH_BATTERY);
        batteryDisplay.setTextSize(1);
        batteryDisplay.setTextColor(SH110X_WHITE);
        batteryDisplay.setCursor(0, 0);
        batteryDisplay.print("OTA Wifi Mode");
        batteryDisplay.display();
    }

    if (ENABLE_EGT) {
        if (!thermocouple.begin()) {
            Serial.println("ERROR: MAX31855 not found!");
        }
    }

    ota_setDiagCallback(diagHandler);

    Serial.println("Ready.\n");
}

// =============================================================================
// Main loop — non-blocking timing
// =============================================================================
void loop() {
    // Service OTA/WiFi if active
    ota_handle();

    // Non-blocking timing
    static unsigned long lastUpdateMs = 0;
    unsigned long now = millis();
    if ((now - lastUpdateMs) < UPDATE_INTERVAL_MS) {
        return;
    }
    lastUpdateMs = now;

    // Frame counters
    static uint8_t batteryFrame = 0;
    static uint8_t map2Frame    = 0;
    static uint8_t egtFrame     = 0;

    // Barometric reference state (independent of boost enable)
    static float   refKpa      = _baselineRefKpa;
    static float   map2Sum     = 0.0f;
    static uint8_t map2Samples = 0;

    // --- MAP2 barometric reference sampling (always runs) ---
    if (map2Frame % 10 == 0) {
        map2Sum += adcToMapKpa(readADCVoltage(PIN_MAP2));
        map2Samples++;
    }
    if (++map2Frame >= MAP2_UPDATE_FRAMES) {
        map2Frame = 0;
        if (map2Samples > 0) {
            float avgMap2 = map2Sum / map2Samples;
            if (avgMap2 >= MAP_REF_MIN_KPA) refKpa = avgMap2;
        }
        map2Sum = 0.0f;
        map2Samples = 0;
    }

    // --- Boost ---
    if (ENABLE_BOOST) {
        float map1Kpa  = adcToMapKpa(readADCVoltage(PIN_MAP1));
        float boostPsi = relativeBoostPsi(map1Kpa, refKpa);

        tcaSelect(MUX_CH_BOOST);
        boostScreen_update(boostPsi);
    }

    // --- Battery — every ~500 ms ---
    if (ENABLE_BATTERY) {
        if (++batteryFrame >= BATTERY_UPDATE_FRAMES) {
            batteryFrame = 0;
            float bat1V = adcToBatVoltage(readADCVoltage(PIN_BAT1));
            float bat2V = adcToBatVoltage(readADCVoltage(PIN_BAT2));

            tcaSelect(MUX_CH_BATTERY);
            batteryScreen_update(bat1V, bat2V);
        }
    }

    // --- EGT — every ~200 ms ---
    if (ENABLE_EGT) {
        if (++egtFrame >= EGT_UPDATE_FRAMES) {
            egtFrame = 0;
            float egtC = thermocouple.readCelsius();
            if (isnan(egtC)) egtC = 0.0f;

            tcaSelect(MUX_CH_EGT);
            egtScreen_update(egtC);
        }
    }

    // --- AFR ---
    if (ENABLE_AFR) {
        tcaSelect(MUX_CH_AFR);
        afrScreen_update(0.0f);
    }
}
