#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rr {

inline constexpr uint16_t kProtocolMagic = 0x5252;
inline constexpr uint8_t kProtocolVersion = 2;
inline constexpr size_t kMaxEspNowPayload = 250;
inline constexpr uint8_t kMessagesPerPage = 4;
inline constexpr uint16_t kMaxCatalogEntries = 64;
inline constexpr size_t kMaxLabelBytes = 31;
inline constexpr uint32_t kDefaultDurationMs = 10000;
inline constexpr uint32_t kMinOverrideDurationMs = 5000;
inline constexpr uint32_t kMaxOverrideDurationMs = 60000;
inline constexpr uint32_t kOverrideDurationStepMs = 5000;
inline constexpr uint8_t kMinBrightnessPercent = 5;
inline constexpr uint8_t kMaxBrightnessPercent = 100;
inline constexpr uint8_t kBrightnessStepPercent = 5;

enum class PacketType : uint8_t {
  CatalogRequest = 1,
  CatalogManifest = 2,
  CatalogPageRequest = 3,
  CatalogPage = 4,
  Display = 5,
  Clear = 6,
  GetStatus = 7,
  Ack = 8,
  Status = 9,
  SetBrightness = 10,
};

enum class AckResult : uint8_t {
  Applied = 0,
  InvalidPreset = 1,
  InvalidDuration = 2,
  InvalidCatalogRevision = 3,
  MalformedRequest = 4,
  InvalidBrightness = 5,
};

struct CatalogEntrySummary {
  uint16_t id = 0;
  uint32_t default_duration_ms = 0;
  std::array<char, kMaxLabelBytes + 1> label{};
};

struct RearState {
  uint32_t catalog_revision = 0;
  bool active = false;
  uint16_t preset_id = 0;
  uint32_t total_duration_ms = 0;
  uint32_t remaining_ms = 0;
  uint8_t brightness_percent = 0;
};

struct Packet {
  PacketType type = PacketType::GetStatus;
  uint32_t sequence = 0;
  uint32_t catalog_revision = 0;
  uint16_t total_entries = 0;
  uint8_t total_pages = 0;
  uint8_t page_index = 0;
  uint8_t entry_count = 0;
  std::array<CatalogEntrySummary, kMessagesPerPage> entries{};
  uint16_t preset_id = 0;
  uint32_t duration_override_ms = 0;
  uint8_t brightness_percent = 0;
  AckResult ack_result = AckResult::MalformedRequest;
  PacketType acknowledged_type = PacketType::GetStatus;
  RearState state{};
};

enum class DecodeError : uint8_t {
  None = 0,
  TooShort,
  TooLong,
  InvalidMagic,
  UnsupportedVersion,
  UnknownType,
  InvalidLength,
  InvalidPayload,
};

struct DecodeResult {
  DecodeError error = DecodeError::None;
  Packet packet{};

  explicit operator bool() const { return error == DecodeError::None; }
};

// Small fixed duplicate window for idempotent Display/Clear handling. It uses
// no dynamic allocation and intentionally remembers the result as well as the
// sequence so a retry receives the same acknowledgement outcome.
class ProcessedCommandWindow {
 public:
  bool find(uint32_t sequence, PacketType type, AckResult& result) const;
  void remember(uint32_t sequence, PacketType type, AckResult result);

 private:
  struct Entry {
    bool valid = false;
    uint32_t sequence = 0;
    PacketType type = PacketType::GetStatus;
    AckResult result = AckResult::MalformedRequest;
  };

  std::array<Entry, 8> entries_{};
  uint8_t cursor_ = 0;
};

size_t encodePacket(const Packet& packet, uint8_t* output, size_t capacity);
DecodeResult decodePacket(const uint8_t* data, size_t length);
uint8_t pageCountFor(uint16_t entry_count);
uint8_t wrappedIndex(uint8_t current, int delta, uint8_t count);
bool isValidDurationOverride(uint32_t duration_ms);
bool isValidBrightness(uint8_t brightness_percent);
uint32_t effectiveDuration(uint32_t default_duration_ms,
                           uint32_t duration_override_ms);
uint32_t remainingDuration(uint32_t now_ms, uint32_t started_ms,
                           uint32_t total_duration_ms);

}  // namespace rr
