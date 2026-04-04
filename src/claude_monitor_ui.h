/**
 * claude_monitor_ui.h
 *
 * Claude Monitor CYD — Dual-screen dashboard
 *
 * Screen 1: Monitor — real-time usage metrics (cost, tokens, msgs, rates)
 * Screen 2: Windows — tap buttons to switch PC terminal windows
 *
 * Tab bar at bottom of both screens: [Monitor] [Windows]
 * Tap a tab to switch views with animation.
 */

#ifndef CLAUDE_MONITOR_UI_H
#define CLAUDE_MONITOR_UI_H

#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * SERIAL OUTPUT (CYD → PC for switch commands)
 * ============================================================ */
#ifdef ESP32
#include <Arduino.h>
#define SERIAL_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define SERIAL_PRINTF(...) printf(__VA_ARGS__)
#endif

/* ============================================================
 * DATA MODELS
 * ============================================================ */

/* Monitor data */
typedef struct {
    int32_t tokens_used;
    int32_t tokens_limit;
    float cost_used;
    float cost_limit;
    int32_t msgs_used;
    int32_t msgs_limit;
    float burn_rate;
    float cost_rate;
    int32_t depletion_min;
    int32_t reset_min;
    int32_t session_elapsed_min;
    int32_t session_total_min;
    char plan_name[16];
    char model[16];
    int32_t model_pct;
    int32_t warning_level;
} claude_data_t;

static claude_data_t g_data = {
    0, 66593, 0.0f, 147.0f, 0, 260, 0.0f, 0.0f,
    999, 300, 0, 300, "Custom", "--", 0, 0,
};

/* Window list data */
#define MAX_WINDOWS 10
#define WIN_NAME_LEN 24

typedef struct {
    int32_t hwnd;
    char name[WIN_NAME_LEN];
} window_entry_t;

static window_entry_t g_windows[MAX_WINDOWS];
static int g_window_count = 0;

/* ============================================================
 * THEME COLORS
 * ============================================================ */
#define CM_BG_DARK      lv_color_hex(0x1A1A2E)
#define CM_BG_CARD      lv_color_hex(0x16213E)
#define CM_HEADER_BG    lv_color_hex(0x0F3460)
#define CM_GREEN        lv_color_hex(0x06D6A0)
#define CM_YELLOW       lv_color_hex(0xFFD166)
#define CM_ORANGE       lv_color_hex(0xF77F00)
#define CM_RED          lv_color_hex(0xD62828)
#define CM_BLUE         lv_color_hex(0x00B4D8)
#define CM_PURPLE       lv_color_hex(0x7209B7)
#define CM_MODEL_COLOR  lv_color_hex(0xFFD166)
#define CM_TEXT_PRIM    lv_color_hex(0xE0E0E0)
#define CM_TEXT_SEC     lv_color_hex(0x9E9E9E)
#define CM_TEXT_DIM     lv_color_hex(0x616161)
#define CM_BAR_BG      lv_color_hex(0x2A2A40)
#define CM_DIVIDER     lv_color_hex(0x2A2A40)
#define CM_TAB_ACTIVE  lv_color_hex(0x00B4D8)
#define CM_TAB_INACTIVE lv_color_hex(0x2A2A40)
#define CM_BTN_BG      lv_color_hex(0x16213E)
#define CM_BTN_PRESSED lv_color_hex(0x0F3460)

/* ============================================================
 * SCREENS & UI HANDLES
 * ============================================================ */
static lv_obj_t *scr_monitor;
static lv_obj_t *scr_windows;
static int current_screen = 0;  /* 0=monitor, 1=windows */

/* Monitor screen widgets */
static lv_obj_t *status_led;
static lv_obj_t *lbl_plan;
static lv_obj_t *lbl_reset_time;
static lv_obj_t *bar_cost;
static lv_obj_t *lbl_cost_pct;
static lv_obj_t *lbl_cost_detail;
static lv_obj_t *bar_tokens;
static lv_obj_t *lbl_tokens_pct;
static lv_obj_t *lbl_tokens_detail;
static lv_obj_t *bar_msgs;
static lv_obj_t *lbl_msgs_pct;
static lv_obj_t *lbl_msgs_detail;
static lv_obj_t *lbl_burn;
static lv_obj_t *lbl_cost_rate;
static lv_obj_t *lbl_model;
static lv_obj_t *lbl_reset;
static lv_obj_t *lbl_depletion;

