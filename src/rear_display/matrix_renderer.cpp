#include "matrix_renderer.hpp"

#include <algorithm>
#include <cstring>

namespace rr::rear {
namespace {

constexpr uint16_t kPanelWidth = 64;
constexpr uint16_t kPanelHeight = 32;
constexpr uint16_t kPanelChain = 1;
constexpr uint32_t kFrameIntervalMs = 33;

constexpr HUB75_I2S_CFG::i2s_pins kWavesharePins = {
    4,   // R1
    5,   // G1
    6,   // B1
    7,   // R2
    15,  // G2
    16,  // B2
    18,  // A
    8,   // B
    3,   // C
    42,  // D
    9,   // E
    40,  // LAT
    2,   // OE
    41,  // CLK
};

}  // namespace

bool MatrixRenderer::begin(uint8_t brightness_percent) {
  HUB75_I2S_CFG config(kPanelWidth, kPanelHeight, kPanelChain, kWavesharePins);
  config.gpio.e = 9;
  config.clkphase = false;
  config.driver = HUB75_I2S_CFG::SHIFTREG;
  config.double_buff = true;

  display_ = std::make_unique<MatrixPanel_I2S_DMA>(config);
  if (!display_->begin()) {
    display_.reset();
    return false;
  }
  setBrightness(brightness_percent);
  display_->clearScreen();
  display_->flipDMABuffer();
  display_->clearScreen();
  display_->flipDMABuffer();
  return true;
}

void MatrixRenderer::setBrightness(uint8_t brightness_percent) {
  brightness_percent_ =
      std::clamp<uint8_t>(brightness_percent, kMinBrightnessPercent,
                          kMaxBrightnessPercent);
  if (display_) {
    const uint8_t driver_brightness = static_cast<uint8_t>(
        (static_cast<uint16_t>(brightness_percent_) * 255U) / 100U);
    display_->setBrightness8(driver_brightness);
  }
}

void MatrixRenderer::show(const PresetDefinition* preset, uint32_t started_ms) {
  preset_ = preset;
  started_ms_ = started_ms;
  last_frame_ms_ = started_ms - kFrameIntervalMs;
}

void MatrixRenderer::clear() {
  preset_ = nullptr;
  if (!display_) return;
  display_->clearScreen();
  display_->flipDMABuffer();
  display_->clearScreen();
}

void MatrixRenderer::tick(uint32_t now_ms) {
  if (preset_ == nullptr || !display_ || now_ms - last_frame_ms_ < kFrameIntervalMs) {
    return;
  }
  last_frame_ms_ = now_ms;
  renderFrame(now_ms);
}

void MatrixRenderer::renderFrame(uint32_t now_ms) {
  const uint32_t elapsed = now_ms - started_ms_;
  display_->clearScreen();
  display_->setTextWrap(false);

  uint16_t color = display_->color565(preset_->color.red, preset_->color.green,
                                      preset_->color.blue);
  switch (preset_->animation) {
    case AnimationKind::Static:
      drawCentered(preset_->matrix_text, color, 1);
      break;
    case AnimationKind::Pulse: {
      const uint16_t phase = static_cast<uint16_t>(elapsed % 1600);
      const uint16_t triangle = phase < 800 ? phase : 1600 - phase;
      const uint16_t scale = 55 + (triangle * 200 / 800);
      color = display_->color565(preset_->color.red * scale / 255,
                                 preset_->color.green * scale / 255,
                                 preset_->color.blue * scale / 255);
      drawCentered(preset_->matrix_text, color, 1);
      break;
    }
    case AnimationKind::Marquee: {
      display_->setTextSize(2);
      int16_t x1 = 0;
      int16_t y1 = 0;
      uint16_t width = 0;
      uint16_t height = 0;
      display_->getTextBounds(preset_->matrix_text, 0, 0, &x1, &y1, &width,
                              &height);
      const uint32_t travel = static_cast<uint32_t>(width) + kPanelWidth;
      const uint32_t phase = (elapsed / 35U) % travel;
      const int32_t x = static_cast<int32_t>(kPanelWidth) -
                        static_cast<int32_t>(phase);
      const int16_t y = (static_cast<int16_t>(kPanelHeight) - height) / 2;
      display_->setCursor(static_cast<int16_t>(x), y);
      display_->setTextColor(color);
      display_->print(preset_->matrix_text);
      break;
    }
    case AnimationKind::ColorCycle:
      drawCentered(preset_->matrix_text,
                   colorWheel(static_cast<uint8_t>(elapsed / 12)), 1);
      break;
  }
  display_->flipDMABuffer();
}

void MatrixRenderer::drawCentered(const char* text, uint16_t color,
                                  uint8_t text_size) {
  display_->setTextSize(text_size);
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  display_->getTextBounds(text, 0, 0, &x1, &y1, &width, &height);
  if (width > kPanelWidth && text_size > 1) {
    drawCentered(text, color, text_size - 1);
    return;
  }
  const int16_t x = std::max<int16_t>(0, (kPanelWidth - width) / 2);
  const int16_t y = std::max<int16_t>(0, (kPanelHeight - height) / 2);
  display_->setCursor(x, y);
  display_->setTextColor(color);
  display_->print(text);
}

uint16_t MatrixRenderer::colorWheel(uint8_t position) const {
  if (position < 85) {
    return display_->color565(position * 3, 255 - position * 3, 0);
  }
  if (position < 170) {
    position -= 85;
    return display_->color565(255 - position * 3, 0, position * 3);
  }
  position -= 170;
  return display_->color565(0, position * 3, 255 - position * 3);
}

}  // namespace rr::rear
