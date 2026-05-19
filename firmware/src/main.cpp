/**
 * main.cpp - Claude Monitor CYD Firmware v4
 *
 * Two-screen: Monitor + Settings
 * Raw HSPI display (portrait 240x320, 180° rotation) + XPT2046 touch + LVGL
 * WiFi/HTTP + USB serial + NTP clock + wttr.in weather
 */

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <time.h>
#include "lvgl.h"

/* ============================================================
 * ILI9341 Raw SPI Driver
 * ============================================================ */

#define LCD_CS   15
#define LCD_DC    2
#define LCD_MOSI 13
#define LCD_MISO 12
#define LCD_SCLK 14
#define LCD_BL   21
#define LCD_W 240
#define LCD_H 320

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
    uint8_t mac[] = {0x08};  /* portrait, BGR */
    lcd_cmd_data(0x36, mac, 1);
    uint8_t pf[] = {0x55};
    lcd_cmd_data(0x3A, pf, 1);

    /* Clear to black */
    uint8_t ca[] = {0x00, 0x00, 0x00, 0xEF};  /* columns 0..239 */
    lcd_cmd_data(0x2A, ca, 4);
    uint8_t ra[] = {0x00, 0x00, 0x01, 0x3F};  /* rows 0..319 */
    lcd_cmd_data(0x2B, ra, 4);
    digitalWrite(LCD_CS, LOW);
    digitalWrite(LCD_DC, LOW);
    hspi.transfer(0x2C);
    digitalWrite(LCD_DC, HIGH);
    for (uint32_t i = 0; i < 240UL * 320UL; i++) {
        hspi.transfer(0x00);
        hspi.transfer(0x00);
    }
    digitalWrite(LCD_CS, HIGH);
}

/* ============================================================
 * LVGL Display Driver — 180° rotation (mirror both axes)
 * ============================================================ */

#define BUF_LINES 20
static lv_color_t draw_buf[LCD_W * BUF_LINES];

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint16_t x1 = area->x1, y1 = area->y1, x2 = area->x2, y2 = area->y2;
    uint32_t w = x2 - x1 + 1, h = y2 - y1 + 1;

    uint16_t mx1 = LCD_W - 1 - x2, mx2 = LCD_W - 1 - x1;
    uint16_t my1 = LCD_H - 1 - y2, my2 = LCD_H - 1 - y1;

    uint8_t ca[] = {(uint8_t)(mx1>>8),(uint8_t)mx1,(uint8_t)(mx2>>8),(uint8_t)mx2};
    lcd_cmd_data(0x2A, ca, 4);
    uint8_t ra[] = {(uint8_t)(my1>>8),(uint8_t)my1,(uint8_t)(my2>>8),(uint8_t)my2};
    lcd_cmd_data(0x2B, ra, 4);

    uint16_t *pixels = (uint16_t *)px_map;
    digitalWrite(LCD_CS, LOW);
    digitalWrite(LCD_DC, LOW);
    hspi.transfer(0x2C);
    digitalWrite(LCD_DC, HIGH);
    for (int32_t row = h - 1; row >= 0; row--) {
        for (int32_t col = w - 1; col >= 0; col--) {
            uint16_t px = pixels[row * w + col];
            hspi.transfer(px >> 8);
            hspi.transfer(px & 0xFF);
        }
    }
    digitalWrite(LCD_CS, HIGH);
    lv_display_flush_ready(disp);
}

static uint32_t lv_tick_cb(void) { return millis(); }

/* ============================================================
 * Touch (XPT2046) — portrait + 180°
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
        int x = map(p.y, 200, 3700, 0, 239);
        int y = 319 - map(p.x, 200, 3700, 0, 319);
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ============================================================
 * UI Header
 * ============================================================ */

#include "../../src/claude_monitor_ui.h"

/* ============================================================
 * Forward declarations
 * ============================================================ */
static bool wifi_connected = false;

/* ============================================================
 * Clock + Countdown
 * ============================================================ */

static time_t g_reset_epoch = 0;   /* absolute time of next reset */

static void update_reset_epoch(void) {
    if (g_data.fh_reset_min > 0) {
        g_reset_epoch = time(nullptr) + (time_t)g_data.fh_reset_min * 60;
    }
}

static void update_clock_and_countdown(void) {
    struct tm ti;
    time_t now = time(nullptr);
    int secs_left = (g_reset_epoch > now) ? (int)(g_reset_epoch - now) : 0;

    if (!getLocalTime(&ti)) {
        update_realtime_ui("--:--", "", secs_left);
        return;
    }
    char timebuf[8], datebuf[16];
    strftime(timebuf, sizeof(timebuf), "%H:%M", &ti);
    strftime(datebuf, sizeof(datebuf), "%a %d %b", &ti);  /* "Mon 17 May" */
    update_realtime_ui(timebuf, datebuf, secs_left);
}

