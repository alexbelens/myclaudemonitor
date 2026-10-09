/* ============================================================
 * claude_pet.h — the Claude Code mascot as a small tamagotchi
 *
 * Lives in the room strip of the Monitor screen (y=147..292). Everything it
 * does is driven by signals the device already has:
 *
 *   waiting sessions  -> waves, speech bubble   (bridge payload "waiting")
 *   Claude acting     -> works at a laptop      (bridge payload "working")
 *   Claude's actions  -> XP                     (bridge payload "xp_add")
 *   any limit >= 90%  -> tired, sweat
 *   night / idle 2h   -> sleeps
 *   no work 2 days    -> bored
 *   rain / snow / <0  -> umbrella, scarf, window (Open-Meteo on device)
 *   air raid alert    -> helmet over everything (alerts.in.ua on device)
 *   tap / long press  -> hearts / cycle hats
 *
 * Progress (level, XP, rewards, streaks) is handed to the host through
 * pet_save_cb, so this file stays platform-neutral: main.cpp stores it in
 * NVS, the PC simulator keeps it in RAM.
 *
 * All objects are created once in pet_build(); the 50 ms timer only swaps
 * image sources and moves things, and only touches an object when its
 * value actually changes, so idle frames cost no redraw.
 * ============================================================ */
#ifndef CLAUDE_PET_H
#define CLAUDE_PET_H

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "lvgl.h"
#include "pet_sprites.h"

/* ── Geometry ── */
#define PET_ROOM_TOP   147
#define PET_FLOOR      256      /* feet stand on this line */
#define PET_CX         120

/* Anchors on the Claude frames, in art cells (see gen_pet_sprites.py) */
#define PET_HEAD_CX    11       /* head spans cols 4..17 */
#define PET_SCARF_COL  4
#define PET_SCARF_ROW  5
#define PET_HOLD_COL   18       /* raised right arm */
#define PET_SWEAT_COL  18
#define PET_SWEAT_ROW  1

/* ── Rules ──
 * XP is Claude's own work, counted by the session hooks and weighed by the
 * bridge (+3 finished answer, +3 file edit, +1 any other tool call); the
 * device only adds what arrives in "xp_add". */
#define PET_XP_DAILY        25u                         /* first action of the day */
#define PET_XP_PET          1u                          /* per tap ... */
#define PET_PETS_PER_DAY    10u                         /* ... up to this many a day */
#define PET_MAX_LEVEL       99u
#define PET_DOZE_MS         (2UL * 60UL * 60UL * 1000UL)/* dozes off after 2h without work */
#define PET_WAITING_TTL_MS  (5UL * 60UL * 1000UL)       /* bridge silent this long -> 0 */
                                                        /* (also for "working") */
#define PET_SAVE_EVERY_MS   (5UL * 60UL * 1000UL)       /* NVS writes, at most */

/* ~1300 XP on a busy day: Lv 10 in about a week, Lv 20 in about a month. */
static uint32_t pet_level_cost(uint32_t level) { return 150u * level + 150u; }

/* ── States ── */
enum { PS_IDLE, PS_WORKING, PS_WAITING, PS_TIRED, PS_SLEEP, PS_BORED, PS_JOY, PS_COUNT };
static const char *const PET_STATE_NAMES[PS_COUNT] = {
    "idle", "working", "waiting", "tired", "sleep", "bored", "joy"
};

enum { PEV_RAIN = 1, PEV_SNOW = 2, PEV_COLD = 4, PEV_NIGHT = 8, PEV_ALERT = 16 };

/* ── Rewards ── */
enum {
    PET_U_BEANIE = 1 << 0, PET_U_PARTY = 1 << 1, PET_U_CROWN = 1 << 2,
    PET_U_PLANT  = 1 << 3, PET_U_MUG   = 1 << 4, PET_U_TROPHY = 1 << 5,
};
#define PET_REWARD_COUNT 6
enum { PET_HAT_NONE, PET_HAT_BEANIE, PET_HAT_PARTY, PET_HAT_CROWN };

/* ── Progress (persisted) ── */
#define PET_PROGRESS_VERSION 1
typedef struct {
    uint8_t  version;
    uint8_t  hat;               /* PET_HAT_* currently worn */
    uint16_t level;
    uint32_t xp;                /* progress inside the current level */
    uint32_t total_xp;
    uint16_t unlocked;          /* PET_U_* */
    uint16_t streak;            /* consecutive days with work */
    uint16_t best_streak;
    uint16_t early_days;        /* days whose first work came before 09:00 */
    uint16_t active_since_cap;  /* active days since 5H last hit 100% */
    uint16_t pets_today;
    int32_t  last_active_day;   /* local days since 1970, -1 = never */
    int32_t  last_early_day;
    int32_t  last_cap_day;
    int32_t  pets_day;
} pet_progress_t;

