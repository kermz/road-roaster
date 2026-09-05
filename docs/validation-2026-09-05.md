# Feature validation and efficiency audit — 2026-09-05

Historical development notes moved from the README. Results and proposed experiments
reflect the audit date; this is not a current test-status report.

## Feature test audit

Audit: 2026-09-05. All 60 automated tests pass: 17 protocol, 25 controller,
18 rear/renderer. There were 23 tests before this audit; 37 were added. The table
maps every Features/Controls item to named cases. Test names below omit `test_`.
C = `test/test_controller/test_main.cpp`, R = `test/test_rear/test_main.cpp`,
P = `test/test_protocol/test_main.cpp`. Hardware cases H1–H6 below remain unrun.

| Feature/action | Automated cases | Hardware boundary |
| --- | --- | --- |
| Encrypted fixed-peer ESP-NOW, radio setup/failure | C `setup_and_radio_failure_states`; P `malformed_packets_are_rejected` | H1: real encryption, peer filtering, range and reception |
| Rear catalog, 64-entry limit, four per page, partial pages | P `maximum_catalog_and_packet_size_are_supported`, `catalog_store_handles_three_and_partial_pages`; R `catalog_requests_and_invalid_revision` | H1 |
| Automatic revision reload/reboot recovery | C `full_catalog_reload_through_packets`, `configured_catalog_validates_and_syncs`; P `revision_changes_and_malformed_pages_are_rejected` | H1 |
| Four touch choices, rotary page wrapping, page transition | C `page_navigation_all_four_slots_and_partial_page` | H2: touch/encoder electrical input and motion smoothness |
| Tap and saved/default duration | C `tap_uses_saved_duration_and_waits_for_ack`, `duration_preference_survives_controller_reboot` | H2/H3 |
| Long-press; 5–60 seconds; Default; Save without sending; Cancel | C `long_press_rotate_cancel_save_and_default` | H2 |
| Failed Save keeps dialog open; retry succeeds | C `duration_saves_selected_id_and_reports_write_failure` | H3: real NVS failures |
| Duration dialog during catalog replacement | C `sync_cancels_duration_and_stale_save`, `duration_rechecks_revision_and_partial_catalog` | — |
| Blank startup, active message, expiration, Clear | R `boot_blank_and_settings_restore`, `display_default_override_duplicate_and_expiry`; C `clear_and_blank_status` | H3/H4 |
| Sending/failed/unavailable status | C `tap_uses_saved_duration_and_waits_for_ack`, `timeout_retries_same_command_then_recovers`, `disconnect_disables_rear_settings_and_recovers` | H1/H2 |
| ACK countdown and heartbeat correction | C `ack_countdown_heartbeat_correction_and_haptics`; R `clear_get_status_and_one_second_heartbeat` | H1 |
| 300 ms retry ×3, same sequence, no timer restart | C `timeout_retries_same_command_then_recovers`; R `display_default_override_duplicate_and_expiry`; P `retransmission_keeps_the_same_sequence_and_bytes` | H1: actual packet loss |
| Reject stale/invalid display commands | R `invalid_display_commands_do_not_replace_active_message` | — |
| Disable message/Clear during pending requests | C `pending_display_and_settings_disable_actions_until_ack` | — |
| 3.5-second disconnect; rear slider/switch disable; recovery | C `disconnect_disables_rear_settings_and_recovers` | H1/H2: gray visual appearance |
| Tap brand for settings; focus slider; drag; Back | C `settings_open_focus_drag_flip_and_back` | H2 |
| Independent brightness, five-percent steps, limits, full knob delta | C `knob_brightness_consumes_every_detent`, `settings_open_focus_drag_flip_and_back`; P `brightness_validation` | H2/H4: physical brightness |
| Independent 180-degree flip | C `settings_open_focus_drag_flip_and_back`; R `renderer_flip_redraws_static_and_brightness_maps` | H2/H4: actual LCD, touch and matrix orientation |
| Persist brightness/flip; debounce; NVS retry | C `controller_settings_debounce_retry_and_reboot`; R `rear_settings_persist_retry_and_invalid_brightness`, `boot_blank_and_settings_restore` | H3 |
| Preserve queued settings through sync/retry | C `sync_preserves_and_sends_queued_settings`, `rear_setting_retry_retains_latest_choice` | — |
| Battery estimate, smoothing, failure threshold and recovery | C `battery_filter_failure_threshold_and_recovery` | H5: ADC calibration and actual state of charge |
| Shared instant modal geometry and haptic triggers | C `popup_and_knob_feedback_and_instant_modals`, `settings_open_focus_drag_flip_and_back`, `ack_countdown_heartbeat_correction_and_haptics` | H2: round-screen clipping, strong-click effect and tactile quality |
| Static/Pulse/ColorCycle, font fit, one to four screens and scrolling | R `renderer_static_fits_largest_font_and_idles`, `renderer_pulse_cycle_and_clear`, `renderer_multiple_screens_gap_loop_and_hold`, `renderer_scrolls_long_text_and_reuses_layout`, `renderer_fits_smaller_font_and_four_screen_order` | H4: actual glyph pixels, DMA scan and flicker |
| UTF-8/Latin-1 and unsupported-character fallback | P `utf8_validation_and_latin1_encoding`; R `renderer_latin1_and_fallback` | H4 |
| Catalog validation and editable text/colors | C `configured_catalog_validates_and_syncs`; P `catalog_store_rejects_duplicates_and_boundaries`; R `invalid_preset_configuration_is_rejected`, `renderer_pulse_cycle_and_clear` | H6: deliberately invalid rear configurations |
| Uptime rollover | P `duration_rules_and_wrap_safe_remaining_time`; R `expiry_and_retry_across_millis_wrap` | — |

