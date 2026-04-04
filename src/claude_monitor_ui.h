/**
 * claude_monitor_ui.h
 *
 * Claude Monitor CYD — Three-screen dashboard
 *
 * Screen 1: Monitor  — real-time usage metrics
 * Screen 2: Windows  — tap buttons to switch PC terminal windows
 * Screen 3: Settings — plan + view mode selection (stored in NVS)
 *
 * Tab bar at bottom: [Monitor] [Windows] [Settings]
 */

#ifndef CLAUDE_MONITOR_UI_H
#define CLAUDE_MONITOR_UI_H

#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Serial output (CYD -> PC) */
#ifdef ESP32
#include <Arduino.h>
#define SERIAL_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define SERIAL_PRINTF(...) printf(__VA_ARGS__)
#endif

/* ============================================================
 * DATA MODELS
 * ============================================================ */

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

#define MAX_WINDOWS 10
#define WIN_NAME_LEN 24

typedef struct {
    int32_t hwnd;
    char name[WIN_NAME_LEN];
} window_entry_t;

static window_entry_t g_windows[MAX_WINDOWS];
static int g_window_count = 0;

/* Settings config */
#define NUM_PLANS 4
#define NUM_VIEWS 3

static const char *plan_names[NUM_PLANS] = {"custom", "pro", "max5", "max20"};
static const char *plan_labels[NUM_PLANS] = {"Custom", "Pro", "Max5", "Max20"};
static const char *view_names[NUM_VIEWS] = {"realtime", "daily", "monthly"};
static const char *view_labels[NUM_VIEWS] = {"Realtime", "Daily", "Monthly"};

static int g_selected_plan = 0;   /* 0=custom, 1=pro, 2=max5, 3=max20 */
static int g_selected_view = 0;   /* 0=realtime, 1=daily, 2=monthly */

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
#define CM_BTN_SELECTED lv_color_hex(0x00B4D8)

/* ============================================================
 * SCREENS
 * ============================================================ */
static lv_obj_t *scr_monitor;
static lv_obj_t *scr_windows;
static lv_obj_t *scr_settings;
static int current_screen = 0;  /* 0=monitor, 1=windows, 2=settings */

/* Monitor widgets */
static lv_obj_t *status_led, *lbl_plan, *lbl_reset_time;
static lv_obj_t *bar_cost, *lbl_cost_pct, *lbl_cost_detail;
static lv_obj_t *bar_tokens, *lbl_tokens_pct, *lbl_tokens_detail;
static lv_obj_t *bar_msgs, *lbl_msgs_pct, *lbl_msgs_detail;
static lv_obj_t *lbl_burn, *lbl_cost_rate, *lbl_model, *lbl_reset, *lbl_depletion;

/* Windows widgets */
static lv_obj_t *lbl_win_header;
static lv_obj_t *win_buttons[MAX_WINDOWS];
static lv_obj_t *win_labels[MAX_WINDOWS];

/* Settings widgets */
static lv_obj_t *plan_btns[NUM_PLANS];
static lv_obj_t *view_btns[NUM_VIEWS];
static lv_obj_t *lbl_settings_status;

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
 * DATA LOADING
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

/* PC file loading */
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
 * SEND CONFIG TO BRIDGE
 * ============================================================ */
static void send_config_to_bridge(void) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "{\"config\":{\"plan\":\"%s\",\"view\":\"%s\"}}\n",
             plan_names[g_selected_plan],
             view_names[g_selected_view]);
    SERIAL_PRINTF("%s", cmd);
}

/* ============================================================
 * UPDATE SETTINGS UI
 * ============================================================ */
static void update_settings_ui(void) {
    /* Highlight active plan button */
    for (int i = 0; i < NUM_PLANS; i++) {
        if (i == g_selected_plan) {
            lv_obj_set_style_bg_color(plan_btns[i], CM_BTN_SELECTED, 0);
            lv_obj_set_style_border_color(plan_btns[i], CM_BTN_SELECTED, 0);
        } else {
            lv_obj_set_style_bg_color(plan_btns[i], CM_BTN_BG, 0);
            lv_obj_set_style_border_color(plan_btns[i], CM_DIVIDER, 0);
        }
    }
    /* Highlight active view button */
    for (int i = 0; i < NUM_VIEWS; i++) {
        if (i == g_selected_view) {
            lv_obj_set_style_bg_color(view_btns[i], CM_BTN_SELECTED, 0);
            lv_obj_set_style_border_color(view_btns[i], CM_BTN_SELECTED, 0);
        } else {
            lv_obj_set_style_bg_color(view_btns[i], CM_BTN_BG, 0);
            lv_obj_set_style_border_color(view_btns[i], CM_DIVIDER, 0);
        }
    }
    /* Status line */
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "Active: %s | %s",
             plan_labels[g_selected_plan],
             view_labels[g_selected_view]);
    lv_label_set_text(lbl_settings_status, tmp);
}

