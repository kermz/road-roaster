# Road Roaster

A wireless message display built around two ESP32-S3 boards: a round knob/touch
controller and a rear LED matrix. Select a preset on the controller to show it
across two chained 64×32 panels, forming a single 128×32 canvas.

The boards communicate directly over encrypted ESP-NOW, without a router or
phone. The rear board owns the message catalog, animations, and default durations;
the controller loads that catalog automatically and shows acknowledged display
state. The rear display starts blank after every reboot.

## What it does

- Browse four messages per page using the rotary knob and touch screen.
- Display static, pulsing, or color-cycling text, with automatic font fitting,
  scrolling, and sequences of up to four screens per message.
- Save a duration for each message, or use its rear-owned default.
- Adjust brightness and rotate either display 180 degrees independently.
- Retain brightness, orientation, and duration preferences across restarts.
- Show the active message, countdown, connection status, and controller battery
  estimate, with haptic feedback for interactions and state changes.
- Resynchronize after catalog changes and retry dropped commands without
  restarting the display timer.

The included catalog has 12 presets, including three editable placeholders.
Up to 64 presets are supported.

## Hardware

| Component | Purpose |
| --- | --- |
| [Waveshare ESP32-S3 Knob Touch LCD 1.8](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm) | Touch screen, rotary input, and haptic controller |
| [Waveshare ESP32-S3 RGB Matrix](https://www.waveshare.com/esp32-s3-rgb-matrix.htm) | Rear display driver |
| Two 64×32 P4 HUB75 panels | Chained 128×32 display |
| Appropriately rated 5 V panel supply | Power for both panels |

Connect the first panel's HUB75 output to the second panel's input and connect
all grounds. Do not route panel current through either ESP32 board or its USB
connector. Default brightness is 35% for the matrix and 80% for the controller;
check panel power and thermal behavior before increasing it.

## Build and pair

Use PlatformIO with the environments in `platformio.ini`. The project pins
pioarduino 55.03.37 (Arduino-ESP32 3.3.7), LVGL 8.4.0, and the display libraries.
Custom board definitions specify the flash and PSRAM for each board.

```sh
pio run -e controller -e rear_display

# Replace the example ports with those assigned to your boards.
pio run -e controller -t upload --upload-port COM4
pio run -e rear_display -t upload --upload-port COM3
```

If `pio` is not on your Windows PATH, invoke it from PowerShell with
`& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe"` followed by the
same arguments.

The example radio configuration is inert. Pair the boards once before use:

1. Build and flash each board with the example configuration. Read its Wi-Fi
   station MAC from the serial monitor at 115200 baud.
2. Copy `include/secrets.example.h` to `include/secrets.h`.
3. Set `kConfigured = true`, enter the controller MAC as `kControllerPeerMac`
   and the rear MAC as `kRearDisplayPeerMac`, and supply private 16-byte PMK
   and LMK values.
4. Use the same keys and channel for both builds; the default channel is 6.
5. Rebuild and flash both boards.

`include/secrets.h` is ignored by Git. The connection uses a fixed, encrypted
unicast peer; changes to pairing or protocol configuration require rebuilding
both firmware images.

## Controls

| Action | Result |
| --- | --- |
| Rotate the knob | Change message pages, wrapping at either end |
| Tap a message | Display it using its saved duration or default |
| Long-press a message | Open its duration dialog |
| Rotate in the duration dialog | Choose `Default` or 5–60 seconds in 5-second steps |
| Tap **Save** / **Cancel** | Save the duration without displaying / discard the edit |
| Tap **ROAD ROASTER** | Open display settings |
| Touch a brightness slider, then rotate or drag | Adjust that display from 5–100% |
| Toggle **FLIP** | Rotate the selected display 180 degrees |
| Tap **Clear** | Blank the active rear message |

Message and Clear actions are disabled while a radio request is pending. After
3.5 seconds without a rear heartbeat, the controller shows **Rear unavailable**
and disables rear controls until communication recovers.

## Customize

Edit `include/config/presentation.hpp` to change controller text, theme colors,
rear presets, and the color-cycle palette. Rebuild and flash the affected board;
rear catalog changes synchronize without rebuilding the controller.

Each preset has a stable ID, controller label, one to four screen strings,
animation, RGB color, and default duration. Keep IDs stable because saved
duration preferences are keyed by ID. The rear validates the catalog at startup.

See [Customization](docs/customization.md) for examples, text limits, font support,
and configuration rules.

## Repository map

| Path | Contents |
| --- | --- |
| `src/controller/` | Controller state machine, catalog synchronization, and LVGL UI |
| `src/rear_display/` | Rear state machine, catalog, animations, and HUB75 renderer |
| `include/config/presentation.hpp` | Editable text, colors, and presets |
| `include/preset_definition.hpp` | Shared preset types and screen limits |
| `lib/road_roaster_protocol/` | Versioned wire format and catalog store |
| `lib/road_roaster_radio/` | Encrypted ESP-NOW transport |
| `lib/knob_board/` | LCD, touch, encoder, battery, backlight, and haptic drivers |
| `boards/` | PlatformIO board definitions |
| `test/` | Native protocol, controller/UI, and rear/renderer suites |
| `tools/fonts/spleen/` | Bitmap font sources and BSD 2-Clause license |
| `docs/` | Customization reference and dated development audits |

## Validation

Run all native suites with a host C/C++ compiler available in `PATH`:

```sh
pio test -e native -e native_controller -e native_rear
```

The controller and rear suites exercise the real application code with simulated
hardware, radio, and preferences. CI runs all three suites and builds both
firmware images. Physical radio range, touch alignment, haptics, panel refresh,
and power consumption require checks on the actual hardware.

The [September 2026 validation audit](docs/validation-2026-09-05.md) records
feature coverage, hardware acceptance cases, and efficiency experiments.

## Credits

Pin maps and display initialization are adapted from Waveshare's examples.
Rear text uses Spleen's native 16×32, 12×24, 8×16, and 6×12 bitmap strikes;
font sources and their license are included in `tools/fonts/spleen/`.