Test-driven changes: these assertions were added and observed failing before
changing production behavior; all pass afterward:

| Test | Before | After |
| --- | --- | --- |
| C `knob_brightness_consumes_every_detent` | Three detents changed 80% to 85% | 95%; large deltas clamp safely |
| C `noop_settings_do_not_send_or_persist` | Unchanged settings queued work | No radio packet or NVS write |
| R `duplicate_setting_does_not_write_again` | Two writes for unchanged settings | Zero writes; commands still acknowledged |
| R `renderer_static_fits_largest_font_and_idles` | 17 cumulative frame submissions during the sample, including initialization | 3 (two initialization frames plus first content frame) |
| R `renderer_multiple_screens_gap_loop_and_hold` | Repeated frame during a hold | Frame retained until content/gap changes |
| R `renderer_scrolls_long_text_and_reuses_layout` | Measurement count increased 8 to 16 on next frame | Remains 4 after activation; scrolling still advances |
| C `lvgl_clock_tracks_elapsed_time_without_periodic_tick` | Clock remained zero without tick callbacks | Tracks elapsed time on demand |
| R `expiration_sends_one_status_at_heartbeat_boundary` | Two identical status packets | One immediate status; next heartbeat one second later |

The LCD invalidation test already passed before optimization: LVGL avoids
invalidating unchanged style values internally, so no extra UI cache was added.
Tests for existing actions can pass on their first run; the failing-first cycle
was used for behavioral fixes and the new efficiency requirements.

## Battery efficiency and next experiments

Implemented without lowering brightness, shortening messages, changing the
67 ms animation sampling, slowing the main input loop, or reducing radio
availability:

- The controller reads LVGL time on demand from `esp_timer_get_time()` instead
  of scheduling a callback every 2 ms (500 callbacks/second removed). This also
  avoids clock drift from skipped timer callbacks. The encoder sampling remains
  at 3 ms and LVGL's display/input periods remain 20 ms.
- Both boards skip unchanged setting work; existing failed-write retries remain
  queued. Rear commands still return ACKs even when the value is unchanged.
- The rear encodes and fits each screen once per activation, caches about 2.2 KB
  of screen data, and submits a frame only when its visible content changes.
  A fitting static screen retains its DMA frame; pulse, cycle, scroll and flip
  still update. **DMA continues scanning the panel**, so this reduces CPU/memory
  work, not LED brightness or the panel's continuous scanning power.