/* ============================================================
 * UI UPDATE — Monitor
 * ============================================================ */
static void update_ui(void) {
    char tmp[64];
    int pct;

    lv_label_set_text(lbl_plan, g_data.plan_name);
    char rs[32];
    format_time(g_data.reset_min, rs, sizeof(rs));
    lv_label_set_text(lbl_reset_time, rs);

    switch (g_data.warning_level) {
        case 0: lv_obj_set_style_bg_color(status_led, CM_GREEN, 0); break;
        case 1: lv_obj_set_style_bg_color(status_led, CM_YELLOW, 0); break;
        case 2: lv_obj_set_style_bg_color(status_led, CM_ORANGE, 0); break;
        default: lv_obj_set_style_bg_color(status_led, CM_RED, 0); break;
    }

    pct = (g_data.cost_limit > 0.01f) ? (int)(g_data.cost_used * 100.0f / g_data.cost_limit) : 0;
    if (pct > 100) pct = 100;
    lv_bar_set_value(bar_cost, pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_cost, warning_color(pct), LV_PART_INDICATOR);
    snprintf(tmp, sizeof(tmp), "%d%%", pct);
    lv_label_set_text(lbl_cost_pct, tmp);
    snprintf(tmp, sizeof(tmp), "$%.0f / $%.0f", g_data.cost_used, g_data.cost_limit);
    lv_label_set_text(lbl_cost_detail, tmp);

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

    pct = (g_data.msgs_limit > 0) ? (int)((int64_t)g_data.msgs_used * 100 / g_data.msgs_limit) : 0;
    if (pct > 100) pct = 100;
    lv_bar_set_value(bar_msgs, pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_msgs, warning_color(pct), LV_PART_INDICATOR);
    snprintf(tmp, sizeof(tmp), "%d%%", pct);
    lv_label_set_text(lbl_msgs_pct, tmp);
    snprintf(tmp, sizeof(tmp), "%d / %d", g_data.msgs_used, g_data.msgs_limit);
    lv_label_set_text(lbl_msgs_detail, tmp);

    snprintf(tmp, sizeof(tmp), "%.1f t/m", g_data.burn_rate);
    lv_label_set_text(lbl_burn, tmp);
    snprintf(tmp, sizeof(tmp), "$%.2f/m", g_data.cost_rate);
    lv_label_set_text(lbl_cost_rate, tmp);
    snprintf(tmp, sizeof(tmp), "%s %d%%", g_data.model, g_data.model_pct);
    lv_label_set_text(lbl_model, tmp);
    char r2[32];
    format_time(g_data.reset_min, r2, sizeof(r2));
    snprintf(tmp, sizeof(tmp), "Reset: %s", r2);
    lv_label_set_text(lbl_reset, tmp);

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
 * TAB & SCREEN SWITCHING
 * ============================================================ */
static void switch_to_screen(int idx);

static void tab_monitor_cb(lv_event_t *e) { (void)e; switch_to_screen(0); }
static void tab_windows_cb(lv_event_t *e) { (void)e; switch_to_screen(1); }
static void tab_settings_cb(lv_event_t *e) { (void)e; switch_to_screen(2); }

static void switch_to_screen(int idx) {
    if (idx == current_screen) return;
    lv_obj_t *targets[] = { scr_monitor, scr_windows, scr_settings };
    lv_scr_load_anim_t anim = (idx > current_screen)
        ? LV_SCR_LOAD_ANIM_MOVE_LEFT
        : LV_SCR_LOAD_ANIM_MOVE_RIGHT;
    current_screen = idx;
    lv_scr_load_anim(targets[idx], anim, 200, 0, false);
}

/* ============================================================
 * BUTTON CALLBACKS
 * ============================================================ */
static void win_btn_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < g_window_count) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "{\"switch\":%d}\n", (int)g_windows[idx].hwnd);
        SERIAL_PRINTF("%s", cmd);
    }
}

