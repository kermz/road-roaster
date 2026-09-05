#include "matrix_renderer.hpp"

#include <algorithm>

#include "rr/text_encoding.hpp"
#include "spleen_bitmaps.hpp"
#include "config/presentation.hpp"

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

bool MatrixRenderer::begin(uint8_t brightness_percent, bool flipped) {
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
  setFlipped(flipped);
  display_->clearScreen();
  display_->flipDMABuffer();
  display_->clearScreen();
  display_->flipDMABuffer();
  return true;
}

void MatrixRenderer::setFlipped(bool flipped) {
  if (flipped_ != flipped) have_rendered_frame_ = false;
  flipped_ = flipped;
  if (display_) display_->setRotation(flipped ? 2 : 0);
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
  have_rendered_frame_ = false;
  screen_count_ = 0;
  cycle_duration_ms_ = 0;
  if (preset == nullptr || !display_) return;
  display_->setTextWrap(false);
  // Encode and fit once per activation, not on every animation frame.
  for (const char* text : preset->matrix_screens) {
    if (text == nullptr) break;
    auto& screen = screens_[screen_count_++];
    rr::encodeLatin1(text, screen.text.data(), screen.text.size());
    const auto metrics = measureWord(display_.get(), screen.text.data());
    screen.font = metrics.font;
    screen.x1 = metrics.x1;
    screen.y1 = metrics.y1;
    screen.width = metrics.width;
    screen.height = metrics.height;
    screen.duration_ms = screen.width <= kDisplayWidth
        ? kWordHoldMs + kWordGapMs
        : (static_cast<uint32_t>(screen.width) + kDisplayWidth) *
              kScrollStepMs + kWordGapMs;
    cycle_duration_ms_ += screen.duration_ms;
  }
}

void MatrixRenderer::clear() {
  preset_ = nullptr;
  have_rendered_frame_ = false;
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
}

void MatrixRenderer::drawScreenSequence(const PresetDefinition&,
                                        uint16_t color,
                                        uint32_t elapsed_ms) {
  if (cycle_duration_ms_ == 0) return;
  uint32_t phase_ms = elapsed_ms % cycle_duration_ms_;
  uint8_t index = 0;
  while (index + 1 < screen_count_ && phase_ms >= screens_[index].duration_ms) {
    phase_ms -= screens_[index++].duration_ms;
  }
  const auto& screen = screens_[index];
  bool visible = true;
  int16_t x = 0;
  if (screen.width <= kDisplayWidth) {
    visible = screen_count_ == 1 || phase_ms < kWordHoldMs;
    x = (static_cast<int16_t>(kDisplayWidth) - screen.width) / 2;
  } else {
    visible = phase_ms < screen.duration_ms - kWordGapMs;
    x = static_cast<int16_t>(kDisplayWidth) -
        static_cast<int16_t>(phase_ms / kScrollStepMs);
  }

  // DMA keeps scanning the last frame. Only submit changed visual content;
  // timing and animation sampling remain exactly as before.
  if (have_rendered_frame_ && visible == last_visible_ &&
      (!visible || (index == last_screen_ && x == last_x_ &&
                    color == last_color_))) return;
  display_->clearScreen();
  if (visible) {
    const WordMetrics metrics{screen.font, screen.x1, screen.y1,
                              screen.width, screen.height};
    drawWord(display_.get(), screen.text.data(), x, metrics, color);
  }
  display_->flipDMABuffer();
  have_rendered_frame_ = true;
  last_visible_ = visible;
  last_screen_ = index;
  last_x_ = x;
  last_color_ = color;
}

uint16_t MatrixRenderer::colorWheel(uint8_t position) const {
  using namespace presentation::matrix;
  auto blend = [this](RgbColor from, RgbColor to, uint8_t step) {
    auto channel = [step](uint8_t a, uint8_t b) -> uint8_t {
      return (static_cast<uint16_t>(a) * (85 - step) +
              static_cast<uint16_t>(b) * step) / 85;
    };
    return display_->color565(channel(from.red, to.red),
                              channel(from.green, to.green),
                              channel(from.blue, to.blue));
  };
  if (position < 85) {
    return blend(kCycleGreen, kCycleRed, position);
  }
  if (position < 170) {
    position -= 85;
    return blend(kCycleRed, kCycleBlue, position);
  }
  position -= 170;
  return blend(kCycleBlue, kCycleGreen, position);
}

}  // namespace rr::rear
