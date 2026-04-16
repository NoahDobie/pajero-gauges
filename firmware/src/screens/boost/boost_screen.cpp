// =============================================================================
// boost_screen.cpp — Boost gauge screen module
//
// Handles all rendering for the boost gauge OLED display.  The caller
// (esp32code.cpp) passes in a pointer to the shared Adafruit_SH1106G display
// object and feeds pre-calculated boost PSI values each frame.
//
// Rendering strategy:
//   - Static components (labels, units, bar border) are drawn once after the
//     startup splash animation and are never redrawn.
//   - Dynamic elements (boost digits, bar fill, max readout) use partial
//     refresh: only the pixels that actually changed are overwritten,
//     minimising I2C traffic and eliminating full-screen flicker.
//   - Each render helper calls display->display() once at the end so the
//     SH1106 page writes are batched per element group.
// =============================================================================

#include "boost_screen.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBoldOblique9pt7b.h>

// =============================================================================
// Module-level state
// =============================================================================
static Adafruit_SH1106G *_display = nullptr;

// Max boost the bar graph represents (PSI).  18 PSI is the 4D56T wastegate
// limit — anything above will just peg the bar at full.
static const float MAX_BAR_PSI = 18.0f;

// Tracking for partial-refresh rendering
static float _lastPressure    = -1.1f;
static float _lastMaxPressure = -1.1f;
static float _lastBarWidth    = -1.0f;

// EMA smoothing state
static float _smoothedPsi = 0.0f;

// Peak tracker
static float _maxPsi = 0.0f;

// =============================================================================
// Pajero splash logo — 118 × 15 px, stored in flash
// =============================================================================
static const int LOGO_W = 118;
static const int LOGO_H = 15;

static const unsigned char PROGMEM _pajeroBitmap[] = {
    0xff, 0xff, 0x03, 0xfc, 0x00, 0x07, 0xf1, 0xff, 0xff, 0x9f, 0xff, 0xf8, 0x07, 0xff, 0x80,
    0xff, 0xff, 0x87, 0xfe, 0x00, 0x07, 0xf1, 0xff, 0xff, 0x9f, 0xff, 0xfe, 0x1f, 0xff, 0xe0,
    0xff, 0xff, 0xc7, 0xfe, 0x00, 0x07, 0xf1, 0xff, 0xff, 0x9f, 0xff, 0xfe, 0x3f, 0xff, 0xf8,
    0xff, 0xff, 0xef, 0xff, 0x00, 0x07, 0xf1, 0xff, 0xff, 0x9f, 0xff, 0xfe, 0x7f, 0xcf, 0xf8,
    0xfe, 0x1f, 0xef, 0xff, 0x00, 0x07, 0xf1, 0xfe, 0x00, 0x1f, 0xe1, 0xff, 0xff, 0x03, 0xfc,
    0xff, 0xff, 0xff, 0xff, 0x80, 0x07, 0xf1, 0xff, 0xf8, 0x1f, 0xff, 0xfe, 0xff, 0x03, 0xfc,
    0xff, 0xff, 0xdf, 0xbf, 0x80, 0x07, 0xf1, 0xff, 0xf8, 0x1f, 0xff, 0xfe, 0xff, 0x03, 0xfc,
    0xff, 0xff, 0xbf, 0x9f, 0xc0, 0x07, 0xf1, 0xff, 0xf8, 0x1f, 0xff, 0xf8, 0xff, 0x03, 0xfc,
    0xff, 0xfe, 0x3f, 0xff, 0xc0, 0x07, 0xf1, 0xff, 0xf8, 0x1f, 0xff, 0xf0, 0xff, 0x03, 0xfc,
    0xfe, 0x00, 0x3f, 0xff, 0xe0, 0x07, 0xf1, 0xfe, 0x00, 0x1f, 0xe7, 0xf0, 0xff, 0x03, 0xfc,
    0xfe, 0x00, 0x7f, 0xff, 0xef, 0xff, 0xf1, 0xff, 0xff, 0x9f, 0xe7, 0xf8, 0x7f, 0x8f, 0xf8,
    0xfe, 0x00, 0xff, 0xff, 0xff, 0xff, 0xf1, 0xff, 0xff, 0x9f, 0xe3, 0xfc, 0x7f, 0xff, 0xf0,
    0xfe, 0x00, 0xfe, 0x07, 0xf7, 0xff, 0xf1, 0xff, 0xff, 0x9f, 0xe3, 0xfe, 0x3f, 0xff, 0xf0,
    0xfe, 0x01, 0xfc, 0x03, 0xf3, 0xff, 0xe1, 0xff, 0xff, 0x9f, 0xe1, 0xfe, 0x0f, 0xff, 0xc0,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


// =============================================================================
// Splash animation
//
// Scrolls the Pajero logo from off-screen at the top to off-screen at the
// bottom over ~1.2 seconds.  Called once during boostScreen_init().  Provides
// a visual startup indicator while the rest of the system initialises.
// =============================================================================
static void _splash() {
    for (int y = -LOGO_H; y <= 64; y++) {
        _display->clearDisplay();
        _display->drawBitmap((128 - LOGO_W) / 2, y,
                         _pajeroBitmap, LOGO_W, LOGO_H, SH110X_WHITE);
        _display->display();
        delay(15);
    }
}


// =============================================================================
// Static Components — drawn once after splash, never touched by partial-refresh
//
// Screen layout (128×64):
//   "Boost"     — italic font (FreeSansBoldOblique9pt), top-left
//   "psi"       — italic font, right side next to the large digits
//   "MAX"       — default 6×8 font, top-right corner (label for peak readout)
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
    _display->print("Boost");

    _display->setCursor(86, 44);
    _display->print("psi");

    _display->setFont();                        // back to default 6×8
    _display->setCursor(107, 0);
    _display->print("MAX");

    _display->drawRect(0, 57, 128, 7, SH110X_WHITE);

    _display->display();
}