static void plan_btn_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < NUM_PLANS && idx != g_selected_plan) {
        g_selected_plan = idx;
        update_settings_ui();
        send_config_to_bridge();
    }
}

static void view_btn_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < NUM_VIEWS && idx != g_selected_view) {
        g_selected_view = idx;
        update_settings_ui();
        send_config_to_bridge();
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

/* Create a styled selection button */
static lv_obj_t* create_select_btn(lv_obj_t *parent, const char *label_text,
                                    int x, int y, int w, int h,
                                    lv_event_cb_t cb, int user_data_idx) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, CM_BTN_BG, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, CM_BTN_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, CM_DIVIDER, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)(intptr_t)user_data_idx);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, CM_TEXT_PRIM, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    return btn;
}

/* 3-tab bar */
static void create_tab_bar_3(lv_obj_t *scr, int active_idx) {
    lv_obj_t *bg = lv_obj_create(scr);
    lv_obj_set_size(bg, 320, 24);
    lv_obj_set_pos(bg, 0, 216);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x0A0A1A), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_set_scrollbar_mode(bg, LV_SCROLLBAR_MODE_OFF);

    struct { const char *text; lv_event_cb_t cb; } tabs[] = {
        {"Monitor",  tab_monitor_cb},
        {"Windows",  tab_windows_cb},
        {"Settings", tab_settings_cb},
    };

    int tab_w = 102;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(bg);
        lv_obj_set_size(btn, tab_w, 22);
        lv_obj_set_pos(btn, 2 + i * (tab_w + 4), 1);
        lv_obj_set_style_bg_color(btn, i == active_idx ? CM_TAB_ACTIVE : CM_TAB_INACTIVE, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_add_event_cb(btn, tabs[i].cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, tabs[i].text);
        lv_obj_set_style_text_color(lbl, i == active_idx ? CM_BG_DARK : CM_TEXT_SEC, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    }
}

/* ============================================================
 * BUILD SCREENS
 * ============================================================ */

