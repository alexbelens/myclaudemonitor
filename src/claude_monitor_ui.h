/**
 * claude_monitor_ui.h — Portrait 240×320 dashboard
 *
 * Screen 0: Monitor  — clock, weather+icon, reset countdown, session, tokens
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
    int32_t tokens_used;
    int32_t tokens_limit;
    float   cost_used;
    float   cost_limit;
    int32_t msgs_used;
    int32_t msgs_limit;
    float   burn_rate;
    float   cost_rate;
    int32_t depletion_min;
    int32_t reset_min;
    int32_t session_elapsed_min;
    int32_t session_total_min;
    char    plan_name[16];
    char    model[16];
    int32_t model_pct;
    int32_t warning_level;
} claude_data_t;

static claude_data_t g_data = {
    0, 66593, 0.0f, 147.0f, 0, 260, 0.0f, 0.0f,
    999, 300, 0, 300, "Pro", "--", 0, 0,
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
/* Session mode (sess_pct < 100) */
static lv_obj_t *lbl_sess_label;
static lv_obj_t *lbl_session_pct;
static lv_obj_t *bar_session;
static lv_obj_t *lbl_time_remain;
/* Reset mode (sess_pct >= 100) */
static lv_obj_t *lbl_reset_in;
static lv_obj_t *lbl_countdown;
static lv_obj_t *lbl_tokens;
static lv_obj_t *lbl_model;
static lv_obj_t *lbl_msgs;

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