/* ============================================================
 * Weather — wttr.in plain text, no API key
 * ============================================================ */

static unsigned long last_weather_ms = 0;

static void fetch_weather(void) {
    if (!wifi_connected || WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin("http://wttr.in/Kharkiv?format=%C+%t");
    http.setTimeout(8000);
    int code = http.GET();
    if (code == 200) {
        /* Read stream directly — avoids String heap allocation on repeated calls */
        WiFiClient *stream = http.getStreamPtr();
        char out[28] = {0};
        int oi = 0;
        unsigned long deadline = millis() + 3000;
        while (oi < 27 && millis() < deadline) {
            if (stream->available()) {
                int c = stream->read();
                if (c < 0) break;
                if ((uint8_t)c >= 0x20) out[oi++] = (char)c;
            }
        }
        out[oi] = '\0';
        Serial.printf("[Weather] HTTP %d oi=%d '%s'\n", code, oi, out);
        if (oi > 0) {
            strncpy(g_weather_str, out, sizeof(g_weather_str) - 1);
            update_weather_display(g_weather_str);
        }
    } else {
        Serial.printf("[Weather] HTTP %d\n", code);
    }
    http.end();
}

/* ============================================================
 * Timezone — NVS persistence
 * ============================================================ */

static void load_tz_from_nvs(void) {
    Preferences prefs;
    prefs.begin("cyd", true);
    g_tz_offset = prefs.getInt("tz_offset", 3);
    prefs.end();
}

static void save_tz_to_nvs(void) {
    Preferences prefs;
    prefs.begin("cyd", false);
    prefs.putInt("tz_offset", g_tz_offset);
    prefs.end();
}

static void apply_timezone(void) {
    configTime((long)g_tz_offset * 3600L, 0, "pool.ntp.org");
}

/* ============================================================
 * Serial Router
 * ============================================================ */

#define SERIAL_BUF_SIZE 1024
static char serial_buf[SERIAL_BUF_SIZE];
static uint16_t serial_pos = 0;

static void handle_wifi_config(const char *buf);

static void route_serial_message(const char *buf) {
    if (strstr(buf, "\"wifi_config\"") != NULL) {
        handle_wifi_config(buf);
    } else {
        load_data_from_json(buf);
        update_reset_epoch();
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
 * WiFi + HTTP Server
 * ============================================================ */

static WebServer http_server(80);
static char http_body_buf[SERIAL_BUF_SIZE];

static void http_handle_monitor(void) {
    if (http_server.hasArg("plain") && http_server.arg("plain").length() > 0) {
        strncpy(http_body_buf, http_server.arg("plain").c_str(), SERIAL_BUF_SIZE - 1);
        http_body_buf[SERIAL_BUF_SIZE - 1] = '\0';
        load_data_from_json(http_body_buf);
        update_reset_epoch();
        update_ui();
        http_server.send(200, "application/json", "{\"ok\":true}");
    } else {
        http_server.send(400, "application/json", "{\"error\":\"no body\"}");
    }
}

static void http_handle_status(void) {
    char resp[128];
    snprintf(resp, sizeof(resp),
        "{\"ok\":true,\"version\":\"4.0\",\"ip\":\"%s\"}",
        WiFi.localIP().toString().c_str());
    http_server.send(200, "application/json", resp);
}

static void http_handle_not_found(void) {
    http_server.send(404, "application/json", "{\"error\":\"not found\"}");
}

/* ============================================================
 * AP Captive Portal
 * ============================================================ */

static WebServer ap_server(80);
static bool ap_mode_active = false;
static bool ap_reboot_pending = false;

static void url_decode(char *dst, const char *src, size_t dst_len) {
    size_t di = 0;
    while (*src && di < dst_len - 1) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], '\0'};
            dst[di++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[di++] = ' ';
            src++;
        } else {
            dst[di++] = *src++;
        }
    }
    dst[di] = '\0';
}

static bool extract_form_field(const char *body, const char *field,
                                char *out, size_t out_len) {
    char search[64];
    snprintf(search, sizeof(search), "%s=", field);
    const char *p = strstr(body, search);
    if (!p) return false;
    p += strlen(search);
    const char *end = strchr(p, '&');
    char raw[256] = {0};
    size_t raw_len = end ? (size_t)(end - p) : strlen(p);
    if (raw_len >= sizeof(raw)) raw_len = sizeof(raw) - 1;
    memcpy(raw, p, raw_len);
    raw[raw_len] = '\0';
    url_decode(out, raw, out_len);
    return out[0] != '\0';
}

