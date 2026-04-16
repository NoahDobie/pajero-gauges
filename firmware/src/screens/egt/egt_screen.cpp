// =============================================================================
// egt_screen.cpp — Exhaust Gas Temperature screen module
//
// Handles all rendering for the EGT OLED display.  The caller (main.cpp)
// passes in a pointer to the display object and feeds the latest
// thermocouple temperature each frame.
//
// Screen layout (128×64):
//   "EGT"       — italic font (FreeSansBoldOblique9pt), top-left
//   "°C"        — italic C + superscript degree, right of last digit (x=72)
//   XXX         — three-digit temperature readout (text size 4)
//   bar graph   — 128×7 outlined rectangle at the bottom, fills 0–800 °C
//
// Rendering strategy:
//   - Static chrome (title, unit, bar border) is drawn once during init
//     and never redrawn.
//   - Dynamic elements (digits, bar fill) use partial refresh: only the
//     pixels that actually changed are overwritten, minimising I2C traffic
//     and eliminating full-screen flicker.
// =============================================================================

#include "egt_screen.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBoldOblique9pt7b.h>

// =============================================================================
// Module-level state
// =============================================================================
static Adafruit_SH1106G *_display = nullptr;

// Max EGT the bar graph represents (°C).
// 800 °C gives a visual margin above the 700 °C sustained safe limit
// for the 4D56T — anything higher pegs the bar at full.
static const float MAX_BAR_EGT = 800.0f;

// Tracking for partial-refresh rendering
static float _lastTemp     = -999.0f;
static float _lastBarWidth = -1.0f;

// EMA smoothing state
static float _smoothedEgt = 0.0f;

// =============================================================================
// Static Components — drawn once after splash, never touched by partial-refresh
//
// Screen layout (128×64):
//   "EGT"       — italic font (FreeSansBoldOblique9pt), top-left
//   "°C"        — default 6×8 font, right side next to large digits
//   bar border  — 128×7 outlined rectangle at the very bottom
//
// These elements occupy fixed pixel regions that the dynamic renderers
// are careful to avoid, so they persist indefinitely without redraw.
// =============================================================================
static void _drawStaticComponents() {
    _display->clearDisplay();

    _display->setFont(&FreeSansBoldOblique9pt7b);
    _display->setTextColor(SH110X_WHITE);
    _display->setCursor(0, 13);
    _display->print("EGT");

    // "°C" — small degree (default font) + italic C, positioned right of the
    // last digit cell (ones ends at x=72), mirroring boost's "psi" at x=86
    _display->setFont();
    _display->setTextSize(1);
    _display->setCursor(73, 36);   // superscript degree
    _display->print((char)247);

    _display->setFont(&FreeSansBoldOblique9pt7b);
    _display->setCursor(77, 48);   // italic C, baseline at bottom of digit row
    _display->print("C");

    _display->setFont();           // back to default 6x8

    _display->drawRect(0, 57, 128, 7, SH110X_WHITE);

    _display->display();
}


// =============================================================================
// Render the main EGT readout as large digits (text size 4 = 24×32 px each)
//
// Displays the current smoothed EGT as "XXX" in the centre of the screen.
// Each digit (hundreds, tens, ones) is tracked independently — only the
// cells whose value changed since the last frame are cleared and reprinted.
// A single display->display() call is made at the end if anything changed.
// =============================================================================
static bool _renderEgtDigits(float temp) {
    int intTemp = (int)temp;
    int hundreds = (intTemp / 100) % 10;
    int tens     = (intTemp / 10)  % 10;
    int ones     = intTemp % 10;

    int lastIntTemp = (int)_lastTemp;
    int lHundreds   = (lastIntTemp / 100) % 10;
    int lTens       = (lastIntTemp / 10)  % 10;
    int lOnes       = lastIntTemp % 10;

    const int SZ = 4;
    const int CW = 6 * SZ;                          // 24 px character cell width
    const int H  = 8 * SZ;                          // 32 px character cell height
    const int Y  = 53 - H;                          // top of digit row = 21

    bool changed = false;

    _display->setTextColor(SH110X_WHITE);   // explicit — never rely on prior state

    if (hundreds != lHundreds || temp < 100) {
        _display->fillRect(0, Y, CW, H, SH110X_BLACK);
        _display->setTextSize(SZ);
        _display->setCursor(0, Y);
        _display->print(hundreds);
        changed = true;
    }
    if (tens != lTens || temp < 10) {
        _display->fillRect(CW, Y, CW, H, SH110X_BLACK);
        _display->setTextSize(SZ);
        _display->setCursor(CW, Y);
        _display->print(tens);
        changed = true;
    }
    if (ones != lOnes) {
        _display->fillRect(2 * CW, Y, CW, H, SH110X_BLACK);
        _display->setTextSize(SZ);
        _display->setCursor(2 * CW, Y);
        _display->print(ones);
        changed = true;
    }

    _lastTemp = temp;
    return changed;
}