- Expiration sends a single status packet when it coincides with a heartbeat.

These are verified reductions in work, not measured battery-life gains. The host
fakes do not model current, regulator efficiency, physical NVS, or radio timing.
The following experiments offer further savings, ranked by practical opportunity;
none is enabled without hardware evidence that the experience remains acceptable:

| Candidate | Board | Required acceptance evidence |
| --- | --- | --- |
| Measure blank panel idle current; investigate OE-safe scan suspension or a switched panel supply while blank | Rear | Restore within the current command-response budget, no flash/stuck row, repeated clear/show stress test; do not stop DMA mid-lit frame |
| Ambient-light brightness control with immediate manual override | Both | Equal readability in daylight/night, no hunting/flicker or surprising dimming; requires an ambient sensor or a user-selected mode |
| DRV2605 standby after effect completion, wake before the same strong click | Controller | Confirm the actual chip and timing; compare click strength/latency and rapid knob bursts. TI documents a low-power standby state and fast startup: [DRV2605](https://www.ti.com/product/DRV2605) |
| Dynamic CPU frequency scaling, followed by automatic light sleep where peripheral locks permit | Both | Rebuild the framework with power management/tickless-idle support, audit LCD/HUB75 driver locks, compare input latency, radio loss, and flicker. The installed qio_opi SDK does not enable `CONFIG_PM_ENABLE` or tickless idle. See [ESP-IDF 5.5 power management](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/power_management.html) |
| Coordinated ESP-NOW receive windows | Both | Measure worst-case tap-to-display latency and packet loss at the intended distance before adopting. ESP-NOW requires configuring both wake window and interval: [ESP-IDF 5.5 ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/network/esp_now.html) |
| Interrupt-driven encoder/touch wake instead of frequent polling | Controller | Verify available hardware interrupt pins and debounce; zero missed/reversed detents and no added first-touch delay |

Avoid treating normal AP modem-sleep settings as a drop-in solution for this
routerless ESP-NOW pair. Keep the current always-listening radio behavior until
coordinated receive windows have been measured. Automatic dimming/deep sleep is
also not enabled because it would change the always-readable, immediate controls.

Hardware acceptance cases (not executed in this audit):

- **H1 — radio:** use the paired boards; verify correct keys work and mismatched
  keys/peer do not. At the intended range, issue at least 100 Display/Clear
  commands, record latency/loss, interrupt reception, verify three retries and
  the 3.5-second unavailable state, then reboot either board and verify recovery.
- **H2 — controls/feel:** tap all four slots on every page; long-press, cancel,
  save, drag/rotate both brightness sliders to both limits, flip both displays,
  and return Back. Repeat rapid rotations and taps. Pass only with no missed
  detents, correct touch coordinates, no clipped dialogs, instant popup opening,
  and the same strong click without countdown vibration.
- **H3 — persistence:** change both brightness/flip settings and a duration; wait
  at least 750 ms after the last display-setting edit, reboot both boards, and
  verify restoration plus blank rear startup. In a fault-injection build force
  NVS writes to fail, then recover; verify retries and visible duration-save error.
- **H4 — matrix:** exercise every preset, all three animations, long scrolling
  text, four screens, both orientations, and 5/35/100% brightness. Pass only with
  correct channels/glyphs, clear gaps, complete blanking and no extra flicker.
- **H5 — power:** measure each board's input power separately plus the rear panel
  supply, with the same voltage/brightness/text/firmware workload before and after.
  Record a 60-second average for blank, fitting-static, animated and interactive
  cases; also record tap-to-display median/p95 and battery-gauge error against a
  meter. Accept a candidate only if power drops and responsiveness/readability
  stay at least as good. Do not infer battery savings from CPU-work counts alone.
- **H6 — config validation:** in a test build introduce a duplicate/zero ID,
  overlength or non-ASCII controller label, empty/noncontiguous screen, invalid
  UTF-8, or more than 64 entries. Each must stop rear startup; restore a valid
  configuration and verify the controller resynchronizes without rebuilding it.
