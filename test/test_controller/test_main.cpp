#include <unity.h>
#include <Arduino.h>
#include <Preferences.h>
#include <array>
#include <cstring>
#include <vector>
#include <deque>
#include <lvgl.h>
#include "rr/catalog_store.hpp"
#include "rr/radio_transport.hpp"
#include "knob_board/knob_board.hpp"
#include "message_catalog.hpp"
#include "config/presentation.hpp"

// Exercise actual application/UI transitions without exposing test-only APIs.
#define private public
#include "controller_app.hpp"
#undef private

namespace {
std::vector<rr::Packet> sent;
std::deque<rr::Packet> incoming;
int encoder_delta = 0;
unsigned haptics = 0;
uint8_t backlight = 0;
bool display_flip = false;
bool battery_available = false;
uint8_t battery_value = 0;
bool radio_start_succeeds = true;
bool radio_setup_required = false;

lv_disp_drv_t display_driver;
lv_disp_draw_buf_t draw_buffer;
lv_color_t pixels[360];
}

namespace rr {
bool RadioTransport::begin(const RadioConfig&) { diagnostic_mode_ = radio_setup_required; ready_ = radio_start_succeeds && !diagnostic_mode_; return ready_; }
bool RadioTransport::send(const Packet& packet) { sent.push_back(packet); return true; }
bool RadioTransport::receive(Packet& packet) { if (incoming.empty()) return false; packet = incoming.front(); incoming.pop_front(); return true; }
}
namespace knob_board {
bool begin() { return true; }
void processLvgl() {}
int consumeEncoderDelta() { int value = encoder_delta; encoder_delta = 0; return value; }
void setBacklight(uint8_t value) { backlight = value; }
void setDisplayFlipped(bool value) { display_flip = value; }
bool readBatteryPercent(uint8_t& value) { value = battery_value; return battery_available; }
void vibrate() { ++haptics; }
}

extern "C" uint32_t rr_lvgl_millis() { return test_now_ms; }

extern "C" void setUp() {
  lv_obj_clean(lv_scr_act());
  sent.clear();
  incoming.clear();
  encoder_delta = 0;
  haptics = 0;
  battery_available = false;
  radio_start_succeeds = true;
  radio_setup_required = false;
  preference_values.clear();
  preference_writes = 0;
  preference_begin_succeeds = true;
  last_preference_key.clear();
  preference_write_succeeds = true;
  test_now_ms = 1000;
}
extern "C" void tearDown() { lv_obj_clean(lv_scr_act()); }

