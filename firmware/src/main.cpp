/**
 * main.cpp - Claude Monitor CYD Firmware v2
 *
 * Dual-screen: Monitor dashboard + Window switcher
 * Raw HSPI display driver + XPT2046 touch + LVGL + serial
 */

#include <Arduino.h>
#include <SPI.h>
#include "lvgl.h"

/* ============================================================
 * ILI9341 Raw SPI Driver (proven working)
 * ============================================================ */

#define LCD_CS   15
#define LCD_DC    2
#define LCD_MOSI 13
#define LCD_MISO 12
#define LCD_SCLK 14
#define LCD_BL   21
#define LCD_W 320
#define LCD_H 240

static SPIClass hspi(HSPI);

static void lcd_cmd(uint8_t cmd) {
    digitalWrite(LCD_CS, LOW);
    digitalWrite(LCD_DC, LOW);
    hspi.transfer(cmd);
    digitalWrite(LCD_DC, HIGH);
    digitalWrite(LCD_CS, HIGH);
}

static void lcd_cmd_data(uint8_t cmd, const uint8_t *data, size_t len) {
    digitalWrite(LCD_CS, LOW);
    digitalWrite(LCD_DC, LOW);
    hspi.transfer(cmd);
    digitalWrite(LCD_DC, HIGH);
    for (size_t i = 0; i < len; i++) hspi.transfer(data[i]);
    digitalWrite(LCD_CS, HIGH);
}

static void lcd_init(void) {
    pinMode(LCD_CS, OUTPUT);
    pinMode(LCD_DC, OUTPUT);
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_CS, HIGH);
    digitalWrite(LCD_BL, HIGH);

    hspi.begin(LCD_SCLK, LCD_MISO, LCD_MOSI, LCD_CS);
    hspi.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));

    lcd_cmd(0x01); delay(150);
    lcd_cmd(0x11); delay(150);
    lcd_cmd(0x29); delay(50);
    uint8_t mac[] = {0x28};
    lcd_cmd_data(0x36, mac, 1);
    uint8_t pf[] = {0x55};
    lcd_cmd_data(0x3A, pf, 1);

    /* Clear to black */
    uint8_t ca[] = {0x00, 0x00, 0x01, 0x3F};
    lcd_cmd_data(0x2A, ca, 4);
    uint8_t ra[] = {0x00, 0x00, 0x00, 0xEF};
    lcd_cmd_data(0x2B, ra, 4);
    digitalWrite(LCD_CS, LOW);
    digitalWrite(LCD_DC, LOW);
    hspi.transfer(0x2C);
    digitalWrite(LCD_DC, HIGH);
    for (uint32_t i = 0; i < 320UL * 240UL; i++) {
        hspi.transfer(0x00);
        hspi.transfer(0x00);
    }
    digitalWrite(LCD_CS, HIGH);
}

/* ============================================================
 * LVGL Display Driver (X-mirror fix in flush)
 * ============================================================ */

#define BUF_LINES 20
static lv_color_t draw_buf[LCD_W * BUF_LINES];

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint16_t x1 = area->x1, y1 = area->y1, x2 = area->x2, y2 = area->y2;
    uint32_t w = x2 - x1 + 1, h = y2 - y1 + 1;

    uint16_t mx1 = LCD_W - 1 - x2;
    uint16_t mx2 = LCD_W - 1 - x1;

    uint8_t ca[] = {(uint8_t)(mx1>>8),(uint8_t)mx1,(uint8_t)(mx2>>8),(uint8_t)mx2};
    lcd_cmd_data(0x2A, ca, 4);
    uint8_t ra[] = {(uint8_t)(y1>>8),(uint8_t)y1,(uint8_t)(y2>>8),(uint8_t)y2};
    lcd_cmd_data(0x2B, ra, 4);

    uint16_t *pixels = (uint16_t *)px_map;
    digitalWrite(LCD_CS, LOW);
    digitalWrite(LCD_DC, LOW);
    hspi.transfer(0x2C);
    digitalWrite(LCD_DC, HIGH);
    for (uint32_t row = 0; row < h; row++) {
        uint16_t *row_start = &pixels[row * w];
        for (int32_t col = w - 1; col >= 0; col--) {
            hspi.transfer(row_start[col] >> 8);
            hspi.transfer(row_start[col] & 0xFF);
        }
    }
    digitalWrite(LCD_CS, HIGH);
    lv_display_flush_ready(disp);
}

static uint32_t lv_tick_cb(void) { return millis(); }

/* ============================================================
 * Touch (XPT2046) — Y mirrored to match display
 * ============================================================ */

#include <XPT2046_Touchscreen.h>

#define TOUCH_CS  33
#define TOUCH_IRQ 36
#define TOUCH_CLK  25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32

static SPIClass touchSPI(VSPI);
static XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        int x = 319 - map(p.x, 200, 3700, 0, 319);  /* Mirror X */
        int y = 239 - map(p.y, 200, 3700, 0, 239);   /* Mirror Y */
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ============================================================
 * Serial Router + Dashboard UI
 * ============================================================ */

#include "../../src/claude_monitor_ui.h"

#define SERIAL_BUF_SIZE 1024
static char serial_buf[SERIAL_BUF_SIZE];
static uint16_t serial_pos = 0;

static void route_serial_message(const char *buf) {
    if (strstr(buf, "\"type\":\"windows\"") != NULL) {
        load_windows_from_json(buf);
        update_windows_ui();
    } else {
        load_data_from_json(buf);
        update_ui();
    }
}

static void check_serial_data(void) {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (serial_pos > 0) {
                serial_buf[serial_pos] = '\0';
                route_serial_message(serial_buf);
                serial_pos = 0;
            }
        } else {
            if (serial_pos < SERIAL_BUF_SIZE - 1) {
                serial_buf[serial_pos++] = c;
            } else {
                serial_pos = 0;
            }
        }
    }
}

/* ============================================================
 * Arduino setup() / loop()
 * ============================================================ */

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=================================");
    Serial.println(" Claude Monitor CYD v2.0");
    Serial.println(" Monitor + Window Switcher");
    Serial.println("=================================\n");

    lcd_init();
    Serial.println("[OK] Display initialized");

    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    ts.begin(touchSPI);
    ts.setRotation(1);
    Serial.println("[OK] Touch initialized (Y-mirrored)");

    lv_init();
    lv_tick_set_cb(lv_tick_cb);

    lv_display_t *disp = lv_display_create(LCD_W, LCD_H);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    Serial.println("[OK] LVGL ready");

    claude_monitor_create_ui();
    Serial.println("[OK] Dual-screen UI created");
    Serial.println("\nWaiting for JSON on Serial...");
}

void loop() {
    check_serial_data();
    uint32_t wait = lv_timer_handler();
    delay(min(wait, (uint32_t)5));
}