static pet_progress_t P;
static void (*pet_save_cb)(const pet_progress_t *p) = NULL;
static bool     pet_dirty        = false;
static uint32_t pet_last_save_ms = 0;

/* ── Inputs ── */
static int      pet_waiting       = 0;
static bool     pet_waiting_seen  = false;
static uint32_t pet_waiting_ms    = 0;
static bool     pet_night         = false;
static int      pet_wcode         = -1;
static int      pet_temp_c        = 99;
static bool     pet_tired         = false;
static int      pet_working       = 0;      /* sessions where Claude acted in the last 2 min */
static bool     pet_working_seen  = false;
static uint32_t pet_working_ms    = 0;
static uint32_t pet_last_usage_ms = 0;      /* last Claude action; boot counts as one */
static uint32_t pet_joy_until     = 0;
static int      pet_demo_state    = -1;
static int      pet_demo_env      = -1;
static uint32_t pet_demo_until    = 0;
static int      pet_state_cur     = PS_IDLE;

/* ── Objects ── */
static lv_obj_t *pet_win, *pet_plant, *pet_mug, *pet_trophy, *pet_laptop;
static lv_obj_t *pet_body, *pet_scarf, *pet_hat, *pet_umbrella, *pet_sweat;
static lv_obj_t *pet_dim, *pet_bubble, *pet_bubble_lbl, *pet_tail, *pet_toast, *pet_hit;
static lv_obj_t *pet_lv, *pet_xpbar, *pet_xplbl, *pet_awards;
static uint32_t  pet_toast_until = 0;
static int       pet_bubble_n    = -1;
static int       pet_bubble_w    = 0;
static int       pet_bx, pet_by, pet_bw;    /* last body box, for hearts */

#define PET_PARTS 8
#define PET_ZS    3
typedef struct {
    lv_obj_t *o;
    int32_t   x, y;          /* px * 256 */
    int32_t   vx, vy;        /* (px * 256) per ms */
    uint32_t  born, life;
    bool      live;
} pet_part_t;
static pet_part_t pet_parts[PET_PARTS];
static pet_part_t pet_zs[PET_ZS];
static uint32_t   pet_next_spawn = 0;
static uint32_t   pet_last_tick  = 0;

/* ============================================================
 * Small helpers — each only touches LVGL when the value changes
 * ============================================================ */
static void pet_show(lv_obj_t *o, bool on) {
    bool hidden = lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (on && hidden)        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else if (!on && !hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
static void pet_pos(lv_obj_t *o, int32_t x, int32_t y) {
    if (lv_obj_get_style_x(o, 0) != x || lv_obj_get_style_y(o, 0) != y) lv_obj_set_pos(o, x, y);
}
static void pet_src(lv_obj_t *o, const lv_image_dsc_t *src) {
    if (lv_image_get_src(o) != (const void *)src) lv_image_set_src(o, src);
}
static void pet_opa(lv_obj_t *o, lv_opa_t opa) {
    if (lv_obj_get_style_opa(o, 0) != opa) lv_obj_set_style_opa(o, opa, 0);
}
static lv_obj_t *pet_image(lv_obj_t *parent, const lv_image_dsc_t *src, int32_t x, int32_t y, bool hidden) {
    lv_obj_t *o = lv_image_create(parent);
    lv_image_set_src(o, src);
    lv_obj_set_pos(o, x, y);
    if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}
static lv_obj_t *pet_rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t hex) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(hex), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}
static int pet_popcount(uint32_t v) { int n = 0; while (v) { n += (int)(v & 1u); v >>= 1; } return n; }

/* ============================================================
 * Calendar — local day numbers, so streaks follow the wall clock
 * ============================================================ */
