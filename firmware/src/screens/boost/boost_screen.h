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
// Draws the static gauge chrome and renders initial zero values.
void boostScreen_init(Adafruit_SH1106G *dsp);

// Call every loop iteration with the latest boost PSI value.
// Handles EMA smoothing, max-pressure tracking, and partial-refresh
// rendering (only redraws digits / bar segments that actually changed).
void boostScreen_update(float boostPsi);

#endif // BOOST_SCREEN_H
