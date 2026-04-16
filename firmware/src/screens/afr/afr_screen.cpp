// =============================================================================
// afr_screen.cpp — Air-Fuel Ratio screen module
//
// Handles all rendering for the AFR OLED display.  The caller passes in a
// pointer to the display object and feeds the latest wideband AFR each frame.
//
// Screen layout (128×64):
//   "AFR"    — italic font (FreeSansBoldOblique9pt), top-left
//   XX.X     — large centred ratio readout (text size 4, no unit suffix)
//   bar graph — 128×7 outlined rectangle at the bottom
//              left edge = 15.0 (rich), right edge = 50.0 (lean)
//
// Rendering strategy:
//   - Static chrome (title, bar border) is drawn once during init.
//   - Dynamic elements (digits, bar fill) use partial refresh: only the
//     pixels that actually changed are overwritten each frame.
//   - A single display->display() call is made per update cycle.
// =============================================================================

#include "afr_screen.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBoldOblique9pt7b.h>

// =============================================================================
// Module-level state
// =============================================================================
static Adafruit_SH1106G *_display = nullptr;

// Bar graph range — left = rich, right = lean
static const float BAR_MIN_AFR = 15.0f;
static const float BAR_MAX_AFR = 50.0f;

// Tracking for partial-refresh rendering
static float _lastAfr      = -999.1f;
static float _lastBarWidth = -1.0f;

// EMA smoothing state
static float _smoothedAfr = 0.0f;

// =============================================================================
// Static Components — drawn once after splash, never touched by partial-refresh
//
// Screen layout (128×64):
//   "AFR"      — italic font (FreeSansBoldOblique9pt), top-left
//   bar border — 128×7 outlined rectangle at the very bottom
//   Rich/Lean  — tiny 6×8 labels at left and right of bar
// =============================================================================
static void _drawStaticComponents() {
    _display->clearDisplay();

    _display->setFont(&FreeSansBoldOblique9pt7b);
    _display->setTextColor(SH110X_WHITE);
    _display->setCursor(0, 13);
    _display->print("AFR");

    _display->setFont();                        // back to default 6×8

    _display->drawRect(0, 57, 128, 7, SH110X_WHITE);

    _display->display();
}


// =============================================================================
// Render the AFR readout as large digits (text size 4 = 24×32 px each)
//
// Identical pixel layout to the boost screen: tens at x=0, ones at x=24,
// decimal point (size 3) at x=44, decimal digit at x=62.
// Each digit is tracked independently — only cells that changed are redrawn.
// Returns true if the framebuffer was modified.
// =============================================================================
static bool _renderAfrDigits(float afr) {
    int intPart  = (int)afr;
    int tens     = (intPart / 10) % 10;
    int ones     = intPart % 10;
    int decimal  = (int)(afr * 10) % 10;

    int lastInt  = (int)_lastAfr;
    int lTens    = (lastInt / 10) % 10;
    int lOnes    = lastInt % 10;
    int lDecimal = (int)(_lastAfr * 10) % 10;

    const int SZ = 4;
    const int H  = 8 * SZ;                          // 32 px
    const int Y  = 53 - H;                          // top of digit row = 21

    bool changed = false;

    _display->setTextColor(SH110X_WHITE);   // explicit — never rely on prior state

    if (tens != lTens || afr < 10) {
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

    _lastAfr = afr;
    return changed;
}


// =============================================================================
// Render the horizontal bar graph inside the bottom border rectangle
//
// Maps BAR_MIN_AFR (15.0) → 0 px, BAR_MAX_AFR (50.0) → 126 px.
// Returns true if the framebuffer was modified.
// =============================================================================
static bool _renderBar(float afr) {
    // Shift so that BAR_MIN_AFR maps to 0
    float span  = BAR_MAX_AFR - BAR_MIN_AFR;
    float barW  = ((afr - BAR_MIN_AFR) / span) * 126.0f;
    barW = constrain(barW, 0.0f, 126.0f);

    int newPx = (int)barW;
    int oldPx = (int)_lastBarWidth;

    if (newPx == oldPx) return false;

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
// Adaptive EMA smoothing
//
// Same dual-alpha approach used on boost and EGT screens:
//   - Slow alpha (0.1) for steady-state — rock-stable digits
//   - Fast alpha (0.5) when the value jumps by more than SPIKE_THRESH
// =============================================================================
static const float ALPHA_SLOW   = 0.1f;
static const float ALPHA_FAST   = 0.5f;
static const float SPIKE_THRESH = 2.0f;   // AFR units that trigger fast mode

static float _smooth(float raw) {
    float jump  = raw - _smoothedAfr;
    float alpha = (fabsf(jump) > SPIKE_THRESH) ? ALPHA_FAST : ALPHA_SLOW;
    _smoothedAfr = alpha * raw + (1.0f - alpha) * _smoothedAfr;
    return _smoothedAfr;
}


// =============================================================================
// Public API
// =============================================================================

void afrScreen_init(Adafruit_SH1106G *dsp) {
    _display = dsp;

    // Reset all tracking state
    _lastAfr      = -999.1f;
    _lastBarWidth = -1.0f;
    _smoothedAfr  = 0.0f;

    _drawStaticComponents();

    // Render digits/bar immediately so the screen isn't blank until the first update
    afrScreen_update(0.0f);
}

void afrScreen_update(float afr) {
    // 0.0 is the sentinel for "no sensor connected" — display 00.0 as-is,
    // bypassing clamping (which would push it to BAR_MIN_AFR ≈ 15.0) and
    // smoothing (which would converge slowly from any prior value).
    if (afr == 0.0f) {
        _smoothedAfr = 0.0f;
        bool dirty = false;
        dirty |= _renderAfrDigits(0.0f);
        dirty |= _renderBar(0.0f);
        if (dirty) _display->display();
        return;
    }

    // Clamp to the displayable/bar range
    float clamped = constrain(afr, BAR_MIN_AFR, BAR_MAX_AFR);

    // Smooth for display
    float smoothed = _smooth(clamped);

    bool dirty = false;
    dirty |= _renderAfrDigits(smoothed);
    dirty |= _renderBar(smoothed);
    if (dirty) _display->display();
}
