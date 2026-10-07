# Arduino_GFX ESP32-P4 DSI fix

The JC8012P4A1C demos use the vendor-modified Arduino_GFX 1.6.4 library,
which supplies `Arduino_ESP32DSIPanel` and `Arduino_DSI_Display`. Its RGB
parallel guards also select ESP32-P4, although Arduino-ESP32 3.1.0 does not
provide the included `esp_lcd_panel_rgb.h`. DSI does not need those classes.

Patch each installed or Arduino CLI cached copy before compiling:

```sh
python3 src/legacy/arduino-gfx-patches/apply-esp32p4-dsi-fix.py \
  ~/Arduino/libraries/GFX_Library_for_Arduino
```

The script is idempotent. It limits the RGB parallel implementation to
ESP32-S3 and leaves the P4 MIPI DSI implementation enabled.