// =============================================================================
// Render the main boost readout as large digits (text size 4 = 24×32 px each)
//
// Displays the current smoothed boost as "XX.X" in the centre of the screen.
// Each digit (tens, ones, decimal) is tracked independently — only the cells
// whose value changed since the last frame are cleared and reprinted.
// The decimal point is always redrawn since it's tiny and not worth tracking.
// A single display->display() call is made at the end if anything changed.
// =============================================================================
static bool _renderBoostDigits(float pressure) {
    int intPart  = (int)pressure;
    int tens     = (intPart / 10) % 10;
    int ones     = intPart % 10;
    int decimal  = (int)(pressure * 10) % 10;

    int lastInt  = (int)_lastPressure;
    int lTens    = (lastInt / 10) % 10;
    int lOnes    = lastInt % 10;
    int lDecimal = (int)(_lastPressure * 10) % 10;

    const int SZ = 4;
    const int H  = 8 * SZ;                          // 32 px
    const int Y  = 53 - H;                          // top of digit row = 21

    bool changed = false;

    _display->setTextColor(SH110X_WHITE);   // explicit — never rely on prior state

    if (tens != lTens || pressure < 10) {
        _display->fillRect(0, Y, 24, H, SH110X_BLACK);
        _display->setTextSize(SZ);
        _display->setCursor(0, Y);
        _display->print(tens);
        changed = true;
    }
    if (ones != lOnes) {
        _display->fillRect(24, Y, 24, H, SH110X_BLACK);
        _display->setTextSize(SZ);
        _display->setCursor(24, Y);
        _display->print(ones);
        changed = true;
    }

    // Decimal point — always redraw (cheap)
    _display->setTextSize(3);
    _display->setCursor(44, Y + 7);
    _display->print(".");

    if (decimal != lDecimal) {
        _display->fillRect(62, Y, 24, H, SH110X_BLACK);
        _display->setTextSize(SZ);
        _display->setCursor(62, Y);
        _display->print(decimal);
        changed = true;
    }

    _lastPressure = pressure;
    return changed;
}