static void fmt_tokens(int32_t n, char *buf, size_t len) {
    if (n < 1000)
        snprintf(buf, len, "%d", (int)n);
    else if (n < 1000000)
        snprintf(buf, len, "%d,%03d", (int)(n/1000), (int)(n%1000));
    else
        snprintf(buf, len, "%dM", (int)(n/1000000));
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
static int   jint(const char *j, const char *k, int d)   { const char *v=jfind(j,k); return v?atoi(v):d; }
static float jflt(const char *j, const char *k, float d) { const char *v=jfind(j,k); return v?(float)atof(v):d; }
static void  jstr(const char *j, const char *k, char *o, size_t l, const char *d) {
    const char *v = jfind(j, k);
    if (!v || *v != '"') { strncpy(o, d, l); return; }
    v++; size_t i=0; while (*v && *v!='"' && i<l-1) o[i++]=*v++; o[i]='\0';
}

/* ============================================================
 * DATA LOADING
 * ============================================================ */
static void load_data_from_json(const char *buf) {
    g_data.tokens_used         = jint(buf, "tokens_used",         g_data.tokens_used);
    g_data.tokens_limit        = jint(buf, "tokens_limit",        g_data.tokens_limit);
    g_data.cost_used           = jflt(buf, "cost_used",           g_data.cost_used);
    g_data.cost_limit          = jflt(buf, "cost_limit",          g_data.cost_limit);
    g_data.msgs_used           = jint(buf, "msgs_used",           g_data.msgs_used);
    g_data.msgs_limit          = jint(buf, "msgs_limit",          g_data.msgs_limit);
    g_data.burn_rate           = jflt(buf, "burn_rate",           g_data.burn_rate);
    g_data.cost_rate           = jflt(buf, "cost_rate",           g_data.cost_rate);
    g_data.depletion_min       = jint(buf, "depletion_min",       g_data.depletion_min);
    g_data.reset_min           = jint(buf, "reset_min",           g_data.reset_min);
    g_data.session_elapsed_min = jint(buf, "session_elapsed_min", g_data.session_elapsed_min);
    g_data.session_total_min   = jint(buf, "session_total_min",   g_data.session_total_min);
    g_data.warning_level       = jint(buf, "warning_level",       g_data.warning_level);
    g_data.model_pct           = jint(buf, "model_pct",           g_data.model_pct);
    jstr(buf, "plan_name", g_data.plan_name, sizeof(g_data.plan_name), g_data.plan_name);
    jstr(buf, "model",     g_data.model,     sizeof(g_data.model),     g_data.model);
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
 * UPDATE: REALTIME — clock + date + countdown (called every second)
 * ============================================================ */
static void update_realtime_ui(const char *timebuf, const char *datebuf, int secs_left) {
    lv_label_set_text(lbl_clock, timebuf);
    if (datebuf && datebuf[0]) lv_label_set_text(lbl_date, datebuf);

    if (secs_left <= 0) {
        lv_label_set_text(lbl_countdown, "0:00:00");
        lv_obj_set_style_text_color(lbl_countdown, CM_RED, 0);
    } else {
        int h = secs_left / 3600;
        int m = (secs_left % 3600) / 60;
        int s = secs_left % 60;
        char buf[12];
        snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
        lv_label_set_text(lbl_countdown, buf);

        lv_color_t col = secs_left < 900  ? CM_RED    :
                         secs_left < 1800 ? CM_ORANGE :
                         secs_left < 3600 ? CM_YELLOW : CM_ACCENT;
        lv_obj_set_style_text_color(lbl_countdown, col, 0);
    }
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
    char tmp[48];

    /* Accent stripe — reserved for air-raid alert status (TODO: wire up when token arrives) */

    /* Session percentage */
    int sess_pct = (g_data.session_total_min > 0)
        ? (int)((int64_t)g_data.session_elapsed_min * 100 / g_data.session_total_min) : 0;
    if (sess_pct > 100) sess_pct = 100;

    int rem_min = g_data.session_total_min - g_data.session_elapsed_min;
    if (rem_min < 0) rem_min = 0;

    if (sess_pct >= 100) {
        /* ── RESET IN mode ── */
        lv_obj_add_flag(lbl_sess_label,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_session_pct, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(bar_session,     LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_time_remain, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_reset_in,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_countdown, LV_OBJ_FLAG_HIDDEN);
    } else {
        /* ── SESSION mode ── */
        lv_obj_clear_flag(lbl_sess_label,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_session_pct, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(bar_session,     LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_time_remain, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_reset_in,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_countdown, LV_OBJ_FLAG_HIDDEN);

        snprintf(tmp, sizeof(tmp), "%d%%", sess_pct);
        lv_label_set_text(lbl_session_pct, tmp);

        lv_bar_set_value(bar_session, sess_pct, LV_ANIM_ON);
        lv_obj_set_style_bg_color(bar_session, warning_bar_color(sess_pct), LV_PART_INDICATOR);

        char remain[16];
        fmt_time(rem_min, remain, sizeof(remain));
        snprintf(tmp, sizeof(tmp), "%s left", remain);
        lv_label_set_text(lbl_time_remain, tmp);
    }

    /* Tokens — large number, section header provides context */
    char tok_buf[16];
    fmt_tokens(g_data.tokens_used, tok_buf, sizeof(tok_buf));
    lv_label_set_text(lbl_tokens, tok_buf);

    /* Model */
    snprintf(tmp, sizeof(tmp), "%s  %d%%", g_data.model, g_data.model_pct);
    lv_label_set_text(lbl_model, tmp);

    /* Msgs + burn rate */
    int mph = (g_data.session_elapsed_min > 0)
        ? (g_data.msgs_used * 60 / g_data.session_elapsed_min) : 0;
    snprintf(tmp, sizeof(tmp), "Msgs %d/%d   %d msg/h",
             g_data.msgs_used, g_data.msgs_limit, mph);
    lv_label_set_text(lbl_msgs, tmp);
}

/* ============================================================
 * BUILD: MONITOR SCREEN
 *
 *  y=  0..70   Header: [dot] clock / date  |  temp / cond
 *  y= 70..73   Accent stripe
 *  y= 83..143  SESSION zone (shared, both modes end ~y=143)
 *               SESSION: label+% / bar / "Xh left"
 *               RESET IN: label / countdown (montserrat_28)
 *  y=148       Divider
 *  y=166       "TOKENS THIS SESSION"
 *  y=183       Token count (montserrat_28, centered)
 *  y=225       Divider
 *  y=235       Model + pct
 *  y=253       Msgs + burn
 *  y=276       Divider
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

    /* Clock — left, montserrat_28 */
    lbl_clock = lv_label_create(hdr);
    lv_label_set_text(lbl_clock, "--:--");
    lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_clock, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(lbl_clock, 8, 4);

    /* Date — left, montserrat_10, directly below clock */
    lbl_date = lv_label_create(hdr);
    lv_label_set_text(lbl_date, "---");
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_date, CM_TEXT_SEC, 0);
    lv_obj_set_pos(lbl_date, 8, 44);

    /* Weather temp — right column, montserrat_28, right-aligned */
    lbl_weather_temp = lv_label_create(hdr);
    lv_label_set_text(lbl_weather_temp, "--");
    lv_obj_set_style_text_font(lbl_weather_temp, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_weather_temp, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(lbl_weather_temp, 118, 4);
    lv_obj_set_width(lbl_weather_temp, 114);
    lv_obj_set_style_text_align(lbl_weather_temp, LV_TEXT_ALIGN_RIGHT, 0);

    /* Weather cond — right column, montserrat_10, right-aligned, tight below temp */
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

    /* ── SESSION zone (y=83..143) ── */

    /* SESSION mode: label + % */
    lbl_sess_label = lv_label_create(scr);
    lv_label_set_text(lbl_sess_label, "SESSION");
    lv_obj_set_style_text_font(lbl_sess_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_sess_label, CM_TEXT_SEC, 0);
    lv_obj_set_pos(lbl_sess_label, 8, 83);

    lbl_session_pct = lv_label_create(scr);
    lv_label_set_text(lbl_session_pct, "0%");
    lv_obj_set_style_text_font(lbl_session_pct, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_session_pct, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(lbl_session_pct, 195, 83);
    lv_obj_set_width(lbl_session_pct, 37);
    lv_obj_set_style_text_align(lbl_session_pct, LV_TEXT_ALIGN_RIGHT, 0);

    /* SESSION mode: progress bar */
    bar_session = make_bar(scr, CM_ACCENT, 6, 99, 228, 14);

    /* SESSION mode: remaining time (montserrat_12, centered) */
    lbl_time_remain = lv_label_create(scr);
    lv_label_set_text(lbl_time_remain, "-- left");
    lv_obj_set_style_text_font(lbl_time_remain, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_time_remain, CM_ACCENT, 0);
    lv_obj_set_width(lbl_time_remain, 240);
    lv_obj_set_style_text_align(lbl_time_remain, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_time_remain, 0, 120);

    /* RESET IN mode: label (hidden by default) */
    lbl_reset_in = lv_label_create(scr);
    lv_label_set_text(lbl_reset_in, "RESET IN");
    lv_obj_set_style_text_font(lbl_reset_in, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_reset_in, CM_TEXT_DIM, 0);
    lv_obj_set_width(lbl_reset_in, 240);
    lv_obj_set_style_text_align(lbl_reset_in, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_reset_in, 0, 83);
    lv_obj_add_flag(lbl_reset_in, LV_OBJ_FLAG_HIDDEN);

    /* RESET IN mode: countdown montserrat_28 (hidden by default) */
    lbl_countdown = lv_label_create(scr);
    lv_label_set_text(lbl_countdown, "0:00:00");
    lv_obj_set_style_text_font(lbl_countdown, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_countdown, CM_ACCENT, 0);
    lv_obj_set_width(lbl_countdown, 240);
    lv_obj_set_style_text_align(lbl_countdown, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_countdown, 0, 97);
    lv_obj_add_flag(lbl_countdown, LV_OBJ_FLAG_HIDDEN);

    /* ── Tokens ── */
    make_divider(scr, 148, CM_DIVIDER);

    make_label(scr, "TOKENS THIS SESSION", &lv_font_montserrat_10, CM_TEXT_DIM, 8, 158);

    /* Token count — montserrat_28, centered */
    lbl_tokens = lv_label_create(scr);
    lv_label_set_text(lbl_tokens, "--");
    lv_obj_set_style_text_font(lbl_tokens, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_tokens, CM_ACCENT, 0);
    lv_obj_set_width(lbl_tokens, 240);
    lv_obj_set_style_text_align(lbl_tokens, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_tokens, 0, 174);

    /* ── Model + Msgs ── */
    make_divider(scr, 218, CM_DIVIDER);

    lbl_model = lv_label_create(scr);
    lv_label_set_text(lbl_model, "--  0%");
    lv_obj_set_style_text_font(lbl_model, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_model, CM_YELLOW, 0);
    lv_obj_set_pos(lbl_model, 8, 228);

    lbl_msgs = lv_label_create(scr);
    lv_label_set_text(lbl_msgs, "Msgs --/--  -- msg/h");
    lv_obj_set_style_text_font(lbl_msgs, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_msgs, CM_TEXT_SEC, 0);
    lv_obj_set_pos(lbl_msgs, 8, 250);

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
