#pragma once

#include <cstdint>

namespace rr::rear::config {

// Start conservatively for bench power supplies. Runtime brightness is stored
// as a user-facing percentage and mapped to the HUB75 driver's 0..255 range.
inline constexpr uint8_t kDefaultBrightnessPercent = 35;

}  // namespace rr::rear::config
