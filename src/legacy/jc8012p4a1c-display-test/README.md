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
  --output-dir ../../../build/jc8012p4a1c-display-test .
```

## Connect and flash

Use the board's dedicated USB-TTL Type-C socket, marked as connector 10 in the
vendor interface drawing. It is the lowest of the three Type-C sockets beside
the CH340C chip. Linux identifies it as a QinHeng CH340 (`1a86:7523`), normally
as `/dev/ttyUSB0`. The other two Type-C sockets connect directly to ESP32-P4
full-speed and high-speed USB peripherals.

The CH340C drives `BOOTMODE` and `CHIP_PU` through DTR and RTS, so a normal
upload resets the board into its ROM loader automatically:

```sh
arduino-cli upload -p /dev/ttyUSB0 \
  -b 'esp32:esp32:esp32p4:CDCOnBoot=default,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled' \
  --input-dir ../../../build/jc8012p4a1c-display-test
```

If automatic entry fails, hold **BOOT**, press and release **RESET**, then
release **BOOT** and repeat the upload. The vendor manual also recommends a
power reset before another upload when necessary.

All three Type-C VBUS pins join the same `USB5V_IN` rail. That rail supplies an
IP5306 power-bank/charger IC and the board. The module specification gives
about 700 mA operating current; the IP5306 can charge a depleted LiPo at up to
about 2.1 A and adapts downward if its 5 V input sags. For first bring-up, use
an externally powered hub or adequate 5 V supply and leave the internal LiPo
disconnected. Avoid attaching independent powered USB sources to multiple
Type-C sockets simultaneously.

The default screen tests direct RGB565 framebuffer output. The serial console
on the same CH340 port runs at 115200 baud and accepts:

- `D`: direct framebuffer color bars, gradient, and gray ramp
- `V`, `H`: MIPI controller vertical or horizontal color bars
- `B`: MIPI bit-error-rate pattern
- `R`, `G`, `U`, `W`, `K`: solid red, green, blue, white, or black

This first bring-up deliberately excludes touch, SD, Wi-Fi, the ESP32-C6
companion, and RunCPM.
