#pragma once

#include <Arduino.h>
#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"

class Jc8012Panel {
 public:
  static constexpr uint16_t kWidth = 800;
  static constexpr uint16_t kHeight = 1280;

  bool begin();
  void backlight(bool on);

  bool showHardwarePattern(mipi_dsi_pattern_type_t pattern);
  bool showFramebuffer();
  bool sync();

  void fillScreen(uint16_t color);
  void fillRect(int16_t x, int16_t y, int16_t width, int16_t height,
                uint16_t color);
  void pixel(int16_t x, int16_t y, uint16_t color);

  uint16_t *framebuffer() const { return framebuffer_; }
  esp_err_t lastError() const { return last_error_; }

 private:
  bool check(esp_err_t result, const char *operation);

  esp_ldo_channel_handle_t phy_power_ = nullptr;
  esp_lcd_dsi_bus_handle_t dsi_bus_ = nullptr;
  esp_lcd_panel_io_handle_t panel_io_ = nullptr;
  esp_lcd_panel_handle_t panel_ = nullptr;
  uint16_t *framebuffer_ = nullptr;
  esp_err_t last_error_ = ESP_OK;
};

