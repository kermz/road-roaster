#include "knob_board/knob_board.hpp"

#include <Arduino.h>
#include <lvgl.h>

#include <algorithm>
#include <atomic>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "knob_board/panel_driver.h"
#include "knob_board/panel_init.h"

namespace knob_board {
namespace {

constexpr spi_host_device_t kLcdHost = SPI2_HOST;
constexpr int kLcdCs = 14;
constexpr int kLcdClock = 13;
constexpr int kLcdData0 = 15;
constexpr int kLcdData1 = 16;
constexpr int kLcdData2 = 17;
constexpr int kLcdData3 = 18;
constexpr int kLcdReset = 21;
constexpr int kBacklight = 47;
constexpr int kTouchSda = 11;
constexpr int kTouchScl = 12;
constexpr uint8_t kTouchAddress = 0x15;
constexpr uint8_t kHapticAddress = 0x5A;
constexpr int kEncoderA = 8;
constexpr int kEncoderB = 7;
constexpr int kLvglBufferRows = 36;

esp_lcd_panel_handle_t panel = nullptr;
lv_disp_draw_buf_t draw_buffer{};
lv_disp_drv_t display_driver{};
lv_indev_drv_t touch_driver{};
esp_timer_handle_t tick_timer = nullptr;
esp_timer_handle_t encoder_timer = nullptr;
std::atomic<int> encoder_delta{0};
std::atomic<uint16_t> pending_flushes{0};
std::atomic<bool> complete_frame_submitted{false};
std::atomic<bool> first_frame_ready{false};
bool backlight_enabled = false;
bool haptic_ready = false;
uint8_t requested_backlight_percent = 80;
uint32_t last_haptic_ms = 0;
uint8_t last_encoder_a = 1;
uint8_t last_encoder_b = 1;
uint8_t encoder_a_low_ticks = 0;
uint8_t encoder_b_low_ticks = 0;

bool flushFinished(esp_lcd_panel_io_handle_t,
                   esp_lcd_panel_io_event_data_t*, void* user_context) {
  const uint16_t previous =
      pending_flushes.fetch_sub(1, std::memory_order_acq_rel);
  if (previous == 1 && complete_frame_submitted.exchange(
                           false, std::memory_order_acq_rel)) {
    first_frame_ready.store(true, std::memory_order_release);
  }
  lv_disp_flush_ready(static_cast<lv_disp_drv_t*>(user_context));
  return false;
}

void flush(lv_disp_drv_t* driver, const lv_area_t* area,
           lv_color_t* pixels) {
  auto handle = static_cast<esp_lcd_panel_handle_t>(driver->user_data);
  pending_flushes.fetch_add(1, std::memory_order_relaxed);
  if (lv_disp_flush_is_last(driver)) {
    complete_frame_submitted.store(true, std::memory_order_release);
  }
  if (esp_lcd_panel_draw_bitmap(handle, area->x1, area->y1, area->x2 + 1,
                                area->y2 + 1, pixels) != ESP_OK) {
    pending_flushes.fetch_sub(1, std::memory_order_relaxed);
    lv_disp_flush_ready(driver);
  }
}

void roundArea(lv_disp_drv_t*, lv_area_t* area) {
  area->x1 = (area->x1 >> 1) << 1;
  area->y1 = (area->y1 >> 1) << 1;
  area->x2 = ((area->x2 >> 1) << 1) + 1;
  area->y2 = ((area->y2 >> 1) << 1) + 1;
}

bool readTouch(uint16_t& x, uint16_t& y) {
  uint8_t start_register = 0;
  uint8_t bytes[7]{};
  const esp_err_t result = i2c_master_write_read_device(
      I2C_NUM_0, kTouchAddress, &start_register, 1, bytes, sizeof(bytes),
      pdMS_TO_TICKS(10));
  if (result != ESP_OK || bytes[2] == 0) return false;
  x = (static_cast<uint16_t>(bytes[3] & 0x0f) << 8) | bytes[4];
  y = (static_cast<uint16_t>(bytes[5] & 0x0f) << 8) | bytes[6];
  return true;
}

void touchRead(lv_indev_drv_t*, lv_indev_data_t* data) {
  uint16_t x = 0;
  uint16_t y = 0;
  if (!readTouch(x, y)) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  data->point.x = std::min<uint16_t>(x, kDisplayWidth - 1);
  data->point.y = std::min<uint16_t>(y, kDisplayHeight - 1);
  data->state = LV_INDEV_STATE_PRESSED;
}

void advanceLvgl(void*) { lv_tick_inc(2); }

void sampleEncoder(void*) {
  // This board does not present a conventional quadrature sequence to the
  // S3. GPIO 8 and 7 produce separate active-low direction pulses. This
  // follows Waveshare's encoder demo while keeping the result thread-safe.
  const uint8_t current_a =
      gpio_get_level(static_cast<gpio_num_t>(kEncoderA));
  const uint8_t current_b =
      gpio_get_level(static_cast<gpio_num_t>(kEncoderB));

  auto process_direction = [](uint8_t current, uint8_t& previous,
                              uint8_t& low_ticks, int direction) {
    if (current == 0) {
      if (low_ticks < 3) ++low_ticks;
    } else {
      if (previous == 0 && low_ticks >= 2) {
        encoder_delta.fetch_add(direction, std::memory_order_relaxed);
      }
      low_ticks = 0;
    }
    previous = current;
  };

  process_direction(current_a, last_encoder_a, encoder_a_low_ticks, 1);
  process_direction(current_b, last_encoder_b, encoder_b_low_ticks, -1);
}

bool beginTouch() {
  i2c_config_t config{};
  config.mode = I2C_MODE_MASTER;
  config.sda_io_num = static_cast<gpio_num_t>(kTouchSda);
  config.scl_io_num = static_cast<gpio_num_t>(kTouchScl);
  config.sda_pullup_en = GPIO_PULLUP_ENABLE;
  config.scl_pullup_en = GPIO_PULLUP_ENABLE;
  config.master.clk_speed = 300000;
  config.clk_flags = 0;
  if (i2c_param_config(I2C_NUM_0, &config) != ESP_OK ||
      i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) {
    return false;
  }
  uint8_t normal_mode[2] = {0x00, 0x00};
  return i2c_master_write_to_device(I2C_NUM_0, kTouchAddress, normal_mode,
                                    sizeof(normal_mode),
                                    pdMS_TO_TICKS(100)) == ESP_OK;
}

bool writeI2cRegister(uint8_t address, uint8_t reg, uint8_t value) {
  const uint8_t data[] = {reg, value};
  return i2c_master_write_to_device(I2C_NUM_0, address, data, sizeof(data),
                                    pdMS_TO_TICKS(20)) == ESP_OK;
}

bool beginHaptics() {
  // DRV2605 internal-trigger mode, ERM effects library 1.
  return writeI2cRegister(kHapticAddress, 0x01, 0x00) &&
         writeI2cRegister(kHapticAddress, 0x03, 0x01) &&
         writeI2cRegister(kHapticAddress, 0x04, 0x00) &&
         writeI2cRegister(kHapticAddress, 0x05, 0x00);
}

bool beginEncoder() {
  gpio_config_t config{};
  config.pin_bit_mask = (1ULL << kEncoderA) | (1ULL << kEncoderB);
  config.mode = GPIO_MODE_INPUT;
  config.pull_up_en = GPIO_PULLUP_ENABLE;
  if (gpio_config(&config) != ESP_OK) return false;
  last_encoder_a = gpio_get_level(static_cast<gpio_num_t>(kEncoderA));
  last_encoder_b = gpio_get_level(static_cast<gpio_num_t>(kEncoderB));
  encoder_a_low_ticks = 0;
  encoder_b_low_ticks = 0;
  const esp_timer_create_args_t args = {
      .callback = sampleEncoder,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "knob_encoder",
      .skip_unhandled_events = true,
  };
  return esp_timer_create(&args, &encoder_timer) == ESP_OK &&
         esp_timer_start_periodic(encoder_timer, 3000) == ESP_OK;
}

bool beginBacklight() {
  ledc_timer_config_t timer{};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_8_BIT;
  timer.timer_num = LEDC_TIMER_3;
  timer.freq_hz = 5000;
  timer.clk_cfg = LEDC_AUTO_CLK;
  ledc_channel_config_t channel{};
  channel.gpio_num = kBacklight;
  channel.speed_mode = LEDC_LOW_SPEED_MODE;
  channel.channel = LEDC_CHANNEL_1;
  channel.intr_type = LEDC_INTR_DISABLE;
  channel.timer_sel = LEDC_TIMER_3;
  channel.duty = 0;
  channel.hpoint = 0;
  return ledc_timer_config(&timer) == ESP_OK &&
         ledc_channel_config(&channel) == ESP_OK;
}

bool beginDisplay() {
  const spi_bus_config_t bus_config = KNOB_LCD_PANEL_BUS_QSPI_CONFIG(
      kLcdClock, kLcdData0, kLcdData1, kLcdData2, kLcdData3,
      kDisplayWidth * kDisplayHeight * sizeof(lv_color_t));
  if (spi_bus_initialize(kLcdHost, &bus_config, SPI_DMA_CH_AUTO) != ESP_OK) {
    return false;
  }

  lv_disp_drv_init(&display_driver);
  esp_lcd_panel_io_spi_config_t io_config = KNOB_LCD_PANEL_IO_QSPI_CONFIG(
      kLcdCs, flushFinished, &display_driver);
  esp_lcd_panel_io_handle_t panel_io = nullptr;
  if (esp_lcd_new_panel_io_spi(
          static_cast<esp_lcd_spi_bus_handle_t>(kLcdHost), &io_config,
          &panel_io) != ESP_OK) {
    return false;
  }

  size_t command_count = 0;
  const knob_lcd_init_cmd_t* commands = knob_lcd_init_commands(&command_count);
  knob_lcd_vendor_config_t vendor_config{};
  vendor_config.init_cmds = commands;
  vendor_config.init_cmds_size = static_cast<uint16_t>(command_count);
  vendor_config.flags.use_qspi_interface = 1;
  esp_lcd_panel_dev_config_t panel_config{};
  panel_config.reset_gpio_num = kLcdReset;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_config.bits_per_pixel = 16;
  panel_config.vendor_config = &vendor_config;
  if (knob_lcd_new_panel(panel_io, &panel_config, &panel) != ESP_OK ||
      esp_lcd_panel_reset(panel) != ESP_OK ||
      esp_lcd_panel_init(panel) != ESP_OK) {
    return false;
  }

  lv_color_t* buffer1 = static_cast<lv_color_t*>(heap_caps_malloc(
      kDisplayWidth * kLvglBufferRows * sizeof(lv_color_t), MALLOC_CAP_DMA));
  lv_color_t* buffer2 = static_cast<lv_color_t*>(heap_caps_malloc(
      kDisplayWidth * kLvglBufferRows * sizeof(lv_color_t), MALLOC_CAP_DMA));
  if (buffer1 == nullptr || buffer2 == nullptr) return false;
  lv_disp_draw_buf_init(&draw_buffer, buffer1, buffer2,
                        kDisplayWidth * kLvglBufferRows);
  display_driver.hor_res = kDisplayWidth;
  display_driver.ver_res = kDisplayHeight;
  display_driver.flush_cb = flush;
  display_driver.rounder_cb = roundArea;
  display_driver.draw_buf = &draw_buffer;
  display_driver.user_data = panel;
  lv_disp_t* display = lv_disp_drv_register(&display_driver);

  lv_indev_drv_init(&touch_driver);
  touch_driver.type = LV_INDEV_TYPE_POINTER;
  touch_driver.disp = display;
  touch_driver.read_cb = touchRead;
  lv_indev_drv_register(&touch_driver);

  const esp_timer_create_args_t tick_args = {
      .callback = advanceLvgl,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "lvgl_tick",
      .skip_unhandled_events = true,
  };
  return esp_timer_create(&tick_args, &tick_timer) == ESP_OK &&
         esp_timer_start_periodic(tick_timer, 2000) == ESP_OK;
}

}  // namespace

bool begin() {
  earlyBlankDisplay();
  if (!beginBacklight()) return false;

  lv_init();
  if (!beginTouch() || !beginEncoder() || !beginDisplay()) {
    return false;
  }
  haptic_ready = beginHaptics();
  backlight_enabled = false;
  first_frame_ready.store(false, std::memory_order_relaxed);
  return true;
}

void earlyBlankDisplay() {
  gpio_reset_pin(static_cast<gpio_num_t>(kBacklight));
  gpio_set_direction(static_cast<gpio_num_t>(kBacklight), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(kBacklight), 0);

  gpio_reset_pin(static_cast<gpio_num_t>(kLcdReset));
  gpio_set_direction(static_cast<gpio_num_t>(kLcdReset), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(kLcdReset), 0);
}

void processLvgl() {
  lv_timer_handler();
  if (!backlight_enabled &&
      first_frame_ready.exchange(false, std::memory_order_acq_rel)) {
    esp_lcd_panel_disp_on_off(panel, true);
    backlight_enabled = true;
    setBacklight(requested_backlight_percent);
  }
}

int consumeEncoderDelta() {
  return encoder_delta.exchange(0, std::memory_order_relaxed);
}

void setBacklight(uint8_t percent) {
  requested_backlight_percent = std::clamp<uint8_t>(percent, 5, 100);
  if (!backlight_enabled) return;
  const uint32_t duty = requested_backlight_percent * 255U / 100U;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

void vibrate() {
  const uint32_t now_ms = millis();
  if (!haptic_ready || now_ms - last_haptic_ms < 55) return;
  last_haptic_ms = now_ms;
  // Effect 1: strong 100% click. Sequence slot 2 terminates playback.
  writeI2cRegister(kHapticAddress, 0x04, 1);
  writeI2cRegister(kHapticAddress, 0x05, 0);
  writeI2cRegister(kHapticAddress, 0x0C, 1);
}

}  // namespace knob_board
