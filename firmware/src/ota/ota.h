// =============================================================================
// ota.h — Triple-reset OTA mode with ArduinoOTA + WebSerial
//
// How to enter OTA mode:
//   1. Turn ignition off/on THREE times within 3 seconds each
//   2. Connect to WiFi AP "PajeroGauges" (password in ota.cpp)
//   3. Flash via PlatformIO:  upload_port = 192.168.4.1
//   4. WebSerial: http://pajero-gauges.local/webserial  (or any URL — captive portal)
//   5. Gauges continue running normally — WiFi runs in the background
// =============================================================================
#ifndef OTA_H
#define OTA_H

// Diagnostic streaming callback — called by ota_handle() at ~1 Hz when a
// diagnostic command is active.  The mode string indicates which sensor
// group to read: "battery", "boost", "egt", or "status".
typedef void (*OtaDiagCallback)(const char *mode);

// Call at the very start of setup(), before any other init.
// Returns true if OTA mode was entered (gauge init should still continue).
bool ota_checkAndStart();

// Force-start OTA mode (e.g. from a menu selection). Always starts WiFi.
void ota_forceStart();

// Call in loop() — services ArduinoOTA, WebSerial, and diagnostic streaming.
void ota_handle();

// Returns true if OTA mode is active.
bool ota_isActive();

// Register a callback for diagnostic sensor reads.
// main.cpp registers this so ota.cpp can request sensor data without
// knowing about ADC pins or conversion functions.
void ota_setDiagCallback(OtaDiagCallback cb);

// Call after the sensor preload window (~3 s) to clear the reset counter
// so a later reset doesn't falsely trigger OTA.
void ota_clearResetCounter();

#endif