/* Tab bar widgets (on each screen) */
static lv_obj_t *tab_monitor_1, *tab_windows_1;  /* on monitor screen */
static lv_obj_t *tab_monitor_2, *tab_windows_2;  /* on windows screen */

/* Windows screen widgets */
static lv_obj_t *lbl_win_header;
static lv_obj_t *win_buttons[MAX_WINDOWS];
static lv_obj_t *win_labels[MAX_WINDOWS];

/* ============================================================
 * HELPERS
 * ============================================================ */
static lv_color_t warning_color(int pct) {
    if (pct >= 90) return CM_RED;
    if (pct >= 75) return CM_ORANGE;
    if (pct >= 50) return CM_YELLOW;
    return CM_GREEN;
}

static void format_time(int minutes, char *buf, size_t len) {
    int h = minutes / 60;
    int m = minutes % 60;
    if (h > 0) snprintf(buf, len, "%dh %02dm", h, m);
    else snprintf(buf, len, "%dm", m);
}

static void format_number_short(int32_t n, char *buf, size_t len) {
    if (n >= 100000) snprintf(buf, len, "%dk", n / 1000);
    else if (n >= 1000) snprintf(buf, len, "%d,%03d", n / 1000, n % 1000);
    else snprintf(buf, len, "%d", n);
}

/* ============================================================
 * JSON MINI-PARSER
 * ============================================================ */
