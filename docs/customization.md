# Customization

Edit `include/config/presentation.hpp`. It is a compile-time configuration file:

- `rr::presentation::text`: controller branding, buttons, dialog titles, status
  messages, errors, placeholders, units, and separators.
- `rr::presentation::theme`: controller colors by purpose, including pressed
  buttons, modal surfaces, warnings, and errors. Colors use `0xRRGGBB`.
- `rr::presentation::matrix`: rear presets and the three color-cycle stops.
  Static and Pulse use the preset's RGB; ColorCycle uses the shared cycle stops.

For example, change `kBrand` to change the title and `kAccent` to change the
controller's accent color. Keep UI text short enough for the round screen and use
characters supported by the enabled LVGL fonts. Formatting stays type-safe in UI
code; configurable text is never interpreted as a printf format string.

Rebuild and flash the controller after changing its text/theme, or the rear board
after changing presets/cycle colors. Rear-only edits do not require a controller
rebuild. The catalog revision includes the cycle palette as well as preset data,
so rear color changes trigger resynchronization. Existing NVS brightness and flip
settings continue to take precedence over startup defaults.

Each entry in `matrix::kPresets` is:

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
- Keep IDs stable when editing or reordering messages: saved controller duration
  preferences are keyed by ID. Use a new ID for a different message if it should
  not inherit the previous message's saved duration.
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
