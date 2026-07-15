/*
 * QSPI panel adapter derived from Waveshare's ESP32-S3-Knob-Touch-LCD-1.8
 * Arduino example and Espressif's Apache-2.0 esp_lcd panel driver.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_lcd_panel_vendor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int cmd;
  const void* data;
  size_t data_bytes;
  unsigned int delay_ms;
} knob_lcd_init_cmd_t;

typedef struct {
  const knob_lcd_init_cmd_t* init_cmds;
  uint16_t init_cmds_size;
  struct {
    unsigned int use_qspi_interface : 1;
  } flags;
} knob_lcd_vendor_config_t;

esp_err_t knob_lcd_new_panel(const esp_lcd_panel_io_handle_t io,
                             const esp_lcd_panel_dev_config_t* panel_config,
                             esp_lcd_panel_handle_t* returned_panel);

#define KNOB_LCD_PANEL_BUS_QSPI_CONFIG(sclk, d0, d1, d2, d3, max_transfer) \
  {                                                                        \
      .data0_io_num = d0, .data1_io_num = d1, .sclk_io_num = sclk,         \
      .data2_io_num = d2, .data3_io_num = d3,                              \
      .max_transfer_sz = max_transfer,                                     \
  }

#define KNOB_LCD_PANEL_IO_QSPI_CONFIG(cs, callback, context)            \
  {                                                                     \
      .cs_gpio_num = cs, .dc_gpio_num = -1, .spi_mode = 0,              \
      .pclk_hz = 40 * 1000 * 1000, .trans_queue_depth = 10,             \
      .on_color_trans_done = callback, .user_ctx = context,             \
      .lcd_cmd_bits = 32, .lcd_param_bits = 8, .flags = {.quad_mode = true}, \
  }

#ifdef __cplusplus
}
#endif