static const char AP_SETUP_HTML[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>CYD WiFi Setup</title>"
    "<style>body{font-family:sans-serif;background:#1a1a2e;color:#e0e0e0;"
    "display:flex;flex-direction:column;align-items:center;padding:20px}"
    "h2{color:#00b4d8}input{width:260px;padding:8px;margin:6px 0;"
    "background:#16213e;color:#e0e0e0;border:1px solid #2a2a40;border-radius:6px}"
    "button{padding:10px 30px;background:#0f3460;color:#e0e0e0;"
    "border:2px solid #00b4d8;border-radius:6px;cursor:pointer;margin-top:10px}"
    "button:hover{background:#00b4d8;color:#1a1a2e}"
    "</style></head><body>"
    "<h2>CYD WiFi Setup</h2>"
    "<form method='post' action='/save'>"
    "<p><label>SSID<br><input name='ssid' type='text' autocomplete='off'></label></p>"
    "<p><label>Password<br><input name='pass' type='password' autocomplete='off'></label></p>"
    "<button type='submit'>Save &amp; Connect</button>"
    "</form></body></html>";

static const char AP_SAVED_HTML[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<title>Saved</title>"
    "<style>body{font-family:sans-serif;background:#1a1a2e;color:#06d6a0;"
    "display:flex;align-items:center;justify-content:center;height:100vh}"
    "p{font-size:1.3em;text-align:center}</style></head><body>"
    "<p>Credentials saved!<br>Device will reboot and connect.</p>"
    "</body></html>";

static void ap_handle_root(void) {
    ap_server.send(200, "text/html", AP_SETUP_HTML);
}

static void ap_handle_save(void) {
    String body = ap_server.hasArg("plain") ? ap_server.arg("plain") : "";
    if (body.length() == 0) {
        String s = ap_server.arg("ssid");
        String pw = ap_server.arg("pass");
        if (s.length() > 0) {
            Preferences prefs;
            prefs.begin("wifi", false);
            prefs.putString("ssid", s);
            prefs.putString("pass", pw);
            prefs.end();
            Serial.printf("[AP] Saved credentials for '%s'. Rebooting...\n", s.c_str());
            ap_server.send(200, "text/html", AP_SAVED_HTML);
            ap_reboot_pending = true;
            return;
        }
        ap_server.send(400, "text/plain", "Missing fields");
        return;
    }

    char ssid[64] = {0}, pass[64] = {0};
    if (!extract_form_field(body.c_str(), "ssid", ssid, sizeof(ssid))) {
        ap_server.send(400, "text/plain", "Missing ssid");
        return;
    }
    extract_form_field(body.c_str(), "pass", pass, sizeof(pass));

    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();

    Serial.printf("[AP] Saved credentials for '%s'. Rebooting...\n", ssid);
    ap_server.send(200, "text/html", AP_SAVED_HTML);
    ap_reboot_pending = true;
}

static void ap_handle_not_found(void) {
    ap_server.sendHeader("Location", "http://192.168.4.1/", true);
    ap_server.send(302, "text/plain", "");
}

static void start_ap_portal(void) {
    if (ap_mode_active) return;
    WiFi.disconnect(false);
    WiFi.mode(WIFI_AP);
    WiFi.softAP("CYD-Setup");
    ap_server.on("/",      HTTP_GET,  ap_handle_root);
    ap_server.on("/save",  HTTP_GET,  ap_handle_root);
    ap_server.on("/save",  HTTP_POST, ap_handle_save);
    ap_server.onNotFound(ap_handle_not_found);
    ap_server.begin();
    ap_mode_active = true;
    ap_reboot_pending = false;
    Serial.println("[AP] Portal started: SSID=CYD-Setup  IP=192.168.4.1");
    g_wifi_ui_state = WIFI_STATE_AP_ACTIVE;
    strncpy(g_wifi_ui_ssid, "CYD-Setup", sizeof(g_wifi_ui_ssid) - 1);
    strncpy(g_wifi_ui_ip,   "192.168.4.1", sizeof(g_wifi_ui_ip) - 1);
    update_wifi_ui();
}

static void stop_ap_portal(void) {
    if (!ap_mode_active) return;
    ap_server.stop();
    WiFi.softAPdisconnect(true);
    ap_mode_active = false;
    ap_reboot_pending = false;
    Serial.println("[AP] Portal stopped");
    WiFi.mode(WIFI_STA);
    g_wifi_ui_state = wifi_connected ? WIFI_STATE_CONNECTED : WIFI_STATE_DISCONNECTED;
    update_wifi_ui();
}

/* ============================================================
 * WiFi STA Setup
 * ============================================================ */

static void wifi_setup(void) {
    Preferences prefs;
    prefs.begin("wifi", true);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();

    if (ssid.length() == 0) {
        Serial.println("[WiFi] No credentials — use Settings > Setup WiFi");
        g_wifi_ui_state = WIFI_STATE_DISCONNECTED;
        g_wifi_ui_ssid[0] = '\0';
        g_wifi_ui_ip[0]   = '\0';
        update_wifi_ui();
        return;
    }

    Serial.printf("[WiFi] Connecting to %s", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\n[WiFi] Failed — USB serial fallback");
        g_wifi_ui_state = WIFI_STATE_DISCONNECTED;
        strncpy(g_wifi_ui_ssid, ssid.c_str(), sizeof(g_wifi_ui_ssid) - 1);
        g_wifi_ui_ip[0] = '\0';
        update_wifi_ui();
        return;
    }

    wifi_connected = true;
    String ip = WiFi.localIP().toString();
    Serial.printf("\n[WiFi] Connected! IP: %s\n", ip.c_str());

    g_wifi_ui_state = WIFI_STATE_CONNECTED;
    strncpy(g_wifi_ui_ssid, ssid.c_str(), sizeof(g_wifi_ui_ssid) - 1);
    strncpy(g_wifi_ui_ip,   ip.c_str(),   sizeof(g_wifi_ui_ip)   - 1);
    update_wifi_ui();

    if (MDNS.begin("claude-monitor")) {
        Serial.println("[mDNS] claude-monitor.local ready");
    }

    http_server.on("/api/monitor", HTTP_POST, http_handle_monitor);
    http_server.on("/api/status",  HTTP_GET,  http_handle_status);
    http_server.onNotFound(http_handle_not_found);
    http_server.begin();
    Serial.println("[HTTP] Server on port 80");

    /* NTP */
    load_tz_from_nvs();
    apply_timezone();
    update_tz_display();
    Serial.printf("[NTP] Syncing (UTC%+d)...\n", g_tz_offset);

    /* Initial weather fetch after a short delay for NTP to settle */
    delay(1000);
    fetch_weather();
    last_weather_ms = millis();
}

static void handle_wifi_config(const char *buf) {
    auto extract = [](const char *src, const char *key, char *out, size_t out_len) -> bool {
        char search[64];
        snprintf(search, sizeof(search), "\"%s\":", key);
        const char *p = strstr(src, search);
        if (!p) return false;
        p += strlen(search);
        while (*p == ' ') p++;
        if (*p != '"') return false;
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i < out_len - 1) out[i++] = *p++;
        out[i] = '\0';
        return i > 0;
    };

    char ssid[64] = {0}, pass[64] = {0};
    if (!extract(buf, "ssid", ssid, sizeof(ssid))) {
        Serial.println("[WiFi] wifi_config: missing ssid");
        return;
    }
    extract(buf, "pass", pass, sizeof(pass));

    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();

    Serial.printf("[WiFi] Saved credentials for '%s'. Rebooting...\n", ssid);
    delay(500);
    ESP.restart();
}

/* ============================================================
 * Arduino setup() / loop()
 * ============================================================ */

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=================================");
    Serial.println(" Claude Monitor CYD v4.0");
    Serial.println(" Clock + Weather + Reset countdown");
    Serial.println("=================================\n");

    lcd_init();
    Serial.println("[OK] Display initialized");

    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    ts.begin(touchSPI);
    ts.setRotation(1);
    Serial.println("[OK] Touch initialized");

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
    Serial.println("[OK] UI created");

    wifi_setup();

    if (!wifi_connected) {
        Serial.println("[USB] Waiting for JSON on Serial...");
    }
}

