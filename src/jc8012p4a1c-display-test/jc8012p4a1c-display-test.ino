#include "jc8012_panel.h"

namespace {
Jc8012Panel display;

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
}

void drawFramebufferTest() {
  static constexpr uint16_t colors[] = {
      rgb565(255, 255, 255), rgb565(255, 255, 0), rgb565(0, 255, 255),
      rgb565(0, 255, 0),     rgb565(255, 0, 255), rgb565(255, 0, 0),
      rgb565(0, 0, 255),     rgb565(0, 0, 0),
  };

  const int bar_width = Jc8012Panel::kWidth / 8;
  for (int i = 0; i < 8; ++i) {
    display.fillRect(i * bar_width, 0, bar_width, 720, colors[i]);
  }

  for (int y = 720; y < 1120; ++y) {
    for (int x = 0; x < Jc8012Panel::kWidth; ++x) {
      const uint8_t red = static_cast<uint8_t>((x * 255) / 799);
      const uint8_t green = static_cast<uint8_t>(((y - 720) * 255) / 399);
      const uint8_t blue = static_cast<uint8_t>(255 - red);
      display.pixel(x, y, rgb565(red, green, blue));
    }
  }

  display.fillRect(0, 1120, 800, 160, rgb565(32, 32, 32));
  for (int gray = 0; gray < 16; ++gray) {
    const uint8_t level = static_cast<uint8_t>(gray * 17);
    display.fillRect(gray * 50, 1160, 50, 80,
                     rgb565(level, level, level));
  }
  display.fillRect(0, 0, 800, 6, rgb565(255, 255, 255));
  display.fillRect(0, 1274, 800, 6, rgb565(255, 255, 255));
  display.fillRect(0, 0, 6, 1280, rgb565(255, 255, 255));
  display.fillRect(794, 0, 6, 1280, rgb565(255, 255, 255));
  display.sync();
  display.showFramebuffer();
  Serial.println("D: direct RGB565 framebuffer test");
}

void printHelp() {
  Serial.println("Commands: D=framebuffer, V=vertical bars, H=horizontal bars,");
  Serial.println("          B=BER pattern, R/G/U/W/K=solid color, ?=help");
}

void solid(uint16_t color, const char *name) {
  display.showFramebuffer();
  display.fillScreen(color);
  display.sync();
  Serial.printf("Solid %s\n", name);
}

void runCommand(char command) {
  switch (command) {
    case 'd': case 'D': drawFramebufferTest(); break;
    case 'v': case 'V':
      display.showHardwarePattern(MIPI_DSI_PATTERN_BAR_VERTICAL);
      Serial.println("V: MIPI vertical bars");
      break;
    case 'h': case 'H':
      display.showHardwarePattern(MIPI_DSI_PATTERN_BAR_HORIZONTAL);
      Serial.println("H: MIPI horizontal bars");
      break;
    case 'b': case 'B':
      display.showHardwarePattern(MIPI_DSI_PATTERN_BER_VERTICAL);
      Serial.println("B: MIPI BER pattern");
      break;
    case 'r': case 'R': solid(rgb565(255, 0, 0), "red"); break;
    case 'g': case 'G': solid(rgb565(0, 255, 0), "green"); break;
    case 'u': case 'U': solid(rgb565(0, 0, 255), "blue"); break;
    case 'w': case 'W': solid(rgb565(255, 255, 255), "white"); break;
    case 'k': case 'K': solid(rgb565(0, 0, 0), "black"); break;
    case '?': printHelp(); break;
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("JC8012P4A1C direct JD9365 display test");
  if (!display.begin()) {
    Serial.println("Display initialization stopped. Check the error above.");
    return;
  }
  Serial.println("Display initialized: 800x1280 RGB565, two-lane MIPI DSI");
  drawFramebufferTest();
  printHelp();
}

void loop() {
  while (Serial.available()) runCommand(Serial.read());
  delay(10);
}

