// MIXWORX gfxsetup style - modified from basic examples
// =====================================================

/*******************************************************************************
 * Start of Arduino_GFX setting
 ******************************************************************************/

//#define WAVESHARE_ESP32_C6_LCD_1_47
#define WAVESHARE_RP2350_LCD_1_47

#if defined(WAVESHARE_ESP32_C6_LCD_1_9)
  #define GFX_BL   15
  #define GFX_CS    7
  #define GFX_RST  14
  #define GFX_SCK   5
  #define GFX_DC    6
  #define GFX_MOSI  4
  #define GFX_MISO -1
#endif

#if defined(WAVESHARE_ESP32_C6_LCD_1_47)

  #define GFX_DEV_DEVICE WAVESHARE_ESP32_C6_LCD_1_47

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
  #define GFX_MISO -1 //5

  #define SD_CS     4
  #define DEV_DEVICE_INIT()        \
    {                              \
        pinMode(SD_CS, OUTPUT);    \
        digitalWrite(SD_CS, HIGH); \
    }

#endif

#if defined(WAVESHARE_RP2350_LCD_1_47)

  #define GFX_DEV_DEVICE WAVESHARE_RP2350_LCD_1_47

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
  #define GFX_MISO -1

  #define SD_CS     4

  // On RP2350/RP2040 Arduino cores, Arduino_HWSPI takes SPIClass*, NOT pin numbers.
  // So we must configure SPI pins via SPI.setSCK()/setTX() (core-dependent) + SPI.begin().
  #define DEV_DEVICE_INIT()                          \
    {                                                \
        pinMode(SD_CS, OUTPUT);                      \
        digitalWrite(SD_CS, HIGH);                   \
                                                     \
        /* Map SPI pins for RP2350/RP2040 cores */   \
        SPI.setSCK(GFX_SCK);                         \
        /* Most RP cores use setTX() for MOSI */     \
        SPI.setTX(GFX_MOSI);                         \
        /* If you later need MISO, enable this: */   \
        /* SPI.setRX(GFX_MISO); */                   \
        SPI.begin();                                 \
    }

#endif


#if defined(U8G2_FONT_SUPPORT)
#include <U8g2lib.h>
#endif

#include <Arduino_GFX_Library.h>

/*
  Bus selection rules:
  - ESP32: prefer Arduino_ESP32SPI (pins passed explicitly, uses ESP32 backend)
  - RP2350/RP2040: Arduino_HWSPI expects SPIClass* (use &SPI); pins are set in DEV_DEVICE_INIT()
  - Fallback: generic Arduino_HWSPI with &SPI
*/

#if defined(ESP32)
  // ESP32 backend: pins passed explicitly (works across ESP32 cores)
  Arduino_DataBus *bus = new Arduino_ESP32SPI(GFX_DC, GFX_CS, GFX_SCK, GFX_MOSI);
#else
  // RP2350/RP2040 (and most other non-ESP32): Arduino_HWSPI takes SPIClass*
  Arduino_DataBus *bus = new Arduino_HWSPI(GFX_DC, GFX_CS, &SPI);
#endif

Arduino_GFX *gfx = new Arduino_ST7789(
  bus,
  GFX_RST,
  GFX_ROT,
  GFX_IPS,
  GFX_MX,
  GFX_MY,
  GFX_DX02,
  GFX_DY02,
  GFX_DX13,
  GFX_DY13
);

/*******************************************************************************
 * End of Arduino_GFX setting
 ******************************************************************************/
