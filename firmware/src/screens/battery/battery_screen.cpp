// =============================================================================
// battery_screen.cpp — Dual battery voltage screen module
//
// Handles all rendering for the battery voltage OLED display.  The caller
// (main.cpp) passes in a pointer to the shared Adafruit_SH1106G display
// object and feeds pre-calculated battery voltages each frame.
//
// Screen layout (128×64):
//   Two side-by-side automotive battery icons, each showing:
//     - "BATTERY 1" / "BATTERY 2" label across the top
//     - Terminal bumps extending above the battery body
//     - Outlined battery body
//     - Large "XX.X" voltage readout (text size 2) inside the body
//     - "V" unit label (text size 1) beside the digits
//     - Horizontal charge-level bar near the bottom of the body
//
// Rendering strategy:
//   - Static chrome (outlines, terminals, labels, bar borders, "V") is drawn
//     once during init and never redrawn.
//   - Dynamic elements (voltage digits, bar fill) use partial refresh:
//     only the pixels that actually changed are overwritten, minimising
//     I2C traffic and eliminating full-screen flicker.
// =============================================================================

#include "battery_screen.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>

// =============================================================================
// Module-level state
// =============================================================================
static Adafruit_SSD1306 *_display = nullptr;  // TEMP — SSD1306 for 0.96" test display

// Voltage range for the charge-level bar graph.
// 10 V = dead flat (bar empty), 15 V = alternator full charge (bar full).
static const float BAR_V_MIN  = 10.0f;
static const float BAR_V_MAX  = 15.0f;
static const int   BAR_MAX_PX = 51;     // max fill width inside bar border

// Per-battery rendering state for partial refresh
struct _BatState {
    float lastVoltage;
    float lastBarWidth;
};

static _BatState _bat1 = { -1.0f, -1.0f };
static _BatState _bat2 = { -1.0f, -1.0f };

// X offsets for the left and right battery halves of the screen
static const int X_LEFT  = 0;
static const int X_RIGHT = 65;


// =============================================================================
// Draw one battery outline — body, terminals, label, "V" unit, bar border
//
// Called once per battery during init.  Everything drawn here is static
// chrome that the partial-refresh renderers are careful to avoid.
//
// Visual layout for one battery (61 × 47 body):
//
//        ┌──┐      ┌──┐          terminals
//   ┌────┘  └──────┘  └────┐
//   │                       │
//   │       XX.X V          │    voltage digits + unit
//   │                       │
//   │   ┌─────────────┐    │
//   │   │ ██████████   │    │    charge-level bar
//   │   └─────────────┘    │
//   └───────────────────────┘
// =============================================================================
static void _drawBatteryChrome(int xOff, const char *label) {
    // --- Battery body outline ---
    _display->drawRect(xOff + 1, 16, 61, 47, SSD1306_WHITE);

    // --- Terminals (small outlined rects above body, opening into it) ---
    //  + terminal
    _display->drawRect(xOff + 11, 9, 10, 8, SSD1306_WHITE);
    _display->drawFastHLine(xOff + 12, 16, 8, SSD1306_BLACK);  // open into body

    //  – terminal
    _display->drawRect(xOff + 41, 9, 10, 8, SSD1306_WHITE);
    _display->drawFastHLine(xOff + 42, 16, 8, SSD1306_BLACK);  // open into body

    // --- Label across the top ---
    _display->setTextSize(1);
    _display->setTextColor(SSD1306_WHITE);
    _display->setCursor(xOff + 2, 0);
    _display->print(label);

    // --- "V" unit beside where the voltage digits will appear ---
    _display->setTextSize(1);
    _display->setCursor(xOff + 55, 36);
    _display->print("V");

    // --- Charge-level bar border (inside body, near bottom) ---
    _display->drawRect(xOff + 5, 50, 53, 8, SSD1306_WHITE);
}


