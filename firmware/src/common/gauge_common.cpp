// =============================================================================
// gauge_common.cpp — Shared rendering and smoothing utilities
// =============================================================================

#include "gauge_common.h"
#include <Arduino.h>

// =============================================================================
// State Initialization
// =============================================================================

void initSmoothingState(SmoothingState *state) {
    state->smoothed = 0.0f;
    state->seeded = false;
}

void initDigitState(DigitState *state) {
    state->lastValue = -999.0f;
    state->lastTens = -1;
    state->lastOnes = -1;
    state->lastDecimal = -1;
}

void initBarState(BarState *state) {
    state->lastWidth = -1.0f;
}

// =============================================================================
// EMA Smoothing
// =============================================================================

float smoothEMA(float raw, SmoothingState *state,
                float alphaSlow, float alphaFast, float spikeThresh) {
    if (!state->seeded) {
        state->smoothed = raw;
        state->seeded = true;
        return raw;
    }

    float jump = raw - state->smoothed;
    float alpha = (fabsf(jump) > spikeThresh) ? alphaFast : alphaSlow;
    state->smoothed = alpha * raw + (1.0f - alpha) * state->smoothed;
    return state->smoothed;
}

float smoothEMASimple(float raw, SmoothingState *state, float alpha) {
    if (!state->seeded) {
        state->smoothed = raw;
        state->seeded = true;
        return raw;
    }

    state->smoothed = alpha * raw + (1.0f - alpha) * state->smoothed;
    return state->smoothed;
}

// =============================================================================
// Digit Rendering — XX.X format with hysteresis
//
// Hysteresis prevents flicker at digit boundaries (e.g., 12.95 ↔ 13.05).
// A digit only changes if:
//   1. It differs from the last displayed digit by more than 1, OR
//   2. The integer part of the value has changed (rollover case)
// =============================================================================

static bool shouldUpdateDigit(int newDigit, int lastDigit, bool forceUpdate) {
    if (lastDigit < 0 || forceUpdate) return true;
    int diff = abs(newDigit - lastDigit);
    return (diff > 1) || (diff == 1 && (newDigit == 0 || lastDigit == 0));
}

bool renderDigitsXX_X(Adafruit_SH1106G *dsp, int x, int y, int textSize,
                      float value, DigitState *state) {
    int intPart = (int)value;
    int tens = (intPart / 10) % 10;
    int ones = intPart % 10;
    int decimal = (int)(value * 10) % 10;

    int lastInt = (int)state->lastValue;
    bool intChanged = (intPart != lastInt);
    bool firstRender = (state->lastValue < -900.0f);

    const int charW = 6 * textSize;
    const int charH = 8 * textSize;

    bool changed = false;
    dsp->setTextColor(SH110X_WHITE);

    // Tens digit
    if (shouldUpdateDigit(tens, state->lastTens, firstRender || intChanged)) {
        dsp->fillRect(x, y, charW, charH, SH110X_BLACK);
        dsp->setTextSize(textSize);
        dsp->setCursor(x, y);
        dsp->print(tens);
        state->lastTens = tens;
        changed = true;
    }

    // Ones digit
    if (shouldUpdateDigit(ones, state->lastOnes, firstRender || intChanged)) {
        dsp->fillRect(x + charW, y, charW, charH, SH110X_BLACK);
        dsp->setTextSize(textSize);
        dsp->setCursor(x + charW, y);
        dsp->print(ones);
        state->lastOnes = ones;
        changed = true;
    }

    // Decimal point — always draw (cheap, ensures it's visible)
    int dotSize = max(1, textSize - 1);
    int dotX = x + 2 * charW - 4;
    int dotY = y + charH - 8 * dotSize;
    dsp->setTextSize(dotSize);
    dsp->setCursor(dotX, dotY);
    dsp->print(".");

    // Decimal digit — use hysteresis to prevent boundary flicker
    bool decimalChanged = shouldUpdateDigit(decimal, state->lastDecimal, firstRender);
    if (decimalChanged || intChanged) {
        int decX = x + 2 * charW + 6 * dotSize - 4;
        dsp->fillRect(decX, y, charW, charH, SH110X_BLACK);
        dsp->setTextSize(textSize);
        dsp->setCursor(decX, y);
        dsp->print(decimal);
        state->lastDecimal = decimal;
        changed = true;
    }

    state->lastValue = value;
    return changed;
}

