#pragma once

#include <cstdint>

#include <Preferences.h>

#include "matrix_renderer.hpp"
#include "rr/radio_transport.hpp"

namespace rr::rear {

class RearApp {
 public:
  bool begin();
  void loop();

 private:
  void handlePacket(const Packet& packet, uint32_t now_ms);
  void sendManifest(uint32_t sequence);
  void sendCatalogPage(uint32_t sequence, uint32_t revision, uint8_t page);
  void sendAck(uint32_t sequence, PacketType type, AckResult result,
               uint32_t now_ms);
  void sendStatus(uint32_t sequence, uint32_t now_ms);
  RearState currentState(uint32_t now_ms) const;
  void clearDisplay();
  void persistBrightnessIfDue(uint32_t now_ms);

  RadioTransport radio_;
  MatrixRenderer renderer_;
  const PresetDefinition* active_preset_ = nullptr;
  uint32_t active_started_ms_ = 0;
  uint32_t active_duration_ms_ = 0;
  uint32_t last_status_ms_ = 0;
  uint32_t transmit_sequence_ = 1;
  ProcessedCommandWindow processed_{};
  Preferences preferences_;
  uint8_t brightness_percent_ = 35;
  bool brightness_save_pending_ = false;
  uint32_t brightness_save_due_ms_ = 0;
};

}  // namespace rr::rear
