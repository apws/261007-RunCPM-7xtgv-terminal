

// MIXWORX gfxsetup style - modified from basic examples
// =====================================================

#if defined(U8G2_FONT_SUPPORT)
#include <U8g2lib.h>
#endif

#include <Arduino_GFX_Library.h>

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_IDF_TARGET_ESP32P4)
#include "../7xtgv-jc8012p4a1c-newpanel.h"
#endif

Arduino_DataBus *bus;
Arduino_GFX *gfx;

// Arduino_GFX 1.2.x (the verified JC8048W550 vendor baseline) returns void
// from begin(); newer releases return success.  Keep callers uniform.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR < 3
  #define GFX_BEGIN_OK() (gfx->begin(), true)
#else
  #define GFX_BEGIN_OK() gfx->begin()
#endif


/*******************************************************************************
 * Start of Arduino_GFX setting
 ******************************************************************************/

//#define USE_FAST_PINIO
//#define LITTLE_FOOT_PRINT

//#define WAVESHARE_ESP32_C6_LCD_1_47
//#define WAVESHARE_RP2350_LCD_1_47
//#define WAVESHARE_RP2350_AMOLED_1_64
#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_IDF_TARGET_ESP32P4)
#define JC8012P4A1C_NEW_PANEL
#elif defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3)
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#define JC8048W535_AXS15231
#else
#define JC8048W550_ST7262
#endif
#else
#define WAVESHARE_RP2350_LCD_2
#endif

#if defined(WAVESHARE_ESP32_C6_LCD_1_9)

  #define GFX_DEV_DEVICE WAVESHARE_ESP32_C6_LCD_1_9

  // 1,47in IPS 320x172 in chars of QL font cell
  //#define SCR_COLS 52
  //#define SCR_ROWS 17

  //7xtgv SCREEN OFFSET in px
  #define GFX_OFS_X 0
  #define GFX_OFS_Y 0


  #define GFX_BL   15
  #define GFX_CS    7
  #define GFX_RST  14
  #define GFX_SCK   5
  #define GFX_DC    6
  #define GFX_MOSI  4
  #define GFX_MISO -1

#endif

#if defined(WAVESHARE_ESP32_C6_LCD_1_47)

  #define GFX_DEVICE WAVESHARE_ESP32_C6_LCD_1_47

  // 1,47in IPS 320x172 in chars of QL font cell
  #define SCR_COLS 52
  #define SCR_ROWS 17

  //7xtgv SCREEN OFFSET in px
  #define GFX_OFS_X 0
  #define GFX_OFS_Y 0


  #define GFX_MX  172
  #define GFX_MY  320
  #define GFX_ROT   3
  #define GFX_IPS   1
  #define GFX_DX02 34
  #define GFX_DY02  0
  #define GFX_DX13 34
  #define GFX_DY13  0

  #define GFX_BL   22
  #define GFX_CS   14
  #define GFX_RST  21
  #define GFX_SCK   7
  #define GFX_DC   15
  #define GFX_MOSI  6
  #define GFX_MISO  5 //-1

  #define SD_CS     4

  void GFX_DEVICE_INIT()
  {
      pinMode(SD_CS, OUTPUT);
      digitalWrite(SD_CS, HIGH);

      // ESP32 backend: pins passed explicitly (works across ESP32 cores)
      //bus = new Arduino_SWSPI(GFX_DC, GFX_CS, GFX_SCK, GFX_MOSI); //works
      bus = new Arduino_HWSPI(GFX_DC, GFX_CS, GFX_SCK, GFX_MOSI); //works
      //bus = new Arduino_ESP32SPI(GFX_DC, GFX_CS, GFX_SCK, GFX_MOSI); //works
      //bus = new Arduino_ESP32SPIDMA(GFX_DC, GFX_CS, GFX_SCK, GFX_MOSI, GFX_MISO, 2); //works, quite slow

      gfx = new Arduino_ST7789(bus, GFX_RST, GFX_ROT, GFX_IPS, GFX_MX, GFX_MY, GFX_DX02, GFX_DY02, GFX_DX13, GFX_DY13);
  }

