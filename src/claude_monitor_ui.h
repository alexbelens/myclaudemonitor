/**
 * claude_monitor_ui.h — Portrait 240×320 dashboard
 *
 * Screen 0: Monitor  — clock, weather, 5h bar, 7d bar, plan name
 * Screen 1: Settings — WiFi setup + timezone
 */

#ifndef CLAUDE_MONITOR_UI_H
#define CLAUDE_MONITOR_UI_H

#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP32
#include <Arduino.h>
#define SERIAL_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define SERIAL_PRINTF(...) printf(__VA_ARGS__)
#endif

/* ============================================================
 * DATA MODEL
 * ============================================================ */
typedef struct {
    int32_t fh_pct;          /* 5-hour utilization 0-100 */
    int32_t fh_reset_min;    /* minutes until 5h block resets */
    int32_t sd_pct;          /* 7-day utilization 0-100 */
    int32_t sd_reset_min;    /* minutes until 7d block resets */
    int32_t warning_level;   /* 0=ok 1=50% 2=75% 3=90% */
    char    plan_name[16];
} claude_data_t;

static claude_data_t g_data = {
    0, 300, 0, 10080, 0, "Pro",
};

/* ============================================================
 * SHARED STATE (read/written by main.cpp)
 * ============================================================ */
static char g_weather_str[48] = "--";   /* raw from fetch: "Partly cloudy +15°C" */
static int  g_tz_offset       = 3;
static int  g_tz_change       = 0;

/* Parsed weather (filled by update_weather_display) */
static char g_weather_temp[16] = "--";
static char g_weather_cond[32] = "--";

/* WiFi provisioning */
typedef enum { WIFI_STATE_DISCONNECTED, WIFI_STATE_CONNECTED, WIFI_STATE_AP_ACTIVE } wifi_ui_state_t;
static wifi_ui_state_t g_wifi_ui_state = WIFI_STATE_DISCONNECTED;
static char g_wifi_ui_ssid[64] = {0};
static char g_wifi_ui_ip[32]   = {0};
static bool g_request_ap_portal = false;
static bool g_cancel_ap_portal  = false;

/* ============================================================
 * COLORS — Dark Navy + Electric Cyan
 * ============================================================ */
#define CM_BG          lv_color_hex(0x080818)
#define CM_SURFACE     lv_color_hex(0x0F0F28)
#define CM_HEADER_BG   lv_color_hex(0x0A0A20)
#define CM_ACCENT      lv_color_hex(0x00C8FF)
#define CM_GREEN       lv_color_hex(0x00FF88)
#define CM_YELLOW      lv_color_hex(0xFFCC00)
#define CM_ORANGE      lv_color_hex(0xFF7700)
#define CM_RED         lv_color_hex(0xFF2255)
#define CM_TEXT_PRIM   lv_color_hex(0xEEEEFF)
#define CM_TEXT_SEC    lv_color_hex(0x9999CC)
#define CM_TEXT_DIM    lv_color_hex(0x7777AA)
#define CM_BAR_BG      lv_color_hex(0x14143A)
#define CM_DIVIDER     lv_color_hex(0x18183A)
#define CM_TAB_ACTIVE  lv_color_hex(0x00C8FF)
#define CM_TAB_BG      lv_color_hex(0x0A0A20)
#define CM_BTN_BG      lv_color_hex(0x0F0F28)
#define CM_BTN_PRESS   lv_color_hex(0x1A1A40)

/* ============================================================
 * SCREENS & WIDGETS
 * ============================================================ */
static lv_obj_t *scr_monitor;
static lv_obj_t *scr_settings;
static int current_screen = 0;

/* Monitor */
static lv_obj_t *lbl_clock;
static lv_obj_t *lbl_date;
static lv_obj_t *lbl_weather_temp;
static lv_obj_t *lbl_weather_cond;
static lv_obj_t *accent_bar;
/* 5H zone */
static lv_obj_t *lbl_fh_pct;
static lv_obj_t *bar_fh;
static lv_obj_t *lbl_fh_remain;
/* 7D zone */
static lv_obj_t *lbl_sd_pct;
static lv_obj_t *bar_sd;
static lv_obj_t *lbl_sd_remain;
/* Plan name */
static lv_obj_t *lbl_plan_name;

