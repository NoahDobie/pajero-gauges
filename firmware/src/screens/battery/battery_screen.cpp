// =============================================================================
// battery_screen.cpp — Dual battery voltage screen module
//
// 128×64 OLED split into two equal 64×64 halves.
// Each half shows a battery outline with two terminal pins and a "XX.X"
// voltage readout centred inside the body.
//
// All pixel positions are hardcoded — no dynamic layout calculations.
//
// Left half  — absolute coords (x 0–63)
// Right half — absolute coords (x 64–127), all x values = left + 64
//
//   [ pin ]  [ pin ]      x: 8,10px  and  46,10px  (within half)
//  ┌────────────────┐     body: x=3, y=16, 58×48 px
//  │                │
//  │    1 2 . 3     │     digits at y=32, textSize 2 (12×16 px)
//  │                │     dot textSize 1 (6×8 px)
//  └────────────────┘
// =============================================================================

#include "battery_screen.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>

static Adafruit_SH1106G *_display = nullptr;

// Partial-refresh state — sentinel forces full redraw on first call
static float _lastV[2] = { -999.0f, -999.0f };

// EMA smoothing — light filter to prevent ±0.1 V flicker on the display
static const float BAT_EMA_ALPHA = 0.3f;
static float _smoothV[2]  = { 0.0f, 0.0f };
static bool  _smoothSeeded = false;

static float _smooth(float raw, int idx) {
    if (!_smoothSeeded) return raw;   // first call — seed, don't filter
    _smoothV[idx] = BAT_EMA_ALPHA * raw + (1.0f - BAT_EMA_ALPHA) * _smoothV[idx];
    return _smoothV[idx];
}


static void _drawChrome(int x0) {
    // Two solid terminal pins above the body
    _display->fillRect(x0 +  8, 10, 10, 6, SH110X_WHITE);
    _display->fillRect(x0 + 46, 10, 10, 6, SH110X_WHITE);
    // Battery body outline — widened to x=1, w=62 to fit textSize 3 digits
    _display->drawRect(x0 + 1, 16, 62, 48, SH110X_WHITE);
}


static bool _renderDigits(int x0, float voltage, float &lastV) {
    int iv    = (int)voltage;
    int tens  = (iv / 10) % 10, ones = iv % 10, dec = (int)(voltage * 10) % 10;

    int lv    = (int)lastV;
    int lTens = (lv / 10) % 10, lOnes = lv % 10, lDec = (int)(lastV * 10) % 10;

    const bool first = (lastV < 0.0f);
    bool changed = false;

    // Digit block: textSize 3 (18×24 px per digit), textSize 1 dot (6×8 px)
    // Block = 18 + 18 + 6 + 18 = 60 px — exact fit in widened 60 px interior (x=2..61)
    // Height 24 px → top at 17 + (46-24)/2 = 28
    const int DX = x0 + 3;   // left edge of digit block (centred in 60 px interior)
    const int DY = 28;        // top of digit row

    _display->setTextColor(SH110X_WHITE);

    // Tens digit  (x: DX,      18×24 px)
    if (tens != lTens || first) {
        _display->fillRect(DX, DY, 18, 24, SH110X_BLACK);
        _display->setTextSize(3); _display->setCursor(DX, DY);
        _display->print(tens);
        changed = true;
    }
    // Ones digit  (x: DX+18,   18×24 px)
    if (ones != lOnes || first) {
        _display->fillRect(DX + 18, DY, 18, 24, SH110X_BLACK);
        _display->setTextSize(3); _display->setCursor(DX + 18, DY);
        _display->print(ones);
        changed = true;
    }
    // Decimal point — textSize 1 (6×8 px), bottom-aligned with digits
    _display->setTextSize(1);
    _display->setCursor(DX + 34, DY + 14);
    _display->print(".");

    // Decimal digit (x: DX+41,  18×24 px)
    if (dec != lDec || first) {
        _display->fillRect(DX + 41, DY, 16, 24, SH110X_BLACK);
        _display->setTextSize(3); _display->setCursor(DX + 41, DY);
        _display->print(dec);
        changed = true;
    }

    lastV = voltage;
    return changed;
}


// =============================================================================
// Public API
// =============================================================================

void batteryScreen_init(Adafruit_SH1106G *dsp) {
    _display = dsp;
    _lastV[0] = _lastV[1] = -999.0f;
    _smoothV[0] = _smoothV[1] = 0.0f;
    _smoothSeeded = false;

    _display->clearDisplay();
    _drawChrome(0);
    _drawChrome(64);
    _display->display();

    batteryScreen_update(0.0f, 0.0f);
}

void batteryScreen_update(float bat1Voltage, float bat2Voltage) {
    // Seed EMA on first real reading
    if (!_smoothSeeded) {
        _smoothV[0] = bat1Voltage;
        _smoothV[1] = bat2Voltage;
        _smoothSeeded = true;
    }

    float v[2] = {
        constrain(_smooth(bat1Voltage, 0), 0.0f, 19.9f),
        constrain(_smooth(bat2Voltage, 1), 0.0f, 19.9f)
    };
    bool dirty = false;
    dirty |= _renderDigits(0,  v[0], _lastV[0]);
    dirty |= _renderDigits(64, v[1], _lastV[1]);
    if (dirty) _display->display();
}