#endif

#if defined(WAVESHARE_RP2350_LCD_1_47)

  #define GFX_DEVICE WAVESHARE_RP2350_LCD_1_47

  // 1,47in IPS 320x172 in chars of QL font cell
  #define SCR_COLS 52
  #define SCR_ROWS 17

  //7xtgv SCREEN OFFSET in px
  #define GFX_OFS_X 0
  #define GFX_OFS_Y 0

  #define GFX_MX  172
  #define GFX_MY  320
  #define GFX_ROT   3
  #define GFX_IPS   1
  #define GFX_DX02 34
  #define GFX_DY02  0
  #define GFX_DX13 34
  #define GFX_DY13  0

  #define GFX_BL   21
  #define GFX_CS   17
  #define GFX_RST  20
  #define GFX_SCK  18
  #define GFX_DC   16
  #define GFX_MOSI 19
  #define GFX_MISO  4 //-1

  #define SD_CS    15

  // On RP2350/RP2040 Arduino cores, Arduino_HWSPI takes SPIClass*, NOT pin numbers.
  // So we must configure SPI pins via SPI.setSCK()/setTX() (core-dependent) + SPI.begin().
  void GFX_DEVICE_INIT()
  {
        pinMode(SD_CS, OUTPUT);
        digitalWrite(SD_CS, HIGH);

        /*
          Bus selection rules:
          - ESP32: prefer Arduino_ESP32SPI (pins passed explicitly, uses ESP32 backend)
          - RP2350/RP2040: Arduino_HWSPI expects SPIClass* (use &SPI); pins are set in DEV_DEVICE_INIT()
          - Fallback: generic Arduino_HWSPI with &SPI
        */

        // RP2350/RP2040 (and most other non-ESP32): Arduino_HWSPI takes SPIClass*
        //bus = new Arduino_SWSPI(GFX_DC, GFX_CS, GFX_SCK, GFX_MOSI); //works
        //bus = new Arduino_HWSPI(GFX_DC, GFX_CS, GFX_SCK, GFX_MOSI); //COMPILE FAILS !!! WEIRD !!!! looks like error in library !!!
        //bus = new Arduino_HWSPI(GFX_DC, GFX_CS, &SPI, true); //NOTHING //NOT SHARED by schematics !!!
        //bus = new Arduino_HWSPI(GFX_DC, GFX_CS); //NOTHING //NOT SHARED by schematics !!!
        bus = new Arduino_RPiPicoSPI(GFX_DC, GFX_CS, GFX_SCK, GFX_MOSI, GFX_MISO); //WORKS !! + NO explicit SPI inits !!!

        gfx = new Arduino_ST7789(bus, GFX_RST, GFX_ROT, GFX_IPS, GFX_MX, GFX_MY, GFX_DX02, GFX_DY02, GFX_DX13, GFX_DY13);

  }

#endif