// =============================================================================
// Render the voltage digits for one battery (text size 2 = 12×16 px each)
//
// Displays "XX.X" inside the battery body.  Each digit is tracked
// independently — only changed cells are cleared and reprinted.
// The decimal point is always redrawn since it's tiny and not worth tracking.
// A single display->display() call is made at the end if anything changed.
// =============================================================================
static void _renderVoltageDigits(int xOff, float voltage, float &lastVoltage) {
    int intPart  = (int)voltage;
    int tens     = (intPart / 10) % 10;
    int ones     = intPart % 10;
    int decimal  = (int)(voltage * 10) % 10;

    int lastInt  = (int)lastVoltage;
    int lTens    = (lastInt / 10) % 10;
    int lOnes    = lastInt % 10;
    int lDecimal = (int)(lastVoltage * 10) % 10;

    const int SZ = 2;
    const int CW = 6 * SZ;                          // 12 px character cell width
    const int CH = 8 * SZ;                          // 16 px character cell height
    const int X  = xOff + 7;                        // first digit x position
    const int Y  = 26;                              // digit row y position

    bool changed = false;

    if (tens != lTens) {
        _display->fillRect(X, Y, CW, CH, SSD1306_BLACK);
        _display->setTextSize(SZ);
        _display->setCursor(X, Y);
        _display->print(tens);
        changed = true;
    }
    if (ones != lOnes) {
        _display->fillRect(X + CW, Y, CW, CH, SSD1306_BLACK);
        _display->setTextSize(SZ);
        _display->setCursor(X + CW, Y);
        _display->print(ones);
        changed = true;
    }

    // Decimal point — always redraw (cheap)
    _display->setTextSize(SZ);
    _display->setCursor(X + 2 * CW, Y);
    _display->print(".");

    if (decimal != lDecimal) {
        _display->fillRect(X + 3 * CW, Y, CW, CH, SSD1306_BLACK);
        _display->setTextSize(SZ);
        _display->setCursor(X + 3 * CW, Y);
        _display->print(decimal);
        changed = true;
    }

    if (changed) _display->display();
    lastVoltage = voltage;
}


// =============================================================================
// Render the charge-level bar for one battery
//
// Maps BAR_V_MIN–BAR_V_MAX (10–15 V) to 0–51 pixels inside the bar border.
// Compares current and previous integer pixel widths — skips the I2C write
// entirely if the bar hasn't moved.  When the bar shrinks, only the vacated
// region is erased (black fill), then the active region is redrawn (white).
// =============================================================================
static void _renderBar(int xOff, float voltage, float &lastBarWidth) {
    float barW = map((long)(voltage * 10), (long)(BAR_V_MIN * 10),
                     (long)(BAR_V_MAX * 10), 0, BAR_MAX_PX * 10) / 10.0f;
    barW = constrain(barW, 0.0f, (float)BAR_MAX_PX);

    int newPx = (int)barW;
    int oldPx = (int)lastBarWidth;

    if (newPx == oldPx) return;

    // Erase using integer pixel boundaries — no truncation gap possible
    if (newPx < oldPx) {
        _display->fillRect(xOff + 6 + newPx, 51, oldPx - newPx, 6, SSD1306_BLACK);
    }

    if (newPx > 0) {
        _display->fillRect(xOff + 6, 51, newPx, 6, SSD1306_WHITE);
    }

    _display->display();
    lastBarWidth = barW;
}


// =============================================================================
// Public API
// =============================================================================

void batteryScreen_init(Adafruit_SSD1306 *dsp) {  // TEMP — SSD1306 for 0.96" test display
    _display = dsp;

    // Reset all tracking state
    _bat1 = { -1.0f, -1.0f };
    _bat2 = { -1.0f, -1.0f };

    _display->clearDisplay();

    _drawBatteryChrome(X_LEFT,  "BATTERY 1");
    _drawBatteryChrome(X_RIGHT, "BATTERY 2");

    _display->display();
}

void batteryScreen_update(float bat1Voltage, float bat2Voltage) {
    // Constrain to a displayable range (XX.X format, max 19.9 V)
    float v1 = constrain(bat1Voltage, 0.0f, 19.9f);
    float v2 = constrain(bat2Voltage, 0.0f, 19.9f);

    _renderVoltageDigits(X_LEFT,  v1, _bat1.lastVoltage);
    _renderBar(X_LEFT,  v1, _bat1.lastBarWidth);

    _renderVoltageDigits(X_RIGHT, v2, _bat2.lastVoltage);
    _renderBar(X_RIGHT, v2, _bat2.lastBarWidth);
}
