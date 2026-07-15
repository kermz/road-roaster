#include "rear_app.hpp"

#include <Arduino.h>

#include <algorithm>

#include "rear_config.hpp"
#include "secrets_loader.h"

namespace rr::rear {
namespace {
constexpr uint32_t kBrightnessSaveDelayMs = 750;
constexpr uint32_t kPreferenceRetryDelayMs = 5000;
constexpr const char* kPreferencesNamespace = "road-roaster";
constexpr const char* kBrightnessKey = "matrix-pct";
}  // namespace

bool RearApp::begin() {
  if (!validateCatalog()) {
    Serial.println("FATAL: rear message catalog is invalid");
    return false;
  }
  Serial.printf("Catalog: %u messages, revision %08lX\n",
                static_cast<unsigned>(presetCount()),
                static_cast<unsigned long>(catalogRevision()));

  preferences_ready_ = preferences_.begin(kPreferencesNamespace, false);
  if (!preferences_ready_) {
    Serial.println("WARNING: rear preferences are unavailable");
  }
  brightness_percent_ = preferences_ready_
                            ? preferences_.getUChar(
                                  kBrightnessKey,
                                  config::kDefaultBrightnessPercent)
                            : config::kDefaultBrightnessPercent;
  if (!isValidBrightness(brightness_percent_)) {
    brightness_percent_ = config::kDefaultBrightnessPercent;
  }

  if (!renderer_.begin(brightness_percent_)) {
    Serial.println("FATAL: HUB75 DMA display initialization failed");
    return false;
  }
  clearDisplay();

  RadioConfig radio_config;
  radio_config.configured = secrets::kConfigured;
  radio_config.wifi_channel = secrets::kWifiChannel;
  radio_config.peer_mac = secrets::kControllerPeerMac;
  radio_config.primary_master_key = secrets::kPrimaryMasterKey;
  radio_config.local_master_key = secrets::kLocalMasterKey;
  const bool radio_started = radio_.begin(radio_config);
  if (!radio_started && !radio_.diagnosticMode()) {
    Serial.println("FATAL: rear radio initialization failed");
    return false;
  }

  transmit_sequence_ = esp_random();
  last_status_ms_ = millis() - 1000;
  if (radio_.ready()) {
    sendManifest(++transmit_sequence_);
    sendStatus(++transmit_sequence_, millis());
    last_status_ms_ = millis();
  }
  return true;
}

void RearApp::loop() {
  const uint32_t now_ms = millis();
  Packet packet;
  while (radio_.receive(packet)) handlePacket(packet, now_ms);

  if (active_preset_ != nullptr &&
      remainingDuration(now_ms, active_started_ms_, active_duration_ms_) == 0) {
    clearDisplay();
    sendStatus(++transmit_sequence_, now_ms);
  }

  renderer_.tick(now_ms);
  persistBrightnessIfDue(now_ms);
  if (radio_.ready() && now_ms - last_status_ms_ >= 1000) {
    last_status_ms_ = now_ms;
    sendStatus(++transmit_sequence_, now_ms);
  }
}

void RearApp::handlePacket(const Packet& packet, uint32_t now_ms) {
  switch (packet.type) {
    case PacketType::CatalogRequest:
      sendManifest(packet.sequence);
      return;
    case PacketType::CatalogPageRequest:
      sendCatalogPage(packet.sequence, packet.catalog_revision,
                      packet.page_index);
      return;
    case PacketType::GetStatus:
      sendStatus(packet.sequence, now_ms);
      return;
    case PacketType::Display:
    case PacketType::Clear:
    case PacketType::SetBrightness:
      break;
    default:
      return;
  }

  AckResult prior_result;
  if (processed_.find(packet.sequence, packet.type, prior_result)) {
    sendAck(packet.sequence, packet.type, prior_result, now_ms);
    return;
  }

  AckResult result = AckResult::Applied;
  if (packet.type == PacketType::Display) {
    const auto* preset = findPreset(packet.preset_id);
    if (!matchesCatalogRevision(packet.catalog_revision, catalogRevision())) {
      result = AckResult::InvalidCatalogRevision;
    } else if (preset == nullptr) {
      result = AckResult::InvalidPreset;
    } else if (!isValidDurationOverride(packet.duration_override_ms)) {
      result = AckResult::InvalidDuration;
    } else {
      active_preset_ = preset;
      active_started_ms_ = now_ms;
      active_duration_ms_ = effectiveDuration(preset->default_duration_ms,
                                               packet.duration_override_ms);
      renderer_.show(active_preset_, active_started_ms_);
    }
  } else if (packet.type == PacketType::Clear) {
    clearDisplay();
  } else if (!isValidBrightness(packet.brightness_percent)) {
    result = AckResult::InvalidBrightness;
  } else {
    brightness_percent_ = packet.brightness_percent;
    renderer_.setBrightness(brightness_percent_);
    brightness_save_pending_ = true;
    brightness_save_due_ms_ = now_ms + kBrightnessSaveDelayMs;
  }

  processed_.remember(packet.sequence, packet.type, result);
  sendAck(packet.sequence, packet.type, result, now_ms);
}

void RearApp::sendManifest(uint32_t sequence) {
  Packet response;
  response.type = PacketType::CatalogManifest;
  response.sequence = sequence;
  response.catalog_revision = catalogRevision();
  response.total_entries = static_cast<uint16_t>(presetCount());
  response.total_pages = pageCountFor(response.total_entries);
  radio_.send(response);
}

void RearApp::sendCatalogPage(uint32_t sequence, uint32_t revision,
                              uint8_t page) {
  if (revision != catalogRevision() || page >= pageCountFor(presetCount())) {
    sendAck(sequence, PacketType::CatalogPageRequest,
            AckResult::InvalidCatalogRevision, millis());
    return;
  }

  Packet response;
  response.type = PacketType::CatalogPage;
  response.sequence = sequence;
  response.catalog_revision = revision;
  response.page_index = page;
  const size_t start = page * kMessagesPerPage;
  response.entry_count = static_cast<uint8_t>(
      std::min<size_t>(kMessagesPerPage, presetCount() - start));
  for (uint8_t index = 0; index < response.entry_count; ++index) {
    response.entries[index] = summaryAt(start + index);
  }
  radio_.send(response);
}

void RearApp::sendAck(uint32_t sequence, PacketType type, AckResult result,
                      uint32_t now_ms) {
  Packet response;
  response.type = PacketType::Ack;
  response.sequence = sequence;
  response.acknowledged_type = type;
  response.ack_result = result;
  response.state = currentState(now_ms);
  radio_.send(response);
}

void RearApp::sendStatus(uint32_t sequence, uint32_t now_ms) {
  Packet response;
  response.type = PacketType::Status;
  response.sequence = sequence;
  response.state = currentState(now_ms);
  radio_.send(response);
}

RearState RearApp::currentState(uint32_t now_ms) const {
  RearState state;
  state.catalog_revision = catalogRevision();
  state.brightness_percent = brightness_percent_;
  state.active = active_preset_ != nullptr;
  if (state.active) {
    state.preset_id = active_preset_->id;
    state.total_duration_ms = active_duration_ms_;
    state.remaining_ms =
        remainingDuration(now_ms, active_started_ms_, active_duration_ms_);
  }
  return state;
}

void RearApp::persistBrightnessIfDue(uint32_t now_ms) {
  if (!brightness_save_pending_ ||
      static_cast<int32_t>(now_ms - brightness_save_due_ms_) < 0) {
    return;
  }
  if (!ensurePreferences() ||
      preferences_.putUChar(kBrightnessKey, brightness_percent_) == 0) {
    Serial.println("WARNING: failed to save rear brightness; retrying");
    brightness_save_due_ms_ = now_ms + kPreferenceRetryDelayMs;
    return;
  }
  brightness_save_pending_ = false;
}

bool RearApp::ensurePreferences() {
  if (preferences_ready_) return true;
  preferences_ready_ = preferences_.begin(kPreferencesNamespace, false);
  return preferences_ready_;
}

void RearApp::clearDisplay() {
  active_preset_ = nullptr;
  active_started_ms_ = 0;
  active_duration_ms_ = 0;
  renderer_.clear();
}

}  // namespace rr::rear