#if defined(WAVESHARE_RP2350_AMOLED_1_64)

  #define GFX_DEVICE WAVESHARE_RP2350_LCD_1_47

  // 1,64in AMOLED 456x280 in chars of QL font cell
  #define SCR_COLS 76
  #define SCR_ROWS 28

  //7xtgv SCREEN OFFSET in px
  #define GFX_OFS_X 0
  #define GFX_OFS_Y 0

  #define GFX_MX  280
  #define GFX_MY  456
  #define GFX_ROT   3
  #define GFX_IPS   0
  #define GFX_DX02 34
  #define GFX_DY02  0
  #define GFX_DX13 34
  #define GFX_DY13  0

  //#define GFX_BL   21
  #define GFX_CS    9
  #define GFX_RST  15
  #define GFX_SCK  10
  //#define GFX_DC   16


  #define GFX_MOSI 11
  #define GFX_MISO  -1

  #define GFX_SI0  11
  #define GFX_SI1  12
  #define GFX_SI2  13
  #define GFX_SI3  14

  #define SD_CS    15

  // On RP2350/RP2040 Arduino cores, Arduino_HWSPI takes SPIClass*, NOT pin numbers.
  // So we must configure SPI pins via SPI.setSCK()/setTX() (core-dependent) + SPI.begin().
  void GFX_DEVICE_INIT()
  {
        pinMode(SD_CS, OUTPUT);
        digitalWrite(SD_CS, HIGH);

        /*
          Bus selection rules:
          - ESP32: prefer Arduino_ESP32SPI (pins passed explicitly, uses ESP32 backend)
          - RP2350/RP2040: Arduino_HWSPI expects SPIClass* (use &SPI); pins are set in DEV_DEVICE_INIT()
          - Fallback: generic Arduino_HWSPI with &SPI
        */

        // RP2350/RP2040 (and most other non-ESP32): Arduino_HWSPI takes SPIClass*
        //bus = new Arduino_SWSPI(GFX_DC, GFX_CS, GFX_SCK, GFX_MOSI); //works
        //bus = new Arduino_HWSPI(GFX_DC, GFX_CS, GFX_SCK, GFX_MOSI); //COMPILE FAILS !!! WEIRD !!!! looks like error in library !!!
        //bus = new Arduino_HWSPI(GFX_DC, GFX_CS, &SPI, true); //NOTHING //NOT SHARED by schematics !!!
        //bus = new Arduino_HWSPI(GFX_DC, GFX_CS); //NOTHING //NOT SHARED by schematics !!!
        bus = new Arduino_RPiPicoSPI(GFX_DC, GFX_CS, GFX_SCK, GFX_MOSI, GFX_MISO); //WORKS !! + NO explicit SPI inits !!!

        gfx = new Arduino_ST7789(bus, GFX_RST, GFX_ROT, GFX_IPS, GFX_MX, GFX_MY, GFX_DX02, GFX_DY02, GFX_DX13, GFX_DY13);

  }

#endif

#if defined(JC8012P4A1C_NEW_PANEL)

  #define GFX_DEVICE JC8012P4A1C_NEW_PANEL

  // Native portrait panel is rotated into 1280x800 landscape.
  // At 2x zoom this gives a 106x40 grid of 12x20 pixel QL cells.
  #define SCR_COLS 213
  #define SCR_ROWS 80

  #define GFX_OFS_X 0
  #define GFX_OFS_Y 0
  #define GFX_BL 23

  Arduino_ESP32DSIPanel *dsiPanel;

  void GFX_DEVICE_INIT()
  {
    dsiPanel = new Arduino_ESP32DSIPanel(
        20, 20, 40,  // horizontal sync, back porch, front porch
        4, 10, 30,   // vertical sync, back porch, front porch
        80000000,    // DPI pixel clock
        1500);       // per-lane MIPI DSI bit rate in Mbps

    gfx = new Arduino_DSI_Display(
        800, 1280, dsiPanel,
        1,      // rotate native portrait framebuffer to landscape
        false,  // flush once after a complete 7xtgv frame
        27,
        jc8012p4a1c_newpanel_init,
        sizeof(jc8012p4a1c_newpanel_init) / sizeof(lcd_init_cmd_t));
  }

#endif

#if defined(JC8048W535_AXS15231)

  #define GFX_DEVICE JC8048W535_AXS15231
  #define SCR_COLS 80
  #define SCR_ROWS 32
  #define GFX_OFS_X 0
  #define GFX_OFS_Y 0
  #define GFX_BL 1

  void GFX_DEVICE_INIT()
  {
    Arduino_ESP32QSPI *qspi = new Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39);
    // Keep the controller native; rotate the canvas so it retains a 320x480
    // framebuffer for flushing while exposing 480x320 drawing coordinates.
    Arduino_AXS15231B *panel = new Arduino_AXS15231B(
        qspi, GFX_NOT_DEFINED, 0, false, 320, 480);
    gfx = new Arduino_Canvas(320, 480, panel, 0, 0, 1);
  }

