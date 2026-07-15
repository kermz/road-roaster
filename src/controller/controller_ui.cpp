#include "controller_ui.hpp"

#include <algorithm>
#include <cstring>
#include <cstdio>

namespace rr::controller {
namespace {

constexpr uint32_t kBlack = 0x050708;
constexpr uint32_t kSurface = 0x111619;
constexpr uint32_t kSurfacePressed = 0x19231F;
constexpr uint32_t kDivider = 0x283236;
constexpr uint32_t kWhite = 0xF7FAF8;
constexpr uint32_t kMuted = 0x7E8985;
constexpr uint32_t kGreen = 0x59E391;
constexpr uint32_t kAmber = 0xF2AD49;
constexpr uint32_t kRed = 0xFF5C5C;
constexpr int16_t kGridX = 34;
constexpr int16_t kGridY = 91;
constexpr int16_t kGridWidth = 292;
constexpr int16_t kGridHeight = 184;
constexpr int16_t kSlotGap = 8;
constexpr int16_t kSlotWidth = (kGridWidth - kSlotGap) / 2;
constexpr int16_t kSlotHeight = (kGridHeight - kSlotGap) / 2;
constexpr int16_t kModalWidth = 250;
constexpr int16_t kModalHeight = 266;
constexpr int16_t kModalX = (360 - kModalWidth) / 2;
constexpr int16_t kModalRestY = (360 - kModalHeight) / 2;
constexpr lv_style_selector_t kPressedSelector =
    static_cast<lv_style_selector_t>(LV_PART_MAIN) |
    static_cast<lv_style_selector_t>(LV_STATE_PRESSED);
constexpr lv_style_selector_t kDisabledSelector =
    static_cast<lv_style_selector_t>(LV_PART_MAIN) |
    static_cast<lv_style_selector_t>(LV_STATE_DISABLED);
constexpr lv_style_selector_t kIndicatorDisabledSelector =
    static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
    static_cast<lv_style_selector_t>(LV_STATE_DISABLED);
constexpr lv_style_selector_t kKnobDisabledSelector =
    static_cast<lv_style_selector_t>(LV_PART_KNOB) |
    static_cast<lv_style_selector_t>(LV_STATE_DISABLED);

void makeLabelPlain(lv_obj_t* label, const lv_font_t* font,
                    uint32_t color) {
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
}

void configureAction(lv_obj_t* object) {
  lv_obj_remove_style_all(object);
  lv_obj_set_style_bg_color(object, lv_color_hex(kSurface), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(object, lv_color_hex(kSurfacePressed),
                            kPressedSelector);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, kPressedSelector);
  lv_obj_set_style_border_color(object, lv_color_hex(kDivider), LV_PART_MAIN);
  lv_obj_set_style_border_color(object, lv_color_hex(kGreen),
                                kPressedSelector);
  lv_obj_set_style_border_width(object, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(object, 18, LV_PART_MAIN);
  lv_obj_set_style_text_color(object, lv_color_hex(kWhite), LV_PART_MAIN);
  lv_obj_set_style_opa(object, LV_OPA_30, kDisabledSelector);
  lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
}

struct ModalLayers {
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
};

// All controller popups should be created through this helper so the round
// safe area, scrim, surface, border, radius, and elevation stay consistent.
ModalLayers createModal(lv_obj_t* parent) {
  ModalLayers modal;
  modal.overlay = lv_obj_create(parent);
  lv_obj_set_size(modal.overlay, 360, 360);
  lv_obj_set_align(modal.overlay, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(modal.overlay, 0, 0);
  lv_obj_set_style_bg_opa(modal.overlay, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(modal.overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(modal.overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(modal.overlay, 0, LV_PART_MAIN);
  lv_obj_clear_flag(modal.overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(modal.overlay, LV_OBJ_FLAG_CLICKABLE);

  modal.card = lv_obj_create(modal.overlay);
  lv_obj_set_size(modal.card, kModalWidth, kModalHeight);
  lv_obj_set_align(modal.card, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(modal.card, kModalX, kModalRestY);
  lv_obj_set_style_bg_color(modal.card, lv_color_hex(0x0D1315), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(modal.card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(modal.card, lv_color_hex(kDivider),
                                LV_PART_MAIN);
  lv_obj_set_style_border_width(modal.card, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(modal.card, 30, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(modal.card, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(modal.card, 0, LV_PART_MAIN);
  lv_obj_clear_flag(modal.card, LV_OBJ_FLAG_SCROLLABLE);
  return modal;
}

lv_obj_t* createModalTitle(lv_obj_t* card, const char* text) {
  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, text);
  makeLabelPlain(title, &lv_font_montserrat_14, kMuted);
  lv_obj_set_style_text_letter_space(title, 2, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);
  return title;
}

void configureModalButton(lv_obj_t* button, bool primary) {
  lv_obj_set_style_bg_color(
      button, lv_color_hex(primary ? 0x17603A : kSurface), LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(primary ? 0x20794A : kSurfacePressed),
      kPressedSelector);
  lv_obj_set_style_border_color(
      button, lv_color_hex(primary ? kGreen : kDivider), LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(button, 19, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
}

void showModal(lv_obj_t* overlay, lv_obj_t* card) {
  // Modal transitions are intentionally instant: even card-only animation
  // creates visible redraw latency on this SPI-connected round panel.
  lv_anim_del(card, nullptr);
  lv_obj_set_align(card, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(card, kModalX, kModalRestY);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(overlay);
}

}  // namespace

void ControllerUi::begin(DisplayCallback display_callback,
                         ClearCallback clear_callback,
                         BrightnessCallback brightness_callback,
                         DurationLookupCallback duration_lookup_callback,
                         DurationSaveCallback duration_save_callback,
                         FeedbackCallback feedback_callback,
                         void* callback_context) {
  display_callback_ = display_callback;
  clear_callback_ = clear_callback;
  brightness_callback_ = brightness_callback;
  duration_lookup_callback_ = duration_lookup_callback;
  duration_save_callback_ = duration_save_callback;
  feedback_callback_ = feedback_callback;
  callback_context_ = callback_context;

  lv_obj_t* screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(kBlack), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* brand = lv_label_create(screen);
  lv_label_set_text(brand, "ROAD ROASTER");
  makeLabelPlain(brand, &lv_font_montserrat_14, kWhite);
  lv_obj_set_style_text_letter_space(brand, 3, LV_PART_MAIN);
  lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 18);
  lv_obj_add_flag(brand, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(brand, 12);
  lv_obj_add_event_cb(brand, settingsEvent, LV_EVENT_SHORT_CLICKED, this);

  lv_obj_t* brand_mark = lv_obj_create(screen);
  lv_obj_remove_style_all(brand_mark);
  lv_obj_set_size(brand_mark, 26, 2);
  lv_obj_set_style_bg_color(brand_mark, lv_color_hex(kGreen), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(brand_mark, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(brand_mark, 1, LV_PART_MAIN);
  lv_obj_align(brand_mark, LV_ALIGN_TOP_MID, 0, 39);

  status_panel_ = lv_obj_create(screen);
  lv_obj_remove_style_all(status_panel_);
  lv_obj_set_size(status_panel_, 238, 32);
  lv_obj_set_style_bg_color(status_panel_, lv_color_hex(kSurface),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(status_panel_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(status_panel_, lv_color_hex(kDivider),
                                LV_PART_MAIN);
  lv_obj_set_style_border_width(status_panel_, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(status_panel_, 16, LV_PART_MAIN);
  lv_obj_align(status_panel_, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_clear_flag(status_panel_, LV_OBJ_FLAG_SCROLLABLE);

  status_dot_ = lv_obj_create(status_panel_);
  lv_obj_remove_style_all(status_dot_);
  lv_obj_set_size(status_dot_, 8, 8);
  lv_obj_set_style_bg_color(status_dot_, lv_color_hex(kAmber), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(status_dot_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(status_dot_, 4, LV_PART_MAIN);
  lv_obj_align(status_dot_, LV_ALIGN_LEFT_MID, 13, 0);

  status_label_ = lv_label_create(status_panel_);
  makeLabelPlain(status_label_, &lv_font_montserrat_14, kWhite);
  lv_obj_set_size(status_label_, 198, 20);
  lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN);
  lv_obj_align(status_label_, LV_ALIGN_CENTER, 8, 0);

  grid_ = lv_obj_create(screen);
  lv_obj_remove_style_all(grid_);
  lv_obj_set_size(grid_, kGridWidth, kGridHeight);
  lv_obj_set_pos(grid_, kGridX, kGridY);
  lv_obj_clear_flag(grid_, LV_OBJ_FLAG_SCROLLABLE);

  for (uint8_t index = 0; index < kMessagesPerPage; ++index) {
    const int column = index % 2;
    const int row = index / 2;
    slots_[index] = lv_obj_create(grid_);
    configureAction(slots_[index]);
    lv_obj_set_size(slots_[index], kSlotWidth, kSlotHeight);
    lv_obj_set_pos(slots_[index], column * (kSlotWidth + kSlotGap),
                   row * (kSlotHeight + kSlotGap));
    lv_obj_set_style_pad_all(slots_[index], 10, LV_PART_MAIN);
    slot_contexts_[index] = {this, index};
    lv_obj_add_event_cb(slots_[index], slotEvent, LV_EVENT_SHORT_CLICKED,
                        &slot_contexts_[index]);
    lv_obj_add_event_cb(slots_[index], slotEvent, LV_EVENT_LONG_PRESSED,
                        &slot_contexts_[index]);

    slot_labels_[index] = lv_label_create(slots_[index]);
    makeLabelPlain(slot_labels_[index], &lv_font_montserrat_22, kWhite);
    lv_label_set_long_mode(slot_labels_[index], LV_LABEL_LONG_WRAP);
    lv_obj_set_width(slot_labels_[index], 116);
    lv_obj_set_style_text_align(slot_labels_[index], LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_align(slot_labels_[index], LV_ALIGN_CENTER, 0, 7);

    slot_index_labels_[index] = lv_label_create(slots_[index]);
    makeLabelPlain(slot_index_labels_[index], &lv_font_montserrat_14, kMuted);
    lv_obj_align(slot_index_labels_[index], LV_ALIGN_TOP_LEFT, 3, 1);
  }

  page_label_ = lv_label_create(screen);
  makeLabelPlain(page_label_, &lv_font_montserrat_18, kMuted);
  lv_obj_set_style_text_letter_space(page_label_, 1, LV_PART_MAIN);
  lv_obj_align(page_label_, LV_ALIGN_BOTTOM_MID, -58, -45);

  clear_button_ = lv_btn_create(screen);
  lv_obj_set_size(clear_button_, 82, 36);
  lv_obj_align(clear_button_, LV_ALIGN_BOTTOM_MID, 64, -39);
  lv_obj_set_style_bg_color(clear_button_, lv_color_hex(kBlack), LV_PART_MAIN);
  lv_obj_set_style_bg_color(clear_button_, lv_color_hex(0x361719),
                            kPressedSelector);
  lv_obj_set_style_border_color(clear_button_, lv_color_hex(kRed),
                                LV_PART_MAIN);
  lv_obj_set_style_border_width(clear_button_, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(clear_button_, 18, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(clear_button_, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(clear_button_, clearEvent, LV_EVENT_SHORT_CLICKED, this);
  lv_obj_t* clear_label = lv_label_create(clear_button_);
  lv_label_set_text(clear_label, "Clear");
  makeLabelPlain(clear_label, &lv_font_montserrat_14, kRed);
  lv_obj_center(clear_label);
  lv_obj_add_flag(clear_button_, LV_OBJ_FLAG_HIDDEN);

  const ModalLayers duration_modal = createModal(screen);
  duration_overlay_ = duration_modal.overlay;
  duration_card_ = duration_modal.card;
  lv_obj_t* duration_card = duration_card_;
  createModalTitle(duration_card, "DURATION");

  duration_message_ = lv_label_create(duration_card);
  makeLabelPlain(duration_message_, &lv_font_montserrat_18, kWhite);
  lv_obj_set_width(duration_message_, 202);
  lv_label_set_long_mode(duration_message_, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(duration_message_, LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN);
  lv_obj_align(duration_message_, LV_ALIGN_TOP_MID, 0, 43);

  duration_arc_ = lv_arc_create(duration_card);
  lv_obj_set_size(duration_arc_, 122, 122);
  lv_arc_set_rotation(duration_arc_, 135);
  lv_arc_set_bg_angles(duration_arc_, 0, 270);
  lv_arc_set_range(duration_arc_, 0, 12);
  lv_arc_set_value(duration_arc_, 0);
  lv_obj_set_style_arc_color(duration_arc_, lv_color_hex(kDivider),
                             LV_PART_MAIN);
  lv_obj_set_style_arc_width(duration_arc_, 5, LV_PART_MAIN);
  lv_obj_set_style_arc_color(duration_arc_, lv_color_hex(kGreen),
                             LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(duration_arc_, 5, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(duration_arc_, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_clear_flag(duration_arc_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(duration_arc_, LV_ALIGN_CENTER, 0, 2);

  duration_value_ = lv_label_create(duration_card);
  makeLabelPlain(duration_value_, &lv_font_montserrat_28, kWhite);
  lv_obj_set_width(duration_value_, 112);
  lv_obj_set_style_text_align(duration_value_, LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN);
  lv_obj_align(duration_value_, LV_ALIGN_CENTER, 0, 2);

  lv_obj_t* cancel = lv_btn_create(duration_card);
  lv_obj_set_size(cancel, 84, 38);
  lv_obj_set_pos(cancel, 31, 212);
  configureModalButton(cancel, false);
  lv_obj_add_event_cb(cancel, cancelEvent, LV_EVENT_SHORT_CLICKED, this);
  lv_obj_t* cancel_label = lv_label_create(cancel);
  lv_label_set_text(cancel_label, "Cancel");
  makeLabelPlain(cancel_label, &lv_font_montserrat_14, kWhite);
  lv_obj_center(cancel_label);

  lv_obj_t* confirm = lv_btn_create(duration_card);
  lv_obj_set_size(confirm, 84, 38);
  lv_obj_set_pos(confirm, 135, 212);
  configureModalButton(confirm, true);
  lv_obj_add_event_cb(confirm, confirmEvent, LV_EVENT_SHORT_CLICKED, this);
  lv_obj_t* confirm_label = lv_label_create(confirm);
  lv_label_set_text(confirm_label, "Save");
  makeLabelPlain(confirm_label, &lv_font_montserrat_14, kWhite);
  lv_obj_center(confirm_label);
  lv_obj_add_flag(duration_card_, LV_OBJ_FLAG_HIDDEN);

  const ModalLayers settings_modal = createModal(screen);
  settings_panel_ = settings_modal.overlay;
  settings_card_ = settings_modal.card;
  lv_obj_t* settings_card = settings_card_;
  createModalTitle(settings_card, "BRIGHTNESS");

  const char* brightness_names[] = {"CONTROLLER", "REAR DISPLAY"};
  const int16_t label_y[] = {58, 139};
  for (uint8_t index = 0; index < 2; ++index) {
    brightness_name_labels_[index] = lv_label_create(settings_card);
    lv_label_set_text(brightness_name_labels_[index], brightness_names[index]);
    makeLabelPlain(brightness_name_labels_[index], &lv_font_montserrat_14,
                   kWhite);
    lv_obj_set_pos(brightness_name_labels_[index], 26, label_y[index]);

    brightness_values_[index] = lv_label_create(settings_card);
    makeLabelPlain(brightness_values_[index], &lv_font_montserrat_14, kGreen);
    lv_obj_set_width(brightness_values_[index], 54);
    lv_obj_set_style_text_align(brightness_values_[index], LV_TEXT_ALIGN_RIGHT,
                                LV_PART_MAIN);
    lv_obj_set_pos(brightness_values_[index], 170, label_y[index]);

    brightness_sliders_[index] = lv_slider_create(settings_card);
    lv_obj_set_size(brightness_sliders_[index], 198, 18);
    lv_obj_set_pos(brightness_sliders_[index], 26, label_y[index] + 27);
    lv_slider_set_range(brightness_sliders_[index], kMinBrightnessPercent,
                        kMaxBrightnessPercent);
    lv_obj_set_style_bg_color(brightness_sliders_[index],
                              lv_color_hex(kDivider), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(brightness_sliders_[index], LV_OPA_COVER,
                            LV_PART_MAIN);
    lv_obj_set_style_radius(brightness_sliders_[index], 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_sliders_[index],
                              lv_color_hex(kGreen), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_sliders_[index],
                              lv_color_hex(kWhite), LV_PART_KNOB);
    lv_obj_set_style_bg_color(brightness_sliders_[index],
                              lv_color_hex(kMuted), kDisabledSelector);
    lv_obj_set_style_bg_color(brightness_sliders_[index],
                              lv_color_hex(kMuted),
                              kIndicatorDisabledSelector);
    lv_obj_set_style_bg_color(brightness_sliders_[index],
                              lv_color_hex(kMuted), kKnobDisabledSelector);
    lv_obj_set_style_opa(brightness_sliders_[index], LV_OPA_50,
                         kDisabledSelector);
    lv_obj_set_style_opa(brightness_sliders_[index], LV_OPA_50,
                         kIndicatorDisabledSelector);
    lv_obj_set_style_opa(brightness_sliders_[index], LV_OPA_50,
                         kKnobDisabledSelector);
    lv_obj_set_style_pad_all(brightness_sliders_[index], 4, LV_PART_KNOB);
    lv_obj_add_event_cb(brightness_sliders_[index], brightnessSliderEvent,
                        LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(brightness_sliders_[index], brightnessSliderEvent,
                        LV_EVENT_VALUE_CHANGED, this);
  }

  lv_obj_t* settings_back = lv_btn_create(settings_card);
  lv_obj_set_size(settings_back, 84, 38);
  lv_obj_set_pos(settings_back, 83, 212);
  configureModalButton(settings_back, false);
  lv_obj_add_event_cb(settings_back, settingsBackEvent, LV_EVENT_SHORT_CLICKED,
                      this);
  lv_obj_t* back_label = lv_label_create(settings_back);
  lv_label_set_text(back_label, "Back");
  makeLabelPlain(back_label, &lv_font_montserrat_14, kWhite);
  lv_obj_center(back_label);
  lv_obj_add_flag(settings_card_, LV_OBJ_FLAG_HIDDEN);

  setFocusedBrightness(0);
  refreshBrightnessLabels();

  renderPage();
  applyViewState(ViewState::Syncing);
  lv_obj_fade_in(status_panel_, 180, 40);
  lv_obj_fade_in(grid_, 240, 100);
}

void ControllerUi::setCatalog(const CatalogStore* catalog) {
  catalog_ = catalog;
  if (catalog_ == nullptr || catalog_->pageCount() == 0) {
    current_page_ = 0;
  } else if (current_page_ >= catalog_->pageCount()) {
    current_page_ = 0;
  }
  renderPage();
}

void ControllerUi::setBrightnessValues(uint8_t controller_percent,
                                       uint8_t rear_percent,
                                       bool rear_available) {
  slider_update_guard_ = true;
  if (lv_slider_get_value(brightness_sliders_[0]) != controller_percent) {
    lv_slider_set_value(brightness_sliders_[0], controller_percent,
                        LV_ANIM_OFF);
  }
  if (isValidBrightness(rear_percent)) {
    if (lv_slider_get_value(brightness_sliders_[1]) != rear_percent) {
      lv_slider_set_value(brightness_sliders_[1], rear_percent, LV_ANIM_OFF);
    }
  }
  slider_update_guard_ = false;
  const bool rear_is_disabled =
      lv_obj_has_state(brightness_sliders_[1], LV_STATE_DISABLED);
  if (rear_available && rear_is_disabled) {
    lv_obj_clear_state(brightness_sliders_[1], LV_STATE_DISABLED);
  } else if (!rear_available && !rear_is_disabled) {
    lv_obj_add_state(brightness_sliders_[1], LV_STATE_DISABLED);
  }
  const uint32_t rear_color = rear_available ? kWhite : kMuted;
  const uint32_t rear_value_color = rear_available ? kGreen : kMuted;
  lv_obj_set_style_text_color(brightness_name_labels_[1],
                              lv_color_hex(rear_color), LV_PART_MAIN);
  lv_obj_set_style_text_color(brightness_values_[1],
                              lv_color_hex(rear_value_color), LV_PART_MAIN);
  if (!rear_available && settings_focus_ == 1) setFocusedBrightness(0);
  refreshBrightnessLabels();
}

void ControllerUi::tick(uint32_t now_ms) {
  if (view_state_ != ViewState::Active || !rear_state_.active) return;
  const uint32_t local_remaining_ms = remainingDuration(
      now_ms, state_received_ms_, rear_state_.remaining_ms);
  const uint32_t remaining_seconds = (local_remaining_ms + 999) / 1000;
  if (remaining_seconds == displayed_remaining_seconds_) return;
  displayed_remaining_seconds_ = remaining_seconds;
  refreshStatus();
}

void ControllerUi::rotate(int delta) {
  if (delta == 0) return;
  if (settings_open_) {
    lv_obj_t* slider = brightness_sliders_[settings_focus_];
    if (lv_obj_has_state(slider, LV_STATE_DISABLED)) return;
    const int value = std::clamp<int>(
        lv_slider_get_value(slider) +
            (delta > 0 ? kBrightnessStepPercent : -kBrightnessStepPercent),
        kMinBrightnessPercent, kMaxBrightnessPercent);
    slider_update_guard_ = true;
    lv_slider_set_value(slider, value, LV_ANIM_ON);
    slider_update_guard_ = false;
    refreshBrightnessLabels();
    if (brightness_callback_ != nullptr) {
      brightness_callback_(callback_context_, settings_focus_ == 1,
                           static_cast<uint8_t>(value));
    }
    return;
  }
  if (duration_picker_open_) {
    // 0 = Default, 1..12 = 5..60 seconds.
    duration_choice_ = wrappedIndex(duration_choice_, delta, 13);
    refreshDurationPicker();
    return;
  }
  if (catalog_ == nullptr || !catalog_->complete() ||
      catalog_->pageCount() == 0) {
    return;
  }
  const int direction = delta > 0 ? 1 : -1;
  current_page_ = wrappedIndex(current_page_, delta, catalog_->pageCount());
  renderPage(direction);
}

void ControllerUi::showSyncing() { applyViewState(ViewState::Syncing); }
void ControllerUi::showSetupRequired() {
  applyViewState(ViewState::SetupRequired);
}
void ControllerUi::showRearUnavailable() {
  applyViewState(ViewState::Unavailable);
}
void ControllerUi::showSending() { applyViewState(ViewState::Sending); }
void ControllerUi::showCommandFailed() { applyViewState(ViewState::Failed); }

void ControllerUi::showRearState(const RearState& state,
                                 uint32_t received_ms) {
  rear_state_ = state;
  state_received_ms_ = received_ms;
  displayed_remaining_seconds_ = (state.remaining_ms + 999) / 1000;
  applyViewState(state.active ? ViewState::Active : ViewState::Blank);
}

void ControllerUi::applyViewState(ViewState state) {
  view_state_ = state;
  if (state == ViewState::Unavailable || state == ViewState::SetupRequired) {
    closeDurationPicker();
  }
  refreshStatus();
  refreshAvailability();
}

void ControllerUi::refreshStatus() {
  uint32_t color = kMuted;
  bool show_dot = true;
  const char* status_text = "";
  char active_text[72]{};
  switch (view_state_) {
    case ViewState::Syncing:
      status_text = "Syncing messages";
      color = kAmber;
      break;
    case ViewState::SetupRequired:
      status_text = "Radio setup required";
      color = kRed;
      break;
    case ViewState::Unavailable:
      status_text = "Rear unavailable";
      color = kRed;
      break;
    case ViewState::Sending:
      status_text = "Sending";
      color = kAmber;
      break;
    case ViewState::Failed:
      status_text = "Command failed";
      color = kRed;
      break;
    case ViewState::Blank:
      show_dot = false;
      break;
    case ViewState::Active: {
      const CatalogEntrySummary* entry =
          catalog_ == nullptr ? nullptr : catalog_->findById(rear_state_.preset_id);
      const char* label = entry == nullptr ? "Unknown" : entry->label.data();
      std::snprintf(active_text, sizeof(active_text),
                    "%s " LV_SYMBOL_BULLET " %lus",
                    label, static_cast<unsigned long>(
                               displayed_remaining_seconds_));
      status_text = active_text;
      color = kGreen;
      break;
    }
  }
  if (std::strcmp(lv_label_get_text(status_label_), status_text) != 0) {
    lv_label_set_text(status_label_, status_text);
  }
  if (status_color_ != color) {
    status_color_ = color;
    lv_obj_set_style_text_color(status_label_, lv_color_hex(color),
                                LV_PART_MAIN);
    lv_obj_set_style_bg_color(status_dot_, lv_color_hex(color), LV_PART_MAIN);
  }
  if (show_dot) {
    if (lv_obj_has_flag(status_dot_, LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_clear_flag(status_dot_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_align(status_label_, LV_ALIGN_CENTER, 8, 0);
    }
  } else {
    if (!lv_obj_has_flag(status_dot_, LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_add_flag(status_dot_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);
    }
  }

  if (rear_state_.active) {
    if (lv_obj_has_flag(clear_button_, LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_clear_flag(clear_button_, LV_OBJ_FLAG_HIDDEN);
    }
  } else {
    if (!lv_obj_has_flag(clear_button_, LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_add_flag(clear_button_, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void ControllerUi::refreshAvailability() {
  const bool radio_action_available =
      (view_state_ == ViewState::Blank || view_state_ == ViewState::Active ||
       view_state_ == ViewState::Sending || view_state_ == ViewState::Failed) &&
      catalog_ != nullptr && catalog_->complete();
  for (uint8_t index = 0; index < kMessagesPerPage; ++index) {
    const bool should_enable =
        radio_action_available && entryForSlot(index) != nullptr;
    const bool is_disabled =
        lv_obj_has_state(slots_[index], LV_STATE_DISABLED);
    if (should_enable && is_disabled) {
      lv_obj_clear_state(slots_[index], LV_STATE_DISABLED);
    } else if (!should_enable && !is_disabled) {
      lv_obj_add_state(slots_[index], LV_STATE_DISABLED);
    }
  }
  const bool should_enable_clear = radio_action_available && rear_state_.active;
  const bool clear_is_disabled =
      lv_obj_has_state(clear_button_, LV_STATE_DISABLED);
  if (should_enable_clear && clear_is_disabled) {
    lv_obj_clear_state(clear_button_, LV_STATE_DISABLED);
  } else if (!should_enable_clear && !clear_is_disabled) {
    lv_obj_add_state(clear_button_, LV_STATE_DISABLED);
  }
}

const CatalogEntrySummary* ControllerUi::entryForSlot(uint8_t slot) const {
  if (catalog_ == nullptr || slot >= kMessagesPerPage) return nullptr;
  return catalog_->entryAt(current_page_ * kMessagesPerPage + slot);
}

void ControllerUi::renderPage(int direction) {
  for (uint8_t index = 0; index < kMessagesPerPage; ++index) {
    const auto* entry = entryForSlot(index);
    lv_label_set_text(slot_labels_[index],
                      entry == nullptr ? "" : entry->label.data());
    char item_number[4];
    std::snprintf(item_number, sizeof(item_number), "%02u",
                  current_page_ * kMessagesPerPage + index + 1);
    lv_label_set_text(slot_index_labels_[index],
                      entry == nullptr ? "" : item_number);
  }
  char page_text[16];
  if (catalog_ != nullptr && catalog_->complete() && catalog_->pageCount() > 0) {
    std::snprintf(page_text, sizeof(page_text), "%u / %u", current_page_ + 1,
                  catalog_->pageCount());
  } else {
    std::snprintf(page_text, sizeof(page_text), "- / -");
  }
  lv_label_set_text(page_label_, page_text);
  refreshAvailability();

  if (direction != 0) {
    // Cancel an in-flight transition so rapid knob turns cannot make both
    // directions appear to originate from the same intermediate position.
    lv_anim_del(grid_, pageAnimation);
    const int32_t enter_x =
        kGridX + (direction > 0 ? 58 : -58);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, grid_);
    lv_anim_set_exec_cb(&animation, pageAnimation);
    lv_anim_set_values(&animation, enter_x, kGridX);
    lv_anim_set_time(&animation, 180);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
    lv_obj_fade_in(grid_, 140, 0);
  }
}

void ControllerUi::openDurationPicker(uint8_t slot) {
  if (entryForSlot(slot) == nullptr ||
      lv_obj_has_state(slots_[slot], LV_STATE_DISABLED)) {
    return;
  }
  duration_slot_ = slot;
  const auto* entry = entryForSlot(slot);
  const uint32_t saved_duration =
      duration_lookup_callback_ == nullptr
          ? 0
          : duration_lookup_callback_(callback_context_, entry->id);
  duration_choice_ = isValidDurationOverride(saved_duration)
                         ? static_cast<uint8_t>(saved_duration / 5000UL)
                         : 0;
  duration_picker_open_ = true;
  lv_label_set_text(duration_message_, entry->label.data());
  refreshDurationPicker();
  showModal(duration_overlay_, duration_card_);
  if (feedback_callback_ != nullptr) feedback_callback_(callback_context_);
}

void ControllerUi::closeDurationPicker() {
  duration_picker_open_ = false;
  if (duration_card_ != nullptr) {
    lv_obj_add_flag(duration_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(duration_overlay_, LV_OBJ_FLAG_CLICKABLE);
  }
}

void ControllerUi::refreshDurationPicker() {
  lv_arc_set_value(duration_arc_, duration_choice_);
  if (duration_choice_ == 0) {
    lv_obj_set_style_text_font(duration_value_, &lv_font_montserrat_22,
                               LV_PART_MAIN);
    lv_label_set_text(duration_value_, "Default");
  } else {
    lv_obj_set_style_text_font(duration_value_, &lv_font_montserrat_28,
                               LV_PART_MAIN);
    char duration[8];
    std::snprintf(duration, sizeof(duration), "%us", duration_choice_ * 5);
    lv_label_set_text(duration_value_, duration);
  }
}

void ControllerUi::slotEvent(lv_event_t* event) {
  auto* context =
      static_cast<SlotContext*>(lv_event_get_user_data(event));
  if (context == nullptr || context->ui == nullptr) return;
  ControllerUi& ui = *context->ui;
  if (lv_event_get_code(event) == LV_EVENT_LONG_PRESSED) {
    ui.openDurationPicker(context->index);
    return;
  }
  const auto* entry = ui.entryForSlot(context->index);
  if (entry != nullptr && ui.display_callback_ != nullptr &&
      !lv_obj_has_state(ui.slots_[context->index], LV_STATE_DISABLED)) {
    const uint32_t saved_duration =
        ui.duration_lookup_callback_ == nullptr
            ? 0
            : ui.duration_lookup_callback_(ui.callback_context_, entry->id);
    ui.display_callback_(ui.callback_context_, entry->id, saved_duration);
  }
}

void ControllerUi::clearEvent(lv_event_t* event) {
  auto* ui = static_cast<ControllerUi*>(lv_event_get_user_data(event));
  if (ui != nullptr && ui->clear_callback_ != nullptr &&
      !lv_obj_has_state(ui->clear_button_, LV_STATE_DISABLED)) {
    ui->clear_callback_(ui->callback_context_);
  }
}

void ControllerUi::cancelEvent(lv_event_t* event) {
  auto* ui = static_cast<ControllerUi*>(lv_event_get_user_data(event));
  if (ui != nullptr) ui->closeDurationPicker();
}

void ControllerUi::confirmEvent(lv_event_t* event) {
  auto* ui = static_cast<ControllerUi*>(lv_event_get_user_data(event));
  if (ui == nullptr) return;
  const auto* entry = ui->entryForSlot(ui->duration_slot_);
  if (entry != nullptr && ui->duration_save_callback_ != nullptr) {
    const uint32_t duration =
        ui->duration_choice_ == 0 ? 0 : ui->duration_choice_ * 5000UL;
    ui->duration_save_callback_(ui->callback_context_, entry->id, duration);
    ui->closeDurationPicker();
  }
}

void ControllerUi::settingsEvent(lv_event_t* event) {
  auto* ui = static_cast<ControllerUi*>(lv_event_get_user_data(event));
  if (ui != nullptr) ui->openSettings();
}

void ControllerUi::settingsBackEvent(lv_event_t* event) {
  auto* ui = static_cast<ControllerUi*>(lv_event_get_user_data(event));
  if (ui != nullptr) ui->closeSettings();
}

void ControllerUi::brightnessSliderEvent(lv_event_t* event) {
  auto* ui = static_cast<ControllerUi*>(lv_event_get_user_data(event));
  if (ui == nullptr || ui->slider_update_guard_) return;
  lv_obj_t* target = lv_event_get_target(event);
  const uint8_t index = target == ui->brightness_sliders_[1] ? 1 : 0;
  ui->setFocusedBrightness(index);
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;

  const int raw = lv_slider_get_value(target);
  const int snapped = std::clamp<int>(
      ((raw + kBrightnessStepPercent / 2) / kBrightnessStepPercent) *
          kBrightnessStepPercent,
      kMinBrightnessPercent, kMaxBrightnessPercent);
  ui->slider_update_guard_ = true;
  lv_slider_set_value(target, snapped, LV_ANIM_OFF);
  ui->slider_update_guard_ = false;
  ui->refreshBrightnessLabels();
  if (ui->brightness_callback_ != nullptr) {
    ui->brightness_callback_(ui->callback_context_, index == 1,
                             static_cast<uint8_t>(snapped));
  }
}

void ControllerUi::openSettings() {
  closeDurationPicker();
  settings_open_ = true;
  showModal(settings_panel_, settings_card_);
  if (feedback_callback_ != nullptr) feedback_callback_(callback_context_);
}

void ControllerUi::closeSettings() {
  settings_open_ = false;
  lv_obj_add_flag(settings_card_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(settings_panel_, LV_OBJ_FLAG_CLICKABLE);
}

void ControllerUi::refreshBrightnessLabels() {
  for (uint8_t index = 0; index < 2; ++index) {
    if (index == 1 &&
        lv_obj_has_state(brightness_sliders_[index], LV_STATE_DISABLED)) {
      if (std::strcmp(lv_label_get_text(brightness_values_[index]), "--") !=
          0) {
        lv_label_set_text(brightness_values_[index], "--");
      }
      continue;
    }
    char text[8];
    std::snprintf(text, sizeof(text), "%d%%",
                  static_cast<int>(lv_slider_get_value(
                      brightness_sliders_[index])));
    if (std::strcmp(lv_label_get_text(brightness_values_[index]), text) != 0) {
      lv_label_set_text(brightness_values_[index], text);
    }
  }
}

void ControllerUi::setFocusedBrightness(uint8_t focus) {
  settings_focus_ = focus > 1 ? 0 : focus;
  for (uint8_t index = 0; index < 2; ++index) {
    const bool disabled =
        lv_obj_has_state(brightness_sliders_[index], LV_STATE_DISABLED);
    lv_obj_set_style_outline_color(brightness_sliders_[index],
                                   lv_color_hex(kGreen), LV_PART_MAIN);
    lv_obj_set_style_outline_width(brightness_sliders_[index],
                                   index == settings_focus_ && !disabled ? 2
                                                                         : 0,
                                   LV_PART_MAIN);
    lv_obj_set_style_outline_pad(brightness_sliders_[index], 5,
                                 LV_PART_MAIN);
  }
}

void ControllerUi::pageAnimation(void* object, int32_t x) {
  lv_obj_set_x(static_cast<lv_obj_t*>(object), x);
}

}  // namespace rr::controller