// =============================================================================
// Render the horizontal bar graph inside the bottom border rectangle
//
// Maps 0–MAX_BAR_EGT (800 °C) to 0–126 pixels (1 px inset from border).
// Compares current and previous integer pixel widths — skips the I2C write
// entirely if the bar hasn't moved.  When the bar shrinks, only the vacated
// region is erased (black fill), then the active region is redrawn (white).
// =============================================================================
static bool _renderBar(float temp) {
    float barW = map((long)(temp * 10), 0,
                     (long)(MAX_BAR_EGT * 10), 0, 1260) / 10.0f;
    barW = constrain(barW, 0.0f, 126.0f);

    int newPx = (int)barW;
    int oldPx = (int)_lastBarWidth;

    if (newPx == oldPx) return false;

    // Erase using integer pixel boundaries — no truncation gap possible
    if (newPx < oldPx) {
        _display->fillRect(1 + newPx, 58, oldPx - newPx, 5, SH110X_BLACK);
    }

    if (newPx > 0) {
        _display->fillRect(1, 58, newPx, 5, SH110X_WHITE);
    }

    _lastBarWidth = barW;
    return true;
}


// =============================================================================
// Adaptive EMA (Exponential Moving Average) smoothing
//
// Filters the raw EGT to eliminate digit flicker from thermocouple noise.
//   - At steady temperature the alpha is low (0.1) → heavy smoothing,
//     rock-stable digits.
//   - When the raw value jumps more than SPIKE_THRESH away from the
//     current smoothed value *in either direction*, alpha jumps to 0.5
//     → the display tracks rapid changes within 2–3 frames.
//   - Small fluctuations (< SPIKE_THRESH) use the slow alpha.
// =============================================================================
static const float ALPHA_SLOW    = 0.1f;   // Steady-state smoothing
static const float ALPHA_FAST    = 0.5f;   // Spike-tracking response
static const float SPIKE_THRESH  = 25.0f;  // °C jump that triggers fast mode

static float _smooth(float raw) {
    float jump = raw - _smoothedEgt;
    float alpha = (fabsf(jump) > SPIKE_THRESH)
                ? ALPHA_FAST
                : ALPHA_SLOW;
    _smoothedEgt = alpha * raw + (1.0f - alpha) * _smoothedEgt;
    return _smoothedEgt;
}


// =============================================================================
// Public API
// =============================================================================

void egtScreen_init(Adafruit_SH1106G *dsp) {
    _display = dsp;

    // Reset all tracking state
    _lastTemp     = -999.0f;
    _lastBarWidth = -1.0f;
    _smoothedEgt  = 0.0f;

    _drawStaticComponents();

    // Render digits/bar immediately so the screen isn't blank until the first update
    egtScreen_update(0.0f);
}

void egtScreen_update(float egtCelsius) {
    // Clamp to displayable range (0–999 °C for XXX format)
    float clamped = constrain(egtCelsius, 0.0f, 999.0f);

    // Smooth for display — adaptive alpha snaps on rapid changes
    float smoothed = _smooth(clamped);

    bool dirty = false;
    dirty |= _renderEgtDigits(smoothed);
    dirty |= _renderBar(smoothed);
    if (dirty) _display->display();
}