#endif

#if defined(JC8048W550_ST7262)

  #define GFX_DEVICE JC8048W550_ST7262
  #define SCR_COLS 133
  #define SCR_ROWS 48
  #define GFX_OFS_X 0
  #define GFX_OFS_Y 0
  #define GFX_BL 2

  void GFX_DEVICE_INIT()
  {
    // JC8048W550 RGB565 data bus and timing from the verified vendor demo.
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR < 3
    Arduino_ESP32RGBPanel *rgbPanel = new Arduino_ESP32RGBPanel(
        GFX_NOT_DEFINED, GFX_NOT_DEFINED, GFX_NOT_DEFINED,
        40, 41, 39, 42,
        45, 48, 47, 21, 14,
        5, 6, 7, 15, 16, 4,
        8, 3, 46, 9, 1);
    gfx = new Arduino_RPi_DPI_RGBPanel(
        rgbPanel,
        800, 0, 8, 4, 8,
        480, 0, 8, 4, 8,
        1, 16000000, true);
    #else
    Arduino_ESP32RGBPanel *rgbPanel = new Arduino_ESP32RGBPanel(
        40, 41, 39, 42,                 // DE, VSYNC, HSYNC, PCLK
        45, 48, 47, 21, 14,             // R0..R4
        5, 6, 7, 15, 16, 4,              // G0..G5
        8, 3, 46, 9, 1,                  // B0..B4
        0, 8, 4, 8,                      // hsync polarity/front/pulse/back
        0, 8, 4, 8,                      // vsync polarity/front/pulse/back
        1, 16000000, false);             // falling PCLK, 16 MHz

    gfx = new Arduino_RGB_Display(800, 480, rgbPanel, 0, true);
    #endif
  }

#endif

#if defined(WAVESHARE_RP2350_LCD_2)

  #define GFX_DEVICE WAVESHARE_RP2350_LCD_2

  // 2in IPS 320x240 in chars of QL font cell
  #define SCR_COLS 52
  #define SCR_ROWS 24

  //7xtgv SCREEN OFFSET in px
  #define GFX_OFS_X 0
  #define GFX_OFS_Y 0

  #define GFX_MX  240
  #define GFX_MY  320
  #define GFX_ROT   3
  #define GFX_IPS   1
  // Native 240x320 ST7789 window: no 34-pixel offset is needed.
  #define GFX_DX02 0
  #define GFX_DY02  0
  #define GFX_DX13 0
  #define GFX_DY13  0

  #define GFX_BL   15
  #define GFX_CS   17
  #define GFX_RST  20
  #define GFX_SCK  18
  #define GFX_DC   16
  #define GFX_MOSI 19
  #define GFX_MISO  1

  #define SD_CS    25

  // On RP2350/RP2040 Arduino cores, Arduino_HWSPI takes SPIClass*, NOT pin numbers.
  // So we must configure SPI pins via SPI.setSCK()/setTX() (core-dependent) + SPI.begin().
  void GFX_DEVICE_INIT()
  {
        pinMode(SD_CS, OUTPUT);
        digitalWrite(SD_CS, HIGH);

        /*
          Bus selection rules:
          - ESP32: prefer Arduino_ESP32SPI (pins passed explicitly, uses ESP32 backend)
          - RP2350/RP2040: Arduino_HWSPI expects SPIClass* (use &SPI); pins are set in DEV_DEVICE_INIT()
          - Fallback: generic Arduino_HWSPI with &SPI
        */

        // RP2350/RP2040 (and most other non-ESP32): Arduino_HWSPI takes SPIClass*
        //bus = new Arduino_SWSPI(GFX_DC, GFX_CS, GFX_SCK, GFX_MOSI); //works
        //bus = new Arduino_HWSPI(GFX_DC, GFX_CS, GFX_SCK, GFX_MOSI); //COMPILE FAILS !!! WEIRD !!!! looks like error in library !!!
        //bus = new Arduino_HWSPI(GFX_DC, GFX_CS, &SPI, true); //NOTHING //NOT SHARED by schematics !!!
        //bus = new Arduino_HWSPI(GFX_DC, GFX_CS); //NOTHING //NOT SHARED by schematics !!!
        bus = new Arduino_RPiPicoSPI(GFX_DC, GFX_CS, GFX_SCK, GFX_MOSI, GFX_MISO); //WORKS !! + NO explicit SPI inits !!!

        gfx = new Arduino_ST7789(bus, GFX_RST, GFX_ROT, GFX_IPS, GFX_MX, GFX_MY, GFX_DX02, GFX_DY02, GFX_DX13, GFX_DY13);

  }

