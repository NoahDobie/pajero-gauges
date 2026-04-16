// =============================================================================
// splash.h — Coordinated multi-screen Pajero logo splash animation
//
// Displays a single 492×42 px bitmap that spans all four OLEDs simultaneously.
// Each screen renders its own horizontal slice:
//
//   Screen 0 (Battery)  — bitmap cols   0–117, drawn at local x=10  (10 px left margin)
//   Screen 1 (AFR)      — bitmap cols 118–245, drawn at local x=0
//   Screen 2 (EGT)      — bitmap cols 246–373, drawn at local x=0
//   Screen 3 (Boost)    — bitmap cols 374–491, drawn at local x=0   (10 px right margin)
//
// The bitmap scrolls down from off-screen top to off-screen bottom.
// Screens are updated in round-robin order each frame, with no added delay
// between screen updates — I2C bus timing alone provides the pacing.
// =============================================================================
#ifndef SPLASH_H
#define SPLASH_H

#include <Adafruit_SH110X.h>

// Run the full-width Pajero logo splash across all four OLEDs.
//
//   selectFn   — function that selects a TCA9548A mux channel (0–7)
//   ch0/disp0  — Battery screen (left-most, 10 px left margin)
//   ch1/disp1  — AFR screen
//   ch2/disp2  — EGT screen
//   ch3/disp3  — Boost screen (right-most, 10 px right margin)
//
// All four displays must already be begin()'d and cleared before calling this.
// Returns when the bitmap has fully scrolled off the bottom of the screens.
void runSplash(
    void (*selectFn)(uint8_t ch),
    uint8_t ch0, Adafruit_SH1106G *disp0,
    uint8_t ch1, Adafruit_SH1106G *disp1,
    uint8_t ch2, Adafruit_SH1106G *disp2,
    uint8_t ch3, Adafruit_SH1106G *disp3
);

#endif // SPLASH_H