static int32_t pet_days_from_civil(int y, int m, int d) {
    y -= (m <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int mp  = (m + 9) % 12;
    int doy = (153 * mp + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int32_t)era * 146097 + doe - 719468;
}
/* Returns -1 until NTP has set the clock. */
static int32_t pet_today(int *hour) {
    time_t now = time(NULL);
    if (now < 1700000000) return -1;
    struct tm *t = localtime(&now);
    if (!t) return -1;
    if (hour) *hour = t->tm_hour;
    return pet_days_from_civil(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
}

/* ============================================================
 * Progress, XP, rewards
 * ============================================================ */
static void pet_update_status(void);
static void pet_toast_show(const char *txt, uint32_t ms);

static void pet_progress_defaults(void) {
    memset(&P, 0, sizeof(P));
    P.version = PET_PROGRESS_VERSION;
    P.level = 1;
    P.last_active_day = -1;
    P.last_early_day = -1;
    P.last_cap_day = -1;
    P.pets_day = -1;
}

static void pet_save_now(void) {
    if (pet_save_cb) pet_save_cb(&P);
    pet_dirty = false;
    pet_last_save_ms = lv_tick_get();
}

static void pet_celebrate(uint32_t ms) { pet_joy_until = lv_tick_get() + ms; }

static void pet_unlock(uint16_t bit, const char *name, uint8_t hat) {
    if (P.unlocked & bit) return;
    P.unlocked |= bit;
    char buf[32];
    snprintf(buf, sizeof(buf), "NEW: %s", name);
    pet_toast_show(buf, 2600);
    pet_celebrate(2200);
    if (hat) P.hat = hat;            /* the newest hat goes straight on */
    pet_save_now();
}

static void pet_check_rewards(void) {
    int32_t d = pet_today(NULL);
    if (P.level >= 3)  pet_unlock(PET_U_BEANIE, "BEANIE",    PET_HAT_BEANIE);
    if (P.level >= 5)  pet_unlock(PET_U_PARTY,  "PARTY HAT", PET_HAT_PARTY);
    if (P.level >= 10) pet_unlock(PET_U_CROWN,  "CROWN",     PET_HAT_CROWN);
    if (P.streak >= 7) pet_unlock(PET_U_PLANT,  "PLANT",     0);
    if (P.early_days >= 5) pet_unlock(PET_U_MUG, "MUG",      0);
    if (d >= 0 && P.last_cap_day >= 0 && d - P.last_cap_day >= 7 && P.active_since_cap >= 5)
        pet_unlock(PET_U_TROPHY, "TROPHY", 0);
}

static void pet_add_xp(uint32_t n) {
    if (n == 0) return;
    P.xp += n;
    P.total_xp += n;
    pet_dirty = true;
    bool up = false;
    while (P.level < PET_MAX_LEVEL && P.xp >= pet_level_cost(P.level)) {
        P.xp -= pet_level_cost(P.level);
        P.level++;
        up = true;
    }
    if (up) {
        char buf[24];
        snprintf(buf, sizeof(buf), "LEVEL UP  Lv %u", (unsigned)P.level);
        pet_toast_show(buf, 2400);
        pet_celebrate(2000);
        pet_check_rewards();          /* may replace the toast with "NEW: ..." */
        pet_save_now();
    }
    pet_update_status();
}

/* First work of a local day: streak, daily bonus, early-bird tally. */
static void pet_note_activity(void) {
    int hour = 12;
    int32_t d = pet_today(&hour);
    if (d < 0 || P.last_active_day == d) return;
    P.streak = (P.last_active_day == d - 1) ? (uint16_t)(P.streak + 1) : 1;
    if (P.streak > P.best_streak) P.best_streak = P.streak;
    P.last_active_day = d;
    if (P.last_cap_day < 0) P.last_cap_day = d;   /* trophy clock starts on day one */
    P.active_since_cap++;
    if (hour < 9 && P.last_early_day != d) { P.early_days++; P.last_early_day = d; }
    pet_dirty = true;
    pet_add_xp(PET_XP_DAILY);
    pet_check_rewards();
}

/* 5H hit 100% today: the "week without a wall" trophy starts over. */
static void pet_note_cap(void) {
    int32_t d = pet_today(NULL);
    if (d < 0 || P.last_cap_day == d) return;
    P.last_cap_day = d;
    P.active_since_cap = 0;
    pet_dirty = true;
}

static bool pet_bored(void) {
    int32_t d = pet_today(NULL);
    return d >= 0 && P.last_active_day >= 0 && d - P.last_active_day >= 2;
}

/* ============================================================
 * Host API
 * ============================================================ */
static void pet_load_progress(const pet_progress_t *p) {
    if (p && p->version == PET_PROGRESS_VERSION && p->level >= 1) P = *p;
    else pet_progress_defaults();
    if (pet_lv) pet_update_status();
}

/* Limits only make the pet tired and reset the trophy clock; XP comes from
 * Claude's actions (pet_add_activity), not from tokens spent. */
static void pet_on_usage(int fh, int worst) {
    pet_tired = (worst >= 90);
    if (fh >= 100) pet_note_cap();
}

/* XP the bridge counted from the hooks since its last push. */
static void pet_add_activity(int xp) {
    if (xp <= 0) return;
    pet_last_usage_ms = lv_tick_get();
    pet_note_activity();
    pet_add_xp((uint32_t)xp);
}

static void pet_set_working(int n) {
    pet_working = n < 0 ? 0 : n;
    pet_working_seen = true;
    pet_working_ms = lv_tick_get();
}

static void pet_set_waiting(int n) {
    pet_waiting = n < 0 ? 0 : n;
    pet_waiting_seen = true;
    pet_waiting_ms = lv_tick_get();
}
static void pet_set_night(bool night) { pet_night = night; }
static void pet_set_weather(int wcode, int temp_c) { pet_wcode = wcode; pet_temp_c = temp_c; }

/* Force a state and/or environment for a few seconds — for checking the art
 * on the real screen. state/env may be NULL; unknown names are ignored. */
static void pet_demo(const char *state, const char *env, int sec) {
    pet_demo_state = -1;
    pet_demo_env = -1;
    if (state) {
        for (int i = 0; i < PS_COUNT; i++)
            if (strcmp(state, PET_STATE_NAMES[i]) == 0) pet_demo_state = i;
    }
    if (env) {
        if      (strcmp(env, "clear") == 0) pet_demo_env = 0;
        else if (strcmp(env, "rain")  == 0) pet_demo_env = PEV_RAIN;
        else if (strcmp(env, "snow")  == 0) pet_demo_env = PEV_SNOW | PEV_COLD;
        else if (strcmp(env, "cold")  == 0) pet_demo_env = PEV_COLD;
        else if (strcmp(env, "night") == 0) pet_demo_env = PEV_NIGHT;
        else if (strcmp(env, "alert") == 0) pet_demo_env = PEV_ALERT;
    }
    if (sec <= 0) sec = 20;
    pet_demo_until = lv_tick_get() + (uint32_t)sec * 1000u;
}

static const char *pet_state_name(void) { return PET_STATE_NAMES[pet_state_cur]; }

/* ============================================================
 * Effective state
 * ============================================================ */
static int pet_waiting_count(uint32_t now) {
    if (!pet_waiting_seen || now - pet_waiting_ms > PET_WAITING_TTL_MS) return 0;
    return pet_waiting;
}

static uint8_t pet_env(uint32_t now) {
    if (now < pet_demo_until && pet_demo_env >= 0) return (uint8_t)pet_demo_env;
    uint8_t f = 0;
    int c = pet_wcode;
    if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95) f |= PEV_RAIN;
    if ((c >= 71 && c <= 77) || c == 85 || c == 86)              f |= PEV_SNOW | PEV_COLD;
    if (pet_temp_c <= 0)                                         f |= PEV_COLD;
    if (pet_night)                                               f |= PEV_NIGHT;
    if (g_alert_state == ALERT_ACTIVE)                           f |= PEV_ALERT;
    return f;
}

/* Priority: a waiting session beats everything except a moment of joy. */
static int pet_state_now(uint32_t now, uint8_t env) {
    if (now < pet_demo_until && pet_demo_state >= 0) return pet_demo_state;
    if (now < pet_joy_until)                      return PS_JOY;
    if (pet_waiting_count(now) > 0)               return PS_WAITING;
    if (pet_tired)                                return PS_TIRED;
    if (pet_working_seen && pet_working > 0 && now - pet_working_ms <= PET_WAITING_TTL_MS)
        return PS_WORKING;
    if (env & PEV_NIGHT)                          return PS_SLEEP;
    if (pet_bored())                              return PS_BORED;
    if (now - pet_last_usage_ms > PET_DOZE_MS)    return PS_SLEEP;
    return PS_IDLE;
}

/* ============================================================
 * Particles
 * ============================================================ */
static void pet_spawn(pet_part_t *pool, int n, const lv_image_dsc_t *src,
                      int32_t x, int32_t y, uint32_t life, uint32_t now) {
    for (int i = 0; i < n; i++) {
        pet_part_t *p = &pool[i];
        if (p->live) continue;
        p->live = true;
        p->x = x * 256; p->y = y * 256;
        p->vx = (int32_t)lv_rand(0, 8) - 4;       /* ±0.015 px/ms */
        p->vy = -6 - (int32_t)lv_rand(0, 5);      /* rises 0.025..0.04 px/ms */
        p->born = now; p->life = life;
        if (src) pet_src(p->o, src);
        pet_opa(p->o, LV_OPA_COVER);
        pet_pos(p->o, x, y);
        pet_show(p->o, true);
        return;
    }
}

static void pet_step_particles(pet_part_t *pool, int n, uint32_t now, uint32_t dt) {
    for (int i = 0; i < n; i++) {
        pet_part_t *p = &pool[i];
        if (!p->live) continue;
        uint32_t age = now - p->born;
        if (age >= p->life) { p->live = false; pet_show(p->o, false); continue; }
        p->x += p->vx * (int32_t)dt;
        p->y += p->vy * (int32_t)dt;
        pet_pos(p->o, p->x / 256, p->y / 256);
        pet_opa(p->o, (lv_opa_t)(255 - age * 255 / p->life));
    }
}

static void pet_hearts(void) {
    uint32_t now = lv_tick_get();
    for (int i = 0; i < 5; i++) {
        int32_t x = pet_bx + pet_bw / 2 - 5 + (int32_t)lv_rand(0, (uint32_t)(pet_bw * 7 / 10)) - pet_bw * 7 / 20;
        pet_spawn(pet_parts, PET_PARTS, &pet_img_heart, x, pet_by - 2, 1100, now);
    }
}

/* ============================================================
 * Toast + status row
 * ============================================================ */
static void pet_toast_show(const char *txt, uint32_t ms) {
    if (!pet_toast) return;
    lv_label_set_text(pet_toast, txt);
    lv_obj_align(pet_toast, LV_ALIGN_TOP_MID, 0, PET_ROOM_TOP + 3);
    pet_show(pet_toast, true);
    pet_toast_until = lv_tick_get() + ms;
}

static void pet_update_status(void) {
    if (!pet_lv) return;
    char buf[24];
    snprintf(buf, sizeof(buf), "Lv %u", (unsigned)P.level);
    lv_label_set_text(pet_lv, buf);
    uint32_t cost = pet_level_cost(P.level);
    lv_bar_set_range(pet_xpbar, 0, (int32_t)cost);
    lv_bar_set_value(pet_xpbar, (int32_t)P.xp, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%u/%u", (unsigned)P.xp, (unsigned)cost);
    lv_label_set_text(pet_xplbl, buf);
    snprintf(buf, sizeof(buf), "%d", pet_popcount(P.unlocked));
    lv_label_set_text(pet_awards, buf);
    pet_show(pet_plant,  (P.unlocked & PET_U_PLANT)  != 0);
    pet_show(pet_mug,    (P.unlocked & PET_U_MUG)    != 0);
    pet_show(pet_trophy, (P.unlocked & PET_U_TROPHY) != 0);
}

/* ============================================================
 * Touch
 * ============================================================ */
static void pet_petted(void) {
    pet_hearts();
    pet_celebrate(1200);
    int32_t d = pet_today(NULL);
    if (d >= 0 && P.pets_day != d) { P.pets_day = d; P.pets_today = 0; }
    if (P.pets_today < PET_PETS_PER_DAY) {
        P.pets_today++;
        pet_add_xp(PET_XP_PET);
    }
}

static void pet_cycle_hat(void) {
    static const uint16_t bits[4] = { 0, PET_U_BEANIE, PET_U_PARTY, PET_U_CROWN };
    static const char *const names[4] = { "NO HAT", "BEANIE", "PARTY HAT", "CROWN" };
    uint8_t h = P.hat;
    for (int i = 0; i < 4; i++) {
        h = (uint8_t)((h + 1) % 4);
        if (h == PET_HAT_NONE || (P.unlocked & bits[h])) break;
    }
    if (h == P.hat) { pet_toast_show("HATS FROM Lv 3", 1800); return; }
    P.hat = h;
    pet_toast_show(names[h], 1500);
    pet_save_now();
}

static void pet_hit_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED)     pet_petted();
    else if (code == LV_EVENT_LONG_PRESSED) pet_cycle_hat();
}

