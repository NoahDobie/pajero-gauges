// =============================================================================
// afr_screen.cpp — Air-Fuel Ratio screen module
//
// Handles all rendering for the AFR OLED display. The caller passes in a
// pointer to the display object and feeds the latest wideband AFR each frame.
//
// Screen layout (128x64):
//   "AFR"      — italic font (FreeSansBoldOblique9pt), top-left
//   XX.X       — large centred ratio readout (text size 4)
//   bar graph  — 128x7 outlined rectangle at the bottom
//                left edge = 15.0 (rich), right edge = 50.0 (lean)
// =============================================================================

#include "afr_screen.h"
#include "common/gauge_common.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBoldOblique9pt7b.h>

// =============================================================================
// Module-level state
// =============================================================================
static Adafruit_SH1106G *_display = nullptr;

// Bar graph range — left = rich, right = lean (diesel runs leaner than gasoline)
static const float BAR_MIN_AFR = 15.0f;
static const float BAR_MAX_AFR = 50.0f;

// Smoothing parameters — unified across all gauges
static const float ALPHA_SLOW   = 0.1f;
static const float ALPHA_FAST   = 0.5f;
static const float SPIKE_THRESH = 2.0f;   // AFR units

// State for common utilities
static SmoothingState _smoothState;
static DigitState     _digitState;
static BarState       _barState;

// =============================================================================
// Static Components — drawn once, never redrawn
// =============================================================================
static void _drawStaticComponents() {
    _display->clearDisplay();

    _display->setFont(&FreeSansBoldOblique9pt7b);
    _display->setTextColor(SH110X_WHITE);
    _display->setCursor(0, 13);
    _display->print("AFR");

    _display->setFont();

    _display->drawRect(0, 57, 128, 7, SH110X_WHITE);

    _display->display();
}

// =============================================================================
// Custom bar rendering for AFR (offset range: 10-20 maps to 0-126px)
// =============================================================================
static bool _renderAfrBar(float afr) {
    float span = BAR_MAX_AFR - BAR_MIN_AFR;
    float barW = ((afr - BAR_MIN_AFR) / span) * 126.0f;
    barW = constrain(barW, 0.0f, 126.0f);

    int newPx = (int)barW;
    int oldPx = (int)_barState.lastWidth;

    if (newPx == oldPx) return false;

    if (newPx < oldPx) {
        _display->fillRect(1 + newPx, 58, oldPx - newPx, 5, SH110X_BLACK);
    }
    if (newPx > 0) {
        _display->fillRect(1, 58, newPx, 5, SH110X_WHITE);
    }

    _barState.lastWidth = barW;
    return true;
}

// =============================================================================
// Public API
// =============================================================================

void afrScreen_init(Adafruit_SH1106G *dsp) {
    _display = dsp;

    initSmoothingState(&_smoothState);
    initDigitState(&_digitState);
    initBarState(&_barState);

    _drawStaticComponents();
    afrScreen_update(0.0f);
}

void afrScreen_update(float afr) {
    // 0.0 is the sentinel for "no sensor connected"
    if (afr == 0.0f) {
        _smoothState.smoothed = 0.0f;
        _smoothState.seeded = false;

        bool dirty = false;
        dirty |= renderDigitsXX_X(_display, 0, 21, 4, 0.0f, &_digitState);
        dirty |= _renderAfrBar(0.0f);
        if (dirty) _display->display();
        return;
    }

    // Clamp to displayable/bar range
    float clamped = constrain(afr, BAR_MIN_AFR, BAR_MAX_AFR);

    // Smooth for display
    float smoothed = smoothEMA(clamped, &_smoothState,
                               ALPHA_SLOW, ALPHA_FAST, SPIKE_THRESH);

    bool dirty = false;

    // AFR digits at y=21
    dirty |= renderDigitsXX_X(_display, 0, 21, 4, smoothed, &_digitState);

    // AFR bar (offset range)
    dirty |= _renderAfrBar(smoothed);

    if (dirty) _display->display();
}
