// =============================================================================
// battery_screen.h — Dual battery voltage screen module
//
// Displays two car battery voltage readings side-by-side, each inside a
// simple automotive battery icon with terminals and a charge-level bar.
//
// The caller owns the Adafruit_SH1106G display object and passes it in
// via batteryScreen_init().  All rendering goes through that pointer.
// =============================================================================
#ifndef BATTERY_SCREEN_H
#define BATTERY_SCREEN_H

#include <Adafruit_SSD1306.h>           // TEMP — swap back to SH110X when 1.3" arrives

// Call once after the display has been begin()'d.
// Draws the static battery outlines, terminals, labels, and bar borders.
//
//   *dsp — pointer to the shared display object
void batteryScreen_init(Adafruit_SSD1306 *dsp);  // TEMP — SSD1306 for 0.96" test display

// Call every loop iteration with the latest battery voltages.
// Handles partial-refresh rendering — only redraws digits and bar segments
// that actually changed since the previous frame.
//
//   bat1Voltage — Battery 1 voltage (from GPIO33, post-divider recovery)
//   bat2Voltage — Battery 2 voltage (from GPIO32, post-divider recovery)
void batteryScreen_update(float bat1Voltage, float bat2Voltage);

#endif // BATTERY_SCREEN_H
