/* ============================================================
 * claude_pet.h — the Claude Code mascot as a small tamagotchi
 *
 * Lives in the room strip of the Monitor screen (y=147..292).
 *
 * XP and levels follow the same rules as a colleague's pet, so the two
 * compare one to one:
 *   +10  Claude finished an answer           (hook Stop)
 *   +5   you answered a permission/question  (hook: waiting -> busy)
 *   +15  a subagent started                  (hook SubagentStart)
 *   +20  the context was compacted           (hook PreCompact)
 *   The same event twice in a row (per session) counts once — the bridge
 *   de-duplicates before sending. XP is never taken away.
 *   Total XP needed for level L: 200 * (L - 1)^2.
 *
 * Every level up to 30 unlocks a thing (src/pet_sprites.h, pet_items); one
 * thing per slot is shown — the newest — and long-press picks the hat.
 * Fifteen achievements hang as medals on a shelf in the room.
 *
 * Mood comes from what the device already knows:
 *   waiting chats -> waves                5H >= 100% -> slumped, x x eyes, smoke
 *   Claude acting -> works at a laptop    5H >= 75% -> tired eyes (sweat at 90%)
 *   night / idle 2h -> sleeps             5H >= 50% -> yawns now and then
 *   no Claude for 3 days -> sits under a rain cloud
 *   5H window reset -> celebrates         rain / snow / alert -> umbrella, scarf, helmet
 *
 * Progress is handed to the host through pet_save_cb (main.cpp keeps it in
 * NVS), so this file stays platform-neutral. All objects are created once in
 * pet_build(); the 50 ms timer only swaps image sources and moves things,
 * and touches an object only when its value actually changes.
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
#define PET_SWEAT_COL  18
#define PET_SWEAT_ROW  1
#define PET_HOLD_COL   18       /* raised right arm, holds the umbrella */

/* ── Rules ── */
#define PET_XP_ANSWER       10u
#define PET_XP_REPLY        5u
#define PET_XP_AGENT        15u
#define PET_XP_COMPACT      20u
#define PET_DOZE_MS         (2UL * 60UL * 60UL * 1000UL)   /* idle this long -> dozes */
#define PET_BORED_DAYS      3
#define PET_WAITING_TTL_MS  (5UL * 60UL * 1000UL)          /* bridge silent -> 0 waiting/working */
#define PET_SAVE_EVERY_MS   (5UL * 60UL * 1000UL)          /* NVS writes, at most */

/* Total XP at which level L starts. */
static uint32_t pet_level_floor(uint32_t level) { return 200u * (level - 1u) * (level - 1u); }
static uint32_t pet_level_of(uint32_t xp) {
    uint32_t l = 1;
    while (pet_level_floor(l + 1) <= xp && l < 250) l++;
    return l;
}

/* ── States ── */
enum { PS_IDLE, PS_WORKING, PS_WAITING, PS_TIRED, PS_SLEEP, PS_BORED, PS_JOY, PS_EXHAUSTED, PS_COUNT };
static const char *const PET_STATE_NAMES[PS_COUNT] = {
    "idle", "working", "waiting", "tired", "sleep", "bored", "joy", "exhausted"
};
enum { PEV_RAIN = 1, PEV_SNOW = 2, PEV_COLD = 4, PEV_NIGHT = 8, PEV_ALERT = 16 };

/* ── Progress (persisted) ── */
#define PET_PROGRESS_VERSION 2
#define PET_PROJ_MAX 5
typedef struct {
    uint8_t  version;
    uint8_t  hat_pick;          /* 0 newest hat, 255 none, else pet_items index + 1 */
    uint16_t pets_total;
    uint32_t total_xp;
    uint32_t achs;              /* 1 << ACH_* */
    uint32_t answers_total;
    uint16_t answers_today;
    uint16_t replies_total;     /* permissions / questions you answered */
    uint16_t compactions;
    uint16_t streak;
    uint16_t best_streak;
    uint16_t early_days;
    uint16_t active_since_cap;
    uint8_t  proj_n;
    uint8_t  reserved;
    int32_t  answers_day;       /* local days since 1970, -1 = never */
    int32_t  last_active_day;
    int32_t  last_early_day;
    int32_t  last_cap_day;
    int32_t  proj_day;
    int32_t  sat_day;           /* last Saturday with an answer, for "weekend" */
    uint32_t proj_hash[PET_PROJ_MAX];
} pet_progress_t;

static pet_progress_t P;
static void (*pet_save_cb)(const pet_progress_t *p) = NULL;
static bool     pet_dirty        = false;
static uint32_t pet_last_save_ms = 0;

