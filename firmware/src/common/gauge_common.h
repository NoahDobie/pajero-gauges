// =============================================================================
// gauge_common.h — Shared rendering and smoothing utilities for gauge screens
//
// Provides reusable functions for:
//   - EMA smoothing with adaptive alpha (slow/fast response)
//   - Decimal digit rendering with hysteresis to prevent boundary flicker
//   - Horizontal bar graph rendering with partial refresh
// =============================================================================
#ifndef GAUGE_COMMON_H
#define GAUGE_COMMON_H

#include <Adafruit_SH110X.h>

// =============================================================================
// EMA Smoothing
// =============================================================================

struct SmoothingState {
    float smoothed;
    bool  seeded;
};

// Adaptive EMA: uses alphaSlow normally, alphaFast when |jump| > spikeThresh
float smoothEMA(float raw, SmoothingState *state,
                float alphaSlow, float alphaFast, float spikeThresh);

// Simple EMA for values that don't need spike detection (e.g., battery)
float smoothEMASimple(float raw, SmoothingState *state, float alpha);

// =============================================================================
// Digit Rendering with Hysteresis
// =============================================================================

struct DigitState {
    float lastValue;
    int   lastTens;
    int   lastOnes;
    int   lastDecimal;
};

// Render "XX.X" format (e.g., boost PSI, battery voltage, AFR)
// Returns true if framebuffer was modified
bool renderDigitsXX_X(Adafruit_SH1106G *dsp, int x, int y, int textSize,
                      float value, DigitState *state);

// Render "XXX" format (e.g., EGT temperature)
// Returns true if framebuffer was modified
bool renderDigitsXXX(Adafruit_SH1106G *dsp, int x, int y, int textSize,
                     float value, DigitState *state);

// Render small "XX.X" for max readout (textSize 1)
bool renderDigitsSmall(Adafruit_SH1106G *dsp, int x, int y,
                       float value, DigitState *state);

// =============================================================================
// Bar Graph Rendering
// =============================================================================

struct BarState {
    float lastWidth;
};

// Render horizontal bar inside a pre-drawn border
// barY = top of fill area (usually border_y + 1)
// barHeight = fill height (usually border_height - 2)
// maxPixels = maximum bar width in pixels
// Returns true if framebuffer was modified
bool renderBar(Adafruit_SH1106G *dsp, int barY, int barHeight, int maxPixels,
               float value, float maxValue, BarState *state);

// =============================================================================
// State Initialization
// =============================================================================

void initSmoothingState(SmoothingState *state);
void initDigitState(DigitState *state);
void initBarState(BarState *state);

#endif // GAUGE_COMMON_H
