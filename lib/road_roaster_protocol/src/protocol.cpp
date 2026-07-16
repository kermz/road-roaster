#include "rr/protocol.hpp"

#include <algorithm>
#include <cstring>

namespace rr {
namespace {

constexpr size_t kHeaderSize = 10;

class Writer {
 public:
  Writer(uint8_t* data, size_t capacity) : data_(data), capacity_(capacity) {}

  bool u8(uint8_t value) { return bytes(&value, sizeof(value)); }
  bool u16(uint16_t value) {
    const uint8_t raw[] = {static_cast<uint8_t>(value),
                           static_cast<uint8_t>(value >> 8)};
    return bytes(raw, sizeof(raw));
  }
  bool u32(uint32_t value) {
    const uint8_t raw[] = {
        static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    return bytes(raw, sizeof(raw));
  }
  bool bytes(const void* source, size_t length) {
    if (position_ + length > capacity_) return false;
    std::memcpy(data_ + position_, source, length);
    position_ += length;
    return true;
  }
  size_t position() const { return position_; }

 private:
  uint8_t* data_;
  size_t capacity_;
  size_t position_ = 0;
};

class Reader {
 public:
  Reader(const uint8_t* data, size_t length) : data_(data), length_(length) {}

  bool u8(uint8_t& value) { return bytes(&value, sizeof(value)); }
  bool u16(uint16_t& value) {
    uint8_t raw[2];
    if (!bytes(raw, sizeof(raw))) return false;
    value = static_cast<uint16_t>(raw[0]) |
            (static_cast<uint16_t>(raw[1]) << 8);
    return true;
  }
  bool u32(uint32_t& value) {
    uint8_t raw[4];
    if (!bytes(raw, sizeof(raw))) return false;
    value = static_cast<uint32_t>(raw[0]) |
            (static_cast<uint32_t>(raw[1]) << 8) |
            (static_cast<uint32_t>(raw[2]) << 16) |
            (static_cast<uint32_t>(raw[3]) << 24);
    return true;
  }
  bool bytes(void* destination, size_t length) {
    if (position_ + length > length_) return false;
    std::memcpy(destination, data_ + position_, length);
    position_ += length;
    return true;
  }
  size_t remaining() const { return length_ - position_; }

