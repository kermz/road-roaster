#include "rr/radio_transport.hpp"

#ifndef ROAD_ROASTER_NATIVE_TEST

#include <Arduino.h>

#include <cstring>

#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

namespace rr {

RadioTransport* RadioTransport::instance_ = nullptr;

bool RadioTransport::begin(const RadioConfig& config) {
  config_ = config;
  instance_ = this;

  esp_netif_init();
  const esp_err_t event_result = esp_event_loop_create_default();
  if (event_result != ESP_OK && event_result != ESP_ERR_INVALID_STATE) {
    Serial.println("Failed to create the Wi-Fi event loop");
    return false;
  }
  wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&wifi_config) != ESP_OK ||
      esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
      esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_start() != ESP_OK ||
      esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK ||
      esp_wifi_set_channel(config_.wifi_channel, WIFI_SECOND_CHAN_NONE) !=
          ESP_OK) {
    Serial.println("Failed to initialize the ESP-NOW Wi-Fi station");
    return false;
  }

  uint8_t station_mac[6]{};
  esp_read_mac(station_mac, ESP_MAC_WIFI_STA);
  Serial.printf("ESP-NOW station MAC: %02X:%02X:%02X:%02X:%02X:%02X, channel: %u\n",
                station_mac[0], station_mac[1], station_mac[2], station_mac[3],
                station_mac[4], station_mac[5], config_.wifi_channel);

  if (!config_.configured) {
    diagnostic_mode_ = true;
    Serial.println(
        "ESP-NOW is not configured. Copy include/secrets.example.h to "
        "include/secrets.h and add both station MAC addresses and private keys.");
    return false;
  }

  receive_queue_ = xQueueCreate(8, sizeof(Packet));
  if (receive_queue_ == nullptr) {
    Serial.println("Failed to create ESP-NOW receive queue");
    return false;
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("esp_now_init failed");
    return false;
  }
  if (esp_now_set_pmk(config_.primary_master_key.data()) != ESP_OK) {
    Serial.println("esp_now_set_pmk failed");
    return false;
  }

  esp_now_peer_info_t peer{};
  std::memcpy(peer.peer_addr, config_.peer_mac.data(), config_.peer_mac.size());
  std::memcpy(peer.lmk, config_.local_master_key.data(),
              config_.local_master_key.size());
  peer.channel = config_.wifi_channel;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = true;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("esp_now_add_peer failed");
    return false;
  }
  if (esp_now_register_recv_cb(&RadioTransport::onReceive) != ESP_OK) {
    Serial.println("esp_now_register_recv_cb failed");
    return false;
  }

  ready_ = true;
  Serial.println("Encrypted ESP-NOW peer ready");
  return true;
}

bool RadioTransport::send(const Packet& packet) {
  if (!ready_) return false;
  std::array<uint8_t, kMaxEspNowPayload> encoded{};
  const size_t length = encodePacket(packet, encoded.data(), encoded.size());
  if (length == 0) return false;
  return esp_now_send(config_.peer_mac.data(), encoded.data(), length) == ESP_OK;
}

bool RadioTransport::receive(Packet& packet) {
  return ready_ && receive_queue_ != nullptr &&
         xQueueReceive(receive_queue_, &packet, 0) == pdTRUE;
}

void RadioTransport::onReceive(const esp_now_recv_info_t* info,
                               const uint8_t* data, int length) {
  if (instance_ == nullptr || info == nullptr || data == nullptr || length <= 0 ||
      instance_->receive_queue_ == nullptr ||
      std::memcmp(info->src_addr, instance_->config_.peer_mac.data(), 6) != 0) {
    return;
  }

  const DecodeResult decoded = decodePacket(data, static_cast<size_t>(length));
  if (!decoded) return;
  xQueueSend(instance_->receive_queue_, &decoded.packet, 0);
}

}  // namespace rr

#else

namespace rr {
bool RadioTransport::begin(const RadioConfig&) { return false; }
bool RadioTransport::send(const Packet&) { return false; }
bool RadioTransport::receive(Packet&) { return false; }
}  // namespace rr

#endif
