// =============================================================================
// egt_screen.h — Exhaust Gas Temperature screen module
//
// Displays the current EGT from a K-type thermocouple via MAX31855 as a
// large three-digit Celsius readout with a colour-implied bar graph
// (0–800 °C).  No peak tracker — just live temperature.
//
// The caller owns the display object and passes it in via egtScreen_init().
// All rendering goes through that pointer.
// =============================================================================
#ifndef EGT_SCREEN_H
#define EGT_SCREEN_H

#include <Adafruit_SSD1306.h>           // TEMP — swap back to SH110X when 1.3" arrives

// Call once after the display has been begin()'d.
// Draws the static gauge chrome (title, unit label, bar border).
//
//   *dsp — pointer to the shared display object
void egtScreen_init(Adafruit_SSD1306 *dsp);  // TEMP — SSD1306 for 0.96" test display

// Call every loop iteration with the latest EGT in °C.
// Handles EMA smoothing and partial-refresh rendering (only redraws
// digits / bar segments that actually changed).
void egtScreen_update(float egtCelsius);

#endif // EGT_SCREEN_H
