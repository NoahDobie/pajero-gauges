// =============================================================================
// battery_screen.h — Dual battery voltage screen module
//
// 128×64 OLED split into two 64×64 halves, each showing a battery icon
// (outlined body with two solid terminal pins) and a centred "XX.X" voltage.
//
// The caller owns the Adafruit_SH1106G display object and passes it in
// via batteryScreen_init().  All rendering goes through that pointer.
// =============================================================================
#ifndef BATTERY_SCREEN_H
#define BATTERY_SCREEN_H

#include <Adafruit_SH110X.h>

// Call once after the display has been begin()'d.
// Draws static chrome: labels, battery outlines, and terminal pins.
//
//   *dsp — pointer to the shared display object
void batteryScreen_init(Adafruit_SH1106G *dsp);

// Call every loop iteration with the latest battery voltages.
// Handles partial-refresh rendering — only redraws digit cells that
// actually changed since the previous frame.
//
//   bat1Voltage — Battery 1 voltage (from GPIO33, post-divider recovery)
//   bat2Voltage — Battery 2 voltage (from GPIO32, post-divider recovery)
void batteryScreen_update(float bat1Voltage, float bat2Voltage);

#endif // BATTERY_SCREEN_H