namespace {
using rr::controller::ControllerApp;
using rr::controller::ControllerUi;

void loadCatalog(ControllerApp& app, uint32_t revision, uint16_t first_id = 1) {
  TEST_ASSERT_TRUE(app.catalog_.begin(revision, 2, 1));
  rr::CatalogEntrySummary entries[2]{};
  entries[0].id = first_id;
  entries[1].id = first_id + 1;
  for (auto& entry : entries) entry.default_duration_ms = 10000;
  std::strcpy(entries[0].label.data(), "First");
  std::strcpy(entries[1].label.data(), "Second");
  TEST_ASSERT_TRUE(app.catalog_.applyPage(revision, 0, 2, entries));
}

void ready(ControllerApp& app) {
  TEST_ASSERT_TRUE(app.begin());
  lv_obj_update_layout(lv_scr_act());
  loadCatalog(app, 42);
  app.have_rear_state_ = true;
  app.last_heartbeat_ms_ = test_now_ms;
  app.rear_state_.catalog_revision = 42;
  app.rear_state_.brightness_percent = 35;
  app.rear_state_.active = true;
  app.rear_state_.preset_id = 1;
  app.rear_state_.remaining_ms = 10000;
  app.completeCatalogSync();
  app.loop();
  sent.clear();
}

void confirm(ControllerUi& ui) {
  // A real touch arrives after LVGL has laid out the opened modal.
  lv_obj_update_layout(lv_scr_act());
  lv_event_t event{};
  event.user_data = &ui;
  ControllerUi::confirmEvent(&event);
}

void test_sync_cancels_duration_and_stale_save() {
  ControllerApp app;
  ready(app);
  app.ui_.openDurationPicker(0);
  TEST_ASSERT_TRUE(app.ui_.duration_picker_open_);
  app.startCatalogSync(test_now_ms);
  TEST_ASSERT_FALSE(app.ui_.duration_picker_open_);
  loadCatalog(app, 43, 10);
  confirm(app.ui_);
  TEST_ASSERT_TRUE(last_preference_key.empty());
}

void test_duration_rechecks_revision_and_partial_catalog() {
  ControllerApp app;
  ready(app);
  app.ui_.openDurationPicker(0);
  // Defensive check even if the underlying catalog changes without a UI event.
  loadCatalog(app, 43, 10);
  confirm(app.ui_);
  TEST_ASSERT_TRUE(last_preference_key.empty());
  TEST_ASSERT_FALSE(app.ui_.duration_picker_open_);
  app.ui_.setCatalog(&app.catalog_);
  app.ui_.openDurationPicker(0);
  TEST_ASSERT_TRUE(app.catalog_.begin(43, 2, 1));
  confirm(app.ui_);
  TEST_ASSERT_TRUE(last_preference_key.empty());
}

void test_duration_saves_selected_id_and_reports_write_failure() {
  ControllerApp app;
  ready(app);
  app.ui_.openDurationPicker(1);
  app.ui_.duration_choice_ = 3;
  preference_write_succeeds = false;
  confirm(app.ui_);
  TEST_ASSERT_TRUE(app.ui_.duration_picker_open_);
  TEST_ASSERT_EQUAL_STRING(rr::presentation::text::kSaveFailed,
                          lv_label_get_text(app.ui_.duration_message_));
  preference_write_succeeds = true;
  confirm(app.ui_);
  TEST_ASSERT_EQUAL_STRING("dur-0002", last_preference_key.c_str());
  TEST_ASSERT_EQUAL_UINT8(3, last_preference_value);
  TEST_ASSERT_FALSE(app.ui_.duration_picker_open_);
}

void test_sync_preserves_and_sends_queued_settings() {
  ControllerApp app;
  ready(app);
  app.startCatalogSync(test_now_ms);
  ControllerApp::brightnessRequested(&app, true, 70);
  ControllerApp::flipRequested(&app, true, true);
  app.completeCatalogSync();
  app.serviceBrightness(test_now_ms);
  TEST_ASSERT_EQUAL_UINT8(70, sent.back().brightness_percent);
  TEST_ASSERT_TRUE(app.desired_rear_flipped_);
  rr::Packet ack;
  ack.type = rr::PacketType::Ack;
  ack.sequence = app.pending_.packet().sequence;
  ack.acknowledged_type = rr::PacketType::SetBrightness;
  ack.ack_result = rr::AckResult::Applied;
  ack.state = app.rear_state_;
  ack.state.brightness_percent = 70;
  app.handleAck(ack, test_now_ms);
  app.serviceBrightness(test_now_ms);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(rr::PacketType::SetFlip),
                        static_cast<int>(sent.back().type));
  TEST_ASSERT_TRUE(sent.back().flipped);
}

