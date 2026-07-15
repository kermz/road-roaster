#pragma once

#include <cstdint>

#include <Preferences.h>

#include "controller_ui.hpp"
#include "rr/catalog_store.hpp"
#include "rr/radio_transport.hpp"

namespace rr::controller {

class ControllerApp {
 public:
  bool begin();
  void loop();

 private:
  static void displayRequested(void* context, uint16_t preset_id,
                               uint32_t duration_override_ms);
  static void clearRequested(void* context);
  static void brightnessRequested(void* context, bool rear_display,
                                  uint8_t brightness_percent);
  static uint32_t durationOverrideRequested(void* context,
                                            uint16_t preset_id);
  static bool durationOverrideSaved(void* context, uint16_t preset_id,
                                    uint32_t duration_override_ms);
  static void uiFeedbackRequested(void* context);

  void handlePacket(const Packet& packet, uint32_t now_ms);
  void handleManifest(const Packet& packet, uint32_t now_ms);
  void handleCatalogPage(const Packet& packet, uint32_t now_ms);
  void handleAck(const Packet& packet, uint32_t now_ms);
  void handleState(const RearState& state, uint32_t now_ms,
                   bool heartbeat);
  void startCatalogSync(uint32_t now_ms);
  void requestNextCatalogPage(uint32_t now_ms);
  void completeCatalogSync();
  void beginRequest(const Packet& packet, uint32_t now_ms);
  void servicePending(uint32_t now_ms);
  bool isCatalogRequestPending() const;
  bool isCommandPending() const;
  bool rearAvailable(uint32_t now_ms) const;
  uint32_t nextSequence();
  void sendDisplay(uint16_t preset_id, uint32_t duration_override_ms);
  void sendClear();
  void serviceBrightness(uint32_t now_ms);
  void sendRearBrightness(uint32_t now_ms);
  void persistControllerBrightnessIfDue(uint32_t now_ms);
  void serviceBattery(uint32_t now_ms);
  bool ensurePreferences();
  uint32_t loadDurationOverride(uint16_t preset_id);
  bool saveDurationOverride(uint16_t preset_id,
                            uint32_t duration_override_ms);

  RadioTransport radio_;
  CatalogStore catalog_;
  ControllerUi ui_;
  RequestCoordinator pending_{};
  RearState rear_state_{};
  bool have_rear_state_ = false;
  bool sync_in_progress_ = false;
  uint32_t transmit_sequence_ = 1;
  uint32_t last_heartbeat_ms_ = 0;
  uint32_t next_sync_attempt_ms_ = 0;
  bool unavailable_shown_ = false;
  Preferences preferences_;
  bool preferences_ready_ = false;
  uint8_t controller_brightness_percent_ = 80;
  uint8_t desired_rear_brightness_percent_ = 35;
  bool rear_brightness_dirty_ = false;
  bool controller_brightness_save_pending_ = false;
  uint32_t controller_brightness_save_due_ms_ = 0;
  uint32_t next_battery_sample_ms_ = 0;
  uint8_t battery_percent_ = 0;
  uint8_t battery_read_failures_ = 0;
  bool have_battery_percent_ = false;
};

}  // namespace rr::controller
