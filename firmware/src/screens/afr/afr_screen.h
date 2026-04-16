// =============================================================================
// afr_screen.h — Air-Fuel Ratio screen module
//
// 128×64 OLED showing the current AFR as a large "XX.X" readout and a
// horizontal bar graph spanning the lean-to-rich operating range.
//
// Bar range : 15.0 (rich end) → 50.0 (lean end)
// No MAX tracker, no unit label — the ratio is self-describing.
//
// The caller owns the Adafruit_SH1106G display object and passes it in
// via afrScreen_init().  All rendering goes through that pointer.
// =============================================================================
#ifndef AFR_SCREEN_H
#define AFR_SCREEN_H

#include <Adafruit_SH110X.h>

// Call once after the display has been begin()'d.
// Runs the Pajero splash animation, draws static chrome, then renders
// initial 00.0 digits so the screen is never blank on boot.
//
//   *dsp — pointer to the shared display object
void afrScreen_init(Adafruit_SH1106G *dsp);

// Call every loop iteration with the latest AFR value.
// Handles EMA smoothing and partial-refresh rendering — only redraws
// digits / bar segments that actually changed since the previous frame.
//
//   afr — air-fuel ratio (e.g. 18.5).  Values outside 15–50 are clamped.
void afrScreen_update(float afr);

#endif // AFR_SCREEN_H
