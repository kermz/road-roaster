#include "message_catalog.hpp"

#include <cstring>

namespace rr::rear {
namespace {

constexpr PresetDefinition kPresets[] = {
    {1, "Message 01", "MESSAGE 01", AnimationKind::Static, {255, 255, 255},
     kDefaultDurationMs},
    {2, "Message 02", "MESSAGE 02", AnimationKind::Pulse, {255, 180, 20},
     kDefaultDurationMs},
    {3, "Message 03", "MESSAGE 03", AnimationKind::Marquee, {50, 220, 255},
     kDefaultDurationMs},
    {4, "Message 04", "MESSAGE 04", AnimationKind::ColorCycle, {255, 255, 255},
     kDefaultDurationMs},
    {5, "Message 05", "MESSAGE 05", AnimationKind::Static, {100, 255, 120},
     kDefaultDurationMs},
    {6, "Message 06", "MESSAGE 06", AnimationKind::Pulse, {255, 80, 80},
     kDefaultDurationMs},
    {7, "Message 07", "MESSAGE 07", AnimationKind::Marquee, {255, 255, 255},
     kDefaultDurationMs},
    {8, "Message 08", "MESSAGE 08", AnimationKind::ColorCycle, {255, 255, 255},
     kDefaultDurationMs},
    {9, "Message 09", "MESSAGE 09", AnimationKind::Static, {255, 220, 40},
     kDefaultDurationMs},
    {10, "Message 10", "MESSAGE 10", AnimationKind::Pulse, {80, 180, 255},
     kDefaultDurationMs},
    {11, "Message 11", "MESSAGE 11", AnimationKind::Marquee, {255, 120, 40},
     kDefaultDurationMs},
    {12, "Message 12", "MESSAGE 12", AnimationKind::ColorCycle, {255, 255, 255},
     kDefaultDurationMs},
};

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
  if (presetCount() == 0 || presetCount() > kMaxCatalogEntries) return false;
  for (size_t index = 0; index < presetCount(); ++index) {
    const auto& candidate = kPresets[index];
    const size_t matrix_text_length = candidate.matrix_text == nullptr
                                          ? 0
                                          : std::strlen(candidate.matrix_text);
    const bool known_animation =
        candidate.animation == AnimationKind::Static ||
        candidate.animation == AnimationKind::Pulse ||
        candidate.animation == AnimationKind::Marquee ||
        candidate.animation == AnimationKind::ColorCycle;
    if (candidate.id == 0 || candidate.label == nullptr ||
        candidate.matrix_text == nullptr ||
        std::strlen(candidate.label) > kMaxLabelBytes ||
        matrix_text_length == 0 || matrix_text_length > kMaxMatrixTextBytes ||
        !known_animation ||
        candidate.default_duration_ms == 0) {
      return false;
    }
    for (size_t other = index + 1; other < presetCount(); ++other) {
      if (candidate.id == kPresets[other].id) return false;
    }
  }
  return true;
}

uint32_t catalogRevision() {
  uint32_t hash = 2166136261UL;
  for (const auto& preset : kPresets) {
    hash = fnvU16(hash, preset.id);
    hash = fnvString(hash, preset.label);
    hash = fnvString(hash, preset.matrix_text);
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