void test_pending_display_and_settings_disable_actions_until_ack() {
  ControllerApp app;
  ready(app);
  for (bool settings : {false, true}) {
    if (settings) {
      ControllerApp::brightnessRequested(&app, true, 70);
      app.serviceBrightness(test_now_ms);
    } else {
      app.sendDisplay(1, 0);
    }
    TEST_ASSERT_TRUE(lv_obj_has_state(app.ui_.slots_[0], LV_STATE_DISABLED));
    TEST_ASSERT_TRUE(lv_obj_has_state(app.ui_.clear_button_, LV_STATE_DISABLED));
    app.loop();
    TEST_ASSERT_TRUE(lv_obj_has_state(app.ui_.slots_[0], LV_STATE_DISABLED));
    rr::Packet ack;
    ack.type = rr::PacketType::Ack;
    ack.sequence = app.pending_.packet().sequence;
    ack.acknowledged_type = app.pending_.packet().type;
    ack.ack_result = rr::AckResult::Applied;
    ack.state = app.rear_state_;
    ack.state.brightness_percent = settings ? 70 : 35;
    app.handleAck(ack, test_now_ms);
    app.loop();
    TEST_ASSERT_FALSE(lv_obj_has_state(app.ui_.slots_[0], LV_STATE_DISABLED));
    TEST_ASSERT_FALSE(lv_obj_has_state(app.ui_.clear_button_, LV_STATE_DISABLED));
  }
}

void test_configured_catalog_validates_and_syncs() {
  TEST_ASSERT_TRUE(rr::rear::validateCatalog());
  rr::CatalogStore store;
  const auto count = static_cast<uint16_t>(rr::rear::presetCount());
  const uint32_t revision = rr::rear::catalogRevision();
  TEST_ASSERT_TRUE(store.begin(revision, count, rr::pageCountFor(count)));
  for (uint8_t page = 0; page < store.pageCount(); ++page) {
    rr::Packet packet;
    packet.type = rr::PacketType::CatalogPage;
    packet.catalog_revision = revision;
    packet.page_index = page;
    for (uint8_t slot = 0; slot < rr::kMessagesPerPage; ++slot) {
      const size_t index = page * rr::kMessagesPerPage + slot;
      if (index >= count) break;
      packet.entries[packet.entry_count++] = rr::rear::summaryAt(index);
    }
    uint8_t bytes[rr::kMaxEspNowPayload];
    auto decoded = rr::decodePacket(bytes, rr::encodePacket(packet, bytes, sizeof(bytes)));
    TEST_ASSERT_TRUE(static_cast<bool>(decoded));
    TEST_ASSERT_TRUE(store.applyPage(revision, page, decoded.packet.entry_count,
                                    decoded.packet.entries.data()));
  }
  TEST_ASSERT_TRUE(store.complete());
  for (uint16_t index = 0; index < count; ++index) {
    TEST_ASSERT_EQUAL_STRING(rr::rear::presets()[index].label,
                            store.entryAt(index)->label.data());
  }
}

