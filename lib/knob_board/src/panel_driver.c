/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "knob_board/panel_driver.h"

#include <stdlib.h>
#include <sys/cdefs.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_OPCODE_WRITE_CMD 0x02ULL
#define LCD_OPCODE_WRITE_COLOR 0x32ULL

static const char* TAG = "knob_lcd";

typedef struct {
  esp_lcd_panel_t base;
  esp_lcd_panel_io_handle_t io;
  int reset_gpio_num;
  int x_gap;
  int y_gap;
  uint8_t fb_bits_per_pixel;
  uint8_t madctl_val;
  uint8_t colmod_val;
  const knob_lcd_init_cmd_t* init_cmds;
  uint16_t init_cmds_size;
  struct {
    unsigned int use_qspi_interface : 1;
    unsigned int reset_level : 1;
  } flags;
} knob_panel_t;

static esp_err_t panel_del(esp_lcd_panel_t* panel);
static esp_err_t panel_reset(esp_lcd_panel_t* panel);
static esp_err_t panel_init(esp_lcd_panel_t* panel);
static esp_err_t panel_draw_bitmap(esp_lcd_panel_t* panel, int x_start,
                                   int y_start, int x_end, int y_end,
                                   const void* color_data);
static esp_err_t panel_invert_color(esp_lcd_panel_t* panel, bool invert);
static esp_err_t panel_mirror(esp_lcd_panel_t* panel, bool mirror_x,
                              bool mirror_y);
static esp_err_t panel_swap_xy(esp_lcd_panel_t* panel, bool swap_axes);
static esp_err_t panel_set_gap(esp_lcd_panel_t* panel, int x_gap, int y_gap);
static esp_err_t panel_disp_on_off(esp_lcd_panel_t* panel, bool on);

static esp_err_t tx_param(knob_panel_t* driver, int command, const void* data,
                          size_t data_size) {
  if (driver->flags.use_qspi_interface) {
    command = ((command & 0xff) << 8) | (LCD_OPCODE_WRITE_CMD << 24);
  }
  return esp_lcd_panel_io_tx_param(driver->io, command, data, data_size);
}

static esp_err_t tx_color(knob_panel_t* driver, int command, const void* data,
                          size_t data_size) {
  if (driver->flags.use_qspi_interface) {
    command = ((command & 0xff) << 8) | (LCD_OPCODE_WRITE_COLOR << 24);
  }
  return esp_lcd_panel_io_tx_color(driver->io, command, data, data_size);
}

esp_err_t knob_lcd_new_panel(const esp_lcd_panel_io_handle_t io,
                             const esp_lcd_panel_dev_config_t* panel_config,
                             esp_lcd_panel_handle_t* returned_panel) {
  ESP_RETURN_ON_FALSE(io && panel_config && returned_panel, ESP_ERR_INVALID_ARG,
                      TAG, "invalid argument");

  esp_err_t ret = ESP_OK;
  knob_panel_t* driver = calloc(1, sizeof(knob_panel_t));
  ESP_GOTO_ON_FALSE(driver, ESP_ERR_NO_MEM, error, TAG, "no panel memory");

  if (panel_config->reset_gpio_num >= 0) {
    gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << panel_config->reset_gpio_num,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_GOTO_ON_ERROR(gpio_config(&reset_config), error, TAG,
                      "reset GPIO configuration failed");
  }

  if (panel_config->rgb_ele_order == LCD_RGB_ELEMENT_ORDER_BGR) {
    driver->madctl_val = LCD_CMD_BGR_BIT;
  } else if (panel_config->rgb_ele_order != LCD_RGB_ELEMENT_ORDER_RGB) {
    ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, error, TAG,
                      "unsupported RGB element order");
  }

  switch (panel_config->bits_per_pixel) {
    case 16:
      driver->colmod_val = 0x55;
      driver->fb_bits_per_pixel = 16;
      break;
    case 18:
      driver->colmod_val = 0x66;
      driver->fb_bits_per_pixel = 18;
      break;
    case 24:
      driver->colmod_val = 0x77;
      driver->fb_bits_per_pixel = 24;
      break;
    default:
      ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, error, TAG,
                        "unsupported pixel width");
  }

  driver->io = io;
  driver->reset_gpio_num = panel_config->reset_gpio_num;
  driver->flags.reset_level = panel_config->flags.reset_active_high;
  const knob_lcd_vendor_config_t* vendor = panel_config->vendor_config;
  if (vendor) {
    driver->init_cmds = vendor->init_cmds;
    driver->init_cmds_size = vendor->init_cmds_size;
    driver->flags.use_qspi_interface = vendor->flags.use_qspi_interface;
  }

  driver->base.del = panel_del;
  driver->base.reset = panel_reset;
  driver->base.init = panel_init;
  driver->base.draw_bitmap = panel_draw_bitmap;
  driver->base.invert_color = panel_invert_color;
  driver->base.set_gap = panel_set_gap;
  driver->base.mirror = panel_mirror;
  driver->base.swap_xy = panel_swap_xy;
  driver->base.disp_on_off = panel_disp_on_off;
  *returned_panel = &driver->base;
  return ESP_OK;