/* Settings */
static lv_obj_t *lbl_wifi_state;
static lv_obj_t *lbl_wifi_ssid_val;
static lv_obj_t *lbl_wifi_ip_val;
static lv_obj_t *btn_wifi_action;
static lv_obj_t *lbl_btn_wifi_action;
static lv_obj_t *lbl_tz;

/* ============================================================
 * HELPERS
 * ============================================================ */
static lv_color_t pct_color(int pct) {
    if (pct >= 90) return CM_RED;
    if (pct >= 75) return CM_ORANGE;
    if (pct >= 50) return CM_YELLOW;
    return CM_GREEN;
}

static lv_color_t warning_bar_color(int pct) {
    if (pct >= 90) return CM_RED;
    if (pct >= 75) return CM_ORANGE;
    if (pct >= 50) return CM_YELLOW;
    return CM_ACCENT;
}

static void fmt_time(int minutes, char *buf, size_t len) {
    int h = minutes / 60, m = minutes % 60;
    if (h > 0) snprintf(buf, len, "%dh %02dm", h, m);
    else        snprintf(buf, len, "%dm", m);
}

static void fmt_time_long(int minutes, char *buf, size_t len) {
    if (minutes >= 24 * 60) {
        int d = minutes / (24 * 60);
        int h = (minutes % (24 * 60)) / 60;
        snprintf(buf, len, "%dd %dh", d, h);
    } else {
        fmt_time(minutes, buf, len);
    }
}

/* ============================================================
 * WEATHER PARSING — split "Partly cloudy +15°C" into cond + temp
 * ============================================================ */
static void parse_weather_str(const char *raw) {
    /* Find last space before a +/- sign or digit = start of temperature */
    const char *last_sp = NULL;
    for (const char *p = raw; *p; p++) {
        if (*p == ' ') {
            char next = *(p + 1);
            if (next == '+' || next == '-' ||
                (next >= '0' && next <= '9')) {
                last_sp = p;
            }
        }
    }
    if (last_sp && last_sp > raw) {
        size_t clen = (size_t)(last_sp - raw);
        if (clen >= sizeof(g_weather_cond)) clen = sizeof(g_weather_cond) - 1;
        memcpy(g_weather_cond, raw, clen);
        g_weather_cond[clen] = '\0';
        strncpy(g_weather_temp, last_sp + 1, sizeof(g_weather_temp) - 1);
        g_weather_temp[sizeof(g_weather_temp)-1] = '\0';
    } else {
        strncpy(g_weather_cond, raw, sizeof(g_weather_cond) - 1);
        strncpy(g_weather_temp, "--", sizeof(g_weather_temp) - 1);
    }
}

/* ============================================================
 * JSON MINI-PARSER
 * ============================================================ */
static const char* jfind(const char *json, const char *key) {
    char s[64]; snprintf(s, sizeof(s), "\"%s\"", key);
    const char *p = strstr(json, s);
    if (!p) return NULL;
    p = strchr(p, ':'); if (!p) return NULL;
    p++; while (*p == ' ' || *p == '\t') p++;
    return p;
}
static int  jint(const char *j, const char *k, int d) { const char *v=jfind(j,k); return v?atoi(v):d; }
static void jstr(const char *j, const char *k, char *o, size_t l, const char *d) {
    const char *v = jfind(j, k);
    if (!v || *v != '"') { strncpy(o, d, l); return; }
    v++; size_t i=0; while (*v && *v!='"' && i<l-1) o[i++]=*v++; o[i]='\0';
}

/* ============================================================
 * DATA LOADING
 * ============================================================ */