/* ============================================================
 * Frame
 * ============================================================ */
static void pet_render(uint32_t now) {
    uint32_t dt = pet_last_tick ? now - pet_last_tick : 0;
    if (dt > 200) dt = 200;
    pet_last_tick = now;

    uint8_t env = pet_env(now);
    int st = pet_state_now(now, env);
    pet_state_cur = st;

    int f = PF_IDLE;
    int32_t dy = 0;
    bool umbrella = false, laptop = false, sweat = false, zs = false, sparks = false, bubble = false;

    switch (st) {
    case PS_IDLE:
        f  = (now % 3740) < 140 ? PF_BLINK : PF_IDLE;       /* firmware's old blink rhythm */
        dy = ((now / 700) % 2) ? -2 : 0;
        if (env & PEV_RAIN) { f = PF_HOLD; umbrella = true; dy = 0; }
        break;
    case PS_WORKING:
        f = ((now / 220) % 2) ? PF_WORK1 : PF_WORK0;
        laptop = true;
        break;
    case PS_WAITING:
        bubble = true;
        if (env & PEV_RAIN) { f = PF_HOLD; umbrella = true; }
        else f = ((now / 280) % 2) ? PF_WAVE1 : PF_WAVE0;
        dy = (now % 1600) < 180 ? -4 : 0;
        break;
    case PS_TIRED:
        f = PF_TIRED; dy = ((now / 1200) % 2) ? 1 : 0; sweat = true;
        break;
    case PS_SLEEP:
        f = PF_SLEEP; dy = ((now / 1100) % 2) ? 1 : 0; zs = true;
        break;
    case PS_BORED:
        f = PF_BORED;
        break;
    case PS_JOY: {
        f = PF_JOY;
        int32_t d = (int32_t)(now % 1040) - 520;               /* parabolic hop */
        dy = -(10 - (d * d * 10) / (520 * 520));
        sparks = true;
        break;
    }
    }

    /* Window follows the weather; precipitation outranks the night sky. */
    const lv_image_dsc_t *win = &pet_img_win_day;
    bool alt = ((now / 450) % 2) != 0;
    if (env & PEV_RAIN)       win = alt ? &pet_img_win_rain1 : &pet_img_win_rain0;
    else if (env & PEV_SNOW)  win = alt ? &pet_img_win_snow1 : &pet_img_win_snow0;
    else if (env & PEV_NIGHT) win = &pet_img_win_night;
    pet_src(pet_win, win);

    pet_show(pet_laptop, laptop);
    if (laptop) pet_src(pet_laptop, ((now / 400) % 2) ? &pet_img_laptop1 : &pet_img_laptop0);

    /* Body */
    const pet_frame_t *fr = &pet_frames[f];
    int32_t w = fr->img->header.w, h = fr->img->header.h;
    int32_t x0 = PET_CX - w / 2;
    int32_t y0 = PET_FLOOR - h + dy;
    int32_t head_y = y0 + fr->head * PET_SCALE;
    pet_src(pet_body, fr->img);
    pet_pos(pet_body, x0, y0);
    pet_bx = x0; pet_by = y0; pet_bw = w;

    pet_show(pet_scarf, (env & PEV_COLD) != 0);
    if (env & PEV_COLD)
        pet_pos(pet_scarf, x0 + PET_SCARF_COL * PET_SCALE, y0 + (fr->head + PET_SCARF_ROW) * PET_SCALE);

    /* Headgear: the helmet during an alert, otherwise the chosen hat. */
    const lv_image_dsc_t *hat = NULL;
    int ov = 0;
    if (env & PEV_ALERT)              { hat = &pet_img_helmet; ov = 2; }
    else if (P.hat == PET_HAT_BEANIE) { hat = &pet_img_beanie; ov = 1; }
    else if (P.hat == PET_HAT_PARTY)  { hat = &pet_img_party;  ov = 0; }
    else if (P.hat == PET_HAT_CROWN)  { hat = &pet_img_crown;  ov = 0; }
    int32_t top_y = head_y;
    pet_show(pet_hat, hat != NULL);
    if (hat) {
        int32_t hx = x0 + PET_HEAD_CX * PET_SCALE - (int32_t)hat->header.w / 2;
        int32_t hy = head_y - (int32_t)hat->header.h + ov * PET_SCALE;
        pet_src(pet_hat, hat);
        pet_pos(pet_hat, hx, hy);
        top_y = hy;
    }

    pet_show(pet_umbrella, umbrella);
    if (umbrella) {
        int32_t uh = pet_img_umbrella.header.h;
        int32_t uy = y0 - uh;                         /* handle meets the raised arm */
        pet_pos(pet_umbrella, x0 + (PET_HOLD_COL - 10) * PET_SCALE, uy);
        if (uy < top_y) top_y = uy;
    }

    pet_show(pet_sweat, sweat);
    if (sweat) {
        uint32_t ph = now % 1400;
        pet_pos(pet_sweat, x0 + PET_SWEAT_COL * PET_SCALE,
                y0 + (fr->head + PET_SWEAT_ROW) * PET_SCALE + (int32_t)(ph * 9 / 1400));
        pet_opa(pet_sweat, (lv_opa_t)(255 - ph * 180 / 1400));
    }

    pet_show(pet_dim, (env & PEV_NIGHT) != 0);

    /* Particles */
    if (now >= pet_next_spawn) {
        if (zs) {
            pet_spawn(pet_zs, PET_ZS, NULL, x0 + w - 10, y0 - 6, 1800, now);
            pet_next_spawn = now + 900;
        } else if (sparks) {
            pet_spawn(pet_parts, PET_PARTS, &pet_img_spark,
                      x0 + (int32_t)lv_rand(0, (uint32_t)w), y0 + (int32_t)lv_rand(0, (uint32_t)(h * 6 / 10)), 1100, now);
            pet_next_spawn = now + 170;
        } else {
            pet_next_spawn = now + 200;
        }
    }
    pet_step_particles(pet_parts, PET_PARTS, now, dt);
    pet_step_particles(pet_zs, PET_ZS, now, dt);

    /* Speech bubble with the number of sessions waiting for a reply */
    pet_show(pet_bubble, bubble);
    pet_show(pet_tail, bubble);
    if (bubble) {
        int n = pet_waiting_count(now);
        if (n < 1) n = 1;                              /* demo with no real sessions */
        if (n != pet_bubble_n) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d waiting", n);
            lv_label_set_text(pet_bubble_lbl, buf);
            lv_obj_update_layout(pet_bubble);
            pet_bubble_w = lv_obj_get_width(pet_bubble);
            pet_bubble_n = n;
        }
        int32_t bx = x0 + w - 4;
        if (bx > 236 - pet_bubble_w) bx = 236 - pet_bubble_w;
        int32_t by = top_y - 18 - 6;
        if (by < PET_ROOM_TOP + 3) by = PET_ROOM_TOP + 3;
        pet_pos(pet_bubble, bx, by);
        pet_pos(pet_tail, bx + 4, by + 17);
    }

    if (pet_toast_until && now > pet_toast_until) { pet_show(pet_toast, false); pet_toast_until = 0; }

    if (pet_dirty && now - pet_last_save_ms > PET_SAVE_EVERY_MS) pet_save_now();
}

