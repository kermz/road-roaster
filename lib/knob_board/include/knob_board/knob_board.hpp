#pragma once

#include <cstdint>

namespace knob_board {

inline constexpr int kDisplayWidth = 360;
inline constexpr int kDisplayHeight = 360;

// Drives the LCD reset and active-high backlight low at the earliest Arduino
// initialization hook, before setup() and peripheral initialization.
void earlyBlankDisplay();

// Initializes the Waveshare 1.8-inch QSPI LCD, CST816 touch controller,
// quadrature encoder, LVGL display driver, and backlight.
bool begin();

// LVGL stays on the Arduino loop task so UI and application state never need
// a cross-task mutex. Call this every loop iteration.
void processLvgl();

// Returns accumulated clockwise/counter-clockwise detents since the last call.
int consumeEncoderDelta();

void setBacklight(uint8_t percent);

// Rotates the LCD and touch coordinates together by 180 degrees.
void setDisplayFlipped(bool flipped);

// Returns an averaged estimate of the connected single-cell Li-ion battery.
// The board exposes the system voltage on GPIO 1 through a 2:1 divider.
// Returns false when the ADC or its calibration data is unavailable.
bool readBatteryPercent(uint8_t& percent);

// Plays a short, non-blocking click through the onboard DRV2605.
void vibrate();

}  // namespace knob_board
