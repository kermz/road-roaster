#include "message_catalog.hpp"

#include <cstring>

#include "rr/text_encoding.hpp"
#include "config/presentation.hpp"

namespace rr::rear {
namespace {

using presentation::matrix::kPresets;

uint32_t fnvByte(uint32_t hash, uint8_t value) {
  return (hash ^ value) * 16777619UL;
}

uint32_t fnvU16(uint32_t hash, uint16_t value) {
  hash = fnvByte(hash, static_cast<uint8_t>(value));
  return fnvByte(hash, static_cast<uint8_t>(value >> 8));
}

uint32_t fnvU32(uint32_t hash, uint32_t value) {
  for (uint8_t byte = 0; byte < 4; ++byte) {
    hash = fnvByte(hash, static_cast<uint8_t>(value >> (byte * 8)));
  }
  return hash;
}

uint32_t fnvString(uint32_t hash, const char* value) {
  while (value != nullptr && *value != '\0') {
    hash = fnvByte(hash, static_cast<uint8_t>(*value++));
  }
  return fnvByte(hash, 0);
}

}  // namespace

const PresetDefinition* presets() { return kPresets; }

size_t presetCount() { return sizeof(kPresets) / sizeof(kPresets[0]); }

const PresetDefinition* findPreset(uint16_t id) {
  for (const auto& preset : kPresets) {
    if (preset.id == id) return &preset;
  }
  return nullptr;
}

bool validateCatalog() {
  return validateCatalog(kPresets, presetCount());
}

bool validateCatalog(const PresetDefinition* definitions, size_t count) {
  if (definitions == nullptr || count == 0 || count > kMaxCatalogEntries) return false;
  for (size_t index = 0; index < count; ++index) {
    const auto& candidate = definitions[index];
    const bool known_animation =
        candidate.animation == AnimationKind::Static ||
        candidate.animation == AnimationKind::Pulse ||
        candidate.animation == AnimationKind::ColorCycle;
    if (candidate.id == 0 || candidate.label == nullptr ||
        *candidate.label == '\0' ||
        std::strlen(candidate.label) > kMaxLabelBytes ||
        !rr::isAscii(candidate.label) ||
        candidate.matrix_screens[0] == nullptr ||
        !known_animation ||
        candidate.default_duration_ms == 0) {
      return false;
    }
    bool reached_end = false;
    for (const char* screen : candidate.matrix_screens) {
      if (screen == nullptr) {
        reached_end = true;
        continue;
      }
      if (reached_end || *screen == '\0' || !rr::isValidUtf8(screen) ||
          std::strlen(screen) > kMaxMatrixTextBytes) {
        return false;
      }
    }
    for (size_t other = index + 1; other < count; ++other) {
      if (candidate.id == definitions[other].id) return false;
    }
  }
  return true;
}

uint32_t catalogRevision() {
  uint32_t hash = 2166136261UL;
  // Cycle colors are rear-owned presentation data too. Include them so changing
  // the shared palette follows the same resynchronization path as preset RGB.
  for (const auto color : {presentation::matrix::kCycleGreen,
                           presentation::matrix::kCycleRed,
                           presentation::matrix::kCycleBlue}) {
    hash = fnvByte(hash, color.red);
    hash = fnvByte(hash, color.green);
    hash = fnvByte(hash, color.blue);
  }
  for (const auto& preset : kPresets) {
    hash = fnvU16(hash, preset.id);
    hash = fnvString(hash, preset.label);
    for (const char* screen : preset.matrix_screens) {
      hash = fnvString(hash, screen);
    }
    hash = fnvByte(hash, static_cast<uint8_t>(preset.animation));
    hash = fnvByte(hash, preset.color.red);
    hash = fnvByte(hash, preset.color.green);
    hash = fnvByte(hash, preset.color.blue);
    hash = fnvU32(hash, preset.default_duration_ms);
  }
  return hash;
}

CatalogEntrySummary summaryAt(size_t index) {
  CatalogEntrySummary summary;
  if (index >= presetCount()) return summary;
  const auto& preset = kPresets[index];
  summary.id = preset.id;
  summary.default_duration_ms = preset.default_duration_ms;
  std::strncpy(summary.label.data(), preset.label, kMaxLabelBytes);
  summary.label[kMaxLabelBytes] = '\0';
  return summary;
}

}  // namespace rr::rear