static void load_data_from_json(const char *buf) {
    g_data.fh_pct       = jint(buf, "fh_pct",       g_data.fh_pct);
    g_data.fh_reset_min = jint(buf, "fh_reset_min",  g_data.fh_reset_min);
    g_data.sd_pct       = jint(buf, "sd_pct",        g_data.sd_pct);
    g_data.sd_reset_min = jint(buf, "sd_reset_min",  g_data.sd_reset_min);
    g_data.warning_level = jint(buf, "warning_level", g_data.warning_level);
    jstr(buf, "plan_name", g_data.plan_name, sizeof(g_data.plan_name), g_data.plan_name);
}

#ifndef ESP32
#define DATA_FILE "/tmp/claude_monitor_data.json"
static void load_data_from_file(void) {
    FILE *f = fopen(DATA_FILE, "r"); if (!f) return;
    char buf[1024]; size_t n = fread(buf, 1, sizeof(buf)-1, f); fclose(f);
    buf[n]='\0'; load_data_from_json(buf);
}
#endif

/* ============================================================
 * SCREEN SWITCHING
 * ============================================================ */
static void switch_to_screen(int idx);
static void tab_monitor_cb(lv_event_t *e)  { (void)e; switch_to_screen(0); }
static void tab_settings_cb(lv_event_t *e) { (void)e; switch_to_screen(1); }

static void switch_to_screen(int idx) {
    if (idx == current_screen) return;
    lv_obj_t *targets[] = { scr_monitor, scr_settings };
    lv_scr_load_anim_t anim = (idx > current_screen)
        ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT;
    current_screen = idx;
    lv_scr_load_anim(targets[idx], anim, 200, 0, false);
}

/* ============================================================
 * BUTTON CALLBACKS
 * ============================================================ */
static void wifi_action_btn_cb(lv_event_t *e) {
    (void)e;
    if (g_wifi_ui_state == WIFI_STATE_AP_ACTIVE) g_cancel_ap_portal = true;
    else g_request_ap_portal = true;
}
static void tz_minus_cb(lv_event_t *e) { (void)e; g_tz_change = -1; }
static void tz_plus_cb(lv_event_t *e)  { (void)e; g_tz_change = +1; }

/* ============================================================
 * WIDGET HELPERS
 * ============================================================ */
static lv_obj_t* make_bar(lv_obj_t *parent, lv_color_t color, int x, int y, int w, int h) {
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, h);
    lv_obj_set_pos(bar, x, y);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, CM_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(bar, 500, LV_PART_INDICATOR);
    return bar;
}

static lv_obj_t* make_label(lv_obj_t *parent, const char *txt,
                             const lv_font_t *font, lv_color_t color,
                             int x, int y) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t* make_divider(lv_obj_t *parent, int y, lv_color_t color) {
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_set_size(d, 240, 1);
    lv_obj_set_pos(d, 0, y);
    lv_obj_set_style_bg_color(d, color, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_radius(d, 0, 0);
    lv_obj_set_scrollbar_mode(d, LV_SCROLLBAR_MODE_OFF);
    return d;
}

static lv_obj_t* make_btn(lv_obj_t *parent, const char *txt,
                           int x, int y, int w, int h,
                           lv_event_cb_t cb, void *ud) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, CM_BTN_BG, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, CM_BTN_PRESS, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, CM_ACCENT, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, CM_TEXT_PRIM, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    return btn;
}

