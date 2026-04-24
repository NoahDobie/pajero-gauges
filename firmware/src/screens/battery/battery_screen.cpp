// =============================================================================
// battery_screen.cpp — Dual battery voltage screen module
//
// 128x64 OLED split into two equal 64x64 halves.
// Each half shows a battery outline with two terminal pins and a "XX.X"
// voltage readout centred inside the body.
//
// Left half  — absolute coords (x 0-63)
// Right half — absolute coords (x 64-127)
// =============================================================================

#include "battery_screen.h"
#include "common/gauge_common.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>

// =============================================================================
// Module-level state
// =============================================================================
static Adafruit_SH1106G *_display = nullptr;

// Simple EMA for battery (no spike detection needed)
static const float BAT_EMA_ALPHA = 0.3f;

// State for common utilities — one per battery
static SmoothingState _smoothState[2];
static DigitState     _digitState[2];

// =============================================================================
// Chrome drawing — battery outline with terminal pins
// =============================================================================
static void _drawChrome(int x0) {
    // Two solid terminal pins above the body
    _display->fillRect(x0 + 8, 10, 10, 6, SH110X_WHITE);
    _display->fillRect(x0 + 46, 10, 10, 6, SH110X_WHITE);
    // Battery body outline
    _display->drawRect(x0 + 1, 16, 62, 48, SH110X_WHITE);
}

// =============================================================================
// Custom digit rendering for battery (textSize 3, centered in battery body)
// =============================================================================
static bool _renderBatteryDigits(int x0, float voltage, DigitState *state) {
    int iv = (int)voltage;
    int tens = (iv / 10) % 10;
    int ones = iv % 10;
    int dec = (int)(voltage * 10) % 10;

    int lastIv = (int)state->lastValue;
    bool firstRender = (state->lastValue < -900.0f);
    bool intChanged = (iv != lastIv);

    // Digit block: textSize 3 (18x24 px per digit), dot textSize 1 (6x8 px)
    const int DX = x0 + 3;
    const int DY = 28;

    bool changed = false;
    _display->setTextColor(SH110X_WHITE);

    // Tens digit
    if (tens != state->lastTens || firstRender || intChanged) {
        _display->fillRect(DX, DY, 18, 24, SH110X_BLACK);
        _display->setTextSize(3);
        _display->setCursor(DX, DY);
        _display->print(tens);
        state->lastTens = tens;
        changed = true;
    }

    // Ones digit
    if (ones != state->lastOnes || firstRender || intChanged) {
        _display->fillRect(DX + 18, DY, 18, 24, SH110X_BLACK);
        _display->setTextSize(3);
        _display->setCursor(DX + 18, DY);
        _display->print(ones);
        state->lastOnes = ones;
        changed = true;
    }

    // Decimal point
    _display->setTextSize(1);
    _display->setCursor(DX + 34, DY + 14);
    _display->print(".");

    // Decimal digit
    if (dec != state->lastDecimal || firstRender || intChanged) {
        _display->fillRect(DX + 41, DY, 16, 24, SH110X_BLACK);
        _display->setTextSize(3);
        _display->setCursor(DX + 41, DY);
        _display->print(dec);
        state->lastDecimal = dec;
        changed = true;
    }

    state->lastValue = voltage;
    return changed;
}

// =============================================================================
// Public API
// =============================================================================

void batteryScreen_init(Adafruit_SH1106G *dsp) {
    _display = dsp;

    initSmoothingState(&_smoothState[0]);
    initSmoothingState(&_smoothState[1]);
    initDigitState(&_digitState[0]);
    initDigitState(&_digitState[1]);

    _display->clearDisplay();
    _drawChrome(0);
    _drawChrome(64);
    _display->display();

    batteryScreen_update(0.0f, 0.0f);
}

void batteryScreen_update(float bat1Voltage, float bat2Voltage) {
    float v[2] = {
        constrain(smoothEMASimple(bat1Voltage, &_smoothState[0], BAT_EMA_ALPHA), 0.0f, 19.9f),
        constrain(smoothEMASimple(bat2Voltage, &_smoothState[1], BAT_EMA_ALPHA), 0.0f, 19.9f)
    };

    bool dirty = false;
    dirty |= _renderBatteryDigits(0, v[0], &_digitState[0]);
    dirty |= _renderBatteryDigits(64, v[1], &_digitState[1]);

    if (dirty) _display->display();
}
