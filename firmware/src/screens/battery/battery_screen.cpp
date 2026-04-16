// =============================================================================
// battery_screen.cpp — Dual battery voltage screen module
//
// 128×64 OLED split into two equal 64×64 halves.
// Each half: battery icon (body outline + two solid terminal pins) with a
// centred "XX.X" voltage readout inside.  Leading zeros always shown.
// Partial-refresh: only digit cells that change are repainted each frame.
//
//    ██████    ██████   ← solid terminal pins (10×8 px)
//   ┌──────────────┐
//   │              │
//   │    XX.X      │   ← centred voltage (textSize 2)
//   │              │
//   └──────────────┘
// =============================================================================

#include "battery_screen.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>

static Adafruit_SH1106G *_display = nullptr;

// Partial-refresh state — sentinel < 0 forces a full redraw on first call
static float _lastV[2] = { -999.0f, -999.0f };

// X offset for each half
static const int HALF_X[2] = { 0, 64 };

// Battery body: 58×48 px, bottom flush with screen (y 16–63)
static const int BODY_X = 3, BODY_Y = 16, BODY_W = 58, BODY_H = 48;

// Two solid terminal pins above body — symmetric about the body centre
static const int PIN_W = 10, PIN_H = 6, PIN_Y = BODY_Y - PIN_H;
static const int PIN_X[2] = { 8, 46 };   // left and right pin x (within half)

// "XX.X" digit block: textSize 2 → 12×16 px per char, 48 px total width
// Centred in the body interior (56 px wide, 46 px tall after 1 px borders)
static const int DIG_SZ = 2;
static const int DIG_CW = 6 * DIG_SZ;  // 12 px
static const int DIG_CH = 8 * DIG_SZ;  // 16 px
static const int DIG_X_OFF = BODY_X + 1 + (BODY_W - 2 - 4 * DIG_CW) / 2;  // = 8
static const int DIG_Y     = BODY_Y + 1 + (BODY_H - 2 - DIG_CH) / 2;       // = 32


static void _drawChrome(int xOff) {
    // Solid terminal pins
    _display->fillRect(xOff + PIN_X[0], PIN_Y, PIN_W, PIN_H, SH110X_WHITE);
    _display->fillRect(xOff + PIN_X[1], PIN_Y, PIN_W, PIN_H, SH110X_WHITE);

    // Battery body outline
    _display->drawRect(xOff + BODY_X, BODY_Y, BODY_W, BODY_H, SH110X_WHITE);
}


static bool _renderDigits(int xOff, float voltage, float &lastV) {
    int iv = (int)voltage;
    int tens  = (iv / 10) % 10,  ones  = iv % 10,  dec  = (int)(voltage * 10) % 10;

    int lv = (int)lastV;
    int lTens = (lv / 10) % 10, lOnes = lv % 10,  lDec = (int)(lastV * 10) % 10;

    const int X = xOff + DIG_X_OFF;
    const int Y = DIG_Y;
    const bool first = (lastV < 0.0f);
    bool changed = false;

    _display->setTextColor(SH110X_WHITE);   // explicit — never rely on prior state

    if (tens != lTens || first) {
        _display->fillRect(X, Y, DIG_CW, DIG_CH, SH110X_BLACK);
        _display->setTextSize(DIG_SZ); _display->setCursor(X, Y);
        _display->print(tens);
        changed = true;
    }
    if (ones != lOnes || first) {
        _display->fillRect(X + DIG_CW, Y, DIG_CW, DIG_CH, SH110X_BLACK);
        _display->setTextSize(DIG_SZ); _display->setCursor(X + DIG_CW, Y);
        _display->print(ones);
        changed = true;
    }

    // Decimal point — static pixel, always write to framebuffer
    _display->setTextSize(DIG_SZ);
    _display->setCursor(X + 2 * DIG_CW, Y);
    _display->print(".");

    if (dec != lDec || first) {
        _display->fillRect(X + 3 * DIG_CW, Y, DIG_CW, DIG_CH, SH110X_BLACK);
        _display->setTextSize(DIG_SZ); _display->setCursor(X + 3 * DIG_CW, Y);
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

    _display->clearDisplay();
    _drawChrome(HALF_X[0]);
    _drawChrome(HALF_X[1]);
    _display->display();

    // Render digits immediately so the screen isn't blank until the first update
    batteryScreen_update(0.0f, 0.0f);
}

void batteryScreen_update(float bat1Voltage, float bat2Voltage) {
    float v[2] = {
        constrain(bat1Voltage, 0.0f, 19.9f),
        constrain(bat2Voltage, 0.0f, 19.9f)
    };
    bool dirty = false;
    for (int i = 0; i < 2; i++) {
        dirty |= _renderDigits(HALF_X[i], v[i], _lastV[i]);
    }
    if (dirty) _display->display();
}