/* 2-tab bar at y=296 */
static void create_tab_bar(lv_obj_t *scr, int active_idx) {
    lv_obj_t *bg = lv_obj_create(scr);
    lv_obj_set_size(bg, 240, 24);
    lv_obj_set_pos(bg, 0, 296);
    lv_obj_set_style_bg_color(bg, CM_TAB_BG, 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_set_scrollbar_mode(bg, LV_SCROLLBAR_MODE_OFF);

    struct { const char *text; lv_event_cb_t cb; } tabs[] = {
        { "Monitor",  tab_monitor_cb  },
        { "Settings", tab_settings_cb },
    };
    int tab_w = 110, gap = 8;
    int start = (240 - (2 * tab_w + gap)) / 2;

    for (int i = 0; i < 2; i++) {
        lv_obj_t *btn = lv_btn_create(bg);
        lv_obj_set_size(btn, tab_w, 22);
        lv_obj_set_pos(btn, start + i * (tab_w + gap), 1);
        lv_obj_set_style_bg_color(btn, i == active_idx ? CM_TAB_ACTIVE : CM_DIVIDER, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_add_event_cb(btn, tabs[i].cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, tabs[i].text);
        lv_obj_set_style_text_color(lbl, i == active_idx ? CM_BG : CM_TEXT_SEC, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    }
}

/* ============================================================
 * UPDATE: REALTIME — clock + date + 5h remain (called every second)
 * ============================================================ */
static void update_realtime_ui(const char *timebuf, const char *datebuf, int secs_left) {
    lv_label_set_text(lbl_clock, timebuf);
    if (datebuf && datebuf[0]) lv_label_set_text(lbl_date, datebuf);

    char remain[20];
    lv_color_t col;
    if (secs_left <= 0) {
        snprintf(remain, sizeof(remain), "resetting...");
        col = CM_RED;
    } else {
        int h = secs_left / 3600;
        int m = (secs_left % 3600) / 60;
        int s = secs_left % 60;
        if (h > 0) snprintf(remain, sizeof(remain), "%dh %02dm %02ds left", h, m, s);
        else        snprintf(remain, sizeof(remain), "%dm %02ds left", m, s);
        col = secs_left < 900  ? CM_RED    :
              secs_left < 1800 ? CM_ORANGE :
              secs_left < 3600 ? CM_YELLOW : CM_ACCENT;
    }
    lv_label_set_text(lbl_fh_remain, remain);
    lv_obj_set_style_text_color(lbl_fh_remain, col, 0);
}

/* ============================================================
 * UPDATE: WEATHER (called after fetch from main.cpp)
 * ============================================================ */
static void update_weather_display(const char *raw) {
    strncpy(g_weather_str, raw, sizeof(g_weather_str) - 1);
    parse_weather_str(raw);
    lv_label_set_text(lbl_weather_temp, g_weather_temp);
    lv_label_set_text(lbl_weather_cond, g_weather_cond);
    lv_obj_invalidate(lbl_weather_temp);
    lv_obj_invalidate(lbl_weather_cond);
}

/* ============================================================
 * UPDATE: TIMEZONE DISPLAY
 * ============================================================ */
static void update_tz_display(void) {
    char buf[16];
    if (g_tz_offset >= 0) snprintf(buf, sizeof(buf), "UTC+%d", g_tz_offset);
    else                   snprintf(buf, sizeof(buf), "UTC%d",  g_tz_offset);
    lv_label_set_text(lbl_tz, buf);
}

/* ============================================================
 * UPDATE: WIFI STATUS in Settings
 * ============================================================ */
static void update_wifi_ui(void) {
    switch (g_wifi_ui_state) {
        case WIFI_STATE_CONNECTED:
            lv_label_set_text(lbl_wifi_state, "Connected");
            lv_obj_set_style_text_color(lbl_wifi_state, CM_GREEN, 0);
            lv_label_set_text(lbl_wifi_ssid_val, g_wifi_ui_ssid[0] ? g_wifi_ui_ssid : "--");
            lv_label_set_text(lbl_wifi_ip_val,   g_wifi_ui_ip[0]   ? g_wifi_ui_ip   : "--");
            lv_label_set_text(lbl_btn_wifi_action, "Reconfigure");
            break;
        case WIFI_STATE_AP_ACTIVE:
            lv_label_set_text(lbl_wifi_state, "Setup active");
            lv_obj_set_style_text_color(lbl_wifi_state, CM_YELLOW, 0);
            lv_label_set_text(lbl_wifi_ssid_val, "CYD-Setup");
            lv_label_set_text(lbl_wifi_ip_val,   "192.168.4.1");
            lv_label_set_text(lbl_btn_wifi_action, "Cancel");
            break;
        default:
            lv_label_set_text(lbl_wifi_state, "Not connected");
            lv_obj_set_style_text_color(lbl_wifi_state, CM_RED, 0);
            lv_label_set_text(lbl_wifi_ssid_val, "--");
            lv_label_set_text(lbl_wifi_ip_val,   "--");
            lv_label_set_text(lbl_btn_wifi_action, "Setup WiFi");
            break;
    }
}

/* ============================================================
 * UPDATE: MONITOR DATA (called when bridge sends payload)
 * ============================================================ */
static void update_ui(void) {
    char tmp[32];

    /* 5H bar */
    snprintf(tmp, sizeof(tmp), "%d%%", g_data.fh_pct);
    lv_label_set_text(lbl_fh_pct, tmp);
    lv_bar_set_value(bar_fh, g_data.fh_pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_fh, warning_bar_color(g_data.fh_pct), LV_PART_INDICATOR);

    /* 7D bar */
    snprintf(tmp, sizeof(tmp), "%d%%", g_data.sd_pct);
    lv_label_set_text(lbl_sd_pct, tmp);
    lv_bar_set_value(bar_sd, g_data.sd_pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_sd, warning_bar_color(g_data.sd_pct), LV_PART_INDICATOR);

    char remain[20];
    fmt_time_long(g_data.sd_reset_min, remain, sizeof(remain));
    snprintf(tmp, sizeof(tmp), "%s left", remain);
    lv_label_set_text(lbl_sd_remain, tmp);

    /* Plan name */
    lv_label_set_text(lbl_plan_name, g_data.plan_name);
}

/* ============================================================
 * BUILD: MONITOR SCREEN
 *
 *  y=  0.. 70  Header: clock / date  |  temp / cond
 *  y= 70.. 73  Alert stripe (air-raid, pending API token)
 *  y= 80..138  5H zone: label+% / bar / remain (ticking)
 *  y=143       Divider
 *  y=150..208  7D zone: label+% / bar / remain
 *  y=213       Divider
 *  y=230..268  Plan name (large, centered)
 *  y=272       Divider
 *  y=296       Tab bar
 * ============================================================ */
static void build_monitor_screen(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, CM_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ── Header (0..70) ── */
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 240, 70);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, CM_HEADER_BG, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_scrollbar_mode(hdr, LV_SCROLLBAR_MODE_OFF);

    lbl_clock = lv_label_create(hdr);
    lv_label_set_text(lbl_clock, "--:--");
    lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_clock, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(lbl_clock, 8, 4);

    lbl_date = lv_label_create(hdr);
    lv_label_set_text(lbl_date, "---");
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_date, CM_TEXT_SEC, 0);
    lv_obj_set_pos(lbl_date, 8, 44);

    lbl_weather_temp = lv_label_create(hdr);
    lv_label_set_text(lbl_weather_temp, "--");
    lv_obj_set_style_text_font(lbl_weather_temp, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_weather_temp, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(lbl_weather_temp, 118, 4);
    lv_obj_set_width(lbl_weather_temp, 114);
    lv_obj_set_style_text_align(lbl_weather_temp, LV_TEXT_ALIGN_RIGHT, 0);

    lbl_weather_cond = lv_label_create(hdr);
    lv_label_set_text(lbl_weather_cond, "--");
    lv_obj_set_style_text_font(lbl_weather_cond, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_weather_cond, CM_TEXT_SEC, 0);
    lv_obj_set_pos(lbl_weather_cond, 118, 44);
    lv_obj_set_width(lbl_weather_cond, 114);
    lv_obj_set_style_text_align(lbl_weather_cond, LV_TEXT_ALIGN_RIGHT, 0);

    /* ── Alert stripe (y=70..73) — green=safe, red=alert ── */
    accent_bar = lv_obj_create(scr);
    lv_obj_set_size(accent_bar, 240, 3);
    lv_obj_set_pos(accent_bar, 0, 70);
    lv_obj_set_style_bg_color(accent_bar, CM_DIVIDER, 0);
    lv_obj_set_style_bg_opa(accent_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(accent_bar, 0, 0);
    lv_obj_set_style_radius(accent_bar, 0, 0);
    lv_obj_set_scrollbar_mode(accent_bar, LV_SCROLLBAR_MODE_OFF);

    /* ── 5H zone (y=80..138) ── */
    make_label(scr, "5H", &lv_font_montserrat_10, CM_TEXT_SEC, 8, 82);

    lbl_fh_pct = lv_label_create(scr);
    lv_label_set_text(lbl_fh_pct, "0%");
    lv_obj_set_style_text_font(lbl_fh_pct, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_fh_pct, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(lbl_fh_pct, 195, 82);
    lv_obj_set_width(lbl_fh_pct, 37);
    lv_obj_set_style_text_align(lbl_fh_pct, LV_TEXT_ALIGN_RIGHT, 0);

    bar_fh = make_bar(scr, CM_ACCENT, 6, 97, 228, 14);

    lbl_fh_remain = lv_label_create(scr);
    lv_label_set_text(lbl_fh_remain, "-- left");
    lv_obj_set_style_text_font(lbl_fh_remain, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_fh_remain, CM_ACCENT, 0);
    lv_obj_set_width(lbl_fh_remain, 240);
    lv_obj_set_style_text_align(lbl_fh_remain, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_fh_remain, 0, 118);

    /* ── Divider ── */
    make_divider(scr, 143, CM_DIVIDER);

    /* ── 7D zone (y=150..208) ── */
    make_label(scr, "7D", &lv_font_montserrat_10, CM_TEXT_SEC, 8, 152);

    lbl_sd_pct = lv_label_create(scr);
    lv_label_set_text(lbl_sd_pct, "0%");
    lv_obj_set_style_text_font(lbl_sd_pct, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_sd_pct, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(lbl_sd_pct, 195, 152);
    lv_obj_set_width(lbl_sd_pct, 37);
    lv_obj_set_style_text_align(lbl_sd_pct, LV_TEXT_ALIGN_RIGHT, 0);

    bar_sd = make_bar(scr, CM_GREEN, 6, 167, 228, 14);

    lbl_sd_remain = lv_label_create(scr);
    lv_label_set_text(lbl_sd_remain, "-- left");
    lv_obj_set_style_text_font(lbl_sd_remain, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_sd_remain, CM_TEXT_SEC, 0);
    lv_obj_set_width(lbl_sd_remain, 240);
    lv_obj_set_style_text_align(lbl_sd_remain, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_sd_remain, 0, 188);

    /* ── Divider ── */
    make_divider(scr, 213, CM_DIVIDER);

    /* ── Plan name (y=230, montserrat_28, centered) ── */
    lbl_plan_name = lv_label_create(scr);
    lv_label_set_text(lbl_plan_name, "Pro");
    lv_obj_set_style_text_font(lbl_plan_name, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_plan_name, CM_ACCENT, 0);
    lv_obj_set_width(lbl_plan_name, 240);
    lv_obj_set_style_text_align(lbl_plan_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_plan_name, 0, 232);

    make_divider(scr, 272, CM_DIVIDER);

    create_tab_bar(scr, 0);
}

/* ============================================================
 * BUILD: SETTINGS SCREEN
 * ============================================================ */
static void build_settings_screen(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, CM_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Header */
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 240, 40);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, CM_HEADER_BG, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_scrollbar_mode(hdr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *ht = lv_label_create(hdr);
    lv_label_set_text(ht, "Settings");
    lv_obj_set_style_text_font(ht, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ht, CM_TEXT_PRIM, 0);
    lv_obj_align(ht, LV_ALIGN_LEFT_MID, 12, 0);

    make_divider(scr, 40, CM_ACCENT);

    /* ── WiFi section ── */
    make_label(scr, "WIFI", &lv_font_montserrat_10, CM_TEXT_DIM, 10, 52);

    lbl_wifi_state = lv_label_create(scr);
    lv_label_set_text(lbl_wifi_state, "Not connected");
    lv_obj_set_style_text_font(lbl_wifi_state, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_wifi_state, CM_RED, 0);
    lv_obj_set_pos(lbl_wifi_state, 10, 68);

    make_label(scr, "SSID:", &lv_font_montserrat_10, CM_TEXT_DIM, 10, 92);
    lbl_wifi_ssid_val = lv_label_create(scr);
    lv_label_set_text(lbl_wifi_ssid_val, "--");
    lv_obj_set_style_text_font(lbl_wifi_ssid_val, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_wifi_ssid_val, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(lbl_wifi_ssid_val, 50, 92);

    make_label(scr, "IP:", &lv_font_montserrat_10, CM_TEXT_DIM, 10, 108);
    lbl_wifi_ip_val = lv_label_create(scr);
    lv_label_set_text(lbl_wifi_ip_val, "--");
    lv_obj_set_style_text_font(lbl_wifi_ip_val, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_wifi_ip_val, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(lbl_wifi_ip_val, 50, 108);

    btn_wifi_action = lv_btn_create(scr);
    lv_obj_set_size(btn_wifi_action, 220, 44);
    lv_obj_set_pos(btn_wifi_action, 10, 126);
    lv_obj_set_style_bg_color(btn_wifi_action, CM_BTN_BG, 0);
    lv_obj_set_style_bg_opa(btn_wifi_action, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn_wifi_action, CM_BTN_PRESS, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_wifi_action, 8, 0);
    lv_obj_set_style_border_width(btn_wifi_action, 1, 0);
    lv_obj_set_style_border_color(btn_wifi_action, CM_ACCENT, 0);
    lv_obj_set_style_pad_all(btn_wifi_action, 0, 0);
    lv_obj_add_event_cb(btn_wifi_action, wifi_action_btn_cb, LV_EVENT_CLICKED, NULL);
    lbl_btn_wifi_action = lv_label_create(btn_wifi_action);
    lv_label_set_text(lbl_btn_wifi_action, "Setup WiFi");
    lv_obj_set_style_text_font(lbl_btn_wifi_action, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_btn_wifi_action, CM_TEXT_PRIM, 0);
    lv_obj_align(lbl_btn_wifi_action, LV_ALIGN_CENTER, 0, 0);

    make_label(scr, "Connect to 'CYD-Setup' WiFi -> 192.168.4.1",
               &lv_font_montserrat_10, CM_TEXT_DIM, 10, 178);

    /* ── Timezone section ── */
    make_divider(scr, 198, CM_DIVIDER);

    make_label(scr, "CLOCK TIMEZONE", &lv_font_montserrat_10, CM_TEXT_DIM, 10, 210);

    make_btn(scr, "-", 10, 228, 50, 40, tz_minus_cb, NULL);

    lbl_tz = lv_label_create(scr);
    lv_label_set_text(lbl_tz, "UTC+3");
    lv_obj_set_style_text_font(lbl_tz, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_tz, CM_ACCENT, 0);
    lv_obj_set_width(lbl_tz, 100);
    lv_obj_set_style_text_align(lbl_tz, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_tz, 68, 238);

    make_btn(scr, "+", 180, 228, 50, 40, tz_plus_cb, NULL);

    make_label(scr, "Saves automatically",
               &lv_font_montserrat_10, CM_TEXT_DIM, 10, 278);

    create_tab_bar(scr, 1);
}

/* ============================================================
 * PC TIMER (simulator only)
 * ============================================================ */
#ifndef ESP32
static void data_poll_cb(lv_timer_t *t) { (void)t; load_data_from_file(); update_ui(); }
#endif

/* ============================================================
 * MAIN ENTRY POINT
 * ============================================================ */
static void claude_monitor_create_ui(void) {
    scr_monitor = lv_screen_active();
    build_monitor_screen(scr_monitor);

    scr_settings = lv_obj_create(NULL);
    build_settings_screen(scr_settings);

    current_screen = 0;
    update_wifi_ui();
    update_tz_display();

#ifndef ESP32
    lv_timer_create(data_poll_cb, 2000, NULL);
    load_data_from_file();
#endif
    update_ui();
}

#endif /* CLAUDE_MONITOR_UI_H */
