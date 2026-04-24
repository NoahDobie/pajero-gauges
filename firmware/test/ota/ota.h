// =============================================================================
// test/ota/ota.h — Test-environment OTA (always-on WiFi + screen commands)
//
// Simplified OTA for the single-display test harness:
//   - No diagnostic sensor streaming (no real sensors in test)
//   - Adds command callback for screen switching via WebSerial
//   - WiFi always starts via ota_forceStart()
//
// WebSerial Commands:
//   Screens:  battery, boost, egt, afr, off, screen
//   System:   info, mem, wifi, perf, reboot
// =============================================================================
#ifndef OTA_H
#define OTA_H

// Callback for unrecognised WebSerial commands — the test harness uses this
// to handle screen-switching commands (battery, boost, egt, afr, off).
typedef void (*OtaCmdCallback)(const char *cmd);

// Check NVS reset counter — enters OTA if triple-reset detected.
bool ota_checkAndStart();

// Force-start OTA mode unconditionally (used by test harness at boot).
void ota_forceStart();

// Service WiFi, ArduinoOTA, WebSerial, DNS. Call in loop().
// Also tracks loop timing statistics.
void ota_handle();

// Returns true if OTA mode is active.
bool ota_isActive();

// Register a callback for unrecognised WebSerial commands.
void ota_setCmdCallback(OtaCmdCallback cb);

// Set the current screen name for display in 'info' and 'screen' commands.
void ota_setCurrentScreen(const char *screen);

// Clear the NVS reset counter.
void ota_clearResetCounter();

#endif
