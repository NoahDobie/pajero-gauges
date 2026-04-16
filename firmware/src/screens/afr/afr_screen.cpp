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
static float _lastAfr      = -999.0f;
static float _lastBarWidth = -1.0f;

// EMA smoothing state
static float _smoothedAfr = 0.0f;

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
// Displays "XX.X" left-aligned, matching the boost screen layout.
// Each digit is tracked independently — only cells that changed are redrawn.
// Returns true if the framebuffer was modified.
// =============================================================================
static const int DIGIT_SZ    = 4;              // textSize for the big digits
static const int DIGIT_W     = 6 * DIGIT_SZ;  // 24 px per character cell
static const int DIGIT_H     = 8 * DIGIT_SZ;  // 32 px
static const int DIGIT_Y     = 53 - DIGIT_H;  // top of digit row = 21
static const int DIGIT_X_OFF = 0;             // left-aligned, same as boost

static bool _renderAfrDigits(float afr) {
    int intPart = (int)afr;
    int tens    = (intPart / 10) % 10;
    int ones    = intPart % 10;
    int decimal = (int)(afr * 10) % 10;

    int lastInt  = (int)_lastAfr;
    int lTens    = (lastInt / 10) % 10;
    int lOnes    = lastInt % 10;
    int lDecimal = (int)(_lastAfr * 10) % 10;

    const bool first = (_lastAfr < 0.0f);
    bool changed = false;

    _display->setTextColor(SH110X_WHITE);   // explicit — never rely on prior state

    if (tens != lTens || first) {
        _display->fillRect(DIGIT_X_OFF, DIGIT_Y, DIGIT_W, DIGIT_H, SH110X_BLACK);
        _display->setTextSize(DIGIT_SZ);
        _display->setCursor(DIGIT_X_OFF, DIGIT_Y);
        _display->print(tens);
        changed = true;
    }
    if (ones != lOnes || first) {
        _display->fillRect(DIGIT_X_OFF + DIGIT_W, DIGIT_Y, DIGIT_W, DIGIT_H, SH110X_BLACK);
        _display->setTextSize(DIGIT_SZ);
        _display->setCursor(DIGIT_X_OFF + DIGIT_W, DIGIT_Y);
        _display->print(ones);
        changed = true;
    }

    // Decimal point — always redraw (cheap, prevents ghosting)
    _display->setTextSize(3);
    _display->setCursor(DIGIT_X_OFF + 2 * DIGIT_W, DIGIT_Y + 7);
    _display->print(".");

    if (decimal != lDecimal || first) {
        _display->fillRect(DIGIT_X_OFF + 2 * DIGIT_W + 18, DIGIT_Y, DIGIT_W, DIGIT_H, SH110X_BLACK);
        _display->setTextSize(DIGIT_SZ);
        _display->setCursor(DIGIT_X_OFF + 2 * DIGIT_W + 18, DIGIT_Y);
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
    _lastAfr      = -999.0f;
    _lastBarWidth = -1.0f;
    _smoothedAfr  = 0.0f;

    _splash();
    _drawStaticComponents();

    // Render digits/bar immediately so the screen isn't blank until the first update
    afrScreen_update(0.0f);
}

void afrScreen_update(float afr) {
    // Clamp to the displayable/bar range
    float clamped = constrain(afr, BAR_MIN_AFR, BAR_MAX_AFR);

    // Smooth for display
    float smoothed = _smooth(clamped);

    bool dirty = false;
    dirty |= _renderAfrDigits(smoothed);
    dirty |= _renderBar(smoothed);
    if (dirty) _display->display();
}
