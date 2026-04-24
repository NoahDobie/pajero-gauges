// =============================================================================
// egt_screen.cpp — Exhaust Gas Temperature screen module
//
// Handles all rendering for the EGT OLED display. The caller passes in a
// pointer to the display object and feeds the latest thermocouple temperature.
//
// Screen layout (128x64):
//   "EGT"      — italic font (FreeSansBoldOblique9pt), top-left
//   "C"        — italic C + superscript degree, right of last digit
//   XXX        — three-digit temperature readout (text size 4)
//   bar graph  — 128x7 outlined rectangle at the bottom, fills 0-800 C
// =============================================================================

#include "egt_screen.h"
#include "common/gauge_common.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBoldOblique9pt7b.h>

// =============================================================================
// Module-level state
// =============================================================================
static Adafruit_SH1106G *_display = nullptr;

// Max EGT the bar graph represents
static const float MAX_BAR_EGT = 800.0f;

// Smoothing parameters — unified across all gauges
static const float ALPHA_SLOW   = 0.1f;
static const float ALPHA_FAST   = 0.5f;
static const float SPIKE_THRESH = 25.0f;   // degrees C

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
    _display->print("EGT");

    // Degree symbol + C
    _display->setFont();
    _display->setTextSize(1);
    _display->setCursor(73, 36);
    _display->print((char)247);   // degree symbol

    _display->setFont(&FreeSansBoldOblique9pt7b);
    _display->setCursor(77, 48);
    _display->print("C");

    _display->setFont();

    _display->drawRect(0, 57, 128, 7, SH110X_WHITE);

    _display->display();
}

// =============================================================================
// Public API
// =============================================================================

void egtScreen_init(Adafruit_SH1106G *dsp) {
    _display = dsp;

    initSmoothingState(&_smoothState);
    initDigitState(&_digitState);
    initBarState(&_barState);

    _drawStaticComponents();
    egtScreen_update(0.0f);
}

void egtScreen_update(float egtCelsius) {
    // Clamp to displayable range (0-999 C for XXX format)
    float clamped = constrain(egtCelsius, 0.0f, 999.0f);

    // Smooth for display
    float smoothed = smoothEMA(clamped, &_smoothState,
                               ALPHA_SLOW, ALPHA_FAST, SPIKE_THRESH);

    bool dirty = false;

    // EGT digits at y=21 (53 - 32)
    dirty |= renderDigitsXXX(_display, 0, 21, 4, smoothed, &_digitState);

    // Bar graph (inside border: y=58, h=5, max=126px)
    dirty |= renderBar(_display, 58, 5, 126, smoothed, MAX_BAR_EGT, &_barState);

    if (dirty) _display->display();
}