/* ── Inputs ── */
static int      pet_waiting       = 0;
static bool     pet_waiting_seen  = false;
static uint32_t pet_waiting_ms    = 0;
static int      pet_cold          = 0;      /* waits older than 5 min: just counted */
static int      pet_working       = 0;
static bool     pet_working_seen  = false;
static uint32_t pet_working_ms    = 0;
static bool     pet_night         = false;
static int      pet_wcode         = -1;
static int      pet_temp_c        = 99;
static int      pet_fh            = 0;      /* 5H %, drives the mood */
static int      pet_prev_fh       = -1;
static uint32_t pet_last_usage_ms = 0;      /* last Claude action; boot counts as one */
static uint32_t pet_joy_until     = 0;
static int      pet_demo_state    = -1;
static int      pet_demo_env      = -1;
static uint32_t pet_demo_until    = 0;
static int      pet_preview_lv    = 0;      /* show this level's things for a while */
static uint32_t pet_preview_until = 0;
static int      pet_state_cur     = PS_IDLE;

/* ── Objects ── */
static lv_obj_t *pet_win, *pet_wall, *pet_decor, *pet_rack, *pet_laptop, *pet_pet;
static lv_obj_t *pet_medal[ACH_COUNT];
static lv_obj_t *pet_slot[SLOT_COUNT];      /* worn slots only: HEAD..HAND, AURA */
static lv_obj_t *pet_body, *pet_umbrella, *pet_sweat, *pet_cloud, *pet_dim, *pet_toast, *pet_hit;
static lv_obj_t *pet_lv, *pet_xpbar, *pet_xplbl, *pet_awards, *pet_coldlbl;
static int       pet_cold_shown = -1;
static int       pet_bx, pet_by, pet_bw;    /* last body box, for hearts */
static uint32_t  pet_next_yawn = 15000;

/* Toasts queue up, so a level, its new thing and an achievement earned in
 * the same push are each shown instead of overwriting one another. */