void slot(ControllerUi& ui, uint8_t index, lv_event_code_t code) {
  lv_event_t event{};
  event.user_data = &ui.slot_contexts_[index];
  event.code = code;
  ControllerUi::slotEvent(&event);
}
void uiEvent(ControllerUi& ui, void (*callback)(lv_event_t*),
             lv_obj_t* target = nullptr, lv_event_code_t code = LV_EVENT_SHORT_CLICKED) {
  lv_event_t event{};
  event.user_data = &ui;
  event.target = target;
  event.code = code;
  callback(&event);
}
void test_tap_uses_saved_duration_and_waits_for_ack() {
  ControllerApp app; ready(app);
  preference_values["dur-0002"] = 4;
  slot(app.ui_, 1, LV_EVENT_SHORT_CLICKED);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(rr::PacketType::Display), static_cast<int>(sent.back().type));
  TEST_ASSERT_EQUAL_UINT16(2, sent.back().preset_id);
  TEST_ASSERT_EQUAL_UINT32(20000, sent.back().duration_override_ms);
  TEST_ASSERT_EQUAL_UINT32(42, sent.back().catalog_revision);
  TEST_ASSERT_EQUAL_STRING(rr::presentation::text::kSending, lv_label_get_text(app.ui_.status_label_));
  TEST_ASSERT_EQUAL_UINT16(1, app.ui_.rear_state_.preset_id);
}
void test_long_press_rotate_cancel_save_and_default() {
  ControllerApp app; ready(app);
  slot(app.ui_, 0, LV_EVENT_LONG_PRESSED);
  TEST_ASSERT_TRUE(app.ui_.duration_picker_open_);
  TEST_ASSERT_TRUE(sent.empty());
  for (int choice = 1; choice <= 12; ++choice) {
    app.ui_.rotate(1);
    TEST_ASSERT_EQUAL_UINT8(choice, app.ui_.duration_choice_);
  }
  app.ui_.rotate(1);
  TEST_ASSERT_EQUAL_UINT8(0, app.ui_.duration_choice_);
  app.ui_.rotate(-1);
  uiEvent(app.ui_, ControllerUi::cancelEvent);
  TEST_ASSERT_FALSE(app.ui_.duration_picker_open_);
  TEST_ASSERT_EQUAL_UINT(0, preference_writes);
  slot(app.ui_, 0, LV_EVENT_LONG_PRESSED);
  app.ui_.rotate(2); confirm(app.ui_);
  TEST_ASSERT_EQUAL_UINT8(2, preference_values["dur-0001"]);
  TEST_ASSERT_TRUE(sent.empty());
  slot(app.ui_, 0, LV_EVENT_LONG_PRESSED);
  TEST_ASSERT_EQUAL_UINT8(2, app.ui_.duration_choice_);
  app.ui_.rotate(-2); confirm(app.ui_);
  TEST_ASSERT_EQUAL_UINT8(0, preference_values["dur-0001"]);
  slot(app.ui_, 0, LV_EVENT_SHORT_CLICKED);
  TEST_ASSERT_EQUAL_UINT32(0, sent.back().duration_override_ms);
}
void test_page_navigation_all_four_slots_and_partial_page() {
  ControllerApp app; ready(app);
  TEST_ASSERT_TRUE(app.catalog_.begin(42, 6, 2));
  rr::CatalogEntrySummary entries[4]{};
  for (int i=0;i<4;++i) { entries[i].id=i+1; entries[i].default_duration_ms=10000; std::strcpy(entries[i].label.data(), "Message"); }
  TEST_ASSERT_TRUE(app.catalog_.applyPage(42,0,4,entries));
  for(int i=0;i<2;++i) entries[i].id=i+5;
  TEST_ASSERT_TRUE(app.catalog_.applyPage(42,1,2,entries));
  app.ui_.setCatalog(&app.catalog_);
  for (int i=0;i<4;++i) {
    app.pending_.clear(); app.ui_.showRearState(app.rear_state_,test_now_ms); app.loop();
    slot(app.ui_,i,LV_EVENT_SHORT_CLICKED);
    TEST_ASSERT_EQUAL_UINT16(i+1,sent.back().preset_id);
  }
  app.pending_.clear(); app.ui_.showRearState(app.rear_state_,test_now_ms); app.loop();
  encoder_delta=1; app.loop();
  TEST_ASSERT_EQUAL_UINT8(1,app.ui_.current_page_);
  TEST_ASSERT_TRUE(lv_obj_has_state(app.ui_.slots_[2],LV_STATE_DISABLED));
  app.ui_.rotate(1); TEST_ASSERT_EQUAL_UINT8(0,app.ui_.current_page_);
  app.ui_.rotate(-1); TEST_ASSERT_EQUAL_UINT8(1,app.ui_.current_page_);
}
void test_clear_and_blank_status() {
  ControllerApp app; ready(app);
  uiEvent(app.ui_,ControllerUi::clearEvent);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(rr::PacketType::Clear),static_cast<int>(sent.back().type));
  rr::Packet ack; ack.sequence=app.pending_.packet().sequence; ack.acknowledged_type=rr::PacketType::Clear;
  ack.ack_result=rr::AckResult::Applied; ack.state=app.rear_state_; ack.state.active=false;
  app.handleAck(ack,test_now_ms); app.loop();
  TEST_ASSERT_EQUAL_STRING("",lv_label_get_text(app.ui_.status_label_));
  TEST_ASSERT_TRUE(lv_obj_has_flag(app.ui_.clear_button_,LV_OBJ_FLAG_HIDDEN));
}
void test_ack_countdown_heartbeat_correction_and_haptics() {
  ControllerApp app; ready(app);
  app.sendDisplay(2,0);
  rr::Packet ack; ack.sequence=app.pending_.packet().sequence; ack.acknowledged_type=rr::PacketType::Display;
  ack.ack_result=rr::AckResult::Applied; ack.state=app.rear_state_; ack.state.preset_id=2; ack.state.remaining_ms=5000;
  unsigned before=haptics; app.handleAck(ack,test_now_ms);
  TEST_ASSERT_EQUAL_UINT(before+1,haptics);
  TEST_ASSERT_EQUAL_UINT32(5,app.ui_.displayed_remaining_seconds_);
  app.ui_.tick(test_now_ms+1000);
  TEST_ASSERT_EQUAL_UINT32(4,app.ui_.displayed_remaining_seconds_);
  TEST_ASSERT_EQUAL_UINT(before+1,haptics);
  ack.state.remaining_ms=2000; app.handleState(ack.state,test_now_ms+1000,true);
  TEST_ASSERT_EQUAL_UINT32(2,app.ui_.displayed_remaining_seconds_);
  TEST_ASSERT_EQUAL_UINT(before+1,haptics);
  ack.state.active=false; app.handleState(ack.state,test_now_ms+2000,true);
  TEST_ASSERT_EQUAL_UINT(before+2,haptics);
}
void test_timeout_retries_same_command_then_recovers() {
  ControllerApp app; ready(app); app.sendDisplay(1,0);
  auto first=sent.back();
  for(int i=1;i<=3;++i) { test_now_ms+=300; app.loop(); TEST_ASSERT_EQUAL_UINT32(first.sequence,sent.back().sequence); }
  TEST_ASSERT_EQUAL_UINT(4,sent.size());
  test_now_ms+=300; app.loop();
  TEST_ASSERT_FALSE(app.pending_.active());
  TEST_ASSERT_EQUAL_STRING(rr::presentation::text::kCommandFailed,lv_label_get_text(app.ui_.status_label_));
  app.handleState(app.rear_state_,test_now_ms,true); app.loop();
  TEST_ASSERT_FALSE(lv_obj_has_state(app.ui_.slots_[0],LV_STATE_DISABLED));
}
void test_disconnect_disables_rear_settings_and_recovers() {
  ControllerApp app; ready(app);
  test_now_ms+=3501; app.loop();
  TEST_ASSERT_EQUAL_STRING(rr::presentation::text::kRearUnavailable,lv_label_get_text(app.ui_.status_label_));
  TEST_ASSERT_TRUE(lv_obj_has_state(app.ui_.slots_[0],LV_STATE_DISABLED));
  TEST_ASSERT_TRUE(lv_obj_has_state(app.ui_.brightness_sliders_[1],LV_STATE_DISABLED));
  TEST_ASSERT_TRUE(lv_obj_has_state(app.ui_.flip_switches_[1],LV_STATE_DISABLED));
  unsigned before=haptics; app.loop(); TEST_ASSERT_EQUAL_UINT(before,haptics);
  auto state=app.rear_state_; app.handleState(state,test_now_ms,true); app.loop();
  TEST_ASSERT_FALSE(lv_obj_has_state(app.ui_.slots_[0],LV_STATE_DISABLED));
  TEST_ASSERT_FALSE(lv_obj_has_state(app.ui_.brightness_sliders_[1],LV_STATE_DISABLED));
}
void test_settings_open_focus_drag_flip_and_back() {
  ControllerApp app; ready(app);
  uiEvent(app.ui_,ControllerUi::settingsEvent);
  TEST_ASSERT_TRUE(app.ui_.settings_open_);
  lv_obj_update_layout(lv_scr_act());
  TEST_ASSERT_EQUAL_INT(250,lv_obj_get_width(app.ui_.settings_card_));
  TEST_ASSERT_EQUAL_INT(266,lv_obj_get_height(app.ui_.duration_card_));
  auto* slider=app.ui_.brightness_sliders_[1];
  uiEvent(app.ui_,ControllerUi::brightnessSliderEvent,slider,LV_EVENT_PRESSED);
  TEST_ASSERT_EQUAL_UINT8(1,app.ui_.settings_focus_);
  lv_slider_set_value(slider,63,LV_ANIM_OFF);
  uiEvent(app.ui_,ControllerUi::brightnessSliderEvent,slider,LV_EVENT_VALUE_CHANGED);
  TEST_ASSERT_EQUAL_UINT8(65,app.desired_rear_brightness_percent_);
  TEST_ASSERT_EQUAL_UINT8(80,app.controller_brightness_percent_);
  auto* toggle=app.ui_.flip_switches_[0]; lv_obj_add_state(toggle,LV_STATE_CHECKED);
  uiEvent(app.ui_,ControllerUi::flipSwitchEvent,toggle,LV_EVENT_VALUE_CHANGED);
  TEST_ASSERT_TRUE(display_flip); TEST_ASSERT_FALSE(app.desired_rear_flipped_);
  toggle=app.ui_.flip_switches_[1]; lv_obj_add_state(toggle,LV_STATE_CHECKED);
  uiEvent(app.ui_,ControllerUi::flipSwitchEvent,toggle,LV_EVENT_VALUE_CHANGED);
  TEST_ASSERT_TRUE(app.desired_rear_flipped_);
  uiEvent(app.ui_,ControllerUi::settingsBackEvent); TEST_ASSERT_FALSE(app.ui_.settings_open_);
}
void test_knob_brightness_consumes_every_detent() {
  ControllerApp app; ready(app); app.ui_.openSettings();
  app.ui_.rotate(3);
  TEST_ASSERT_EQUAL_UINT8(95,app.controller_brightness_percent_);
  app.ui_.rotate(100); TEST_ASSERT_EQUAL_UINT8(100,app.controller_brightness_percent_);
  app.ui_.rotate(-100); TEST_ASSERT_EQUAL_UINT8(5,app.controller_brightness_percent_);
}
void test_controller_settings_debounce_retry_and_reboot() {
  ControllerApp app; ready(app);
  ControllerApp::brightnessRequested(&app,false,60); ControllerApp::flipRequested(&app,false,true);
  app.persistControllerBrightnessIfDue(test_now_ms+749); TEST_ASSERT_EQUAL_UINT(0,preference_writes);
  preference_write_succeeds=false;
  app.persistControllerBrightnessIfDue(test_now_ms+750); app.persistControllerFlipIfDue(test_now_ms+750);
  TEST_ASSERT_TRUE(app.controller_brightness_save_pending_); TEST_ASSERT_TRUE(app.controller_flip_save_pending_);
  preference_write_succeeds=true;
  app.persistControllerBrightnessIfDue(test_now_ms+5750); app.persistControllerFlipIfDue(test_now_ms+5750);
  TEST_ASSERT_EQUAL_UINT8(60,preference_values["lcd-pct"]); TEST_ASSERT_EQUAL_UINT8(1,preference_values["lcd-flip"]);
  lv_obj_clean(lv_scr_act()); ControllerApp reboot; TEST_ASSERT_TRUE(reboot.begin());
  TEST_ASSERT_EQUAL_UINT8(60,backlight); TEST_ASSERT_TRUE(display_flip);
}
void test_battery_filter_failure_threshold_and_recovery() {
  ControllerApp app; ready(app); battery_available=true; battery_value=80;
  app.serviceBattery(11000); TEST_ASSERT_EQUAL_STRING("80%",lv_label_get_text(app.ui_.battery_label_));
  battery_value=40; app.serviceBattery(21000); TEST_ASSERT_EQUAL_STRING("70%",lv_label_get_text(app.ui_.battery_label_));
  battery_available=false; app.serviceBattery(31000); app.serviceBattery(41000);
  TEST_ASSERT_EQUAL_STRING("70%",lv_label_get_text(app.ui_.battery_label_));
  app.serviceBattery(51000); TEST_ASSERT_EQUAL_STRING("--%",lv_label_get_text(app.ui_.battery_label_));
  battery_available=true; app.serviceBattery(61000); TEST_ASSERT_EQUAL_STRING("40%",lv_label_get_text(app.ui_.battery_label_));
}
void test_setup_and_radio_failure_states() {
  radio_setup_required=true; ControllerApp app; TEST_ASSERT_TRUE(app.begin());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ControllerUi::ViewState::SetupRequired),static_cast<int>(app.ui_.view_state_));
  lv_obj_clean(lv_scr_act()); radio_setup_required=false; radio_start_succeeds=false;
  ControllerApp failed; TEST_ASSERT_TRUE(failed.begin());
  // Long-dot labels may abbreviate long copy; the state is authoritative.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ControllerUi::ViewState::RadioFailure),static_cast<int>(failed.ui_.view_state_));
}
void test_full_catalog_reload_through_packets() {
  ControllerApp app; ready(app); rr::Packet status; status.type=rr::PacketType::Status;
  status.state=app.rear_state_; status.state.catalog_revision=99;
  incoming.push_back(status); app.loop(); TEST_ASSERT_TRUE(app.sync_in_progress_);
  rr::Packet manifest; manifest.type=rr::PacketType::CatalogManifest; manifest.sequence=app.pending_.packet().sequence;
  manifest.catalog_revision=99; manifest.total_entries=1; manifest.total_pages=1;
  incoming.push_back(manifest); app.loop();
  rr::Packet page; page.type=rr::PacketType::CatalogPage; page.sequence=app.pending_.packet().sequence;
  page.catalog_revision=99; page.entry_count=1; page.entries[0].id=20; page.entries[0].default_duration_ms=10000;
  std::strcpy(page.entries[0].label.data(),"New"); incoming.push_back(page); app.loop();
  TEST_ASSERT_FALSE(app.sync_in_progress_); TEST_ASSERT_EQUAL_STRING("New",lv_label_get_text(app.ui_.slot_labels_[0]));
}
void test_noop_settings_do_not_send_or_persist() {
  ControllerApp app; ready(app);
  ControllerApp::brightnessRequested(&app,false,80); ControllerApp::flipRequested(&app,false,false);
  ControllerApp::brightnessRequested(&app,true,35); ControllerApp::flipRequested(&app,true,false);
  app.serviceBrightness(test_now_ms); app.persistControllerBrightnessIfDue(test_now_ms+750); app.persistControllerFlipIfDue(test_now_ms+750);
  TEST_ASSERT_TRUE(sent.empty()); TEST_ASSERT_EQUAL_UINT(0,preference_writes);
}
void test_identical_settings_do_not_invalidate_lcd() {
  ControllerApp app; ready(app);
  lv_obj_update_layout(lv_scr_act());
  auto* display=lv_disp_get_default();
  // Remove startup/page animations from this steady-state measurement.
  lv_anim_del_all(); lv_refr_now(display); display->inv_p=0;
  app.refreshDisplaySettings(true);
  TEST_ASSERT_EQUAL_UINT(0,display->inv_p);
}

}

