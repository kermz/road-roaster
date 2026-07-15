#pragma once

#include <cstddef>
#include <cstdint>

#include "rr/protocol.hpp"

namespace rr::rear {

enum class AnimationKind : uint8_t {
  Static = 0,
  Pulse = 1,
  Marquee = 2,
  ColorCycle = 3,
};

struct RgbColor {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

struct PresetDefinition {
  uint16_t id;
  const char* label;
  const char* matrix_text;
  AnimationKind animation;
  RgbColor color;
  uint32_t default_duration_ms;
};

const PresetDefinition* presets();
size_t presetCount();
const PresetDefinition* findPreset(uint16_t id);
bool validateCatalog();
uint32_t catalogRevision();
CatalogEntrySummary summaryAt(size_t index);

}  // namespace rr::rear