#define PET_TOASTS 4
static char      pet_toastq[PET_TOASTS][64];
static int       pet_toast_head = 0, pet_toast_n = 0;
static uint32_t  pet_toast_until = 0;

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
/* -1 until NTP has set the clock. */
static int32_t pet_today(int *hour, int *wday) {
    time_t now = time(NULL);
    if (now < 1700000000) return -1;
    struct tm *t = localtime(&now);
    if (!t) return -1;
    if (hour) *hour = t->tm_hour;
    if (wday) *wday = t->tm_wday;
    return pet_days_from_civil(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
}

/* ============================================================
 * Toasts
 * ============================================================ */
static void pet_toast_push(const char *txt) {
    if (pet_toast_n == PET_TOASTS) {          /* full: drop the oldest */
        pet_toast_head = (pet_toast_head + 1) % PET_TOASTS;
        pet_toast_n--;
    }
    int i = (pet_toast_head + pet_toast_n) % PET_TOASTS;
    snprintf(pet_toastq[i], sizeof(pet_toastq[i]), "%s", txt);
    pet_toast_n++;
}

static void pet_toast_step(uint32_t now) {
    if (!pet_toast) return;
    if (pet_toast_until && now < pet_toast_until) return;
    if (pet_toast_n == 0) {
        if (pet_toast_until) { pet_show(pet_toast, false); pet_toast_until = 0; }
        return;
    }
    lv_label_set_text(pet_toast, pet_toastq[pet_toast_head]);
    pet_toast_head = (pet_toast_head + 1) % PET_TOASTS;
    pet_toast_n--;
    lv_obj_align(pet_toast, LV_ALIGN_TOP_MID, 0, PET_ROOM_TOP + 3);
    pet_show(pet_toast, true);
    pet_toast_until = now + 2600;
}

/* ============================================================
 * Progress, levels, unlocks, achievements
 * ============================================================ */
static void pet_update_status(void);
static void pet_update_medals(void);

static void pet_progress_defaults(void) {
    memset(&P, 0, sizeof(P));
    P.version = PET_PROGRESS_VERSION;
    P.answers_day = P.last_active_day = P.last_early_day = -1;
    P.last_cap_day = P.proj_day = P.sat_day = -1;
}

static void pet_save_now(void) {
    if (pet_save_cb) pet_save_cb(&P);
    pet_dirty = false;
    pet_last_save_ms = lv_tick_get();
}

static void pet_celebrate(uint32_t ms) {
    uint32_t until = lv_tick_get() + ms;
    if (until > pet_joy_until) pet_joy_until = until;
}

static void pet_achieve(int a) {
    if (a < 0 || a >= ACH_COUNT || (P.achs & (1UL << a))) return;
    P.achs |= (1UL << a);
    char buf[64];
    snprintf(buf, sizeof(buf), "Ачивка: %s", pet_achs[a].name);
    pet_toast_push(buf);
    pet_celebrate(2200);
    pet_update_medals();
    pet_update_status();
    pet_save_now();
}

static void pet_add_xp(uint32_t n) {
    if (n == 0) return;
    uint32_t before = pet_level_of(P.total_xp);
    P.total_xp += n;
    pet_dirty = true;
    uint32_t after = pet_level_of(P.total_xp);
    if (after > before) {
        bool new_hat = false;
        const char *last = NULL;
        for (int i = 0; i < PET_ITEM_COUNT; i++) {
            if (pet_items[i].level > before && pet_items[i].level <= after) {
                last = pet_items[i].name;
                if (pet_items[i].slot == SLOT_HEAD) new_hat = true;
            }
        }
        if (new_hat) P.hat_pick = 0;           /* a new hat goes straight on */
        char buf[64];
        if (last) snprintf(buf, sizeof(buf), "Уровень %u · %s", (unsigned)after, last);
        else      snprintf(buf, sizeof(buf), "Уровень %u", (unsigned)after);
        pet_toast_push(buf);
        pet_celebrate(2400);
        pet_save_now();
    }
    pet_update_status();
}

/* Any XP-bearing event: streaks, early bird, weekend, the trophy clock. */
static void pet_note_activity(void) {
    int hour = 12, wday = 3;
    int32_t d = pet_today(&hour, &wday);
    if (d < 0) return;
    if (P.last_active_day != d) {
        P.streak = (P.last_active_day == d - 1) ? (uint16_t)(P.streak + 1) : 1;
        if (P.streak > P.best_streak) P.best_streak = P.streak;
        P.last_active_day = d;
        if (P.last_cap_day < 0) P.last_cap_day = d;   /* the "steady" clock starts on day one */
        P.active_since_cap++;
        if (hour < 9 && P.last_early_day != d) { P.early_days++; P.last_early_day = d; }
        pet_dirty = true;
    }
    if (P.streak >= 7)     pet_achieve(ACH_NODAYSOFF);
    if (P.early_days >= 5) pet_achieve(ACH_EARLY);
    if (P.last_cap_day >= 0 && d - P.last_cap_day >= 7 && P.active_since_cap >= 5)
        pet_achieve(ACH_STEADY);
}

static void pet_note_answers(int n) {
    int hour = 12, wday = 3;
    int32_t d = pet_today(&hour, &wday);
    if (d >= 0 && P.answers_day != d) { P.answers_day = d; P.answers_today = 0; }
    P.answers_today = (uint16_t)(P.answers_today + n);
    P.answers_total += (uint32_t)n;
    pet_achieve(ACH_FIRST);
    if (hour >= 2 && hour < 5)    pet_achieve(ACH_OWL);
    if (P.answers_today >= 100)   pet_achieve(ACH_MARATHON);
    if (P.answers_total >= 1000)  pet_achieve(ACH_THOUSAND);
    if (d >= 0 && wday == 6) P.sat_day = d;
    if (d >= 0 && wday == 0 && P.sat_day == d - 1) pet_achieve(ACH_WEEKEND);
}

/* 5H hit 100% today: the "steady" week starts over. */
static void pet_note_cap(void) {
    int32_t d = pet_today(NULL, NULL);
    if (d < 0 || P.last_cap_day == d) return;
    P.last_cap_day = d;
    P.active_since_cap = 0;
    pet_dirty = true;
}

static bool pet_bored(void) {
    int32_t d = pet_today(NULL, NULL);
    return d >= 0 && P.last_active_day >= 0 && d - P.last_active_day >= PET_BORED_DAYS;
}

/* ============================================================
 * What's worn / shown
 * ============================================================ */
static int pet_level_shown(uint32_t now) {
    if (pet_preview_lv > 0 && now < pet_preview_until) return pet_preview_lv;
    return (int)pet_level_of(P.total_xp);
}

static bool pet_item_on(int i, int level) {
    return level >= pet_items[i].level && (pet_items[i].until == 0 || level < pet_items[i].until);
}

/* Newest unlocked item for a slot, or -1. The hat honours a long-press pick. */
static int pet_slot_item(int slot, int level, bool preview) {
    if (slot == SLOT_HEAD && !preview) {
        if (P.hat_pick == 255) return -1;
        int pick = (int)P.hat_pick - 1;
        if (pick >= 0 && pick < PET_ITEM_COUNT && pet_items[pick].slot == SLOT_HEAD && pet_item_on(pick, level))
            return pick;
    }
    int best = -1;
    for (int i = 0; i < PET_ITEM_COUNT; i++)
        if (pet_items[i].slot == slot && pet_item_on(i, level)) best = i;
    return best;
}

/* ============================================================
 * Host API
 * ============================================================ */
static void pet_load_progress(const pet_progress_t *p) {
    if (p && p->version == PET_PROGRESS_VERSION) P = *p;
    else pet_progress_defaults();
    if (pet_lv) { pet_update_status(); pet_update_medals(); }
}

/* Counts the bridge de-duplicated from the hooks since its last push. */
static void pet_on_events(int answers, int replies, int agents, int compactions) {
    if (answers < 0) answers = 0;
    if (replies < 0) replies = 0;
    if (agents < 0) agents = 0;
    if (compactions < 0) compactions = 0;
    uint32_t xp = PET_XP_ANSWER * (uint32_t)answers + PET_XP_REPLY * (uint32_t)replies
                + PET_XP_AGENT * (uint32_t)agents + PET_XP_COMPACT * (uint32_t)compactions;
    if (xp == 0) return;
    pet_last_usage_ms = lv_tick_get();
    pet_note_activity();
    if (answers) pet_note_answers(answers);
    if (replies) {
        P.replies_total = (uint16_t)(P.replies_total + replies);
        if (P.replies_total >= 100) pet_achieve(ACH_TRUST);
    }
    if (compactions) {
        P.compactions = (uint16_t)(P.compactions + compactions);
        if (P.compactions >= 10) pet_achieve(ACH_ARCHIVIST);
    }
    pet_add_xp(xp);
}

/* Projects that finished answers since the last push, as CRC32 hex. */
static void pet_on_projects(const char *csv) {
    int32_t d = pet_today(NULL, NULL);
    if (d < 0) return;
    if (P.proj_day != d) { P.proj_day = d; P.proj_n = 0; }
    while (*csv) {
        uint32_t h = (uint32_t)strtoul(csv, NULL, 16);
        bool seen = false;
        for (int i = 0; i < P.proj_n; i++) if (P.proj_hash[i] == h) seen = true;
        if (!seen && P.proj_n < PET_PROJ_MAX) { P.proj_hash[P.proj_n++] = h; pet_dirty = true; }
        const char *comma = strchr(csv, ',');
        if (!comma) break;
        csv = comma + 1;
    }
    if (P.proj_n >= 5) pet_achieve(ACH_POLYGLOT);
}

static void pet_on_agents(int concurrent) { if (concurrent >= 3) pet_achieve(ACH_FOREMAN); }

/* Limits: the 5H window sets the mood; any limit at 100% is "on fumes". */
static void pet_on_usage(int fh, int worst) {
    if (pet_prev_fh >= 50 && fh <= 5) {                /* the 5h window just reset */
        pet_toast_push("5H обновился!");
        pet_celebrate(3000);
    }
    pet_prev_fh = fh;
    pet_fh = fh;
    if (worst >= 100) pet_achieve(ACH_FUMES);
    if (fh >= 100) pet_note_cap();
}

static void pet_set_cold(int n) { pet_cold = n < 0 ? 0 : n; }
static void pet_set_waiting(int n) {
    pet_waiting = n < 0 ? 0 : n;
    pet_waiting_seen = true;
    pet_waiting_ms = lv_tick_get();
    if (pet_waiting + pet_cold >= 5) pet_achieve(ACH_JUGGLER);
}
static void pet_set_working(int n) {
    pet_working = n < 0 ? 0 : n;
    pet_working_seen = true;
    pet_working_ms = lv_tick_get();
}
static void pet_set_night(bool night) { pet_night = night; }
static void pet_set_weather(int wcode, int temp_c) { pet_wcode = wcode; pet_temp_c = temp_c; }

/* Force a state, an environment and/or the things of a level for a few
 * seconds — for checking the art on the real screen. */
static void pet_demo(const char *state, const char *env, int level, int sec) {
    if (sec <= 0) sec = 20;
    uint32_t until = lv_tick_get() + (uint32_t)sec * 1000u;
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
    pet_demo_until = until;
    if (level > 0) { pet_preview_lv = level; pet_preview_until = until; }
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

static int pet_state_now(uint32_t now, uint8_t env) {
    if (now < pet_demo_until && pet_demo_state >= 0) return pet_demo_state;
    if (now < pet_joy_until)                      return PS_JOY;
    /* Working outranks waiting: the ticker already lists who waits, and a
     * chat idling somewhere shouldn't hide that Claude is busy elsewhere. */
    if (pet_working_seen && pet_working > 0 && now - pet_working_ms <= PET_WAITING_TTL_MS)
        return PS_WORKING;
    if (pet_waiting_count(now) > 0)               return PS_WAITING;
    if (pet_fh >= 100)                            return PS_EXHAUSTED;
    if (pet_fh >= 75)                             return PS_TIRED;
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

/* ============================================================
 * Status row + medals
 * ============================================================ */
static void pet_fmt_xp(char *out, size_t n, uint32_t v) {
    if (v >= 10000)     snprintf(out, n, "%uk", (unsigned)(v / 1000));
    else if (v >= 1000) snprintf(out, n, "%u.%uk", (unsigned)(v / 1000), (unsigned)(v % 1000 / 100));
    else                snprintf(out, n, "%u", (unsigned)v);
}

static void pet_update_status(void) {
    if (!pet_lv) return;
    uint32_t lv = pet_level_of(P.total_xp);
    uint32_t base = pet_level_floor(lv), need = pet_level_floor(lv + 1) - base;
    uint32_t in = P.total_xp - base;
    char buf[32], a[12], b[12];
    snprintf(buf, sizeof(buf), "Lv %u", (unsigned)lv);
    lv_label_set_text(pet_lv, buf);
    lv_bar_set_range(pet_xpbar, 0, (int32_t)need);
    lv_bar_set_value(pet_xpbar, (int32_t)in, LV_ANIM_OFF);
    pet_fmt_xp(a, sizeof(a), in);
    pet_fmt_xp(b, sizeof(b), need);
    snprintf(buf, sizeof(buf), "%s/%s", a, b);
    lv_label_set_text(pet_xplbl, buf);
    snprintf(buf, sizeof(buf), "%d/%d", pet_popcount(P.achs), ACH_COUNT);
    lv_label_set_text(pet_awards, buf);
}

static void pet_update_medals(void) {
    if (!pet_medal[0]) return;
    for (int a = 0; a < ACH_COUNT; a++)
        pet_src(pet_medal[a], (P.achs & (1UL << a)) ? pet_achs[a].medal : &pet_img_medal_locked);
}

/* ============================================================
 * Touch
 * ============================================================ */
static void pet_hearts(void) {
    uint32_t now = lv_tick_get();
    for (int i = 0; i < 5; i++) {
        int32_t x = pet_bx + pet_bw / 2 - 5 + (int32_t)lv_rand(0, (uint32_t)(pet_bw * 7 / 10)) - pet_bw * 7 / 20;
        pet_spawn(pet_parts, PET_PARTS, &pet_img_heart, x, pet_by - 2, 1100, now);
    }
}

static void pet_petted(void) {
    pet_hearts();
    pet_celebrate(1200);
    P.pets_total++;
    pet_dirty = true;
    if (P.pets_total >= 100) pet_achieve(ACH_KIND);
}

/* Long press: next unlocked hat, then none, then back round. */
static void pet_cycle_hat(void) {
    int level = (int)pet_level_of(P.total_xp);
    int cur = pet_slot_item(SLOT_HEAD, level, false);
    int next = -1;
    for (int i = cur + 1; i < PET_ITEM_COUNT; i++)
        if (pet_items[i].slot == SLOT_HEAD && pet_item_on(i, level)) { next = i; break; }
    if (cur < 0) {                          /* from "no hat" back to the first one */
        for (int i = 0; i < PET_ITEM_COUNT; i++)
            if (pet_items[i].slot == SLOT_HEAD && pet_item_on(i, level)) { next = i; break; }
    }
    if (next < 0 && cur < 0) { pet_toast_push("Шапки — со 2 уровня"); return; }
    P.hat_pick = next < 0 ? 255 : (uint8_t)(next + 1);
    pet_toast_push(next < 0 ? "Без шапки" : pet_items[next].name);
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
static void pet_place_worn(int slot, int item, const pet_frame_t *fr, int32_t x0, int32_t y0) {
    lv_obj_t *o = pet_slot[slot];
    if (!o) return;
    pet_show(o, item >= 0);
    if (item < 0) return;
    const pet_item_t *it = &pet_items[item];
    pet_src(o, it->img[0]);
    int32_t c, r;
    if (slot == SLOT_HAND) { c = fr->hand_c - it->col; r = fr->hand_r - it->row; }
    else                   { c = it->col;              r = fr->head + it->row;   }
    pet_pos(o, x0 + c * PET_SCALE, y0 + r * PET_SCALE);
}

static void pet_render(uint32_t now) {
    uint32_t dt = pet_last_tick ? now - pet_last_tick : 0;
    if (dt > 200) dt = 200;
    pet_last_tick = now;

    uint8_t env = pet_env(now);
    int st = pet_state_now(now, env);
    pet_state_cur = st;
    int level = pet_level_shown(now);
    bool preview = pet_preview_lv > 0 && now < pet_preview_until;

    int f = PF_IDLE;
    int32_t dy = 0;
    bool umbrella = false, laptop = false, sweat = false, zs = false, sparks = false;
    bool cloud = false, smoke = false;

    switch (st) {
    case PS_IDLE:
        f  = (now % 3740) < 140 ? PF_BLINK : PF_IDLE;
        dy = ((now / 700) % 2) ? -2 : 0;
        if (pet_fh >= 50 && now >= pet_next_yawn) {            /* half the 5h window gone */
            f = PF_YAWN; dy = 0;
            if (now >= pet_next_yawn + 1200) pet_next_yawn = now + 15000 + lv_rand(0, 10000);
        }
        if (env & PEV_RAIN) { f = PF_HOLD; umbrella = true; dy = 0; }
        break;
    case PS_WORKING:
        f = ((now / 220) % 2) ? PF_WORK1 : PF_WORK0;
        laptop = true;
        break;
    case PS_WAITING:
        if (env & PEV_RAIN) { f = PF_HOLD; umbrella = true; }
        else f = ((now / 280) % 2) ? PF_WAVE1 : PF_WAVE0;
        dy = (now % 1600) < 180 ? -4 : 0;
        break;
    case PS_TIRED:
        f = PF_TIRED; dy = ((now / 1200) % 2) ? 1 : 0;
        sweat = pet_fh >= 90;
        break;
    case PS_EXHAUSTED:
        f = PF_DEAD; smoke = true;
        break;
    case PS_SLEEP:
        f = PF_SLEEP; dy = ((now / 1100) % 2) ? 1 : 0; zs = true;
        break;
    case PS_BORED:
        f = PF_BORED; cloud = true;
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

    /* Room things of this level: wall, floor decor, server rack, companion */
    bool tick2 = ((now / 650) % 2) != 0;
    int wall = pet_slot_item(SLOT_WALL, level, preview);
    pet_show(pet_wall, wall >= 0);
    if (wall >= 0) {
        const lv_image_dsc_t *img = (tick2 && pet_items[wall].img[1]) ? pet_items[wall].img[1] : pet_items[wall].img[0];
        pet_src(pet_wall, img);
        pet_pos(pet_wall, PET_CX - (int32_t)img->header.w / 2, PET_ROOM_TOP + 4);
    }
    int decor = pet_slot_item(SLOT_DECOR, level, preview);
    const lv_image_dsc_t *dimg = decor >= 0
        ? ((tick2 && pet_items[decor].img[1]) ? pet_items[decor].img[1] : pet_items[decor].img[0])
        : &pet_img_plant;
    pet_src(pet_decor, dimg);
    pet_pos(pet_decor, 4, PET_FLOOR - (int32_t)dimg->header.h);
    int rack = pet_slot_item(SLOT_RACK, level, preview);
    pet_show(pet_rack, rack >= 0);
    if (rack >= 0) pet_src(pet_rack, pet_items[rack].img[tick2 ? 1 : 0]);
    int comp = pet_slot_item(SLOT_PET, level, preview);
    pet_show(pet_pet, comp >= 0);
    if (comp >= 0) {
        const lv_image_dsc_t *img = pet_items[comp].img[((now / 600) % 2) && st != PS_SLEEP ? 1 : 0];
        pet_src(pet_pet, img);
        int32_t hop = (st == PS_JOY && ((now / 260) % 2)) ? -4 : 0;
        pet_pos(pet_pet, 38, PET_FLOOR - (int32_t)img->header.h + hop);
    }

    pet_show(pet_laptop, laptop);
    if (laptop) pet_src(pet_laptop, ((now / 400) % 2) ? &pet_img_laptop1 : &pet_img_laptop0);

    /* Body and everything he wears */
    const pet_frame_t *fr = &pet_frames[f];
    int32_t w = fr->img->header.w, h = fr->img->header.h;
    int32_t x0 = PET_CX - w / 2;
    int32_t y0 = PET_FLOOR - h + dy;
    pet_src(pet_body, fr->img);
    pet_pos(pet_body, x0, y0);
    pet_bx = x0; pet_by = y0; pet_bw = w;

    int aura = pet_slot_item(SLOT_AURA, level, preview);
    pet_place_worn(SLOT_AURA, aura, fr, x0, y0);
    if (aura >= 0) pet_opa(pet_slot[SLOT_AURA], (lv_opa_t)(150 + (int)((now / 80) % 20) * 5));
    pet_place_worn(SLOT_BACK,  pet_slot_item(SLOT_BACK,  level, preview), fr, x0, y0);
    pet_place_worn(SLOT_SHIRT, pet_slot_item(SLOT_SHIRT, level, preview), fr, x0, y0);
    int neck = pet_slot_item(SLOT_NECK, level, preview);
    if (neck < 0 && (env & PEV_COLD)) {                         /* weather lends the scarf */
        for (int i = 0; i < PET_ITEM_COUNT; i++) if (pet_items[i].slot == SLOT_NECK) neck = i;
    }
    pet_place_worn(SLOT_NECK, neck, fr, x0, y0);
    pet_place_worn(SLOT_FACE, pet_slot_item(SLOT_FACE, level, preview), fr, x0, y0);
    pet_place_worn(SLOT_EARS, pet_slot_item(SLOT_EARS, level, preview), fr, x0, y0);

    /* Headgear: the helmet during an alert, otherwise the hat. */
    int32_t top_y = y0 + fr->head * PET_SCALE;
    lv_obj_t *head = pet_slot[SLOT_HEAD];
    if (env & PEV_ALERT) {
        pet_show(head, true);
        pet_src(head, &pet_img_helmet);
        pet_pos(head, x0 + 3 * PET_SCALE, y0 + (fr->head - 3) * PET_SCALE);
        top_y = y0 + (fr->head - 3) * PET_SCALE;
    } else {
        int hat = pet_slot_item(SLOT_HEAD, level, preview);
        pet_place_worn(SLOT_HEAD, hat, fr, x0, y0);
        if (hat >= 0) top_y = y0 + (fr->head + pet_items[hat].row) * PET_SCALE;
    }
    pet_place_worn(SLOT_HAND, pet_slot_item(SLOT_HAND, level, preview), fr, x0, y0);

    pet_show(pet_umbrella, umbrella);
    if (umbrella) {
        int32_t uy = y0 - (int32_t)pet_img_umbrella.header.h;   /* handle meets the raised arm */
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
    pet_show(pet_cloud, cloud);
    if (cloud) {
        pet_src(pet_cloud, ((now / 500) % 2) ? &pet_img_cloud1 : &pet_img_cloud0);
        pet_pos(pet_cloud, PET_CX - (int32_t)pet_img_cloud0.header.w / 2, top_y - (int32_t)pet_img_cloud0.header.h - 4);
    }

    pet_show(pet_dim, (env & PEV_NIGHT) != 0);

    /* Particles: Z's asleep, sparks in joy, smoke when spent */
    if (now >= pet_next_spawn) {
        if (zs) {
            pet_spawn(pet_zs, PET_ZS, NULL, x0 + w - 10, y0 - 6, 1800, now);
            pet_next_spawn = now + 900;
        } else if (sparks) {
            pet_spawn(pet_parts, PET_PARTS, &pet_img_spark,
                      x0 + (int32_t)lv_rand(0, (uint32_t)w), y0 + (int32_t)lv_rand(0, (uint32_t)(h * 6 / 10)), 1100, now);
            pet_next_spawn = now + 170;
        } else if (smoke) {
            pet_spawn(pet_parts, PET_PARTS, &pet_img_smoke, PET_CX - 6 + (int32_t)lv_rand(0, 12), top_y - 8, 1600, now);
            pet_next_spawn = now + 450;
        } else {
            pet_next_spawn = now + 200;
        }
    }
    pet_step_particles(pet_parts, PET_PARTS, now, dt);
    pet_step_particles(pet_zs, PET_ZS, now, dt);

    /* Cooled waits: a quiet count under the window */
    int cold = (pet_waiting_seen && now - pet_waiting_ms <= PET_WAITING_TTL_MS) ? pet_cold : 0;
    if (cold != pet_cold_shown) {
        pet_cold_shown = cold;
        pet_show(pet_coldlbl, cold > 0);
        if (cold > 0) {
            char buf[24];
            snprintf(buf, sizeof(buf), "ждут: %d", cold);
            lv_label_set_text(pet_coldlbl, buf);
        }
    }

    pet_toast_step(now);
    if (pet_dirty && now - pet_last_save_ms > PET_SAVE_EVERY_MS) pet_save_now();
}

static void pet_tick_cb(lv_timer_t *t) { (void)t; pet_render(lv_tick_get()); }

/* ============================================================
 * Build — called once from build_monitor_screen()
 * ============================================================ */
static void pet_build(lv_obj_t *scr) {
    if (P.version != PET_PROGRESS_VERSION) pet_progress_defaults();

    pet_rect(scr, 0, PET_ROOM_TOP, 240, PET_FLOOR - PET_ROOM_TOP, 0x0C0C22);   /* wall */
    pet_win  = pet_image(scr, &pet_img_win_day, 14, 158, false);
    pet_wall = pet_image(scr, &pet_img_poster, 0, 0, true);

    /* Medal shelf: every achievement has a slot, grey until it's earned */
    for (int row = 0; row < 3; row++)
        pet_rect(scr, 170, PET_ROOM_TOP + 19 + row * 18, 68, 2, 0x5E3B22);
    for (int a = 0; a < ACH_COUNT; a++)
        pet_medal[a] = pet_image(scr, &pet_img_medal_locked,
                                 172 + (a % 5) * 13, PET_ROOM_TOP + 3 + (a / 5) * 18, false);

    pet_coldlbl = lv_label_create(scr);                                         /* "ждут: N" */
    lv_label_set_text(pet_coldlbl, "");
    lv_obj_set_style_text_font(pet_coldlbl, CM_FONT_CYR_12, 0);
    lv_obj_set_style_text_color(pet_coldlbl, CM_YELLOW, 0);
    lv_obj_set_pos(pet_coldlbl, 14, 194);
    lv_obj_add_flag(pet_coldlbl, LV_OBJ_FLAG_HIDDEN);

    pet_rect(scr, 0, PET_FLOOR, 240, 12, 0x11112E);                            /* floor */
    pet_rect(scr, 0, PET_FLOOR, 240, 1, 0x1D1D46);

    pet_decor  = pet_image(scr, &pet_img_plant, 4, PET_FLOOR - (int32_t)pet_img_plant.header.h, false);
    pet_rack   = pet_image(scr, &pet_img_rack0, 206, PET_FLOOR - (int32_t)pet_img_rack0.header.h, true);
    pet_laptop = pet_image(scr, &pet_img_laptop0, 157, PET_FLOOR - (int32_t)pet_img_laptop0.header.h, true);
    pet_pet    = pet_image(scr, &pet_img_duck0, 38, 0, true);

    /* Draw order = creation order: what's behind him first */
    pet_slot[SLOT_AURA]  = pet_image(scr, &pet_img_aura, 0, 0, true);
    pet_slot[SLOT_BACK]  = pet_image(scr, &pet_img_cloak, 0, 0, true);
    pet_body             = pet_image(scr, &pet_img_idle, 0, 0, false);
    pet_slot[SLOT_SHIRT] = pet_image(scr, &pet_img_hoodie, 0, 0, true);
    pet_slot[SLOT_NECK]  = pet_image(scr, &pet_img_scarf, 0, 0, true);
    pet_slot[SLOT_FACE]  = pet_image(scr, &pet_img_glasses, 0, 0, true);
    pet_slot[SLOT_EARS]  = pet_image(scr, &pet_img_headphones, 0, 0, true);
    pet_slot[SLOT_HEAD]  = pet_image(scr, &pet_img_cap, 0, 0, true);
    pet_slot[SLOT_HAND]  = pet_image(scr, &pet_img_mug, 0, 0, true);
    pet_umbrella = pet_image(scr, &pet_img_umbrella, 0, 0, true);
    pet_sweat    = pet_image(scr, &pet_img_sweat, 0, 0, true);
    pet_cloud    = pet_image(scr, &pet_img_cloud0, 0, 0, true);

    /* Night: darken the room, but keep particles and toasts on top */
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

    pet_toast = lv_label_create(scr);
    lv_label_set_text(pet_toast, "");
    lv_obj_set_style_text_font(pet_toast, CM_FONT_CYR_16, 0);
    lv_obj_set_style_text_color(pet_toast, CM_YELLOW, 0);
    lv_obj_set_style_bg_color(pet_toast, lv_color_hex(0x0A0A20), 0);
    lv_obj_set_style_bg_opa(pet_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pet_toast, 9, 0);
    lv_obj_set_style_pad_hor(pet_toast, 8, 0);
    lv_obj_set_style_pad_ver(pet_toast, 2, 0);
    lv_obj_add_flag(pet_toast, LV_OBJ_FLAG_HIDDEN);

    /* One fixed tap target over the pet: frames change size, the finger doesn't */
    pet_hit = lv_obj_create(scr);
    lv_obj_set_pos(pet_hit, 60, PET_ROOM_TOP + 40);
    lv_obj_set_size(pet_hit, 120, PET_FLOOR - PET_ROOM_TOP - 40);
    lv_obj_set_style_bg_opa(pet_hit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pet_hit, 0, 0);
    lv_obj_remove_flag(pet_hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(pet_hit, pet_hit_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(pet_hit, pet_hit_cb, LV_EVENT_LONG_PRESSED, NULL);

    /* Status row: level, XP inside the level, achievements */
    pet_rect(scr, 0, 270, 240, 1, 0x18183A);
    pet_lv = lv_label_create(scr);
    lv_obj_set_style_text_font(pet_lv, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pet_lv, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(pet_lv, 8, 275);
    pet_xpbar = lv_bar_create(scr);
    lv_obj_set_size(pet_xpbar, 76, 8);
    lv_obj_set_pos(pet_xpbar, 50, 280);
    lv_obj_set_style_bg_color(pet_xpbar, CM_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pet_xpbar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(pet_xpbar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pet_xpbar, CM_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(pet_xpbar, 4, LV_PART_INDICATOR);
    pet_xplbl = lv_label_create(scr);
    lv_obj_set_style_text_font(pet_xplbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pet_xplbl, CM_TEXT_SEC, 0);
    lv_obj_set_pos(pet_xplbl, 132, 277);
    pet_image(scr, &pet_img_star, 194, 277, false);
    pet_awards = lv_label_create(scr);
    lv_obj_set_style_text_font(pet_awards, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pet_awards, CM_TEXT_PRIM, 0);
    lv_obj_set_pos(pet_awards, 210, 275);

    pet_update_status();
    pet_update_medals();
    pet_render(lv_tick_get());
    lv_timer_create(pet_tick_cb, 50, NULL);
}

#endif /* CLAUDE_PET_H */