static void pet_tick_cb(lv_timer_t *t) { (void)t; pet_render(lv_tick_get()); }

/* ============================================================
 * Build — called once from build_monitor_screen()
 * ============================================================ */
static void pet_build(lv_obj_t *scr) {
    if (P.version != PET_PROGRESS_VERSION) pet_progress_defaults();

    pet_rect(scr, 0, PET_ROOM_TOP, 240, PET_FLOOR - PET_ROOM_TOP, 0x0C0C22);   /* wall */
    pet_win = pet_image(scr, &pet_img_win_day, 14, 158, false);
    pet_rect(scr, 0, PET_FLOOR, 240, 12, 0x11112E);                            /* floor */
    pet_rect(scr, 0, PET_FLOOR, 240, 1, 0x1D1D46);

    pet_plant  = pet_image(scr, &pet_img_plant,  10,  PET_FLOOR - (int32_t)pet_img_plant.header.h,  true);
    pet_mug    = pet_image(scr, &pet_img_mug,    54,  PET_FLOOR - (int32_t)pet_img_mug.header.h,    true);
    pet_trophy = pet_image(scr, &pet_img_trophy, 206, PET_FLOOR - (int32_t)pet_img_trophy.header.h, true);
    pet_laptop = pet_image(scr, &pet_img_laptop0, 157, PET_FLOOR - (int32_t)pet_img_laptop0.header.h, true);

    pet_body     = pet_image(scr, &pet_img_idle, 0, 0, false);
    pet_scarf    = pet_image(scr, &pet_img_scarf, 0, 0, true);
    pet_hat      = pet_image(scr, &pet_img_beanie, 0, 0, true);
    pet_umbrella = pet_image(scr, &pet_img_umbrella, 0, 0, true);
    pet_sweat    = pet_image(scr, &pet_img_sweat, 0, 0, true);

    /* Night: darken the room, but keep the bubble and particles on top */
    pet_dim = pet_rect(scr, 0, PET_ROOM_TOP, 240, PET_FLOOR + 12 - PET_ROOM_TOP, 0x040416);
    lv_obj_set_style_bg_opa(pet_dim, LV_OPA_50, 0);
    lv_obj_add_flag(pet_dim, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < PET_PARTS; i++) {
        pet_parts[i].o = pet_image(scr, &pet_img_heart, 0, 0, true);
        pet_parts[i].live = false;
    }
    for (int i = 0; i < PET_ZS; i++) {
        lv_obj_t *z = lv_label_create(scr);
        lv_label_set_text(z, (i % 2) ? "Z" : "z");
        lv_obj_set_style_text_font(z, (i % 2) ? &lv_font_montserrat_14 : &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(z, CM_TEXT_SEC, 0);
        lv_obj_add_flag(z, LV_OBJ_FLAG_HIDDEN);
        pet_zs[i].o = z;
        pet_zs[i].live = false;
    }

    pet_bubble = lv_obj_create(scr);
    lv_obj_set_size(pet_bubble, LV_SIZE_CONTENT, 18);
    lv_obj_set_style_bg_color(pet_bubble, lv_color_hex(0xEEEEFF), 0);
    lv_obj_set_style_bg_opa(pet_bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pet_bubble, 0, 0);
    lv_obj_set_style_radius(pet_bubble, 4, 0);
    lv_obj_set_style_pad_hor(pet_bubble, 6, 0);
    lv_obj_set_style_pad_ver(pet_bubble, 2, 0);
    lv_obj_remove_flag(pet_bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(pet_bubble, LV_OBJ_FLAG_CLICKABLE);
    pet_bubble_lbl = lv_label_create(pet_bubble);
    lv_label_set_text(pet_bubble_lbl, "1 waiting");
    lv_obj_set_style_text_font(pet_bubble_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pet_bubble_lbl, lv_color_hex(0x0A0A20), 0);
    lv_obj_center(pet_bubble_lbl);
    lv_obj_add_flag(pet_bubble, LV_OBJ_FLAG_HIDDEN);
    pet_tail = pet_image(scr, &pet_img_tail, 0, 0, true);

    pet_toast = lv_label_create(scr);
    lv_label_set_text(pet_toast, "");
    lv_obj_set_style_text_font(pet_toast, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pet_toast, CM_YELLOW, 0);
    lv_obj_set_style_bg_color(pet_toast, lv_color_hex(0x0A0A20), 0);
    lv_obj_set_style_bg_opa(pet_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pet_toast, 9, 0);
    lv_obj_set_style_pad_hor(pet_toast, 8, 0);
    lv_obj_set_style_pad_ver(pet_toast, 2, 0);
    lv_obj_add_flag(pet_toast, LV_OBJ_FLAG_HIDDEN);

    /* One fixed tap target over the pet: frames change size, the finger doesn't */
    pet_hit = lv_obj_create(scr);
    lv_obj_set_pos(pet_hit, 60, PET_ROOM_TOP);
    lv_obj_set_size(pet_hit, 120, PET_FLOOR - PET_ROOM_TOP);
    lv_obj_set_style_bg_opa(pet_hit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pet_hit, 0, 0);
    lv_obj_remove_flag(pet_hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(pet_hit, pet_hit_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(pet_hit, pet_hit_cb, LV_EVENT_LONG_PRESSED, NULL);

    /* Status row: level, XP to the next level, rewards collected */
    pet_rect(scr, 0, 270, 240, 1, 0x18183A);
    pet_lv = lv_label_create(scr);
    lv_obj_set_style_text_font(pet_lv, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pet_lv, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(pet_lv, 8, 275);
    pet_xpbar = lv_bar_create(scr);
    lv_obj_set_size(pet_xpbar, 88, 8);
    lv_obj_set_pos(pet_xpbar, 52, 280);
    lv_obj_set_style_bg_color(pet_xpbar, CM_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pet_xpbar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(pet_xpbar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pet_xpbar, CM_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(pet_xpbar, 4, LV_PART_INDICATOR);
    pet_xplbl = lv_label_create(scr);
    lv_obj_set_style_text_font(pet_xplbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pet_xplbl, CM_TEXT_SEC, 0);
    lv_obj_set_pos(pet_xplbl, 146, 277);
    pet_image(scr, &pet_img_star, 206, 277, false);
    pet_awards = lv_label_create(scr);
    lv_obj_set_style_text_font(pet_awards, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pet_awards, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(pet_awards, 222, 275);

    pet_update_status();
    pet_render(lv_tick_get());
    lv_timer_create(pet_tick_cb, 50, NULL);
}

#endif /* CLAUDE_PET_H */
