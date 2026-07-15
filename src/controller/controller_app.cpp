#include "controller_app.hpp"

#include <Arduino.h>

#include <cstdio>

#include "knob_board/knob_board.hpp"
#include "secrets_loader.h"

namespace rr::controller {
namespace {

constexpr uint32_t kAckTimeoutMs = 300;
constexpr uint8_t kMaximumAttempts = 4;  // Initial send plus three retries.
constexpr uint32_t kHeartbeatTimeoutMs = 3500;
constexpr uint32_t kCatalogRetryDelayMs = 1000;
constexpr uint32_t kBrightnessSaveDelayMs = 750;
constexpr const char* kPreferencesNamespace = "road-roaster";
constexpr const char* kBrightnessKey = "lcd-pct";

void durationKey(uint16_t preset_id, char (&key)[10]) {
  std::snprintf(key, sizeof(key), "dur-%04X", preset_id);
}

}  // namespace

bool ControllerApp::begin() {
  if (!knob_board::begin()) {
    Serial.println("FATAL: knob LCD/touch/encoder initialization failed");
    return false;
  }
  preferences_.begin(kPreferencesNamespace, false);
  controller_brightness_percent_ =
      preferences_.getUChar(kBrightnessKey, 80);
  if (!isValidBrightness(controller_brightness_percent_)) {
    controller_brightness_percent_ = 80;
  }
  knob_board::setBacklight(controller_brightness_percent_);

  ui_.begin(displayRequested, clearRequested, brightnessRequested,
            durationOverrideRequested, durationOverrideSaved,
            uiFeedbackRequested, this);
  ui_.setBrightnessValues(controller_brightness_percent_,
                          desired_rear_brightness_percent_, false);

  RadioConfig config;
  config.configured = secrets::kConfigured;
  config.wifi_channel = secrets::kWifiChannel;
  config.peer_mac = secrets::kRearDisplayPeerMac;
  config.primary_master_key = secrets::kPrimaryMasterKey;
  config.local_master_key = secrets::kLocalMasterKey;
  radio_.begin(config);

  transmit_sequence_ = esp_random();
  if (radio_.diagnosticMode() || !radio_.ready()) {
    ui_.showSetupRequired();
  } else {
    startCatalogSync(millis());
  }
  return true;
}

void ControllerApp::loop() {
  const uint32_t now_ms = millis();
  Packet packet;
  while (radio_.receive(packet)) handlePacket(packet, now_ms);

  servicePending(now_ms);
  serviceBrightness(now_ms);
  persistControllerBrightnessIfDue(now_ms);
  if (sync_in_progress_ && !pending_.active &&
      static_cast<int32_t>(now_ms - next_sync_attempt_ms_) >= 0) {
    startCatalogSync(now_ms);
  }

  if (radio_.ready() && !rearAvailable(now_ms) && !unavailable_shown_ &&
      now_ms >= kHeartbeatTimeoutMs) {
    unavailable_shown_ = true;
    ui_.showRearUnavailable();
    ui_.setBrightnessValues(controller_brightness_percent_,
                            desired_rear_brightness_percent_, false);
    knob_board::vibrate();
  }

  const int encoder_delta = knob_board::consumeEncoderDelta();
  if (encoder_delta != 0) {
    knob_board::vibrate();
    ui_.rotate(encoder_delta);
  }
  ui_.tick(now_ms);
  knob_board::processLvgl();
}

void ControllerApp::handlePacket(const Packet& packet, uint32_t now_ms) {
  switch (packet.type) {
    case PacketType::CatalogManifest:
      handleManifest(packet, now_ms);
      break;
    case PacketType::CatalogPage:
      handleCatalogPage(packet, now_ms);
      break;
    case PacketType::Ack:
      handleAck(packet, now_ms);
      break;
    case PacketType::Status:
      handleState(packet.state, now_ms, true);
      break;
    default:
      break;
  }
}

void ControllerApp::handleManifest(const Packet& packet, uint32_t now_ms) {
  const bool expected =
      pending_.active && pending_.packet.type == PacketType::CatalogRequest &&
      pending_.packet.sequence == packet.sequence;
  if (!expected && !sync_in_progress_) {
    // Rear boot advertisement or a catalog change. A matching status packet
    // will also trigger this path, but accepting the manifest saves one round.
    sync_in_progress_ = true;
    ui_.showSyncing();
  } else if (!expected) {
    return;
  }
  pending_.active = false;

  if (catalog_.complete() && catalog_.revision() == packet.catalog_revision &&
      catalog_.size() == packet.total_entries &&
      catalog_.pageCount() == packet.total_pages) {
    completeCatalogSync();
    return;
  }
  if (!catalog_.begin(packet.catalog_revision, packet.total_entries,
                      packet.total_pages)) {
    next_sync_attempt_ms_ = now_ms + kCatalogRetryDelayMs;
    return;
  }
  requestNextCatalogPage(now_ms);
}

void ControllerApp::handleCatalogPage(const Packet& packet, uint32_t now_ms) {
  if (!pending_.active ||
      pending_.packet.type != PacketType::CatalogPageRequest ||
      pending_.packet.sequence != packet.sequence ||
      pending_.packet.page_index != packet.page_index) {
    return;
  }
  pending_.active = false;
  if (!catalog_.applyPage(packet.catalog_revision, packet.page_index,
                          packet.entry_count, packet.entries.data())) {
    next_sync_attempt_ms_ = now_ms + kCatalogRetryDelayMs;
    return;
  }
  requestNextCatalogPage(now_ms);
}

void ControllerApp::handleAck(const Packet& packet, uint32_t now_ms) {
  if (!pending_.active || pending_.packet.sequence != packet.sequence ||
      pending_.packet.type != packet.acknowledged_type) {
    return;
  }
  const PacketType request_type = pending_.packet.type;
  pending_.active = false;

  if (request_type == PacketType::SetBrightness) {
    have_rear_state_ = true;
    rear_state_ = packet.state;
    unavailable_shown_ = false;
    if (packet.ack_result == AckResult::Applied) {
      if (desired_rear_brightness_percent_ ==
          rear_state_.brightness_percent) {
        rear_brightness_dirty_ = false;
      }
      if (!rear_brightness_dirty_) {
        ui_.setBrightnessValues(controller_brightness_percent_,
                                rear_state_.brightness_percent, true);
      }
      knob_board::vibrate();
    }
    return;
  }

  if (request_type == PacketType::Display || request_type == PacketType::Clear) {
    have_rear_state_ = true;
    rear_state_ = packet.state;
    unavailable_shown_ = false;
    if (packet.ack_result == AckResult::Applied) {
      ui_.showRearState(rear_state_, now_ms);
    } else {
      ui_.showCommandFailed();
    }
    knob_board::vibrate();
    if (rear_state_.catalog_revision != catalog_.revision()) {
      startCatalogSync(now_ms);
    }
    return;
  }

  if (request_type == PacketType::CatalogPageRequest) {
    next_sync_attempt_ms_ = now_ms + kCatalogRetryDelayMs;
  }
}

void ControllerApp::handleState(const RearState& state, uint32_t now_ms,
                                bool heartbeat) {
  const bool meaningful_change =
      have_rear_state_ &&
      (rear_state_.active != state.active ||
       rear_state_.preset_id != state.preset_id ||
       rear_state_.brightness_percent != state.brightness_percent);
  rear_state_ = state;
  have_rear_state_ = true;
  if (heartbeat) last_heartbeat_ms_ = now_ms;
  unavailable_shown_ = false;
  if (!rear_brightness_dirty_ &&
      !(pending_.active &&
        pending_.packet.type == PacketType::SetBrightness)) {
    desired_rear_brightness_percent_ = state.brightness_percent;
    ui_.setBrightnessValues(controller_brightness_percent_,
                            state.brightness_percent, true);
  }
  if (meaningful_change) knob_board::vibrate();

  if (!catalog_.complete() || state.catalog_revision != catalog_.revision()) {
    if (!sync_in_progress_ || !isCatalogRequestPending()) {
      startCatalogSync(now_ms);
    }
    return;
  }
  if (!isCommandPending() && !sync_in_progress_) {
    ui_.showRearState(state, now_ms);
  }
}

void ControllerApp::startCatalogSync(uint32_t now_ms) {
  if (!radio_.ready()) return;
  sync_in_progress_ = true;
  if (!unavailable_shown_) ui_.showSyncing();
  Packet request;
  request.type = PacketType::CatalogRequest;
  request.sequence = nextSequence();
  request.catalog_revision = catalog_.complete() ? catalog_.revision() : 0;
  beginRequest(request, now_ms);
}

void ControllerApp::requestNextCatalogPage(uint32_t now_ms) {
  if (catalog_.complete()) {
    completeCatalogSync();
    return;
  }
  Packet request;
  request.type = PacketType::CatalogPageRequest;
  request.sequence = nextSequence();
  request.catalog_revision = catalog_.revision();
  request.page_index = catalog_.firstMissingPage();
  beginRequest(request, now_ms);
}

void ControllerApp::completeCatalogSync() {
  sync_in_progress_ = false;
  pending_.active = false;
  ui_.setCatalog(&catalog_);
  if (have_rear_state_ && rear_state_.catalog_revision == catalog_.revision() &&
      rearAvailable(millis())) {
    ui_.showRearState(rear_state_, millis());
    ui_.setBrightnessValues(controller_brightness_percent_,
                            rear_state_.brightness_percent, true);
  } else {
    ui_.showRearUnavailable();
    ui_.setBrightnessValues(controller_brightness_percent_,
                            desired_rear_brightness_percent_, false);
  }
}

void ControllerApp::beginRequest(const Packet& packet, uint32_t now_ms) {
  pending_.active = true;
  pending_.packet = packet;
  pending_.attempts = 1;
  pending_.last_sent_ms = now_ms;
  radio_.send(packet);
}

void ControllerApp::servicePending(uint32_t now_ms) {
  if (!pending_.active || now_ms - pending_.last_sent_ms < kAckTimeoutMs) {
    return;
  }
  if (pending_.attempts < kMaximumAttempts) {
    radio_.send(pending_.packet);
    ++pending_.attempts;
    pending_.last_sent_ms = now_ms;
    return;
  }

  const PacketType failed_type = pending_.packet.type;
  pending_.active = false;
  if (failed_type == PacketType::Display || failed_type == PacketType::Clear) {
    ui_.showCommandFailed();
  } else if (failed_type == PacketType::SetBrightness) {
    // Keep the latest requested value queued. A heartbeat will make it
    // eligible for another command attempt without disrupting catalog state.
    rear_brightness_dirty_ = true;
  } else {
    sync_in_progress_ = true;
    next_sync_attempt_ms_ = now_ms + kCatalogRetryDelayMs;
  }
}

bool ControllerApp::isCatalogRequestPending() const {
  return pending_.active &&
         (pending_.packet.type == PacketType::CatalogRequest ||
          pending_.packet.type == PacketType::CatalogPageRequest);
}

bool ControllerApp::isCommandPending() const {
  return pending_.active &&
         (pending_.packet.type == PacketType::Display ||
          pending_.packet.type == PacketType::Clear ||
          pending_.packet.type == PacketType::SetBrightness);
}

bool ControllerApp::rearAvailable(uint32_t now_ms) const {
  return last_heartbeat_ms_ != 0 &&
         now_ms - last_heartbeat_ms_ <= kHeartbeatTimeoutMs;
}

uint32_t ControllerApp::nextSequence() { return ++transmit_sequence_; }

void ControllerApp::sendDisplay(uint16_t preset_id,
                                uint32_t duration_override_ms) {
  const uint32_t now_ms = millis();
  if (pending_.active || sync_in_progress_ || !rearAvailable(now_ms) ||
      catalog_.findById(preset_id) == nullptr ||
      !isValidDurationOverride(duration_override_ms)) {
    return;
  }
  Packet request;
  request.type = PacketType::Display;
  request.sequence = nextSequence();
  request.preset_id = preset_id;
  request.duration_override_ms = duration_override_ms;
  beginRequest(request, now_ms);
  ui_.showSending();
}

void ControllerApp::sendClear() {
  const uint32_t now_ms = millis();
  if (pending_.active || sync_in_progress_ || !rearAvailable(now_ms) ||
      !rear_state_.active) {
    return;
  }
  Packet request;
  request.type = PacketType::Clear;
  request.sequence = nextSequence();
  beginRequest(request, now_ms);
  ui_.showSending();
}

void ControllerApp::displayRequested(void* context, uint16_t preset_id,
                                     uint32_t duration_override_ms) {
  static_cast<ControllerApp*>(context)->sendDisplay(preset_id,
                                                    duration_override_ms);
}

void ControllerApp::clearRequested(void* context) {
  static_cast<ControllerApp*>(context)->sendClear();
}

void ControllerApp::brightnessRequested(void* context, bool rear_display,
                                        uint8_t brightness_percent) {
  auto* app = static_cast<ControllerApp*>(context);
  if (app == nullptr || !isValidBrightness(brightness_percent)) return;
  knob_board::vibrate();
  if (rear_display) {
    app->desired_rear_brightness_percent_ = brightness_percent;
    app->rear_brightness_dirty_ = true;
    return;
  }
  app->controller_brightness_percent_ = brightness_percent;
  knob_board::setBacklight(brightness_percent);
  app->controller_brightness_save_pending_ = true;
  app->controller_brightness_save_due_ms_ =
      millis() + kBrightnessSaveDelayMs;
}

uint32_t ControllerApp::durationOverrideRequested(void* context,
                                                  uint16_t preset_id) {
  auto* app = static_cast<ControllerApp*>(context);
  return app == nullptr ? 0 : app->loadDurationOverride(preset_id);
}

void ControllerApp::durationOverrideSaved(void* context, uint16_t preset_id,
                                          uint32_t duration_override_ms) {
  auto* app = static_cast<ControllerApp*>(context);
  if (app == nullptr || !isValidDurationOverride(duration_override_ms)) return;
  app->saveDurationOverride(preset_id, duration_override_ms);
  knob_board::vibrate();
}

void ControllerApp::uiFeedbackRequested(void* context) {
  if (context != nullptr) knob_board::vibrate();
}

void ControllerApp::serviceBrightness(uint32_t now_ms) {
  if (rear_brightness_dirty_ && !pending_.active && !sync_in_progress_ &&
      rearAvailable(now_ms)) {
    sendRearBrightness(now_ms);
  }
}

void ControllerApp::sendRearBrightness(uint32_t now_ms) {
  Packet request;
  request.type = PacketType::SetBrightness;
  request.sequence = nextSequence();
  request.brightness_percent = desired_rear_brightness_percent_;
  beginRequest(request, now_ms);
}

void ControllerApp::persistControllerBrightnessIfDue(uint32_t now_ms) {
  if (!controller_brightness_save_pending_ ||
      static_cast<int32_t>(now_ms - controller_brightness_save_due_ms_) < 0) {
    return;
  }
  preferences_.putUChar(kBrightnessKey, controller_brightness_percent_);
  controller_brightness_save_pending_ = false;
}

uint32_t ControllerApp::loadDurationOverride(uint16_t preset_id) {
  char key[10];
  durationKey(preset_id, key);
  const uint8_t choice = preferences_.getUChar(key, 0);
  const uint32_t duration_override_ms = choice * 5000UL;
  return isValidDurationOverride(duration_override_ms) ? duration_override_ms
                                                       : 0;
}

void ControllerApp::saveDurationOverride(uint16_t preset_id,
                                         uint32_t duration_override_ms) {
  char key[10];
  durationKey(preset_id, key);
  if (duration_override_ms == 0) {
    preferences_.remove(key);
    return;
  }
  preferences_.putUChar(key,
                        static_cast<uint8_t>(duration_override_ms / 5000UL));
}

}  // namespace rr::controller
