// =============================================================================
// boost_screen.h — Boost gauge screen module
//
// Provides splash animation, static layout, and efficient partial-refresh
// rendering for the altitude-compensated boost gauge display.
//
// The caller owns the Adafruit_SH1106G display object and passes it in
// via boostScreen_init().  All rendering goes through that pointer.
// =============================================================================
#ifndef BOOST_SCREEN_H
#define BOOST_SCREEN_H

#include <Adafruit_SH110X.h>

// Call once after the display has been begin()'d.
// Runs the Pajero splash animation while sampling the MAP sensors to build
// a baseline atmospheric reference, then draws the static gauge chrome.
//
//   *dsp        — pointer to the shared display object
//   baselineKpa — receives the averaged barometric baseline (from MAP2)
void boostScreen_init(Adafruit_SH1106G *dsp, float &baselineKpa);

// Call every loop iteration with the latest boost PSI value.
// Handles EMA smoothing, max-pressure tracking, and partial-refresh
// rendering (only redraws digits / bar segments that actually changed).
void boostScreen_update(float boostPsi);

#endif // BOOST_SCREEN_H