void test_lvgl_clock_tracks_elapsed_time_without_periodic_tick() {
  test_now_ms=12340;
  TEST_ASSERT_EQUAL_UINT32(test_now_ms,lv_tick_get());
  test_now_ms+=37;
  TEST_ASSERT_EQUAL_UINT32(test_now_ms,lv_tick_get());
}


void test_popup_and_knob_feedback_and_instant_modals() {
  ControllerApp app;ready(app);unsigned before=haptics;
  slot(app.ui_,0,LV_EVENT_LONG_PRESSED);
  TEST_ASSERT_EQUAL_UINT(before+1,haptics);
  TEST_ASSERT_NULL(lv_anim_get(app.ui_.duration_card_,nullptr));
  uiEvent(app.ui_,ControllerUi::cancelEvent);
  before=haptics;uiEvent(app.ui_,ControllerUi::settingsEvent);
  TEST_ASSERT_EQUAL_UINT(before+1,haptics);
  TEST_ASSERT_NULL(lv_anim_get(app.ui_.settings_card_,nullptr));
  app.ui_.closeSettings();before=haptics;encoder_delta=1;app.loop();
  TEST_ASSERT_EQUAL_UINT(before+1,haptics);
}
void test_duration_preference_survives_controller_reboot() {
  ControllerApp app;ready(app);slot(app.ui_,1,LV_EVENT_LONG_PRESSED);app.ui_.rotate(5);confirm(app.ui_);
  lv_obj_clean(lv_scr_act());ControllerApp reboot;ready(reboot);
  slot(reboot.ui_,1,LV_EVENT_SHORT_CLICKED);
  TEST_ASSERT_EQUAL_UINT32(25000,sent.back().duration_override_ms);
}
void test_rear_setting_retry_retains_latest_choice() {
  ControllerApp app;ready(app);ControllerApp::brightnessRequested(&app,true,60);app.serviceBrightness(test_now_ms);
  ControllerApp::brightnessRequested(&app,true,75);
  for(int i=0;i<4;++i) { test_now_ms+=300;app.servicePending(test_now_ms); }
  TEST_ASSERT_TRUE(app.rear_brightness_dirty_);app.serviceBrightness(test_now_ms);
  TEST_ASSERT_EQUAL_UINT8(75,sent.back().brightness_percent);
}

