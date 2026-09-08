# JC8012P4A1C direct display test

Standalone Arduino test for the 10.1-inch JC8012P4A1C ESP32-P4 module. It
drives the 800x1280 JD9365 panel directly through the ESP-IDF MIPI DSI LCD
API. It does not use LVGL or another graphics library.

The included JD9365 driver and initialization table come from the `NewPanel`
Arduino example in the vendor resource archive under `docs/tft-ips-modules`.
Those two files retain Espressif's Apache-2.0 notices.

The LCD pixel path is MIPI DSI rather than SPI. One 800x1280 RGB565 framebuffer
(2,048,000 bytes) is allocated from ESP32-P4 PSRAM and continuously scanned by
the MIPI DPI hardware. The installed Arduino_GFX 1.6.4 also has generic
`Arduino_ESP32DSIPanel` / `Arduino_DSI_Display` support, but its bundled JD9365
table and default 750 Mbps lane rate differ from this NewPanel vendor example's
table and 1500 Mbps setting. This bring-up therefore uses `esp_lcd` directly.

## Build

Use Arduino ESP32 core 3.1.0, the version specified by the vendor package and
used for the verified build. `sketch.yaml` stores the ESP32-P4 board options;
keeping platform installation separate also permits offline builds.

```sh
arduino-cli compile --profile jc8012p4a1c \
  --output-dir ../../build/jc8012p4a1c-display-test .
```

The default screen tests direct RGB565 framebuffer output. The serial console
at 115200 baud accepts:

- `D`: direct framebuffer color bars, gradient, and gray ramp
- `V`, `H`: MIPI controller vertical or horizontal color bars
- `B`: MIPI bit-error-rate pattern
- `R`, `G`, `U`, `W`, `K`: solid red, green, blue, white, or black

This first bring-up deliberately excludes touch, SD, Wi-Fi, the ESP32-C6
companion, and RunCPM.