void loop() {
    check_serial_data();

    /* Clock update every second */
    static unsigned long last_clock_ms = 0;
    unsigned long now_ms = millis();
    if (now_ms - last_clock_ms >= 1000UL) {
        last_clock_ms = now_ms;
        update_clock_and_countdown();
    }

    /* Weather refresh every 3 minutes */
    if (wifi_connected && (now_ms - last_weather_ms >= 3UL * 60UL * 1000UL)) {
        last_weather_ms = now_ms;
        fetch_weather();
    }

    /* Timezone change from [−]/[+] buttons in Settings */
    if (g_tz_change != 0) {
        g_tz_offset += g_tz_change;
        if (g_tz_offset >  14) g_tz_offset =  14;
        if (g_tz_offset < -12) g_tz_offset = -12;
        g_tz_change = 0;
        if (wifi_connected) apply_timezone();
        save_tz_to_nvs();
        update_tz_display();
    }

    /* AP portal button from Settings */
    if (g_request_ap_portal) {
        g_request_ap_portal = false;
        if (!ap_mode_active) start_ap_portal();
    }
    if (g_cancel_ap_portal) {
        g_cancel_ap_portal = false;
        stop_ap_portal();
    }

    if (ap_mode_active) {
        ap_server.handleClient();
        if (ap_reboot_pending) {
            delay(1000);
            ESP.restart();
        }
    }

    if (wifi_connected) {
        http_server.handleClient();
    }

    uint32_t wait = lv_timer_handler();
    delay(min(wait, (uint32_t)5));
}