bool renderDigitsXXX(Adafruit_SH1106G *dsp, int x, int y, int textSize,
                     float value, DigitState *state) {
    int intVal = (int)value;
    int hundreds = (intVal / 100) % 10;
    int tens = (intVal / 10) % 10;
    int ones = intVal % 10;

    int lastInt = (int)state->lastValue;
    bool intChanged = (intVal != lastInt);
    bool firstRender = (state->lastValue < -900.0f);

    const int charW = 6 * textSize;
    const int charH = 8 * textSize;

    bool changed = false;
    dsp->setTextColor(SH110X_WHITE);

    // Hundreds digit
    int lastHundreds = (lastInt / 100) % 10;
    if (shouldUpdateDigit(hundreds, lastHundreds, firstRender) || intChanged) {
        dsp->fillRect(x, y, charW, charH, SH110X_BLACK);
        dsp->setTextSize(textSize);
        dsp->setCursor(x, y);
        dsp->print(hundreds);
        changed = true;
    }

    // Tens digit
    int lastTens = (lastInt / 10) % 10;
    if (shouldUpdateDigit(tens, lastTens, firstRender) || intChanged) {
        dsp->fillRect(x + charW, y, charW, charH, SH110X_BLACK);
        dsp->setTextSize(textSize);
        dsp->setCursor(x + charW, y);
        dsp->print(tens);
        changed = true;
    }

    // Ones digit
    int lastOnes = lastInt % 10;
    if (shouldUpdateDigit(ones, lastOnes, firstRender) || intChanged) {
        dsp->fillRect(x + 2 * charW, y, charW, charH, SH110X_BLACK);
        dsp->setTextSize(textSize);
        dsp->setCursor(x + 2 * charW, y);
        dsp->print(ones);
        changed = true;
    }

    state->lastValue = value;
    return changed;
}

bool renderDigitsSmall(Adafruit_SH1106G *dsp, int x, int y,
                       float value, DigitState *state) {
    int intPart = (int)value;
    int tens = (intPart / 10) % 10;
    int ones = intPart % 10;
    int decimal = (int)(value * 10) % 10;

    int lastInt = (int)state->lastValue;
    bool intChanged = (intPart != lastInt);
    bool firstRender = (state->lastValue < -900.0f);

    const int SZ = 1;
    const int W = 6, H = 8;

    bool changed = false;
    dsp->setTextColor(SH110X_WHITE);
    dsp->setTextSize(SZ);

    // Tens
    if (shouldUpdateDigit(tens, state->lastTens, firstRender || intChanged)) {
        dsp->fillRect(x, y, W, H, SH110X_BLACK);
        dsp->setCursor(x, y);
        dsp->print(tens);
        state->lastTens = tens;
        changed = true;
    }

    // Ones
    if (shouldUpdateDigit(ones, state->lastOnes, firstRender || intChanged)) {
        dsp->fillRect(x + W, y, W, H, SH110X_BLACK);
        dsp->setCursor(x + W, y);
        dsp->print(ones);
        state->lastOnes = ones;
        changed = true;
    }

    // Dot
    dsp->setCursor(x + 2 * W - 1, y);
    dsp->print(".");

    // Decimal
    bool decimalChanged = shouldUpdateDigit(decimal, state->lastDecimal, firstRender);
    if (decimalChanged || intChanged) {
        dsp->fillRect(x + 2 * W + 5, y, W, H, SH110X_BLACK);
        dsp->setCursor(x + 2 * W + 5, y);
        dsp->print(decimal);
        state->lastDecimal = decimal;
        changed = true;
    }

    state->lastValue = value;
    return changed;
}

// =============================================================================
// Bar Graph Rendering
// =============================================================================

bool renderBar(Adafruit_SH1106G *dsp, int barY, int barHeight, int maxPixels,
               float value, float maxValue, BarState *state) {
    float barW = (value / maxValue) * (float)maxPixels;
    barW = constrain(barW, 0.0f, (float)maxPixels);

    int newPx = (int)barW;
    int oldPx = (int)state->lastWidth;

    if (newPx == oldPx) return false;

    // Erase shrunk portion
    if (newPx < oldPx) {
        dsp->fillRect(1 + newPx, barY, oldPx - newPx, barHeight, SH110X_BLACK);
    }

    // Draw filled portion
    if (newPx > 0) {
        dsp->fillRect(1, barY, newPx, barHeight, SH110X_WHITE);
    }

    state->lastWidth = barW;
    return true;
}
