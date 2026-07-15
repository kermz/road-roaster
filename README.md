# Road Roaster

Road Roaster is a two-board ESP32-S3 template for selecting preset messages on
a Waveshare knob/touch controller and displaying them on a 64×32 P4 HUB75 LED
matrix. The boards communicate directly over encrypted ESP-NOW; no router or
phone is involved.

The rear board is the source of truth. It owns every message, its matrix text,
animation, and default duration. The controller learns the catalog at runtime,
shows four messages per page, and displays only state acknowledged by the rear
board.

## Hardware

- [Waveshare ESP32-S3 RGB Matrix](https://www.waveshare.com/esp32-s3-rgb-matrix.htm)
- One 64×32 P4 HUB75 panel
- [Waveshare ESP32-S3 Knob Touch LCD 1.8](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm)

Use a properly rated 5 V supply for the matrix and connect the grounds. Do not
route panel current through either ESP32 board or its USB connector. The matrix
starts at 35% brightness and the controller LCD at 80%. Both values can be
changed independently from the controller and persist in each board's NVS
memory. Check panel power and thermal behavior before using high brightness.

## Features

- Synchronizes up to 64 rear-owned catalog entries in pages of four.
- Starts the rear display blank after every reboot.
- Uses four 2×2 LVGL touch zones and wraps pages with the rotary knob.
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
- Disables radio actions after 3.5 seconds without a rear heartbeat.
- Automatically reloads the catalog when its runtime revision hash changes.
- Opens brightness settings by tapping **ROAD ROASTER**.
- Controls the controller LCD and rear matrix independently in 5% steps from
  5–100%.
- Reports rear brightness over ESP-NOW and shows its slider disabled in gray
  while the rear unit is unavailable.
- Persists controller brightness, rear brightness, and per-message duration
  preferences in NVS memory.
- Uses one shared, round-screen-safe popup style for duration, brightness, and
  future dialogs. Popups open instantly without display animation.
- Uses the onboard DRV2605 motor's strong-click effect for knob interactions,
  popup opening, saved settings, acknowledgements, state changes, and
  connection loss, without vibrating on every countdown tick.

The initial catalog contains `Message 01` through `Message 12`, spread across
three UI pages, with static, pulse, marquee, and color-cycle examples.

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
- Tap **ROAD ROASTER**: open brightness settings.
- Touch a brightness slider, then rotate the knob or drag the slider: adjust
  the selected display brightness.
- Tap **Clear**: blank the active rear message.

## Customize messages

Edit only `src/rear_display/message_catalog.cpp`. Each entry is:

```cpp
{id, "Controller label", "MATRIX TEXT", AnimationKind::Static,
 {red, green, blue}, default_duration_ms}
```

Requirements:

- IDs are non-zero `uint16_t` values and must be unique.
- Controller labels are at most 31 UTF-8 bytes.
- The catalog may contain at most 64 entries.
- Matrix text stays on the rear and is not constrained by the radio label
  limit, although long text is best paired with `AnimationKind::Marquee`.
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
- `lib/knob_board/` — Waveshare LCD, CST816 touch, encoder, backlight, and
  DRV2605 haptic BSP
- `test/test_protocol/` — native packet, catalog boundary, and retry tests

The LCD and HUB75 pin maps and display initialization are adapted from
Waveshare's examples. The rear configuration uses GPIO E 9, `SHIFTREG`, a
false clock phase, and double buffering as required by the RGB Matrix board.

## Hardware verification checklist

Compilation validates the pinned APIs, but these checks require the physical
boards and panel:

- Confirm red, green, and blue channels and row order with all four animations.
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