static const char* json_find_key(const char *json, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return NULL;
    p = strchr(p, ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static int json_get_int(const char *json, const char *key, int def) {
    const char *v = json_find_key(json, key);
    return v ? atoi(v) : def;
}

static float json_get_float(const char *json, const char *key, float def) {
    const char *v = json_find_key(json, key);
    return v ? (float)atof(v) : def;
}

static void json_get_str(const char *json, const char *key, char *out, size_t len, const char *def) {
    const char *v = json_find_key(json, key);
    if (!v || *v != '"') { strncpy(out, def, len); return; }
    v++;
    size_t i = 0;
    while (*v && *v != '"' && i < len - 1) { out[i++] = *v++; }
    out[i] = '\0';
}

/* ============================================================
 * DATA LOADING — Monitor
 * ============================================================ */
static void load_data_from_json(const char *buf) {
    g_data.tokens_used   = json_get_int(buf, "tokens_used", g_data.tokens_used);
    g_data.tokens_limit  = json_get_int(buf, "tokens_limit", g_data.tokens_limit);
    g_data.cost_used     = json_get_float(buf, "cost_used", g_data.cost_used);
    g_data.cost_limit    = json_get_float(buf, "cost_limit", g_data.cost_limit);
    g_data.msgs_used     = json_get_int(buf, "msgs_used", g_data.msgs_used);
    g_data.msgs_limit    = json_get_int(buf, "msgs_limit", g_data.msgs_limit);
    g_data.burn_rate     = json_get_float(buf, "burn_rate", g_data.burn_rate);
    g_data.cost_rate     = json_get_float(buf, "cost_rate", g_data.cost_rate);
    g_data.depletion_min = json_get_int(buf, "depletion_min", g_data.depletion_min);
    g_data.reset_min     = json_get_int(buf, "reset_min", g_data.reset_min);
    g_data.session_elapsed_min = json_get_int(buf, "session_elapsed_min", g_data.session_elapsed_min);
    g_data.session_total_min   = json_get_int(buf, "session_total_min", g_data.session_total_min);
    g_data.warning_level = json_get_int(buf, "warning_level", g_data.warning_level);
    g_data.model_pct     = json_get_int(buf, "model_pct", g_data.model_pct);
    json_get_str(buf, "plan_name", g_data.plan_name, sizeof(g_data.plan_name), g_data.plan_name);
    json_get_str(buf, "model", g_data.model, sizeof(g_data.model), g_data.model);
}

/* ============================================================
 * DATA LOADING — Windows
 * Parse: {"type":"windows","list":[{"id":123,"name":"dexter"},..]}
 * ============================================================ */
static void load_windows_from_json(const char *buf) {
    g_window_count = 0;
    const char *p = strstr(buf, "\"list\"");
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    p++;

    while (g_window_count < MAX_WINDOWS) {
        const char *obj = strchr(p, '{');
        if (!obj) break;
        const char *obj_end = strchr(obj, '}');
        if (!obj_end) break;

        /* Extract "id" and "name" from this object */
        char sub[256];
        size_t sub_len = obj_end - obj + 1;
        if (sub_len >= sizeof(sub)) sub_len = sizeof(sub) - 1;
        memcpy(sub, obj, sub_len);
        sub[sub_len] = '\0';

        g_windows[g_window_count].hwnd = json_get_int(sub, "id", 0);
        json_get_str(sub, "name", g_windows[g_window_count].name, WIN_NAME_LEN, "???");

        g_window_count++;
        p = obj_end + 1;
    }
}

/* ============================================================
 * PC FILE LOADING (simulator only)
 * ============================================================ */
#ifndef ESP32
#ifdef _WIN32
#include <windows.h>
static char _data_file_path[MAX_PATH] = {0};
static const char* get_data_file_path(void) {
    if (_data_file_path[0] == '\0') {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        snprintf(_data_file_path, MAX_PATH, "%sclaude_monitor_data.json", tmp);
    }
    return _data_file_path;
}
#define DATA_FILE get_data_file_path()
#else
#define DATA_FILE "/tmp/claude_monitor_data.json"
#endif
static void load_data_from_file(void) {
    FILE *f = fopen(DATA_FILE, "r");
    if (!f) return;
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    load_data_from_json(buf);
}
#endif

/* ============================================================
 * UI UPDATE — Monitor
 * ============================================================ */
static void update_ui(void) {
    char tmp[64];
    int pct;

    lv_label_set_text(lbl_plan, g_data.plan_name);

    char reset_s[32];
    format_time(g_data.reset_min, reset_s, sizeof(reset_s));
    lv_label_set_text(lbl_reset_time, reset_s);

    switch (g_data.warning_level) {
        case 0: lv_obj_set_style_bg_color(status_led, CM_GREEN, 0); break;
        case 1: lv_obj_set_style_bg_color(status_led, CM_YELLOW, 0); break;
        case 2: lv_obj_set_style_bg_color(status_led, CM_ORANGE, 0); break;
        default: lv_obj_set_style_bg_color(status_led, CM_RED, 0); break;
    }

    /* Cost */
    pct = (g_data.cost_limit > 0.01f) ? (int)(g_data.cost_used * 100.0f / g_data.cost_limit) : 0;
    if (pct > 100) pct = 100;
    lv_bar_set_value(bar_cost, pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_cost, warning_color(pct), LV_PART_INDICATOR);
    snprintf(tmp, sizeof(tmp), "%d%%", pct);
    lv_label_set_text(lbl_cost_pct, tmp);
    snprintf(tmp, sizeof(tmp), "$%.0f / $%.0f", g_data.cost_used, g_data.cost_limit);
    lv_label_set_text(lbl_cost_detail, tmp);

    /* Tokens */
    pct = (g_data.tokens_limit > 0) ? (int)((int64_t)g_data.tokens_used * 100 / g_data.tokens_limit) : 0;
    if (pct > 100) pct = 100;
    lv_bar_set_value(bar_tokens, pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_tokens, warning_color(pct), LV_PART_INDICATOR);
    snprintf(tmp, sizeof(tmp), "%d%%", pct);
    lv_label_set_text(lbl_tokens_pct, tmp);
    char us[16], ls[16];
    format_number_short(g_data.tokens_used, us, sizeof(us));
    format_number_short(g_data.tokens_limit, ls, sizeof(ls));
    snprintf(tmp, sizeof(tmp), "%s / %s", us, ls);
    lv_label_set_text(lbl_tokens_detail, tmp);

    /* Messages */
    pct = (g_data.msgs_limit > 0) ? (int)((int64_t)g_data.msgs_used * 100 / g_data.msgs_limit) : 0;
    if (pct > 100) pct = 100;
    lv_bar_set_value(bar_msgs, pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_msgs, warning_color(pct), LV_PART_INDICATOR);
    snprintf(tmp, sizeof(tmp), "%d%%", pct);
    lv_label_set_text(lbl_msgs_pct, tmp);
    snprintf(tmp, sizeof(tmp), "%d / %d", g_data.msgs_used, g_data.msgs_limit);
    lv_label_set_text(lbl_msgs_detail, tmp);

    /* Rates */
    snprintf(tmp, sizeof(tmp), "%.1f t/m", g_data.burn_rate);
    lv_label_set_text(lbl_burn, tmp);
    snprintf(tmp, sizeof(tmp), "$%.2f/m", g_data.cost_rate);
    lv_label_set_text(lbl_cost_rate, tmp);

    /* Model + Reset */
    snprintf(tmp, sizeof(tmp), "%s %d%%", g_data.model, g_data.model_pct);
    lv_label_set_text(lbl_model, tmp);
    char r2[32];
    format_time(g_data.reset_min, r2, sizeof(r2));
    snprintf(tmp, sizeof(tmp), "Reset: %s", r2);
    lv_label_set_text(lbl_reset, tmp);

    /* Footer */
    if (g_data.depletion_min < 999) {
        char ds[32];
        format_time(g_data.depletion_min, ds, sizeof(ds));
        snprintf(tmp, sizeof(tmp), "Tokens deplete in %s", ds);
        lv_obj_set_style_text_color(lbl_depletion,
            g_data.depletion_min < 60 ? CM_RED :
            g_data.depletion_min < 120 ? CM_ORANGE : CM_TEXT_SEC, 0);
    } else {
        snprintf(tmp, sizeof(tmp), "Active session | P90 limits");
        lv_obj_set_style_text_color(lbl_depletion, CM_TEXT_DIM, 0);
    }
    lv_label_set_text(lbl_depletion, tmp);
}

/* ============================================================
 * UI UPDATE — Windows
 * ============================================================ */
static void update_windows_ui(void) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "CLI Windows (%d)", g_window_count);
    lv_label_set_text(lbl_win_header, tmp);

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (i < g_window_count) {
            lv_label_set_text(win_labels[i], g_windows[i].name);
            lv_obj_remove_flag(win_buttons[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(win_buttons[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ============================================================
 * TAB SWITCHING
 * ============================================================ */
static void switch_to_monitor(void) {
    if (current_screen == 0) return;
    current_screen = 0;
    lv_scr_load_anim(scr_monitor, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
}

static void switch_to_windows(void) {
    if (current_screen == 1) return;
    current_screen = 1;
    lv_scr_load_anim(scr_windows, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

static void tab_monitor_cb(lv_event_t *e) { (void)e; switch_to_monitor(); }
static void tab_windows_cb(lv_event_t *e) { (void)e; switch_to_windows(); }

/* ============================================================
 * WINDOW BUTTON CLICK — sends switch command to PC
 * ============================================================ */
static void win_btn_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < g_window_count) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "{\"switch\":%d}\n", (int)g_windows[idx].hwnd);
        SERIAL_PRINTF("%s", cmd);
    }
}

/* ============================================================
 * WIDGET BUILDERS
 * ============================================================ */

static lv_obj_t* create_metric_bar(lv_obj_t *parent, lv_color_t color, int x, int y, int w) {
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, 8);
    lv_obj_set_pos(bar, x, y);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, CM_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(bar, 400, LV_PART_INDICATOR);
    return bar;
}

typedef struct { lv_obj_t *bar; lv_obj_t *pct_label; lv_obj_t *detail_label; } metric_row_t;

static metric_row_t create_metric_row(lv_obj_t *scr, const char *title, lv_color_t color, int y) {
    metric_row_t row;
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, CM_TEXT_SEC, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl, 6, y + 1);
    row.bar = create_metric_bar(scr, color, 52, y + 2, 160);
    row.pct_label = lv_label_create(scr);
    lv_label_set_text(row.pct_label, "0%");
    lv_obj_set_style_text_color(row.pct_label, CM_TEXT_PRIM, 0);
    lv_obj_set_style_text_font(row.pct_label, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(row.pct_label, 218, y);
    row.detail_label = lv_label_create(scr);
    lv_label_set_text(row.detail_label, "-- / --");
    lv_obj_set_style_text_color(row.detail_label, CM_TEXT_DIM, 0);
    lv_obj_set_style_text_font(row.detail_label, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(row.detail_label, 52, y + 14);
    return row;
}

static void create_divider(lv_obj_t *scr, int y) {
    lv_obj_t *div = lv_obj_create(scr);
    lv_obj_set_size(div, 304, 1);
    lv_obj_set_pos(div, 8, y);
    lv_obj_set_style_bg_color(div, CM_DIVIDER, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_scrollbar_mode(div, LV_SCROLLBAR_MODE_OFF);
}

/* Create the tab bar on a screen */
static void create_tab_bar(lv_obj_t *scr, lv_obj_t **tab_mon, lv_obj_t **tab_win, int active_idx) {
    /* Background strip */
    lv_obj_t *bar_bg = lv_obj_create(scr);
    lv_obj_set_size(bar_bg, 320, 24);
    lv_obj_set_pos(bar_bg, 0, 216);
    lv_obj_set_style_bg_color(bar_bg, lv_color_hex(0x0A0A1A), 0);
    lv_obj_set_style_bg_opa(bar_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar_bg, 0, 0);
    lv_obj_set_style_radius(bar_bg, 0, 0);
    lv_obj_set_style_pad_all(bar_bg, 0, 0);
    lv_obj_set_scrollbar_mode(bar_bg, LV_SCROLLBAR_MODE_OFF);

    /* Monitor tab (left half) */
    *tab_mon = lv_btn_create(bar_bg);
    lv_obj_set_size(*tab_mon, 156, 22);
    lv_obj_set_pos(*tab_mon, 2, 1);
    lv_obj_set_style_bg_color(*tab_mon, active_idx == 0 ? CM_TAB_ACTIVE : CM_TAB_INACTIVE, 0);
    lv_obj_set_style_bg_opa(*tab_mon, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(*tab_mon, 4, 0);
    lv_obj_set_style_border_width(*tab_mon, 0, 0);
    lv_obj_set_style_pad_all(*tab_mon, 0, 0);
    lv_obj_add_event_cb(*tab_mon, tab_monitor_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lm = lv_label_create(*tab_mon);
    lv_label_set_text(lm, "Monitor");
    lv_obj_set_style_text_color(lm, active_idx == 0 ? CM_BG_DARK : CM_TEXT_SEC, 0);
    lv_obj_set_style_text_font(lm, &lv_font_montserrat_10, 0);
    lv_obj_align(lm, LV_ALIGN_CENTER, 0, 0);

    /* Windows tab (right half) */
    *tab_win = lv_btn_create(bar_bg);
    lv_obj_set_size(*tab_win, 156, 22);
    lv_obj_set_pos(*tab_win, 162, 1);
    lv_obj_set_style_bg_color(*tab_win, active_idx == 1 ? CM_TAB_ACTIVE : CM_TAB_INACTIVE, 0);
    lv_obj_set_style_bg_opa(*tab_win, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(*tab_win, 4, 0);
    lv_obj_set_style_border_width(*tab_win, 0, 0);
    lv_obj_set_style_pad_all(*tab_win, 0, 0);
    lv_obj_add_event_cb(*tab_win, tab_windows_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lw = lv_label_create(*tab_win);
    lv_label_set_text(lw, "Windows");
    lv_obj_set_style_text_color(lw, active_idx == 1 ? CM_BG_DARK : CM_TEXT_SEC, 0);
    lv_obj_set_style_text_font(lw, &lv_font_montserrat_10, 0);
    lv_obj_align(lw, LV_ALIGN_CENTER, 0, 0);
}

/* ============================================================
 * BUILD MONITOR SCREEN
 * ============================================================ */
static void build_monitor_screen(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, CM_BG_DARK, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Header */
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, 320, 26);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, CM_HEADER_BG, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 3, 0);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);

    status_led = lv_obj_create(header);
    lv_obj_set_size(status_led, 8, 8);
    lv_obj_align(status_led, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_color(status_led, CM_GREEN, 0);
    lv_obj_set_style_bg_opa(status_led, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(status_led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(status_led, 0, 0);

    lv_obj_t *t = lv_label_create(header);
    lv_label_set_text(t, "CLAUDE MONITOR");
    lv_obj_set_style_text_color(t, CM_TEXT_PRIM, 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_10, 0);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 16, 0);

    lbl_plan = lv_label_create(header);
    lv_label_set_text(lbl_plan, "Custom");
    lv_obj_set_style_text_color(lbl_plan, CM_MODEL_COLOR, 0);
    lv_obj_set_style_text_font(lbl_plan, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_plan, LV_ALIGN_CENTER, 30, 0);

    lbl_reset_time = lv_label_create(header);
    lv_label_set_text(lbl_reset_time, "5h 00m");
    lv_obj_set_style_text_color(lbl_reset_time, CM_TEXT_SEC, 0);
    lv_obj_set_style_text_font(lbl_reset_time, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_reset_time, LV_ALIGN_RIGHT_MID, -4, 0);

    /* Metric rows */
    int y = 32;
    metric_row_t cr = create_metric_row(scr, "Cost", CM_GREEN, y);
    bar_cost = cr.bar; lbl_cost_pct = cr.pct_label; lbl_cost_detail = cr.detail_label;

    y = 62; create_divider(scr, y - 3);
    metric_row_t tr = create_metric_row(scr, "Tokens", CM_BLUE, y);
    bar_tokens = tr.bar; lbl_tokens_pct = tr.pct_label; lbl_tokens_detail = tr.detail_label;

    y = 92; create_divider(scr, y - 3);
    metric_row_t mr = create_metric_row(scr, "Msgs", CM_PURPLE, y);
    bar_msgs = mr.bar; lbl_msgs_pct = mr.pct_label; lbl_msgs_detail = mr.detail_label;

    create_divider(scr, 119);

    /* Stats cards */
    lv_obj_t *c1 = lv_obj_create(scr);
    lv_obj_set_size(c1, 304, 26); lv_obj_set_pos(c1, 8, 124);
    lv_obj_set_style_bg_color(c1, CM_BG_CARD, 0); lv_obj_set_style_bg_opa(c1, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c1, 5, 0); lv_obj_set_style_border_width(c1, 1, 0);
    lv_obj_set_style_border_color(c1, CM_DIVIDER, 0); lv_obj_set_style_pad_all(c1, 3, 0);
    lv_obj_set_scrollbar_mode(c1, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *bi = lv_label_create(c1);
    lv_label_set_text(bi, "Burn:"); lv_obj_set_style_text_color(bi, CM_TEXT_SEC, 0);
    lv_obj_set_style_text_font(bi, &lv_font_montserrat_10, 0); lv_obj_align(bi, LV_ALIGN_LEFT_MID, 2, 0);
    lbl_burn = lv_label_create(c1);
    lv_label_set_text(lbl_burn, "0.0 t/m"); lv_obj_set_style_text_color(lbl_burn, CM_ORANGE, 0);
    lv_obj_set_style_text_font(lbl_burn, &lv_font_montserrat_10, 0); lv_obj_align(lbl_burn, LV_ALIGN_LEFT_MID, 38, 0);
    lv_obj_t *ci = lv_label_create(c1);
    lv_label_set_text(ci, "Cost:"); lv_obj_set_style_text_color(ci, CM_TEXT_SEC, 0);
    lv_obj_set_style_text_font(ci, &lv_font_montserrat_10, 0); lv_obj_align(ci, LV_ALIGN_CENTER, 20, 0);
    lbl_cost_rate = lv_label_create(c1);
    lv_label_set_text(lbl_cost_rate, "$0.00/m"); lv_obj_set_style_text_color(lbl_cost_rate, CM_GREEN, 0);
    lv_obj_set_style_text_font(lbl_cost_rate, &lv_font_montserrat_10, 0); lv_obj_align(lbl_cost_rate, LV_ALIGN_CENTER, 62, 0);

    lv_obj_t *c2 = lv_obj_create(scr);
    lv_obj_set_size(c2, 304, 26); lv_obj_set_pos(c2, 8, 154);
    lv_obj_set_style_bg_color(c2, CM_BG_CARD, 0); lv_obj_set_style_bg_opa(c2, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c2, 5, 0); lv_obj_set_style_border_width(c2, 1, 0);
    lv_obj_set_style_border_color(c2, CM_DIVIDER, 0); lv_obj_set_style_pad_all(c2, 3, 0);
    lv_obj_set_scrollbar_mode(c2, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *mi = lv_label_create(c2);
    lv_label_set_text(mi, "Model:"); lv_obj_set_style_text_color(mi, CM_TEXT_SEC, 0);
    lv_obj_set_style_text_font(mi, &lv_font_montserrat_10, 0); lv_obj_align(mi, LV_ALIGN_LEFT_MID, 2, 0);
    lbl_model = lv_label_create(c2);
    lv_label_set_text(lbl_model, "-- 0%"); lv_obj_set_style_text_color(lbl_model, CM_MODEL_COLOR, 0);
    lv_obj_set_style_text_font(lbl_model, &lv_font_montserrat_10, 0); lv_obj_align(lbl_model, LV_ALIGN_LEFT_MID, 42, 0);
    lbl_reset = lv_label_create(c2);
    lv_label_set_text(lbl_reset, "Reset: 5h 00m"); lv_obj_set_style_text_color(lbl_reset, CM_BLUE, 0);
    lv_obj_set_style_text_font(lbl_reset, &lv_font_montserrat_10, 0); lv_obj_align(lbl_reset, LV_ALIGN_RIGHT_MID, -4, 0);

    create_divider(scr, 184);

    /* Footer */
    lbl_depletion = lv_label_create(scr);
    lv_label_set_text(lbl_depletion, "Active session | P90 limits");
    lv_obj_set_style_text_color(lbl_depletion, CM_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_depletion, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl_depletion, 60, 194);

    /* Tab bar */
    create_tab_bar(scr, &tab_monitor_1, &tab_windows_1, 0);
}

/* ============================================================
 * BUILD WINDOWS SCREEN
 * ============================================================ */
static void build_windows_screen(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, CM_BG_DARK, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Header */
    lv_obj_t *wh = lv_obj_create(scr);
    lv_obj_set_size(wh, 320, 26);
    lv_obj_set_pos(wh, 0, 0);
    lv_obj_set_style_bg_color(wh, CM_HEADER_BG, 0);
    lv_obj_set_style_bg_opa(wh, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wh, 0, 0);
    lv_obj_set_style_radius(wh, 0, 0);
    lv_obj_set_style_pad_all(wh, 3, 0);
    lv_obj_set_scrollbar_mode(wh, LV_SCROLLBAR_MODE_OFF);

    lbl_win_header = lv_label_create(wh);
    lv_label_set_text(lbl_win_header, "CLI Windows (0)");
    lv_obj_set_style_text_color(lbl_win_header, CM_TEXT_PRIM, 0);
    lv_obj_set_style_text_font(lbl_win_header, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_win_header, LV_ALIGN_LEFT_MID, 8, 0);

    /* Button grid: 2 columns x 5 rows, in area y=30 to y=212 */
    int col_w = 148;
    int row_h = 36;
    int pad = 4;
    int start_y = 30;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        int col = i % 2;
        int row = i / 2;
        int x = 8 + col * (col_w + pad);
        int y = start_y + row * (row_h + pad);

        win_buttons[i] = lv_btn_create(scr);
        lv_obj_set_size(win_buttons[i], col_w, row_h);
        lv_obj_set_pos(win_buttons[i], x, y);
        lv_obj_set_style_bg_color(win_buttons[i], CM_BTN_BG, 0);
        lv_obj_set_style_bg_opa(win_buttons[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(win_buttons[i], CM_BTN_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_radius(win_buttons[i], 6, 0);
        lv_obj_set_style_border_width(win_buttons[i], 1, 0);
        lv_obj_set_style_border_color(win_buttons[i], CM_DIVIDER, 0);
        lv_obj_set_style_border_color(win_buttons[i], CM_TAB_ACTIVE, LV_STATE_PRESSED);
        lv_obj_add_event_cb(win_buttons[i], win_btn_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_flag(win_buttons[i], LV_OBJ_FLAG_HIDDEN);

        win_labels[i] = lv_label_create(win_buttons[i]);
        lv_label_set_text(win_labels[i], "");
        lv_obj_set_style_text_color(win_labels[i], CM_TEXT_PRIM, 0);
        lv_obj_set_style_text_font(win_labels[i], &lv_font_montserrat_10, 0);
        lv_obj_align(win_labels[i], LV_ALIGN_CENTER, 0, 0);
    }

    /* Tab bar */
    create_tab_bar(scr, &tab_monitor_2, &tab_windows_2, 1);
}

/* ============================================================
 * PC TIMER CALLBACK
 * ============================================================ */
#ifndef ESP32
static void data_poll_cb(lv_timer_t *timer) {
    (void)timer;
    load_data_from_file();
    update_ui();
}
#endif

/* ============================================================
 * MAIN UI ENTRY POINT
 * ============================================================ */
static void claude_monitor_create_ui(void) {
    /* Screen 1: Monitor (starts as active) */
    scr_monitor = lv_screen_active();
    build_monitor_screen(scr_monitor);

    /* Screen 2: Windows */
    scr_windows = lv_obj_create(NULL);
    build_windows_screen(scr_windows);

    current_screen = 0;

#ifndef ESP32
    lv_timer_create(data_poll_cb, 2000, NULL);
    load_data_from_file();
#endif

    update_ui();
}

#endif /* CLAUDE_MONITOR_UI_H */
