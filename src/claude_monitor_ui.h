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
    /* Per-model weekly cap (Fable, Opus, ...). Plans without one send an
     * empty sc_name and the whole block stays hidden. */
    int32_t sc_pct;          /* scoped-limit utilization 0-100 */
    int32_t sc_reset_min;    /* minutes until it resets */
    char    sc_name[16];     /* model name, e.g. "Fable" */
    int32_t active_win;      /* which limit binds now: 5, 7, 3=scoped, 0=unknown */
} claude_data_t;

static claude_data_t g_data = {
    0, 300, 0, 10080, 0, "Pro", 0, 10080, "", 0,
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

/* Air raid alert — alerts.in.ua, polled by main.cpp.
 * UNKNOWN covers both "never polled yet" and "last poll is too old": a
 * display that cannot reach the API must never read as "all clear". */
typedef enum {
    ALERT_UNKNOWN = 0,
    ALERT_CLEAR,
    ALERT_PARTIAL,
    ALERT_ACTIVE,
} alert_state_t;
static alert_state_t g_alert_state = ALERT_UNKNOWN;

/* WiFi provisioning */
typedef enum { WIFI_STATE_DISCONNECTED, WIFI_STATE_CONNECTED, WIFI_STATE_AP_ACTIVE } wifi_ui_state_t;
static wifi_ui_state_t g_wifi_ui_state = WIFI_STATE_DISCONNECTED;
static char g_wifi_ui_ssid[64] = {0};
static char g_wifi_ui_ip[32]   = {0};
static bool g_request_ap_portal = false;
static bool g_cancel_ap_portal  = false;

/* Device number — 1 → claude-monitor.local, 2 → claude-monitor-2.local */
static int  g_dev_num        = 1;
static int  g_dev_num_change = 0;   /* -1 or +1 */

/* Sleep/wake schedule */
static bool g_sleep_enabled     = false;
static int  g_sleep_hour        = 22;
static int  g_wake_hour         = 7;
static int  g_sleep_toggle      = 0;    /* +1=enable, -1=disable */
static int  g_sleep_hour_change = 0;    /* -1 or +1 */
static int  g_wake_hour_change  = 0;    /* -1 or +1 */

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
/* Claude Code mascot — colors sampled from the real CLI banner.
 * The panel now runs MADCTL=0x00 (RGB), so these are the true values;
 * they were previously stored pre-swapped to work around a BGR panel. */
#define CM_CLAUDE      lv_color_hex(0xD77757)  /* terracotta body */
#define CM_CLAUDE_EYE  lv_color_hex(0x000000)  /* black eyes */
#define CM_SWEAT       lv_color_hex(0x66CCFF)  /* light blue stress drop */

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
static lv_obj_t *lbl_alert;
static lv_obj_t *accent_bar;
/* Claude Code mascot (lives inside the ring) — built from obj rectangles */
static lv_obj_t *mascot_cont;   /* transparent container (bobs up/down) */
static lv_obj_t *mascot_head;
static lv_obj_t *mascot_eye_l;
static lv_obj_t *mascot_eye_r;
static lv_obj_t *mascot_sweat;  /* stress drop, hidden until critical */
static bool g_mascot_blink    = false;  /* true while eyes closed */
static bool g_mascot_critical = false;  /* true when the hottest limit >= 90% */

/* Three limits, equal weight, fixed order: 5H, 7D, per-model weekly */
#define LIMIT_COUNT 3
static lv_obj_t *lim_title[LIMIT_COUNT];
static lv_obj_t *lim_pct[LIMIT_COUNT];
static lv_obj_t *lim_bar[LIMIT_COUNT];
static lv_obj_t *lim_reset[LIMIT_COUNT];

/* Settings — WiFi */
static lv_obj_t *lbl_wifi_state;
static lv_obj_t *lbl_wifi_ssid_val;
static lv_obj_t *lbl_wifi_ip_val;
static lv_obj_t *btn_wifi_action;
static lv_obj_t *lbl_btn_wifi_action;
static lv_obj_t *lbl_dev_num;
static lv_obj_t *lbl_tz;

/* Settings — sleep/wake */
static lv_obj_t *btn_sleep_on;
static lv_obj_t *lbl_btn_sleep_on;
static lv_obj_t *btn_sleep_off;
static lv_obj_t *lbl_btn_sleep_off;
static lv_obj_t *lbl_sleep_hour;
static lv_obj_t *lbl_wake_hour;

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

/* ── Claude Code mascot — eye geometry (relative to container) ──
 * Eyes keep a fixed vertical center; only their height changes:
 * open (idle) / blink (closed line) / squint (tense, when 5H >= 90%). */
/* Sprite is an 18x5 subpixel grid; each cell is SW x SH px on screen. */
#define MASCOT_SW           5
#define MASCOT_SH           9
#define MASCOT_EYE_W        MASCOT_SW
#define MASCOT_EYE_X_L      (5 * MASCOT_SW)    /* col 5  */
#define MASCOT_EYE_X_R      (12 * MASCOT_SW)   /* col 12 */
#define MASCOT_EYE_Y_OPEN   (1 * MASCOT_SH)    /* row 1  */
#define MASCOT_EYE_H_OPEN   MASCOT_SH
#define MASCOT_EYE_H_SQUINT 3
#define MASCOT_EYE_H_BLINK  2
#define MASCOT_Y            222   /* inside the hero ring */

static void mascot_set_eyes(int h) {
    int y = MASCOT_EYE_Y_OPEN + (MASCOT_EYE_H_OPEN - h) / 2;  /* keep center fixed */
    lv_obj_set_height(mascot_eye_l, h); lv_obj_set_y(mascot_eye_l, y);
    lv_obj_set_height(mascot_eye_r, h); lv_obj_set_y(mascot_eye_r, y);
}

/* Apply current state: critical (squint + sweat, no blink) vs idle (blink). */
static void mascot_apply(void) {
    if (!mascot_eye_l) return;
    if (g_mascot_critical) {
        mascot_set_eyes(MASCOT_EYE_H_SQUINT);
        lv_obj_remove_flag(mascot_sweat, LV_OBJ_FLAG_HIDDEN);
    } else {
        mascot_set_eyes(g_mascot_blink ? MASCOT_EYE_H_BLINK : MASCOT_EYE_H_OPEN);
        lv_obj_add_flag(mascot_sweat, LV_OBJ_FLAG_HIDDEN);
    }
}

static void mascot_blink_cb(lv_timer_t *t) {
    if (g_mascot_critical) {          /* tense: hold squint, don't blink */
        lv_timer_set_period(t, 700);
        return;
    }
    g_mascot_blink = !g_mascot_blink;
    mascot_apply();
    lv_timer_set_period(t, g_mascot_blink ? 140 : 3600);  /* short closed, long open */
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
    g_data.sc_pct       = jint(buf, "sc_pct",       g_data.sc_pct);
    g_data.sc_reset_min = jint(buf, "sc_reset_min",  g_data.sc_reset_min);
    g_data.active_win   = jint(buf, "active_win",    g_data.active_win);
    jstr(buf, "sc_name", g_data.sc_name, sizeof(g_data.sc_name), g_data.sc_name);
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
static void dev_num_minus_cb(lv_event_t *e) { (void)e; g_dev_num_change = -1; }
static void dev_num_plus_cb(lv_event_t *e)  { (void)e; g_dev_num_change = +1; }
static void sleep_on_cb(lv_event_t *e)      { (void)e; g_sleep_toggle = 1; }
static void sleep_off_cb(lv_event_t *e)     { (void)e; g_sleep_toggle = -1; }
static void sleep_h_minus_cb(lv_event_t *e) { (void)e; g_sleep_hour_change = -1; }
static void sleep_h_plus_cb(lv_event_t *e)  { (void)e; g_sleep_hour_change = +1; }
static void wake_h_minus_cb(lv_event_t *e)  { (void)e; g_wake_hour_change = -1; }
static void wake_h_plus_cb(lv_event_t *e)   { (void)e; g_wake_hour_change = +1; }

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
        if (h > 0) snprintf(remain, sizeof(remain), "%dh %02dm left", h, m);
        else        snprintf(remain, sizeof(remain), "%dm left", m);
        col = secs_left < 900  ? CM_RED    :
              secs_left < 1800 ? CM_ORANGE :
              secs_left < 3600 ? CM_YELLOW : CM_ACCENT;
    }
    /* 5H is always the first block, so the live countdown goes there */
    lv_label_set_text(lim_reset[0], remain);
    lv_obj_set_style_text_color(lim_reset[0], col, 0);
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
 * UPDATE: AIR RAID ALERT (called after fetch from main.cpp)
 * ============================================================ */
static void update_alert_display(alert_state_t st) {
    g_alert_state = st;

    switch (st) {
    case ALERT_ACTIVE:
        lv_label_set_text(lbl_alert, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(lbl_alert, CM_RED, 0);
        lv_obj_remove_flag(lbl_alert, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(accent_bar, CM_RED, 0);
        break;

    case ALERT_PARTIAL:
        /* Alert in part of the region only — same glyph, softer colour. */
        lv_label_set_text(lbl_alert, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(lbl_alert, CM_ORANGE, 0);
        lv_obj_remove_flag(lbl_alert, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(accent_bar, CM_ORANGE, 0);
        break;

    case ALERT_UNKNOWN:
        /* No fresh answer from the API. Show a dim "stale" glyph rather than
         * nothing, so an unreachable API can never be mistaken for silence. */
        lv_label_set_text(lbl_alert, LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_color(lbl_alert, CM_TEXT_DIM, 0);
        lv_obj_remove_flag(lbl_alert, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(accent_bar, CM_DIVIDER, 0);
        break;

    case ALERT_CLEAR:
    default:
        lv_obj_add_flag(lbl_alert, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(accent_bar, CM_DIVIDER, 0);
        break;
    }

    lv_obj_invalidate(lbl_alert);
    lv_obj_invalidate(accent_bar);
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
 * UPDATE: DEVICE NUMBER UI in Settings
 * ============================================================ */
static void update_dev_num_ui(void) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", g_dev_num);
    lv_label_set_text(lbl_dev_num, buf);
}

/* ============================================================
 * UPDATE: SLEEP/WAKE UI in Settings
 * ============================================================ */
static void update_sleep_ui(void) {
    char buf[8];
    lv_obj_set_style_bg_color(btn_sleep_on,
        g_sleep_enabled ? CM_ACCENT : CM_BTN_BG, 0);
    lv_obj_set_style_border_color(btn_sleep_on,
        g_sleep_enabled ? CM_ACCENT : CM_DIVIDER, 0);
    lv_obj_set_style_text_color(lbl_btn_sleep_on,
        g_sleep_enabled ? CM_BG : CM_TEXT_SEC, 0);
    lv_obj_set_style_bg_color(btn_sleep_off,
        !g_sleep_enabled ? CM_BTN_PRESS : CM_BTN_BG, 0);
    lv_obj_set_style_border_color(btn_sleep_off,
        !g_sleep_enabled ? CM_RED : CM_DIVIDER, 0);
    lv_obj_set_style_text_color(lbl_btn_sleep_off,
        !g_sleep_enabled ? CM_TEXT_PRIM : CM_TEXT_DIM, 0);
    snprintf(buf, sizeof(buf), "%02d:00", g_sleep_hour);
    lv_label_set_text(lbl_sleep_hour, buf);
    snprintf(buf, sizeof(buf), "%02d:00", g_wake_hour);
    lv_label_set_text(lbl_wake_hour, buf);
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
    char tmp[32], remain[20];

    bool has_scoped = (g_data.sc_name[0] != '\0');

    /* Fixed order — nothing jumps around between refreshes */
    const char *nm[LIMIT_COUNT] = { "5H", "7D", g_data.sc_name };
    int32_t     pc[LIMIT_COUNT] = { g_data.fh_pct, g_data.sd_pct, g_data.sc_pct };
    int32_t     rs[LIMIT_COUNT] = { g_data.fh_reset_min, g_data.sd_reset_min, g_data.sc_reset_min };

    for (int i = 0; i < LIMIT_COUNT; i++) {
        /* The third block only exists on plans with a per-model weekly cap */
        bool shown = (i < 2) || has_scoped;
        lv_obj_t *parts[] = { lim_title[i], lim_pct[i], lim_bar[i], lim_reset[i] };
        for (unsigned k = 0; k < sizeof(parts) / sizeof(parts[0]); k++) {
            if (shown) lv_obj_remove_flag(parts[k], LV_OBJ_FLAG_HIDDEN);
            else       lv_obj_add_flag(parts[k],    LV_OBJ_FLAG_HIDDEN);
        }
        if (!shown) continue;

        lv_label_set_text(lim_title[i], nm[i]);
        snprintf(tmp, sizeof(tmp), "%d%%", (int)pc[i]);
        lv_label_set_text(lim_pct[i], tmp);
        lv_bar_set_value(lim_bar[i], pc[i], LV_ANIM_ON);
        lv_obj_set_style_bg_color(lim_bar[i], warning_bar_color(pc[i]), LV_PART_INDICATOR);

        /* The 5H line is overwritten every second by the live countdown */
        fmt_time_long(rs[i], remain, sizeof(remain));
        snprintf(tmp, sizeof(tmp), "%s left", remain);
        lv_label_set_text(lim_reset[i], tmp);
    }

    /* Mascot reacts to the hottest limit, whichever that is */
    int32_t worst = pc[0];
    for (int i = 1; i < LIMIT_COUNT; i++) {
        if ((i < 2 || has_scoped) && pc[i] > worst) worst = pc[i];
    }
    g_mascot_critical = (worst >= 90);
    mascot_apply();
}

/* Solid filled rounded block — used to build the mascot from primitives */
static lv_obj_t* make_block(lv_obj_t *parent, lv_color_t color,
                            int x, int y, int w, int h, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    return o;
}

/* ============================================================
 * BUILD: MONITOR SCREEN
 *
 *  y=  0.. 70  Header: clock / date  |  temp / cond
 *  y= 70.. 73  Alert stripe (air-raid, pending API token)
 *  y= 74..118  5H zone: label+% / bar / remain (ticking)
 *  y=124       Divider
 *  y=125..169  7D zone: label+% / bar / remain
 *  y=175       Divider
 *  y=176..204  Per-model weekly cap (Fable/Opus); hidden if the plan has none
 *  y=226..271  Claude Code mascot (18x5 sprite: body + arms + eyes + legs)
 *  A soft panel slides behind whichever of the three is currently binding.
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

    /* ── Air raid icon — lives in the dead gap between the clock (%H:%M ends
     *    near x=80) and the weather block (starts at x=118). Hidden only when
     *    the API has confirmed there is no alert. ── */
    lbl_alert = lv_label_create(hdr);
    lv_label_set_text(lbl_alert, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(lbl_alert, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_alert, CM_RED, 0);
    lv_obj_set_pos(lbl_alert, 84, 6);
    lv_obj_add_flag(lbl_alert, LV_OBJ_FLAG_HIDDEN);

    /* ── Alert stripe (y=70..73) — green=safe, red=alert ── */
    accent_bar = lv_obj_create(scr);
    lv_obj_set_size(accent_bar, 240, 3);
    lv_obj_set_pos(accent_bar, 0, 70);
    lv_obj_set_style_bg_color(accent_bar, CM_DIVIDER, 0);
    lv_obj_set_style_bg_opa(accent_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(accent_bar, 0, 0);
    lv_obj_set_style_radius(accent_bar, 0, 0);
    lv_obj_set_scrollbar_mode(accent_bar, LV_SCROLLBAR_MODE_OFF);

    /* ── Three limit blocks (y=76 / 134 / 192), same size, fixed order.
     *    Each: name on the left, big number on the right, full-width bar,
     *    and the time until it resets. ── */
    for (int i = 0; i < LIMIT_COUNT; i++) {
        int y = 76 + i * 50;

        lim_title[i] = make_label(scr, "--", &lv_font_montserrat_14, CM_TEXT_SEC, 8, y);

        lim_pct[i] = lv_label_create(scr);
        lv_label_set_text(lim_pct[i], "0%");
        lv_obj_set_style_text_font(lim_pct[i], &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(lim_pct[i], CM_TEXT_PRIM, 0);
        lv_obj_set_pos(lim_pct[i], 110, y - 3);
        lv_obj_set_width(lim_pct[i], 122);
        lv_obj_set_style_text_align(lim_pct[i], LV_TEXT_ALIGN_RIGHT, 0);

        lim_bar[i] = make_bar(scr, CM_ACCENT, 8, y + 24, 224, 14);

        lim_reset[i] = lv_label_create(scr);
        lv_label_set_text(lim_reset[i], "");
        lv_obj_set_style_text_font(lim_reset[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lim_reset[i], CM_TEXT_DIM, 0);
        lv_obj_set_pos(lim_reset[i], 62, y + 1);
        lv_obj_set_width(lim_reset[i], 120);
        lv_obj_set_style_text_align(lim_reset[i], LV_TEXT_ALIGN_LEFT, 0);
    }

    /* ── Claude Code mascot (centered, container bobs gently) ──
     * Reproduces the real CLI banner sprite (18x5 subpixel grid):
     *      ############        head (rounded)
     *      ##.######.##        two small eyes near the edges
     *    ################      middle row is wider (little arms)
     *      ############        lower body
     *       # #    # #         four legs in two pairs
     * Body/arms/legs are terracotta blocks; eyes are black blocks on top. */
    mascot_cont = lv_obj_create(scr);
    lv_obj_set_size(mascot_cont, 18 * MASCOT_SW, 5 * MASCOT_SH);  /* 72x30 */
    lv_obj_set_pos(mascot_cont, (240 - 18 * MASCOT_SW) / 2, MASCOT_Y);
    lv_obj_set_style_bg_opa(mascot_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mascot_cont, 0, 0);
    lv_obj_set_style_radius(mascot_cont, 0, 0);
    lv_obj_set_style_pad_all(mascot_cont, 0, 0);
    lv_obj_remove_flag(mascot_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(mascot_cont, LV_SCROLLBAR_MODE_OFF);

    /* body: rows 0..3, cols 3..14 (rounded head) */
    make_block(mascot_cont, CM_CLAUDE, 3 * MASCOT_SW, 0,
               12 * MASCOT_SW, 4 * MASCOT_SH, 6);
    /* arms: row 2 sticks out two cols on each side */
    make_block(mascot_cont, CM_CLAUDE, 1 * MASCOT_SW, 2 * MASCOT_SH,
               2 * MASCOT_SW, MASCOT_SH, 2);
    make_block(mascot_cont, CM_CLAUDE, 15 * MASCOT_SW, 2 * MASCOT_SH,
               2 * MASCOT_SW, MASCOT_SH, 2);
    /* legs: row 4, cols 4 & 6 (left pair), 11 & 13 (right pair) */
    make_block(mascot_cont, CM_CLAUDE,  4 * MASCOT_SW, 4 * MASCOT_SH, MASCOT_SW, MASCOT_SH, 2);
    make_block(mascot_cont, CM_CLAUDE,  6 * MASCOT_SW, 4 * MASCOT_SH, MASCOT_SW, MASCOT_SH, 2);
    make_block(mascot_cont, CM_CLAUDE, 11 * MASCOT_SW, 4 * MASCOT_SH, MASCOT_SW, MASCOT_SH, 2);
    make_block(mascot_cont, CM_CLAUDE, 13 * MASCOT_SW, 4 * MASCOT_SH, MASCOT_SW, MASCOT_SH, 2);
    /* eyes (black blocks on top of the body, row 1) */
    mascot_eye_l = make_block(mascot_cont, CM_CLAUDE_EYE,
        MASCOT_EYE_X_L, MASCOT_EYE_Y_OPEN, MASCOT_EYE_W, MASCOT_EYE_H_OPEN, 1);
    mascot_eye_r = make_block(mascot_cont, CM_CLAUDE_EYE,
        MASCOT_EYE_X_R, MASCOT_EYE_Y_OPEN, MASCOT_EYE_W, MASCOT_EYE_H_OPEN, 1);
    /* stress drop (top-right), shown only when 5H >= 90% */
    mascot_sweat = make_block(mascot_cont, CM_SWEAT, 15 * MASCOT_SW, MASCOT_SH / 2,
               MASCOT_SW, MASCOT_SH, 2);
    lv_obj_add_flag(mascot_sweat, LV_OBJ_FLAG_HIDDEN);

    make_divider(scr, 272, CM_DIVIDER);

    create_tab_bar(scr, 0);
}

/* ============================================================
 * BUILD: SETTINGS SCREEN
 *
 *  y=  0.. 40  Header
 *  y= 40       Accent divider
 *  y= 42..144  WiFi section (state / SSID / IP / Device# / button)
 *  y=146       Divider
 *  y=148..192  Timezone section
 *  y=196       Divider
 *  y=198..284  Sleep/Wake section
 *  y=286       Divider
 *  y=296       Tab bar
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

    /* ── WiFi section (y=42..144) ── */
    make_label(scr, "WIFI", &lv_font_montserrat_10, CM_TEXT_DIM, 10, 44);

    lbl_wifi_state = lv_label_create(scr);
    lv_label_set_text(lbl_wifi_state, "Not connected");
    lv_obj_set_style_text_font(lbl_wifi_state, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_wifi_state, CM_RED, 0);
    lv_obj_set_pos(lbl_wifi_state, 10, 56);

    make_label(scr, "SSID:", &lv_font_montserrat_10, CM_TEXT_DIM, 10, 70);
    lbl_wifi_ssid_val = lv_label_create(scr);
    lv_label_set_text(lbl_wifi_ssid_val, "--");
    lv_obj_set_style_text_font(lbl_wifi_ssid_val, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_wifi_ssid_val, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(lbl_wifi_ssid_val, 50, 70);

    make_label(scr, "IP:", &lv_font_montserrat_10, CM_TEXT_DIM, 10, 82);
    lbl_wifi_ip_val = lv_label_create(scr);
    lv_label_set_text(lbl_wifi_ip_val, "--");
    lv_obj_set_style_text_font(lbl_wifi_ip_val, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_wifi_ip_val, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(lbl_wifi_ip_val, 50, 82);

    /* Device number row */
    make_label(scr, "Device #:", &lv_font_montserrat_10, CM_TEXT_DIM, 10, 96);
    make_btn(scr, "-", 80, 92, 26, 20, dev_num_minus_cb, NULL);
    lbl_dev_num = lv_label_create(scr);
    lv_label_set_text(lbl_dev_num, "1");
    lv_obj_set_style_text_font(lbl_dev_num, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_dev_num, CM_ACCENT, 0);
    lv_obj_set_width(lbl_dev_num, 20);
    lv_obj_set_style_text_align(lbl_dev_num, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_dev_num, 108, 96);
    make_btn(scr, "+", 130, 92, 26, 20, dev_num_plus_cb, NULL);
    make_label(scr, "(reboots)", &lv_font_montserrat_10, CM_TEXT_DIM, 160, 96);

    btn_wifi_action = lv_btn_create(scr);
    lv_obj_set_size(btn_wifi_action, 220, 28);
    lv_obj_set_pos(btn_wifi_action, 10, 116);
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

    /* ── Timezone section (y=148..192) ── */
    make_divider(scr, 146, CM_DIVIDER);
    make_label(scr, "TIMEZONE", &lv_font_montserrat_10, CM_TEXT_DIM, 10, 150);

    make_btn(scr, "-", 10, 163, 50, 28, tz_minus_cb, NULL);

    lbl_tz = lv_label_create(scr);
    lv_label_set_text(lbl_tz, "UTC+3");
    lv_obj_set_style_text_font(lbl_tz, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_tz, CM_ACCENT, 0);
    lv_obj_set_width(lbl_tz, 100);
    lv_obj_set_style_text_align(lbl_tz, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_tz, 68, 168);

    make_btn(scr, "+", 180, 163, 50, 28, tz_plus_cb, NULL);

    /* ── Sleep/Wake section (y=198..284) ── */
    make_divider(scr, 196, CM_DIVIDER);
    make_label(scr, "SLEEP / WAKE", &lv_font_montserrat_10, CM_TEXT_DIM, 10, 200);

    btn_sleep_on = lv_btn_create(scr);
    lv_obj_set_size(btn_sleep_on, 106, 22);
    lv_obj_set_pos(btn_sleep_on, 8, 212);
    lv_obj_set_style_bg_color(btn_sleep_on, CM_BTN_BG, 0);
    lv_obj_set_style_bg_opa(btn_sleep_on, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn_sleep_on, CM_BTN_PRESS, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_sleep_on, 6, 0);
    lv_obj_set_style_border_width(btn_sleep_on, 1, 0);
    lv_obj_set_style_border_color(btn_sleep_on, CM_DIVIDER, 0);
    lv_obj_set_style_pad_all(btn_sleep_on, 0, 0);
    lv_obj_add_event_cb(btn_sleep_on, sleep_on_cb, LV_EVENT_CLICKED, NULL);
    lbl_btn_sleep_on = lv_label_create(btn_sleep_on);
    lv_label_set_text(lbl_btn_sleep_on, "Enabled");
    lv_obj_set_style_text_font(lbl_btn_sleep_on, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_btn_sleep_on, CM_TEXT_SEC, 0);
    lv_obj_align(lbl_btn_sleep_on, LV_ALIGN_CENTER, 0, 0);

    btn_sleep_off = lv_btn_create(scr);
    lv_obj_set_size(btn_sleep_off, 106, 22);
    lv_obj_set_pos(btn_sleep_off, 120, 212);
    lv_obj_set_style_bg_color(btn_sleep_off, CM_BTN_BG, 0);
    lv_obj_set_style_bg_opa(btn_sleep_off, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn_sleep_off, CM_BTN_PRESS, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_sleep_off, 6, 0);
    lv_obj_set_style_border_width(btn_sleep_off, 1, 0);
    lv_obj_set_style_border_color(btn_sleep_off, CM_DIVIDER, 0);
    lv_obj_set_style_pad_all(btn_sleep_off, 0, 0);
    lv_obj_add_event_cb(btn_sleep_off, sleep_off_cb, LV_EVENT_CLICKED, NULL);
    lbl_btn_sleep_off = lv_label_create(btn_sleep_off);
    lv_label_set_text(lbl_btn_sleep_off, "Disabled");
    lv_obj_set_style_text_font(lbl_btn_sleep_off, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_btn_sleep_off, CM_TEXT_PRIM, 0);
    lv_obj_align(lbl_btn_sleep_off, LV_ALIGN_CENTER, 0, 0);

    make_label(scr, "Sleep:", &lv_font_montserrat_10, CM_TEXT_SEC, 8, 242);
    make_btn(scr, "-", 72, 238, 26, 22, sleep_h_minus_cb, NULL);
    lbl_sleep_hour = lv_label_create(scr);
    lv_label_set_text(lbl_sleep_hour, "22:00");
    lv_obj_set_style_text_font(lbl_sleep_hour, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_sleep_hour, CM_ACCENT, 0);
    lv_obj_set_width(lbl_sleep_hour, 44);
    lv_obj_set_style_text_align(lbl_sleep_hour, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_sleep_hour, 100, 242);
    make_btn(scr, "+", 146, 238, 26, 22, sleep_h_plus_cb, NULL);

    make_label(scr, "Wake: ", &lv_font_montserrat_10, CM_TEXT_SEC, 8, 266);
    make_btn(scr, "-", 72, 262, 26, 22, wake_h_minus_cb, NULL);
    lbl_wake_hour = lv_label_create(scr);
    lv_label_set_text(lbl_wake_hour, "07:00");
    lv_obj_set_style_text_font(lbl_wake_hour, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_wake_hour, CM_ACCENT, 0);
    lv_obj_set_width(lbl_wake_hour, 44);
    lv_obj_set_style_text_align(lbl_wake_hour, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_wake_hour, 100, 266);
    make_btn(scr, "+", 146, 262, 26, 22, wake_h_plus_cb, NULL);

    make_divider(scr, 286, CM_DIVIDER);

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
    update_dev_num_ui();
    update_tz_display();
    update_sleep_ui();

    /* Mascot: blink loop + gentle idle bob (2px up/down) */
    mascot_apply();
    lv_timer_create(mascot_blink_cb, 3600, NULL);

    lv_anim_t bob;
    lv_anim_init(&bob);
    lv_anim_set_var(&bob, mascot_cont);
    lv_anim_set_exec_cb(&bob, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&bob, MASCOT_Y, MASCOT_Y - 3);
    lv_anim_set_duration(&bob, 1400);
    lv_anim_set_playback_duration(&bob, 1400);
    lv_anim_set_repeat_count(&bob, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&bob, lv_anim_path_ease_in_out);
    lv_anim_start(&bob);

#ifndef ESP32
    lv_timer_create(data_poll_cb, 2000, NULL);
    load_data_from_file();
#endif
    update_ui();
}

#endif /* CLAUDE_MONITOR_UI_H */
