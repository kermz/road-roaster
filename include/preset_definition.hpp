#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rr::rear {

inline constexpr size_t kMaxMatrixTextBytes = 512;
inline constexpr size_t kMaxMatrixScreens = 4;

enum class AnimationKind : uint8_t {
  Static = 0,
  Pulse = 1,
  ColorCycle = 2,
};

struct RgbColor {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

struct PresetDefinition {
  uint16_t id;
  const char* label;
  std::array<const char*, kMaxMatrixScreens> matrix_screens;
  AnimationKind animation;
  RgbColor color;
  uint32_t default_duration_ms;
};

}  // namespace rr::rear
