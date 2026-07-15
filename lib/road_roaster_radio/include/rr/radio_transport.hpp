#pragma once

#include <array>
#include <cstdint>

#include "rr/protocol.hpp"

#ifndef ROAD_ROASTER_NATIVE_TEST
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#endif

namespace rr {

struct RadioConfig {
  bool configured = false;
  uint8_t wifi_channel = 6;
  std::array<uint8_t, 6> peer_mac{};
  std::array<uint8_t, 16> primary_master_key{};
  std::array<uint8_t, 16> local_master_key{};
};

class RadioTransport {
 public:
  bool begin(const RadioConfig& config);
  bool send(const Packet& packet);
  bool receive(Packet& packet);

  bool ready() const { return ready_; }
  bool diagnosticMode() const { return diagnostic_mode_; }

 private:
#ifndef ROAD_ROASTER_NATIVE_TEST
  static void onReceive(const esp_now_recv_info_t* info, const uint8_t* data,
                        int length);
  static RadioTransport* instance_;
  QueueHandle_t receive_queue_ = nullptr;
#endif

  RadioConfig config_{};
  bool ready_ = false;
  bool diagnostic_mode_ = false;
};

}  // namespace rr

