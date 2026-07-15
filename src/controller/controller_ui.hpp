#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include "rr/catalog_store.hpp"

namespace rr::controller {

class ControllerUi {
 public:
  using DisplayCallback = void (*)(void* context, uint16_t preset_id,
                                   uint32_t duration_override_ms);
  using ClearCallback = void (*)(void* context);
  using BrightnessCallback = void (*)(void* context, bool rear_display,
                                      uint8_t brightness_percent);
  using DurationLookupCallback = uint32_t (*)(void* context,
                                              uint16_t preset_id);
  using DurationSaveCallback = void (*)(void* context, uint16_t preset_id,
                                        uint32_t duration_override_ms);
  using FeedbackCallback = void (*)(void* context);

  void begin(DisplayCallback display_callback, ClearCallback clear_callback,
             BrightnessCallback brightness_callback,
             DurationLookupCallback duration_lookup_callback,
             DurationSaveCallback duration_save_callback,
             FeedbackCallback feedback_callback,
             void* callback_context);
  void setCatalog(const CatalogStore* catalog);
  void setBrightnessValues(uint8_t controller_percent, uint8_t rear_percent,
                           bool rear_available);
  void tick(uint32_t now_ms);
  void rotate(int delta);

  void showSyncing();
  void showSetupRequired();
  void showRearUnavailable();
  void showSending();
  void showCommandFailed();
  void showRearState(const RearState& state, uint32_t received_ms);

 private:
  struct SlotContext {
    ControllerUi* ui = nullptr;
    uint8_t index = 0;
  };

  enum class ViewState : uint8_t {
    Syncing,
    SetupRequired,
    Unavailable,
    Sending,
    Failed,
    Blank,
    Active,
  };

  static void slotEvent(lv_event_t* event);
  static void clearEvent(lv_event_t* event);
  static void cancelEvent(lv_event_t* event);
  static void confirmEvent(lv_event_t* event);
  static void settingsEvent(lv_event_t* event);
  static void settingsBackEvent(lv_event_t* event);
  static void brightnessSliderEvent(lv_event_t* event);
  static void pageAnimation(void* object, int32_t x);

  void renderPage(int direction = 0);
  void refreshStatus();
  void refreshAvailability();
  void openDurationPicker(uint8_t slot);
  void closeDurationPicker();
  void refreshDurationPicker();
  const CatalogEntrySummary* entryForSlot(uint8_t slot) const;
  void applyViewState(ViewState state);
  void openSettings();
  void closeSettings();
  void refreshBrightnessLabels();
  void setFocusedBrightness(uint8_t focus);

  DisplayCallback display_callback_ = nullptr;
  ClearCallback clear_callback_ = nullptr;
  BrightnessCallback brightness_callback_ = nullptr;
  DurationLookupCallback duration_lookup_callback_ = nullptr;
  DurationSaveCallback duration_save_callback_ = nullptr;
  FeedbackCallback feedback_callback_ = nullptr;
  void* callback_context_ = nullptr;
  const CatalogStore* catalog_ = nullptr;
  uint8_t current_page_ = 0;
  ViewState view_state_ = ViewState::Syncing;
  RearState rear_state_{};
  bool duration_picker_open_ = false;
  bool settings_open_ = false;
  uint8_t settings_focus_ = 0;
  bool slider_update_guard_ = false;
  uint8_t duration_slot_ = 0;
  uint8_t duration_choice_ = 0;
  uint32_t status_color_ = 0xFFFFFFFF;
  uint32_t state_received_ms_ = 0;
  uint32_t displayed_remaining_seconds_ = 0;

  lv_obj_t* status_label_ = nullptr;
  lv_obj_t* status_panel_ = nullptr;
  lv_obj_t* status_dot_ = nullptr;
  lv_obj_t* grid_ = nullptr;
  std::array<lv_obj_t*, kMessagesPerPage> slots_{};
  std::array<lv_obj_t*, kMessagesPerPage> slot_labels_{};
  std::array<lv_obj_t*, kMessagesPerPage> slot_index_labels_{};
  std::array<SlotContext, kMessagesPerPage> slot_contexts_{};
  lv_obj_t* page_label_ = nullptr;
  lv_obj_t* clear_button_ = nullptr;
  lv_obj_t* duration_overlay_ = nullptr;
  lv_obj_t* duration_card_ = nullptr;
  lv_obj_t* duration_arc_ = nullptr;
  lv_obj_t* duration_message_ = nullptr;
  lv_obj_t* duration_value_ = nullptr;
  lv_obj_t* settings_panel_ = nullptr;
  lv_obj_t* settings_card_ = nullptr;
  std::array<lv_obj_t*, 2> brightness_name_labels_{};
  std::array<lv_obj_t*, 2> brightness_sliders_{};
  std::array<lv_obj_t*, 2> brightness_values_{};
};

}  // namespace rr::controller