static void build_monitor_screen(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, CM_BG_DARK, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Header */
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 320, 26); lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, CM_HEADER_BG, 0); lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0); lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 3, 0); lv_obj_set_scrollbar_mode(hdr, LV_SCROLLBAR_MODE_OFF);

    status_led = lv_obj_create(hdr);
    lv_obj_set_size(status_led, 8, 8); lv_obj_align(status_led, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_color(status_led, CM_GREEN, 0); lv_obj_set_style_bg_opa(status_led, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(status_led, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_border_width(status_led, 0, 0);

    lv_obj_t *t = lv_label_create(hdr);
    lv_label_set_text(t, "CLAUDE MONITOR"); lv_obj_set_style_text_color(t, CM_TEXT_PRIM, 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_10, 0); lv_obj_align(t, LV_ALIGN_LEFT_MID, 16, 0);

    lbl_plan = lv_label_create(hdr);
    lv_label_set_text(lbl_plan, "Custom"); lv_obj_set_style_text_color(lbl_plan, CM_MODEL_COLOR, 0);
    lv_obj_set_style_text_font(lbl_plan, &lv_font_montserrat_10, 0); lv_obj_align(lbl_plan, LV_ALIGN_CENTER, 30, 0);

    lbl_reset_time = lv_label_create(hdr);
    lv_label_set_text(lbl_reset_time, "5h 00m"); lv_obj_set_style_text_color(lbl_reset_time, CM_TEXT_SEC, 0);
    lv_obj_set_style_text_font(lbl_reset_time, &lv_font_montserrat_10, 0); lv_obj_align(lbl_reset_time, LV_ALIGN_RIGHT_MID, -4, 0);

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

    /* Stats card 1 */
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

    /* Stats card 2 */
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

    lbl_depletion = lv_label_create(scr);
    lv_label_set_text(lbl_depletion, "Active session | P90 limits");
    lv_obj_set_style_text_color(lbl_depletion, CM_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_depletion, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl_depletion, 60, 194);

    create_tab_bar_3(scr, 0);
}

static void build_windows_screen(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, CM_BG_DARK, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *wh = lv_obj_create(scr);
    lv_obj_set_size(wh, 320, 26); lv_obj_set_pos(wh, 0, 0);
    lv_obj_set_style_bg_color(wh, CM_HEADER_BG, 0); lv_obj_set_style_bg_opa(wh, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wh, 0, 0); lv_obj_set_style_radius(wh, 0, 0);
    lv_obj_set_style_pad_all(wh, 3, 0); lv_obj_set_scrollbar_mode(wh, LV_SCROLLBAR_MODE_OFF);

    lbl_win_header = lv_label_create(wh);
    lv_label_set_text(lbl_win_header, "CLI Windows (0)");
    lv_obj_set_style_text_color(lbl_win_header, CM_TEXT_PRIM, 0);
    lv_obj_set_style_text_font(lbl_win_header, &lv_font_montserrat_10, 0);
    lv_obj_align(lbl_win_header, LV_ALIGN_LEFT_MID, 8, 0);

    for (int i = 0; i < MAX_WINDOWS; i++) {
        int col = i % 2, row = i / 2;
        int x = 8 + col * 152, y = 30 + row * 40;

        win_buttons[i] = lv_btn_create(scr);
        lv_obj_set_size(win_buttons[i], 148, 36); lv_obj_set_pos(win_buttons[i], x, y);
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

    create_tab_bar_3(scr, 1);
}

static void build_settings_screen(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, CM_BG_DARK, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Header */
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 320, 26); lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, CM_HEADER_BG, 0); lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0); lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 3, 0); lv_obj_set_scrollbar_mode(hdr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *ht = lv_label_create(hdr);
    lv_label_set_text(ht, "Settings");
    lv_obj_set_style_text_color(ht, CM_TEXT_PRIM, 0);
    lv_obj_set_style_text_font(ht, &lv_font_montserrat_12, 0);
    lv_obj_align(ht, LV_ALIGN_LEFT_MID, 8, 0);

    /* Plan section */
    lv_obj_t *pl = lv_label_create(scr);
    lv_label_set_text(pl, "Plan:");
    lv_obj_set_style_text_color(pl, CM_TEXT_SEC, 0);
    lv_obj_set_style_text_font(pl, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(pl, 10, 36);

    int btn_w = 70, btn_h = 32, gap = 6;
    int start_x = 10;
    for (int i = 0; i < NUM_PLANS; i++) {
        plan_btns[i] = create_select_btn(scr, plan_labels[i],
            start_x + i * (btn_w + gap), 50, btn_w, btn_h,
            plan_btn_cb, i);
    }

    /* Divider */
    create_divider(scr, 92);

    /* View section */
    lv_obj_t *vl = lv_label_create(scr);
    lv_label_set_text(vl, "View:");
    lv_obj_set_style_text_color(vl, CM_TEXT_SEC, 0);
    lv_obj_set_style_text_font(vl, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(vl, 10, 102);

    int vbtn_w = 95;
    for (int i = 0; i < NUM_VIEWS; i++) {
        view_btns[i] = create_select_btn(scr, view_labels[i],
            start_x + i * (vbtn_w + gap), 116, vbtn_w, btn_h,
            view_btn_cb, i);
    }

    /* Divider */
    create_divider(scr, 158);

    /* Status line */
    lbl_settings_status = lv_label_create(scr);
    lv_label_set_text(lbl_settings_status, "Active: Custom | Realtime");
    lv_obj_set_style_text_color(lbl_settings_status, CM_GREEN, 0);
    lv_obj_set_style_text_font(lbl_settings_status, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl_settings_status, 10, 170);

    /* Hint */
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Tap to change. Sent to bridge instantly.");
    lv_obj_set_style_text_color(hint, CM_TEXT_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(hint, 10, 190);

    /* Tab bar */
    create_tab_bar_3(scr, 2);
}

/* ============================================================
 * PC TIMER
 * ============================================================ */
#ifndef ESP32
static void data_poll_cb(lv_timer_t *timer) {
    (void)timer;
    load_data_from_file();
    update_ui();
}
#endif

/* ============================================================
 * MAIN ENTRY POINT
 * ============================================================ */
static void claude_monitor_create_ui(void) {
    scr_monitor = lv_screen_active();
    build_monitor_screen(scr_monitor);

    scr_windows = lv_obj_create(NULL);
    build_windows_screen(scr_windows);

    scr_settings = lv_obj_create(NULL);
    build_settings_screen(scr_settings);

    current_screen = 0;
    update_settings_ui();

#ifndef ESP32
    lv_timer_create(data_poll_cb, 2000, NULL);
    load_data_from_file();
#endif

    update_ui();
}

#endif /* CLAUDE_MONITOR_UI_H */