// =============================================================================
// Render the horizontal bar graph inside the bottom border rectangle
//
// Maps 0–MAX_BAR_PSI (18 PSI) to 0–126 pixels (1 px inset from border).
// Compares current and previous integer pixel widths — skips the I2C write
// entirely if the bar hasn't moved.  When the bar shrinks, only the vacated
// region is erased (black fill), then the active region is redrawn (white).
// =============================================================================
static bool _renderBar(float pressure) {
    float barW = map((long)(pressure * 10), 0,
                     (long)(MAX_BAR_PSI * 10), 0, 1260) / 10.0f;
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
// Render the peak boost readout (text size 1 = 6×8 px) in the top-right
// corner, directly below the static "MAX" label.
//
// Displays the highest boost value seen since power-on as "XX.X".
// Uses the same per-digit partial-refresh approach as _renderBoostDigits()
// to avoid unnecessary redraws — the max value only ever increases, so
// in practice only a few digit updates happen across an entire drive.
// =============================================================================
static bool _renderMax(float maxPsi) {
    int intPart  = (int)maxPsi;
    int tens     = (intPart / 10) % 10;
    int ones     = intPart % 10;
    int decimal  = (int)(maxPsi * 10) % 10;

    int lastInt  = (int)_lastMaxPressure;
    int lTens    = (lastInt / 10) % 10;
    int lOnes    = lastInt % 10;
    int lDecimal = (int)(_lastMaxPressure * 10) % 10;

    const int X = 105, Y = 10, SZ = 1, H = 8;
    bool changed = false;

    if (tens != lTens || maxPsi < 10) {
        _display->fillRect(X, Y, 6, H, SH110X_BLACK);
        _display->setTextSize(SZ);
        _display->setCursor(X, Y);
        _display->print(tens);
        changed = true;
    }
    if (ones != lOnes) {
        _display->fillRect(X + 6, Y, 6, H, SH110X_BLACK);
        _display->setTextSize(SZ);
        _display->setCursor(X + 6, Y);
        _display->print(ones);
        changed = true;
    }

    int dotX = X + 11;
    _display->setTextSize(SZ);
    _display->setCursor(dotX, Y);
    _display->print(".");

    if (decimal != lDecimal) {
        _display->fillRect(dotX + 6, Y, 6, H, SH110X_BLACK);
        _display->setTextSize(SZ);
        _display->setCursor(dotX + 6, Y);
        _display->print(decimal);
        changed = true;
    }

    _lastMaxPressure = maxPsi;
    return changed;
}


// =============================================================================
// Adaptive EMA (Exponential Moving Average) smoothing
//
// Filters the raw boost PSI to eliminate digit flicker from ADC noise.
//   - At idle / steady cruise the alpha is low (0.1) → heavy smoothing,
//     rock-stable digits.
//   - When the raw value jumps more than SPIKE_THRESH away from the
//     current smoothed value *in either direction*, alpha jumps to 0.5
//     → the display tracks rapid changes within 2–3 frames.
//   - Small fluctuations (< SPIKE_THRESH) use the slow alpha.
//
// Note: peak tracking (MAX) uses the raw un-smoothed value so spikes
// are captured at full magnitude regardless of the filter.
// =============================================================================
static const float ALPHA_SLOW    = 0.1f;   // Steady-state smoothing
static const float ALPHA_FAST    = 0.5f;   // Spike-tracking response
static const float SPIKE_THRESH  = 1.0f;   // PSI jump that triggers fast mode

static float _smooth(float raw) {
    float jump = raw - _smoothedPsi;
    float alpha = (fabsf(jump) > SPIKE_THRESH)
                ? ALPHA_FAST
                : ALPHA_SLOW;
    _smoothedPsi = alpha * raw + (1.0f - alpha) * _smoothedPsi;
    return _smoothedPsi;
}


// =============================================================================
// Public API
// =============================================================================

void boostScreen_init(Adafruit_SH1106G *dsp, float &baselineKpa) {
    _display = dsp;

    // Reset all tracking state
    _lastPressure    = -1.1f;
    _lastMaxPressure = -1.1f;
    _lastBarWidth    = -1.0f;
    _smoothedPsi     = 0.0f;
    _maxPsi          = 0.0f;

    _splash();
    _drawStaticComponents();

    // baselineKpa is sampled by the caller (main) during its own init
    // sequence — we don't read sensors here so the module stays pure display.
    (void)baselineKpa;

    // Render digits/bar immediately so the screen isn't blank until the first update
    boostScreen_update(0.0f);
}

void boostScreen_update(float boostPsi) {
    // Clamp negative values to zero — gauge shows boost only (vacuum is 0)
    float clamped = max(boostPsi, 0.0f);

    // Track peak from the RAW value — catches every spike accurately
    if (clamped > _maxPsi) _maxPsi = clamped;

    // Smooth for display — adaptive alpha snaps up on spikes, smooth at idle
    float smoothed = _smooth(clamped);

    bool dirty = false;
    dirty |= _renderBoostDigits(smoothed);
    dirty |= _renderMax(_maxPsi);
    dirty |= _renderBar(smoothed);
    if (dirty) _display->display();
}
