/**
 * User_Setup.h - TFT_eSPI for ESP32-2432S028 (CYD)
 *
 * Known-working config from ESP32-CYD community.
 * Display: ILI9341 on HSPI bus.
 */

#ifndef USER_SETUP_H
#define USER_SETUP_H

#define USER_SETUP_INFO "ESP32-2432S028-CYD"

/* ---- Display driver ---- */
#define ILI9341_DRIVER

/* ---- Display dimensions ---- */
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

/* ---- CYD display SPI pins (HSPI) ---- */
#define TFT_MOSI  13
#define TFT_MISO  12
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1

/* ---- Backlight ---- */
#define TFT_BL    21
#define TFT_BACKLIGHT_ON HIGH

/* ---- SPI bus selection: HSPI is mandatory for CYD ---- */
#define USE_HSPI_PORT

/* ---- SPI frequency ---- */
#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  16000000
#define SPI_TOUCH_FREQUENCY  2500000

/* ---- Touch ---- */
#define TOUCH_CS   33

/* ---- Color order ---- */
#define TFT_RGB_ORDER TFT_BGR

/* ---- Load the ILI9341 specific commands ---- */
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#endif