error:
  if (driver) {
    if (panel_config->reset_gpio_num >= 0) {
      gpio_reset_pin(panel_config->reset_gpio_num);
    }
    free(driver);
  }
  return ret;
}

static esp_err_t panel_del(esp_lcd_panel_t* panel) {
  knob_panel_t* driver = __containerof(panel, knob_panel_t, base);
  if (driver->reset_gpio_num >= 0) gpio_reset_pin(driver->reset_gpio_num);
  free(driver);
  return ESP_OK;
}

static esp_err_t panel_reset(esp_lcd_panel_t* panel) {
  knob_panel_t* driver = __containerof(panel, knob_panel_t, base);
  if (driver->reset_gpio_num >= 0) {
    gpio_set_level(driver->reset_gpio_num, driver->flags.reset_level);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(driver->reset_gpio_num, !driver->flags.reset_level);
    vTaskDelay(pdMS_TO_TICKS(150));
  } else {
    ESP_RETURN_ON_ERROR(tx_param(driver, LCD_CMD_SWRESET, NULL, 0), TAG,
                        "software reset failed");
    vTaskDelay(pdMS_TO_TICKS(80));
  }
  return ESP_OK;
}

static esp_err_t panel_init(esp_lcd_panel_t* panel) {
  knob_panel_t* driver = __containerof(panel, knob_panel_t, base);
  ESP_RETURN_ON_ERROR(
      tx_param(driver, LCD_CMD_MADCTL, &driver->madctl_val, 1), TAG,
      "MADCTL failed");
  ESP_RETURN_ON_ERROR(
      tx_param(driver, LCD_CMD_COLMOD, &driver->colmod_val, 1), TAG,
      "COLMOD failed");

  for (uint16_t index = 0; index < driver->init_cmds_size; ++index) {
    const knob_lcd_init_cmd_t* command = &driver->init_cmds[index];
    ESP_RETURN_ON_ERROR(
        tx_param(driver, command->cmd, command->data, command->data_bytes), TAG,
        "panel initialization command failed");
    vTaskDelay(pdMS_TO_TICKS(command->delay_ms));
  }
  return ESP_OK;
}

static esp_err_t panel_draw_bitmap(esp_lcd_panel_t* panel, int x_start,
                                   int y_start, int x_end, int y_end,
                                   const void* color_data) {
  knob_panel_t* driver = __containerof(panel, knob_panel_t, base);
  ESP_RETURN_ON_FALSE(x_start < x_end && y_start < y_end,
                      ESP_ERR_INVALID_ARG, TAG, "invalid drawing area");
  x_start += driver->x_gap;
  x_end += driver->x_gap;
  y_start += driver->y_gap;
  y_end += driver->y_gap;

  const uint8_t columns[] = {x_start >> 8, x_start & 0xff, (x_end - 1) >> 8,
                             (x_end - 1) & 0xff};
  const uint8_t rows[] = {y_start >> 8, y_start & 0xff, (y_end - 1) >> 8,
                          (y_end - 1) & 0xff};
  ESP_RETURN_ON_ERROR(tx_param(driver, LCD_CMD_CASET, columns, sizeof(columns)),
                      TAG, "CASET failed");
  ESP_RETURN_ON_ERROR(tx_param(driver, LCD_CMD_RASET, rows, sizeof(rows)), TAG,
                      "RASET failed");
  const size_t length = (x_end - x_start) * (y_end - y_start) *
                        driver->fb_bits_per_pixel / 8;
  return tx_color(driver, LCD_CMD_RAMWR, color_data, length);
}

static esp_err_t panel_invert_color(esp_lcd_panel_t* panel, bool invert) {
  knob_panel_t* driver = __containerof(panel, knob_panel_t, base);
  return tx_param(driver, invert ? LCD_CMD_INVON : LCD_CMD_INVOFF, NULL, 0);
}

static esp_err_t panel_mirror(esp_lcd_panel_t* panel, bool mirror_x,
                              bool mirror_y) {
  knob_panel_t* driver = __containerof(panel, knob_panel_t, base);
  if (mirror_y) return ESP_ERR_NOT_SUPPORTED;
  if (mirror_x)
    driver->madctl_val |= BIT(6);
  else
    driver->madctl_val &= ~BIT(6);
  return tx_param(driver, LCD_CMD_MADCTL, &driver->madctl_val, 1);
}

static esp_err_t panel_swap_xy(esp_lcd_panel_t*, bool) {
  return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t panel_set_gap(esp_lcd_panel_t* panel, int x_gap, int y_gap) {
  knob_panel_t* driver = __containerof(panel, knob_panel_t, base);
  driver->x_gap = x_gap;
  driver->y_gap = y_gap;
  return ESP_OK;
}

static esp_err_t panel_disp_on_off(esp_lcd_panel_t* panel, bool on) {
  knob_panel_t* driver = __containerof(panel, knob_panel_t, base);
  return tx_param(driver, on ? LCD_CMD_DISPON : LCD_CMD_DISPOFF, NULL, 0);
}