 private:
  const uint8_t* data_;
  size_t length_;
  size_t position_ = 0;
};

bool isKnownType(uint8_t raw) {
  return raw >= static_cast<uint8_t>(PacketType::CatalogRequest) &&
         raw <= static_cast<uint8_t>(PacketType::SetFlip);
}

bool writeState(Writer& writer, const RearState& state) {
  return writer.u32(state.catalog_revision) && writer.u8(state.active ? 1 : 0) &&
         writer.u16(state.preset_id) && writer.u32(state.total_duration_ms) &&
         writer.u32(state.remaining_ms) && writer.u8(state.brightness_percent) &&
         writer.u8(state.flipped ? 1 : 0);
}

bool readState(Reader& reader, RearState& state) {
  uint8_t active = 0;
  uint8_t flipped = 0;
  if (!reader.u32(state.catalog_revision) || !reader.u8(active) || active > 1 ||
      !reader.u16(state.preset_id) || !reader.u32(state.total_duration_ms) ||
      !reader.u32(state.remaining_ms) ||
      !reader.u8(state.brightness_percent) ||
      !isValidBrightness(state.brightness_percent) || !reader.u8(flipped) ||
      flipped > 1) {
    return false;
  }
  state.active = active == 1;
  state.flipped = flipped == 1;
  return true;
}

bool writePayload(Writer& writer, const Packet& packet) {
  switch (packet.type) {
    case PacketType::CatalogRequest:
      return writer.u32(packet.catalog_revision);
    case PacketType::CatalogManifest:
      return writer.u32(packet.catalog_revision) &&
             writer.u16(packet.total_entries) && writer.u8(packet.total_pages);
    case PacketType::CatalogPageRequest:
      return writer.u32(packet.catalog_revision) && writer.u8(packet.page_index);
    case PacketType::CatalogPage: {
      if (packet.entry_count > kMessagesPerPage) return false;
      if (!writer.u32(packet.catalog_revision) || !writer.u8(packet.page_index) ||
          !writer.u8(packet.entry_count)) {
        return false;
      }
      for (uint8_t i = 0; i < packet.entry_count; ++i) {
        const auto& entry = packet.entries[i];
        const size_t label_length = strnlen(entry.label.data(), kMaxLabelBytes + 1);
        if (label_length > kMaxLabelBytes) return false;
        if (!writer.u16(entry.id) || !writer.u32(entry.default_duration_ms) ||
            !writer.u8(static_cast<uint8_t>(label_length)) ||
            !writer.bytes(entry.label.data(), label_length)) {
          return false;
        }
      }
      return true;
    }
    case PacketType::Display:
      return writer.u32(packet.catalog_revision) &&
             writer.u16(packet.preset_id) &&
             writer.u32(packet.duration_override_ms);
    case PacketType::SetBrightness:
      return writer.u8(packet.brightness_percent);
    case PacketType::SetFlip:
      return writer.u8(packet.flipped ? 1 : 0);
    case PacketType::Clear:
    case PacketType::GetStatus:
      return true;
    case PacketType::Ack:
      return writer.u8(static_cast<uint8_t>(packet.ack_result)) &&
             writer.u8(static_cast<uint8_t>(packet.acknowledged_type)) &&
             writeState(writer, packet.state);
    case PacketType::Status:
      return writeState(writer, packet.state);
  }
  return false;
}

bool readPayload(Reader& reader, Packet& packet) {
  switch (packet.type) {
    case PacketType::CatalogRequest:
      return reader.u32(packet.catalog_revision);
    case PacketType::CatalogManifest:
      return reader.u32(packet.catalog_revision) &&
             reader.u16(packet.total_entries) && reader.u8(packet.total_pages) &&
             packet.total_entries <= kMaxCatalogEntries &&
             packet.total_pages == pageCountFor(packet.total_entries);
    case PacketType::CatalogPageRequest:
      return reader.u32(packet.catalog_revision) && reader.u8(packet.page_index);
    case PacketType::CatalogPage: {
      if (!reader.u32(packet.catalog_revision) || !reader.u8(packet.page_index) ||
          !reader.u8(packet.entry_count) ||
          packet.entry_count > kMessagesPerPage) {
        return false;
      }
      for (uint8_t i = 0; i < packet.entry_count; ++i) {
        auto& entry = packet.entries[i];
        uint8_t label_length = 0;
        if (!reader.u16(entry.id) || !reader.u32(entry.default_duration_ms) ||
            !reader.u8(label_length) || label_length > kMaxLabelBytes ||
            !reader.bytes(entry.label.data(), label_length)) {
          return false;
        }
        entry.label[label_length] = '\0';
      }
      return true;
    }
    case PacketType::Display:
      return reader.u32(packet.catalog_revision) &&
             reader.u16(packet.preset_id) &&
             reader.u32(packet.duration_override_ms);
    case PacketType::SetBrightness:
      return reader.u8(packet.brightness_percent);
    case PacketType::SetFlip: {
      uint8_t flipped = 0;
      if (!reader.u8(flipped) || flipped > 1) return false;
      packet.flipped = flipped == 1;
      return true;
    }
    case PacketType::Clear:
    case PacketType::GetStatus:
      return true;
    case PacketType::Ack: {
      uint8_t result = 0;
      uint8_t acknowledged = 0;
      if (!reader.u8(result) ||
          result > static_cast<uint8_t>(AckResult::InvalidBrightness) ||
          !reader.u8(acknowledged) || !isKnownType(acknowledged) ||
          !readState(reader, packet.state)) {
        return false;
      }
      packet.ack_result = static_cast<AckResult>(result);
      packet.acknowledged_type = static_cast<PacketType>(acknowledged);
      return true;
    }
    case PacketType::Status:
      return readState(reader, packet.state);
  }
  return false;
}

}  // namespace

size_t encodePacket(const Packet& packet, uint8_t* output, size_t capacity) {
  if (output == nullptr || capacity < kHeaderSize || capacity > kMaxEspNowPayload) {
    return 0;
  }

  std::array<uint8_t, kMaxEspNowPayload> payload{};
  Writer payload_writer(payload.data(), payload.size());
  if (!writePayload(payload_writer, packet)) return 0;

  Writer writer(output, capacity);
  if (!writer.u16(kProtocolMagic) || !writer.u8(kProtocolVersion) ||
      !writer.u8(static_cast<uint8_t>(packet.type)) ||
      !writer.u32(packet.sequence) ||
      !writer.u16(static_cast<uint16_t>(payload_writer.position())) ||
      !writer.bytes(payload.data(), payload_writer.position())) {
    return 0;
  }
  return writer.position();
}

DecodeResult decodePacket(const uint8_t* data, size_t length) {
  DecodeResult result;
  if (data == nullptr || length < kHeaderSize) {
    result.error = DecodeError::TooShort;
    return result;
  }
  if (length > kMaxEspNowPayload) {
    result.error = DecodeError::TooLong;
    return result;
  }

  Reader reader(data, length);
  uint16_t magic = 0;
  uint8_t version = 0;
  uint8_t type = 0;
  uint16_t payload_length = 0;
  reader.u16(magic);
  reader.u8(version);
  reader.u8(type);
  reader.u32(result.packet.sequence);
  reader.u16(payload_length);

  if (magic != kProtocolMagic) {
    result.error = DecodeError::InvalidMagic;
    return result;
  }
  if (version != kProtocolVersion) {
    result.error = DecodeError::UnsupportedVersion;
    return result;
  }
  if (!isKnownType(type)) {
    result.error = DecodeError::UnknownType;
    return result;
  }
  if (payload_length != reader.remaining()) {
    result.error = DecodeError::InvalidLength;
    return result;
  }

  result.packet.type = static_cast<PacketType>(type);
  if (!readPayload(reader, result.packet) || reader.remaining() != 0) {
    result.error = DecodeError::InvalidPayload;
  }
  return result;
}

bool ProcessedCommandWindow::find(uint32_t sequence, PacketType type,
                                  AckResult& result) const {
  for (const auto& entry : entries_) {
    if (entry.valid && entry.sequence == sequence && entry.type == type) {
      result = entry.result;
      return true;
    }
  }
  return false;
}

void ProcessedCommandWindow::remember(uint32_t sequence, PacketType type,
                                      AckResult result) {
  entries_[cursor_] = {true, sequence, type, result};
  cursor_ = (cursor_ + 1) % entries_.size();
}

void RequestCoordinator::begin(const Packet& packet, uint32_t now_ms) {
  active_ = true;
  packet_ = packet;
  attempts_ = 1;
  last_sent_ms_ = now_ms;
}

void RequestCoordinator::clear() { active_ = false; }

bool RequestCoordinator::matches(uint32_t sequence, PacketType type) const {
  return active_ && packet_.sequence == sequence && packet_.type == type;
}

bool RequestCoordinator::matchesManifest(const Packet& response) const {
  return response.type == PacketType::CatalogManifest &&
         matches(response.sequence, PacketType::CatalogRequest);
}

bool RequestCoordinator::matchesCatalogPage(const Packet& response) const {
  return response.type == PacketType::CatalogPage &&
         matches(response.sequence, PacketType::CatalogPageRequest) &&
         packet_.page_index == response.page_index;
}

CatalogManifestAction RequestCoordinator::manifestAction(
    const Packet& response, bool sync_in_progress,
    bool command_pending) const {
  return catalogManifestAction(matchesManifest(response), sync_in_progress,
                               command_pending);
}

RequestTimeoutAction RequestCoordinator::timeoutAction(
    uint32_t now_ms, uint32_t timeout_ms,
    uint8_t maximum_attempts) const {
  return requestTimeoutAction(active_, now_ms, last_sent_ms_, timeout_ms,
                              attempts_, maximum_attempts);
}

void RequestCoordinator::markRetried(uint32_t now_ms) {
  if (!active_) return;
  ++attempts_;
  last_sent_ms_ = now_ms;
}

uint8_t pageCountFor(uint16_t entry_count) {
  if (entry_count == 0) return 0;
  return static_cast<uint8_t>((entry_count + kMessagesPerPage - 1) /
                              kMessagesPerPage);
}

uint8_t wrappedIndex(uint8_t current, int delta, uint8_t count) {
  if (count == 0) return 0;
  const int64_t value = static_cast<int64_t>(current) + delta;
  int64_t wrapped = value % count;
  if (wrapped < 0) wrapped += count;
  return static_cast<uint8_t>(wrapped);
}

bool isValidDurationOverride(uint32_t duration_ms) {
  return duration_ms == 0 ||
         (duration_ms >= kMinOverrideDurationMs &&
          duration_ms <= kMaxOverrideDurationMs &&
          duration_ms % kOverrideDurationStepMs == 0);
}

bool isValidBrightness(uint8_t brightness_percent) {
  return brightness_percent >= kMinBrightnessPercent &&
         brightness_percent <= kMaxBrightnessPercent &&
         brightness_percent % kBrightnessStepPercent == 0;
}

uint32_t effectiveDuration(uint32_t default_duration_ms,
                           uint32_t duration_override_ms) {
  return duration_override_ms == 0 ? default_duration_ms
                                   : duration_override_ms;
}

uint32_t remainingDuration(uint32_t now_ms, uint32_t started_ms,
                           uint32_t total_duration_ms) {
  const uint32_t elapsed = now_ms - started_ms;
  return elapsed >= total_duration_ms ? 0 : total_duration_ms - elapsed;
}

bool canStartCatalogSync(bool request_pending) { return !request_pending; }

CatalogManifestAction catalogManifestAction(bool expected,
                                            bool sync_in_progress,
                                            bool command_pending) {
  if (expected) return CatalogManifestAction::AcceptExpected;
  if (command_pending || sync_in_progress) {
    return CatalogManifestAction::Ignore;
  }
  return CatalogManifestAction::BeginSync;
}

bool matchesCatalogRevision(uint32_t request_revision,
                            uint32_t current_revision) {
  return request_revision == current_revision;
}

RequestTimeoutAction requestTimeoutAction(bool active, uint32_t now_ms,
                                          uint32_t last_sent_ms,
                                          uint32_t timeout_ms,
                                          uint8_t attempts,
                                          uint8_t maximum_attempts) {
  if (!active || now_ms - last_sent_ms < timeout_ms) {
    return RequestTimeoutAction::Wait;
  }
  return attempts < maximum_attempts ? RequestTimeoutAction::Retry
                                     : RequestTimeoutAction::Fail;
}

}  // namespace rr
