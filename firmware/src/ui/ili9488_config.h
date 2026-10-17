#pragma once
// ESP32 / ILI9488 / 4-wire SPI / 480x320 landscape.
// VCC and backlight power must follow the particular module's specification.
#ifndef TFT_PIN_SCLK
#define TFT_PIN_SCLK 18
#endif
#ifndef TFT_PIN_MOSI
#define TFT_PIN_MOSI 23
#endif
#ifndef TFT_PIN_CS
#define TFT_PIN_CS 27
#endif
#ifndef TFT_PIN_DC
#define TFT_PIN_DC 16
#endif
#ifndef TFT_PIN_RST
#define TFT_PIN_RST 17
#endif
#ifndef TFT_PIN_BL
#define TFT_PIN_BL -1
#endif
#ifndef TFT_SPI_HZ
#define TFT_SPI_HZ 40000000u
#endif
#ifndef TFT_SPI_MODE
#define TFT_SPI_MODE 0
#endif
#ifndef TFT_ROTATION
#define TFT_ROTATION 1
#endif
#ifndef TFT_COLOR_BGR
#define TFT_COLOR_BGR 1
#endif
#ifndef TFT_PANEL_INVERT
#define TFT_PANEL_INVERT 0
#endif
#ifndef TFT_FRAME_MS
#define TFT_FRAME_MS 33u
#endif
static_assert(TFT_ROTATION == 1 || TFT_ROTATION == 3, "This layout requires landscape");
static_assert(TFT_SPI_HZ <= 40000000u, "Use a conservative display SPI clock");
