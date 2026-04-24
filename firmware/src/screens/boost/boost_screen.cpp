// =============================================================================
// boost_screen.cpp — Boost gauge screen module
//
// Handles all rendering for the boost gauge OLED display. The caller
// passes in a pointer to the shared Adafruit_SH1106G display object and
// feeds pre-calculated boost PSI values each frame.
//
// Screen layout (128x64):
//   "Boost"    — italic font (FreeSansBoldOblique9pt), top-left
//   "psi"      — italic font, right side next to the large digits
//   "MAX"      — default 6x8 font, top-right corner (label for peak readout)
//   XX.X       — large boost readout (text size 4)
//   bar graph  — 128x7 outlined rectangle at the bottom
// =============================================================================

#include "boost_screen.h"
#include "common/gauge_common.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBoldOblique9pt7b.h>

// =============================================================================
// Module-level state
// =============================================================================
static Adafruit_SH1106G *_display = nullptr;

// Max boost the bar graph represents (PSI)
static const float MAX_BAR_PSI = 18.0f;

// Smoothing parameters — unified across all gauges
static const float ALPHA_SLOW   = 0.1f;
static const float ALPHA_FAST   = 0.5f;
static const float SPIKE_THRESH = 2.0f;   // PSI

// State for common utilities
static SmoothingState _smoothState;
static DigitState     _digitState;
static DigitState     _maxDigitState;
static BarState       _barState;

// Peak tracker
static float _maxPsi = 0.0f;

// =============================================================================
// Static Components — drawn once, never redrawn
// =============================================================================
static void _drawStaticComponents() {
    _display->clearDisplay();

    _display->setFont(&FreeSansBoldOblique9pt7b);
    _display->setTextColor(SH110X_WHITE);
    _display->setCursor(0, 13);
    _display->print("Boost");

    _display->setCursor(86, 44);
    _display->print("psi");

    _display->setFont();
    _display->setCursor(107, 0);
    _display->print("MAX");

    _display->drawRect(0, 57, 128, 7, SH110X_WHITE);

    _display->display();
}

// =============================================================================
// Public API
// =============================================================================

void boostScreen_init(Adafruit_SH1106G *dsp) {
    _display = dsp;

    initSmoothingState(&_smoothState);
    initDigitState(&_digitState);
    initDigitState(&_maxDigitState);
    initBarState(&_barState);
    _maxPsi = 0.0f;

    _drawStaticComponents();
    boostScreen_update(0.0f);
}

void boostScreen_update(float boostPsi) {
    // Clamp negative values to zero — gauge shows boost only
    float clamped = max(boostPsi, 0.0f);

    // Track peak from RAW value — catches every spike accurately
    if (clamped > _maxPsi) _maxPsi = clamped;

    // Smooth for display
    float smoothed = smoothEMA(clamped, &_smoothState,
                               ALPHA_SLOW, ALPHA_FAST, SPIKE_THRESH);

    bool dirty = false;

    // Main boost digits at y=21 (53 - 32)
    dirty |= renderDigitsXX_X(_display, 0, 21, 4, smoothed, &_digitState);

    // Max readout below "MAX" label
    dirty |= renderDigitsSmall(_display, 105, 10, _maxPsi, &_maxDigitState);

    // Bar graph (inside border: y=58, h=5, max=126px)
    dirty |= renderBar(_display, 58, 5, 126, smoothed, MAX_BAR_PSI, &_barState);

    if (dirty) _display->display();
}