#endif


/*******************************************************************************
 * End of Arduino_GFX setting
 ******************************************************************************/





// #define GFX_BL DF_GFX_BL // default backlight pin, you may replace DF_GFX_BL to actual backlight pin

// /* More dev device declaration: https://github.com/moononournation/Arduino_GFX/wiki/Dev-Device-Declaration */
// #if defined(DISPLAY_DEV_KIT)
// Arduino_GFX *gfx = create_default_Arduino_GFX();
// #else /* !defined(DISPLAY_DEV_KIT) */

// /* More data bus class: https://github.com/moononournation/Arduino_GFX/wiki/Data-Bus-Class */
// Arduino_DataBus *bus = create_default_Arduino_DataBus();

// /* More display class: https://github.com/moononournation/Arduino_GFX/wiki/Display-Class */
// Arduino_GFX *gfx = new Arduino_ILI9341(bus, DF_GFX_RST, 0 /* rotation */, false /* IPS */);

// #endif /* !defined(DISPLAY_DEV_KIT) */



/* ESP32 specific

 ******************************************************************************
 * Start of Arduino_GFX setting
 *
 * Arduino_GFX try to find the settings depends on selected board in Arduino IDE
 * Or you can define the display dev kit not in the board list
 * Defalult pin list for non display dev kit:
 * Arduino Nano, Micro and more: CS:  9, DC:  8, RST:  7, BL:  6, SCK: 13, MOSI: 11, MISO: 12
 * ESP32 various dev board     : CS:  5, DC: 27, RST: 33, BL: 22, SCK: 18, MOSI: 23, MISO: nil
 * ESP32-C2/3 various dev board: CS:  7, DC:  2, RST:  1, BL:  3, SCK:  4, MOSI:  6, MISO: nil
 * ESP32-C5 various dev board  : CS: 23, DC: 24, RST: 25, BL: 26, SCK: 10, MOSI:  8, MISO: nil
 * ESP32-C6 various dev board  : CS: 18, DC: 22, RST: 23, BL: 15, SCK: 21, MOSI: 19, MISO: nil
 * ESP32-H2 various dev board  : CS:  0, DC: 12, RST:  8, BL: 22, SCK: 10, MOSI: 25, MISO: nil
 * ESP32-P4 various dev board  : CS: 26, DC: 27, RST: 25, BL: 24, SCK: 36, MOSI: 32, MISO: nil
 * ESP32-S2 various dev board  : CS: 34, DC: 38, RST: 33, BL: 21, SCK: 36, MOSI: 35, MISO: nil
 * ESP32-S3 various dev board  : CS: 40, DC: 41, RST: 42, BL: 48, SCK: 36, MOSI: 35, MISO: nil
 * ESP8266 various dev board   : CS: 15, DC:  4, RST:  2, BL:  5, SCK: 14, MOSI: 13, MISO: 12
 * Raspberry Pi Pico dev board : CS: 17, DC: 27, RST: 26, BL: 28, SCK: 18, MOSI: 19, MISO: 16
 * RTL8720 BW16 old patch core : CS: 18, DC: 17, RST:  2, BL: 23, SCK: 19, MOSI: 21, MISO: 20
 * RTL8720_BW16 Official core  : CS:  9, DC:  8, RST:  6, BL:  3, SCK: 10, MOSI: 12, MISO: 11
 * RTL8722 dev board           : CS: 18, DC: 17, RST: 22, BL: 23, SCK: 13, MOSI: 11, MISO: 12
 * RTL8722_mini dev board      : CS: 12, DC: 14, RST: 15, BL: 13, SCK: 11, MOSI:  9, MISO: 10
 * Seeeduino XIAO dev board    : CS:  3, DC:  2, RST:  1, BL:  0, SCK:  8, MOSI: 10, MISO:  9
 * Teensy 4.1 dev board        : CS: 39, DC: 41, RST: 40, BL: 22, SCK: 13, MOSI: 11, MISO: 12
 ******************************************************************************/





