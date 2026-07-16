#include "matrix_renderer.hpp"

#include <algorithm>

#include "rr/text_encoding.hpp"
#include "spleen_bitmaps.hpp"

namespace rr::rear {
namespace {

constexpr uint16_t kPanelWidth = 64;
constexpr uint16_t kPanelHeight = 32;
constexpr uint16_t kPanelChain = 2;
constexpr uint16_t kDisplayWidth = kPanelWidth * kPanelChain;
constexpr uint16_t kDisplayHeight = kPanelHeight;
constexpr uint32_t kFrameIntervalMs = 67;
constexpr uint32_t kWordHoldMs = 750;
constexpr uint32_t kWordGapMs = 100;
constexpr uint32_t kScrollStepMs = 35;

struct WordMetrics {
  const GFXfont* font;
  int16_t x1;
  int16_t y1;
  uint16_t width;
  uint16_t height;
};

WordMetrics measureWord(MatrixPanel_I2S_DMA* display, const char* word) {
  WordMetrics scrolling_fallback{};
  for (size_t index = 0; index < matrix_font::kStrikeCount; ++index) {
    const GFXfont* font = matrix_font::kStrikes[index];
    display->setFont(font);
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    display->getTextBounds(word, 0, 0, &x1, &y1, &width, &height);
    const WordMetrics candidate{font, x1, y1, width, height};
    if (height <= kDisplayHeight && scrolling_fallback.font == nullptr) {
      scrolling_fallback = candidate;
    }
    if (width <= kDisplayWidth && height <= kDisplayHeight) {
      return candidate;
    }
  }
  return scrolling_fallback.font != nullptr ? scrolling_fallback
                                             : WordMetrics{};
}

void drawWord(MatrixPanel_I2S_DMA* display, const char* word, int16_t left,
              const WordMetrics& metrics, uint16_t color) {
  if (metrics.font == nullptr) return;
  display->setFont(metrics.font);
  const int16_t top =
      (static_cast<int16_t>(kDisplayHeight) - metrics.height) / 2;
  display->setCursor(left - metrics.x1, top - metrics.y1);
  display->setTextColor(color);
  display->print(word);
}

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
    -1,  // E (unused by the 1/16-scan panel)
    40,  // LAT
    2,   // OE
    41,  // CLK
};

}  // namespace

bool MatrixRenderer::begin(uint8_t brightness_percent) {
  HUB75_I2S_CFG config(kPanelWidth, kPanelHeight, kPanelChain, kWavesharePins);
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
  if (preset_ == nullptr || !display_ ||
      now_ms - last_frame_ms_ < kFrameIntervalMs) {
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
      break;
    case AnimationKind::Pulse: {
      const uint16_t phase = static_cast<uint16_t>(elapsed % 1600);
      const uint16_t triangle = phase < 800 ? phase : 1600 - phase;
      const uint16_t scale = 55 + (triangle * 200 / 800);
      color = display_->color565(preset_->color.red * scale / 255,
                                 preset_->color.green * scale / 255,
                                 preset_->color.blue * scale / 255);
      break;
    }
    case AnimationKind::ColorCycle:
      color = colorWheel(static_cast<uint8_t>(elapsed / 12));
      break;
  }
  drawScreenSequence(*preset_, color, elapsed);
  display_->flipDMABuffer();
}

void MatrixRenderer::drawScreenSequence(const PresetDefinition& preset,
                                        uint16_t color,
                                        uint32_t elapsed_ms) {
  auto screenDuration = [](uint16_t width) -> uint32_t {
    if (width <= kDisplayWidth) return kWordHoldMs + kWordGapMs;
    return (static_cast<uint32_t>(width) + kDisplayWidth) * kScrollStepMs +
           kWordGapMs;
  };

  char encoded_screen[kMaxMatrixTextBytes + 1]{};
  uint32_t cycle_duration_ms = 0;
  size_t screen_count = 0;
  for (const char* screen : preset.matrix_screens) {
    if (screen == nullptr) break;
    rr::encodeLatin1(screen, encoded_screen, sizeof(encoded_screen));
    const WordMetrics metrics = measureWord(display_.get(), encoded_screen);
    cycle_duration_ms += screenDuration(metrics.width);
    ++screen_count;
  }
  if (cycle_duration_ms == 0) return;

  uint32_t phase_ms = elapsed_ms % cycle_duration_ms;
  for (const char* screen : preset.matrix_screens) {
    if (screen == nullptr) break;
    rr::encodeLatin1(screen, encoded_screen, sizeof(encoded_screen));
    const WordMetrics metrics = measureWord(display_.get(), encoded_screen);
    const uint32_t duration_ms = screenDuration(metrics.width);
    if (phase_ms >= duration_ms) {
      phase_ms -= duration_ms;
      continue;
    }

    int16_t x = 0;
    if (metrics.width <= kDisplayWidth) {
      // A single fitting screen is steady. Gaps are only useful as a visual
      // separator while advancing through multiple screens.
      if (screen_count > 1 && phase_ms >= kWordHoldMs) return;
      x = (static_cast<int16_t>(kDisplayWidth) - metrics.width) / 2;
    } else {
      const uint32_t scroll_duration_ms =
          (static_cast<uint32_t>(metrics.width) + kDisplayWidth) *
          kScrollStepMs;
      if (phase_ms >= scroll_duration_ms) return;
      x = static_cast<int16_t>(kDisplayWidth) -
          static_cast<int16_t>(phase_ms / kScrollStepMs);
    }
    drawWord(display_.get(), encoded_screen, x, metrics, color);
    return;
  }
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
