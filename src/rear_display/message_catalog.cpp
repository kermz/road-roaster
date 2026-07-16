#include "message_catalog.hpp"

#include <cstring>

#include "rr/text_encoding.hpp"

namespace rr::rear {
namespace {

constexpr PresetDefinition kPresets[] = {
    {1, "Sell it", {{"SELL IT"}}, AnimationKind::ColorCycle, {255, 255, 255},
     kDefaultDurationMs},
    {2, "Thanks", {{"Thanks", "Aitäh"}}, AnimationKind::Static, {255, 0, 0},
     kDefaultDurationMs},
    {3, "Sorry", {{"Sorry", "Vabandust"}}, AnimationKind::Static,
     {50, 220, 255},
     kDefaultDurationMs},
    {4, "Wanna race?", {{"You seem fast", "prove it"}},
     AnimationKind::Static, {255, 255, 255},
     kDefaultDurationMs},
    {5, "Koer oled?", {{"KOER OLED?", "HOIA PIKIVAHET"}},
     AnimationKind::Static, {100, 255, 120},
     kDefaultDurationMs},
    {6, "Maantee tont", {{"REASTU KAUGEMAL", "TONT"}},
     AnimationKind::Static, {255, 80, 80},
     kDefaultDurationMs},
    {7, "Reguleeri tulesi", {{"Reguleeri", "esitulesi!"}},
     AnimationKind::Static, {255, 255, 255},
     kDefaultDurationMs},
    {8, "Turvaline mooduda", {{"<- Sõida mööda!"}},
     AnimationKind::ColorCycle, {255, 255, 255},
     kDefaultDurationMs},
    {9, "Nice car", {{"Nice car", "Äge auto"}}, AnimationKind::Static,
     {255, 220, 40},
     kDefaultDurationMs},
    {10, "Message 10", {{"MESSAGE 10"}}, AnimationKind::Pulse,
     {80, 180, 255},
     kDefaultDurationMs},
    {11, "Message 11", {{"MESSAGE 11"}}, AnimationKind::Static,
     {255, 120, 40},
     kDefaultDurationMs},
    {12, "Message 12", {{"MESSAGE 12"}}, AnimationKind::ColorCycle,
     {255, 255, 255},
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