//"multiple display" settings

/*******************************************************************************
 * Start of Arduino_GFX setting
 ******************************************************************************/
// #include <Arduino_GFX_Library.h>

// /* all display share same SPI Data Bus with individual CS, RST pins connected to MCU RST pin */
// Arduino_DataBus *bus1 = new Arduino_HWSPI(DF_GFX_DC, 21 /* CS */);
// Arduino_GFX *gfx1 = new Arduino_SSD1331(bus1, GFX_NOT_DEFINED /* RST */, 0 /* rotation */);

// Arduino_DataBus *bus2 = new Arduino_HWSPI(DF_GFX_DC, 32 /* CS */);
// Arduino_GFX *gfx2 = new Arduino_ST7735(bus2, GFX_NOT_DEFINED /* RST */, 3 /* rotation */, true /* IPS */, 80 /* width */, 160 /* height */, 26 /* col offset 1 */, 1 /* row offset 1 */, 26 /* col offset 2 */, 1 /* row offset 2 */);

// Arduino_DataBus *bus3 = new Arduino_HWSPI(DF_GFX_DC, 22 /* CS */);
// Arduino_GFX *gfx3 = new Arduino_ST7789(bus3, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, true /* IPS */, 240 /* width */, 240 /* height */, 0 /* col offset 1 */, 0 /* row offset 1 */, 0 /* col offset 2 */, 80 /* row offset 2 */);

// Arduino_DataBus *bus4 = new Arduino_HWSPI(DF_GFX_DC, 5 /* CS */);
// Arduino_GFX *gfx4 = new Arduino_ILI9341(bus4, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, false /* IPS */);
/*******************************************************************************
 * End of Arduino_GFX setting
 ******************************************************************************/



//"PDQgraphicstesst" flexible config

/*******************************************************************************
 * Start of Arduino_GFX setting
 ******************************************************************************/
// #include <Arduino_GFX_Library.h>

// /* OPTION 1: Uncomment a dev device in Arduino_GFX_dev_device.h */
// #include "Arduino_GFX_dev_device.h"

// /* Check if not Uncommented any GFX_DEV_DEVICE in OPTION 1, include OPTION 2 header files */
// #ifndef GFX_DEV_DEVICE

// /* OPTION 2: Manual define hardware */

// /* Step 1: Define pins in Arduino_GFX_databus.h */
// #include "Arduino_GFX_pins.h"

// /* Step 2: Uncomment your databus in Arduino_GFX_databus.h */
// #include "Arduino_GFX_databus.h"

// /* Step 3: Uncomment your display driver in Arduino_GFX_display.h */
// #include "Arduino_GFX_display.h"

// #endif /* Manual define hardware */
/*******************************************************************************
 * End of Arduino_GFX setting
 ******************************************************************************/


// specific "WIFIAnalyser" WIO default

/*******************************************************************************
 * Start of Arduino_GFX setting
 ******************************************************************************/
// #include <Arduino_GFX_Library.h>

// #define GFX_BL DF_GFX_BL // default backlight pin, you may replace DF_GFX_BL to actual backlight pin

// Arduino_GFX *gfx = create_default_Arduino_GFX();

/*******************************************************************************
 * End of Arduino_GFX setting
 ******************************************************************************/
