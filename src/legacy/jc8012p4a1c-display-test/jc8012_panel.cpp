#include "jc8012_panel.h"

#include <algorithm>
#include <cstring>

#include "driver/gpio.h"
#include "esp_cache.h"
#include "soc/soc_caps.h"
#include "src/esp_lcd_jd9365.h"

#if !CONFIG_IDF_TARGET_ESP32P4
#error "JC8012P4A1C display test requires an ESP32-P4 target"
#endif

#if !SOC_MIPI_DSI_SUPPORTED
#error "Selected target does not support MIPI DSI"
#endif

namespace {
constexpr gpio_num_t kLcdReset = GPIO_NUM_27;
constexpr gpio_num_t kBacklight = GPIO_NUM_23;
constexpr int kMipiPhyLdoChannel = 3;
constexpr int kMipiPhyMillivolts = 2500;
constexpr size_t kFramebufferBytes =
    Jc8012Panel::kWidth * Jc8012Panel::kHeight * sizeof(uint16_t);
}  // namespace

bool Jc8012Panel::check(esp_err_t result, const char *operation) {
  last_error_ = result;
  if (result == ESP_OK) return true;
  Serial.printf("FAIL: %s: %s (0x%x)\n", operation, esp_err_to_name(result),
                static_cast<unsigned>(result));
  return false;
}

bool Jc8012Panel::begin() {
  backlight(false);

  esp_ldo_channel_config_t ldo_config = {};
  ldo_config.chan_id = kMipiPhyLdoChannel;
  ldo_config.voltage_mv = kMipiPhyMillivolts;
  if (!check(esp_ldo_acquire_channel(&ldo_config, &phy_power_),
             "power MIPI DSI PHY")) {
    return false;
  }

  esp_lcd_dsi_bus_config_t bus_config = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
  if (!check(esp_lcd_new_dsi_bus(&bus_config, &dsi_bus_),
             "create MIPI DSI bus")) {
    return false;
  }

  esp_lcd_dbi_io_config_t io_config = JD9365_PANEL_IO_DBI_CONFIG();
  if (!check(esp_lcd_new_panel_io_dbi(dsi_bus_, &io_config, &panel_io_),
             "create MIPI DBI command channel")) {
    return false;
  }

  esp_lcd_dpi_panel_config_t dpi_config =
      JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);

  jd9365_vendor_config_t vendor_config = {};
  vendor_config.mipi_config.dsi_bus = dsi_bus_;
  vendor_config.mipi_config.dpi_config = &dpi_config;
  vendor_config.mipi_config.lane_num = 2;

  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = kLcdReset;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_config.bits_per_pixel = 16;
  panel_config.vendor_config = &vendor_config;

  if (!check(esp_lcd_new_panel_jd9365(panel_io_, &panel_config, &panel_),
             "create JD9365 panel")) {
    return false;
  }
  if (!check(esp_lcd_panel_reset(panel_), "reset JD9365 panel") ||
      !check(esp_lcd_panel_init(panel_), "initialize JD9365 panel") ||
      !check(esp_lcd_dpi_panel_get_frame_buffer(
                 panel_, 1, reinterpret_cast<void **>(&framebuffer_)),
             "get framebuffer")) {
    return false;
  }

  fillScreen(0x0000);
  if (!sync()) return false;
  backlight(true);
  return true;
}

void Jc8012Panel::backlight(bool on) {
  gpio_config_t config = {};
  config.pin_bit_mask = 1ULL << kBacklight;
  config.mode = GPIO_MODE_OUTPUT;
  gpio_config(&config);
  gpio_set_level(kBacklight, on ? 1 : 0);
}

bool Jc8012Panel::showHardwarePattern(mipi_dsi_pattern_type_t pattern) {
  return check(esp_lcd_dpi_panel_set_pattern(panel_, pattern),
               "select MIPI hardware pattern");
}

bool Jc8012Panel::showFramebuffer() {
  return showHardwarePattern(MIPI_DSI_PATTERN_NONE);
}

bool Jc8012Panel::sync() {
  return check(esp_cache_msync(framebuffer_, kFramebufferBytes,
                               ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                                   ESP_CACHE_MSYNC_FLAG_TYPE_DATA),
               "flush framebuffer cache");
}

void Jc8012Panel::fillScreen(uint16_t color) {
  fillRect(0, 0, kWidth, kHeight, color);
}

void Jc8012Panel::fillRect(int16_t x, int16_t y, int16_t width,
                          int16_t height, uint16_t color) {
  if (!framebuffer_ || width <= 0 || height <= 0) return;
  const int x0 = std::max<int>(0, x);
  const int y0 = std::max<int>(0, y);
  const int x1 = std::min<int>(kWidth, x + width);
  const int y1 = std::min<int>(kHeight, y + height);
  for (int row = y0; row < y1; ++row) {
    std::fill(framebuffer_ + row * kWidth + x0,
              framebuffer_ + row * kWidth + x1, color);
  }
}

void Jc8012Panel::pixel(int16_t x, int16_t y, uint16_t color) {
  if (framebuffer_ && x >= 0 && x < kWidth && y >= 0 && y < kHeight) {
    framebuffer_[y * kWidth + x] = color;
  }
}

