# JC8048W535 3.5-inch baseline

The supplied panel resources identify an ESP32-S3 with an AXS15231B QSPI display,
320x480 native resolution, and backlight GPIO 1. The verified direct baseline uses
Arduino_GFX 1.6.4, QSPI pins CS/SCK/D0..D3 `45,47,21,48,40,39`, OPI PSRAM, and
the `jc8048w535` profile.

The 7xtgv demos expose the panel as landscape 480x320 by keeping the controller
and flush framebuffer native (320x480) and rotating `Arduino_Canvas` with rotation
1. This preserves the same Arduino_GFX path used by the other displays and gives
the intended 80x32 terminal grid.

The supplied non-LVGL touch library was not used as the demo dependency: its
optional JPEG/WiFi code requires JPEGDecoder and produced core-link incompatibility
in the supplied example. A display-only Arduino_GFX baseline compiled and flashed
successfully before the 7xtgv integration.
