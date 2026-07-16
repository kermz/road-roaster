#pragma once

#include <cstdint>
#include <memory>

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "message_catalog.hpp"

namespace rr::rear {

class MatrixRenderer {
 public:
  bool begin(uint8_t brightness_percent, bool flipped);
  void setBrightness(uint8_t brightness_percent);
  void setFlipped(bool flipped);
  void show(const PresetDefinition* preset, uint32_t started_ms);
  void clear();
  void tick(uint32_t now_ms);

 private:
  void renderFrame(uint32_t now_ms);
  void drawScreenSequence(const PresetDefinition& preset, uint16_t color,
                          uint32_t elapsed_ms);
  uint16_t colorWheel(uint8_t position) const;

  std::unique_ptr<MatrixPanel_I2S_DMA> display_;
  const PresetDefinition* preset_ = nullptr;
  uint32_t started_ms_ = 0;
  uint32_t last_frame_ms_ = 0;
  uint8_t brightness_percent_ = 35;
  bool flipped_ = false;
};

}  // namespace rr::rear