int main(int, char**) {
  lv_init();
  lv_disp_draw_buf_init(&draw_buffer, pixels, nullptr, 360);
  lv_disp_drv_init(&display_driver);
  display_driver.hor_res = 360;
  display_driver.ver_res = 360;
  display_driver.draw_buf = &draw_buffer;
  display_driver.flush_cb = [](lv_disp_drv_t* driver, const lv_area_t*, lv_color_t*) {
    lv_disp_flush_ready(driver);
  };
  lv_disp_drv_register(&display_driver);
  UNITY_BEGIN();
  RUN_TEST(test_sync_cancels_duration_and_stale_save);
  RUN_TEST(test_duration_rechecks_revision_and_partial_catalog);
  RUN_TEST(test_duration_saves_selected_id_and_reports_write_failure);
  RUN_TEST(test_sync_preserves_and_sends_queued_settings);
  RUN_TEST(test_pending_display_and_settings_disable_actions_until_ack);
  RUN_TEST(test_configured_catalog_validates_and_syncs);
  RUN_TEST(test_tap_uses_saved_duration_and_waits_for_ack);
  RUN_TEST(test_long_press_rotate_cancel_save_and_default);
  RUN_TEST(test_page_navigation_all_four_slots_and_partial_page);
  RUN_TEST(test_clear_and_blank_status);
  RUN_TEST(test_ack_countdown_heartbeat_correction_and_haptics);
  RUN_TEST(test_timeout_retries_same_command_then_recovers);
  RUN_TEST(test_disconnect_disables_rear_settings_and_recovers);
  RUN_TEST(test_settings_open_focus_drag_flip_and_back);
  RUN_TEST(test_knob_brightness_consumes_every_detent);
  RUN_TEST(test_controller_settings_debounce_retry_and_reboot);
  RUN_TEST(test_battery_filter_failure_threshold_and_recovery);
  RUN_TEST(test_setup_and_radio_failure_states);
  RUN_TEST(test_full_catalog_reload_through_packets);
  RUN_TEST(test_noop_settings_do_not_send_or_persist);
  RUN_TEST(test_identical_settings_do_not_invalidate_lcd);
  RUN_TEST(test_lvgl_clock_tracks_elapsed_time_without_periodic_tick);
  RUN_TEST(test_popup_and_knob_feedback_and_instant_modals);
  RUN_TEST(test_duration_preference_survives_controller_reboot);
  RUN_TEST(test_rear_setting_retry_retains_latest_choice);
  return UNITY_END();
}
