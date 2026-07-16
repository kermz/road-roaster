# Road Roaster

Road Roaster is a two-board ESP32-S3 template for selecting preset messages on
a Waveshare knob/touch controller and displaying them across two chained 64×32
P4 HUB75 LED panels as one 128×32 canvas. The boards communicate directly over
encrypted ESP-NOW; no router or phone is involved.

The rear board is the source of truth. It owns every message, its explicit list
of matrix screens, animation, and default duration. The controller learns the
catalog at runtime, shows four messages per page, and displays only state
acknowledged by the rear board.

## Hardware

- [Waveshare ESP32-S3 RGB Matrix](https://www.waveshare.com/esp32-s3-rgb-matrix.htm)
- Two daisy-chained 64×32 P4 HUB75 panels (128×32 total)
- [Waveshare ESP32-S3 Knob Touch LCD 1.8](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm)

Connect the HUB75 output of the first panel to the input of the second. Use a
properly rated 5 V supply for both panels and connect all grounds. Do not route
panel current through either ESP32 board or its USB connector. The matrix starts
at 35% brightness and the controller LCD at 80%. Both values can be changed
independently from the controller and persist in each board's NVS memory. Check
panel power and thermal behavior before using high brightness.

## Features

- Synchronizes up to 64 rear-owned catalog entries in pages of four.
- Starts the rear display blank after every reboot.
- Uses four 2×2 LVGL touch zones and wraps pages with the rotary knob.
- Shows a subtle, filtered controller battery estimate beneath the page count.
- Sends a message by tapping it and opens its duration popup by long-pressing.
- Saves a persistent per-message `Default` or 5–60 second duration preference;
  normal taps reuse the saved choice.
- Uses **Save** to store a duration without displaying the message immediately.
  Selecting `Default` removes the stored override.
- Shows an empty status box while blank, `Sending`, acknowledged message and
  timer state, `Command failed`, or `Rear unavailable`.
- Provides a separate **Clear** action while a message is active.
- Starts a local countdown from the rear acknowledgement immediately, while
  one-second rear heartbeats continue to correct the authoritative state.
- Retries the same sequence number after 300 ms, up to three retries, so a
  dropped acknowledgement cannot restart the rear timer.
- Includes the catalog revision in display commands, so a controller with a
  stale label-to-ID mapping cannot activate a different rear message.
- Disables radio actions after 3.5 seconds without a rear heartbeat.
- Automatically reloads the catalog when its runtime revision hash changes.
- Opens display settings by tapping **ROAD ROASTER**.
- Controls the controller LCD and rear matrix independently in 5% steps from
  5–100%.
- Flips the controller LCD/touch coordinates and rear matrix independently by
  180 degrees from the same display settings popup.
- Reports rear brightness over ESP-NOW and shows its slider disabled in gray
  while the rear unit is unavailable.
- Persists controller/rear brightness, both flip settings, and per-message
  duration preferences in NVS memory, retries failed display-setting writes,
  and keeps the duration dialog open with an error when a duration cannot be
  saved.
- Uses one shared, round-screen-safe popup style for duration, brightness, and
  future dialogs. Popups open instantly without display animation.
- Uses the onboard DRV2605 motor's strong-click effect for knob interactions,
  popup opening, saved settings, acknowledgements, state changes, and
  connection loss, without vibrating on every countdown tick.

The current catalog contains 12 entries across three UI pages. The first nine
are road messages such as `Sell it`, `Thanks`, `Sorry`, `Wanna race?`, and
`Nice car`; entries 10–12 remain editable example placeholders. The catalog
includes static, pulse, and color-cycle animation settings, while oversized
screens scroll automatically.

## First-time radio setup

The committed example secrets are intentionally inert. Both boards print their
Wi-Fi station MAC to the 115200-baud serial monitor while `kConfigured` is
false.

1. Build and flash each board once, then record both printed station MACs.
2. Copy `include/secrets.example.h` to `include/secrets.h`.
3. Set `kConfigured = true` and enter:
   - the controller MAC as `kControllerPeerMac`;
   - the rear MAC as `kRearDisplayPeerMac`;
   - private 16-byte PMK and LMK values.
4. Keep the same PMK, LMK, and channel on both builds. Channel 6 is the
   template default.
5. Rebuild and flash both boards. `include/secrets.h` is ignored by Git.

The peer is fixed, unicast, and encrypted. Changing a MAC, channel, PMK, LMK,
or protocol version requires rebuilding both firmware images.

## Build and upload

The PlatformIO configuration pins pioarduino `55.03.37`, which supplies
Arduino-ESP32 3.3.7. It also pins LVGL 8.4.0 and the HUB75 DMA library.
The custom definitions in `boards/` describe the controller's 16 MB flash / 8
MB PSRAM and the rear board's 32 MB flash / 16 MB PSRAM accurately.

PlatformIO is installed in its own Python environment and may not add `pio` to
the Windows PATH. These PowerShell commands work with the default installation:

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe"

& $pio run -e rear_display
& $pio run -e controller

# Replace these example ports with the ports assigned on your machine.
& $pio run -e rear_display -t upload --upload-port COM3
& $pio run -e controller -t upload --upload-port COM4
```

Run shared protocol and catalog tests with a host C/C++ compiler available in
`PATH`:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" test -e native
```

## Controls

- Rotate the knob: change message pages.
- Tap a message: display it using its saved override or rear-owned default.
- Long-press a message: open the duration popup.
- Rotate inside the duration popup: select `Default` or 5–60 seconds.
- Tap **Save**: persist the selected duration; tap **Cancel** to discard it.
- Tap **ROAD ROASTER**: open display settings.
- Touch a brightness slider, then rotate the knob or drag the slider: adjust
  the selected display brightness.
- Toggle **FLIP** for the controller or rear display independently.
- Tap **Clear**: blank the active rear message.

## Customize messages

Edit only `src/rear_display/message_catalog.cpp`. Each entry is:

```cpp
{id, "Controller label", {{"SCREEN 1", "SCREEN 2"}}, AnimationKind::Static,
 {red, green, blue}, default_duration_ms}
```

Each string inside the nested braces is one intentional display screen. Keep
text that must appear together in the same string. For example:

```cpp
{4, "Wanna race?", {{"You seem fast", "prove it"}},
 AnimationKind::Static, {255, 255, 255}, kDefaultDurationMs}
```

This creates two screens. Commas and other punctuation inside a screen remain
ordinary visible text and have no layout meaning.

Requirements:

- IDs are non-zero `uint16_t` values and must be unique.
- Controller labels are non-empty ASCII strings of at most 31 bytes. The
  bundled LVGL Montserrat builds only contain ASCII plus selected symbols, so
  catalog validation rejects non-ASCII labels. The bundled
  `Montserrat-Medium.ttf` contains Estonian characters and can be used to
  generate custom LVGL subsets if accented controller labels are needed; add
  the subset at both 18 px and 22 px.
- The catalog may contain at most 64 entries.
- A preset contains one to four non-empty screens. Screen entries must be
  contiguous; do not leave an empty slot between two screens.
- Each screen stays on the rear, must be valid UTF-8, may contain at most 512
  bytes, and is not constrained by the radio label limit. The Spleen strikes
  cover Latin-1; valid characters outside Latin-1 render as `?`.
- Every screen independently selects the largest native bitmap strike that
  fits the 128×32 canvas. A screen that remains too wide scrolls automatically.
- A single fitting screen remains steady. Multiple screens advance in their
  listed order and loop for the preset's active duration.
- The controller does not need to be rebuilt after a rear catalog change.

Duplicate IDs or invalid entries stop rear application startup and print a
fatal configuration error instead of advertising an ambiguous catalog.

## Source map

- `src/rear_display/` — catalog, HUB75 renderer, animations, rear state machine,
  and persisted matrix brightness
- `src/controller/` — catalog synchronization, retry logic, persisted message
  durations, acknowledged countdown, and LVGL UI
- `lib/road_roaster_protocol/` — versioned wire format and catalog store
- `lib/road_roaster_radio/` — encrypted ESP-NOW transport
- `lib/knob_board/` — Waveshare LCD, CST816 touch, encoder, battery ADC,
  backlight, and DRV2605 haptic BSP
- `test/test_protocol/` — native packet, catalog boundary, retry, revision,
  and request-coordination tests
- `boards/` — hardware-accurate PlatformIO definitions for both ESP32-S3 boards

The LCD and HUB75 pin maps and display initialization are adapted from
Waveshare's examples. The rear configuration drives two chained 64×32 panels
as a 128×32 canvas, leaves GPIO E unused for the 1/16-scan panels, and uses
`SHIFTREG`, a false clock phase, and double buffering.
Rear-display text uses Spleen's native bitmap strikes at 16×32, 12×24, 8×16,
and 6×12 pixels. The BDF sources and BSD 2-Clause License are in
`tools/fonts/spleen/`.

Each rear-display preset declares an explicit list of screens in
`message_catalog.cpp`. Text within one entry is fitted and displayed together;
separate entries advance as separate screens. For example,
`{{"You seem fast", "prove it"}}` creates two screens without treating commas
or other punctuation as layout commands.

## Hardware verification checklist

Compilation validates the pinned APIs, but these checks require the physical
boards and panels:

- Confirm red, green, and blue channels and row order with all three animation
  modes and automatic scrolling.
- Look for flicker or ghosting at the intended brightness and supply load.
- Confirm clockwise knob direction; swap the sign in `ControllerApp::loop()`
  if the installed encoder feels reversed.
- Power-cycle each board and confirm brightness and message-duration
  preferences persist.
- Reboot the controller during an active message and confirm catalog and active
  state restoration.
- Reboot the rear board and confirm it starts blank and resynchronizes.
- Temporarily shield or power down one board to verify the 3.5-second
  unavailable state and catalog/command retries.
