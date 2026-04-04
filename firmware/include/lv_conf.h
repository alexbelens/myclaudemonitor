/**
 * lv_conf.h - LVGL configuration for ESP32-2432S028 (CYD)
 *
 * Tuned for ESP32 with ILI9341 320x240 display:
 * - 16-bit color (RGB565) to match hardware and save RAM
 * - 48KB LVGL heap (ESP32 has ~300KB free SRAM)
 * - Only Montserrat 10 + 12 fonts enabled (used by UI)
 * - Unused widgets disabled to save flash
 * - TFT_eSPI display driver enabled, SDL disabled
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ============================================================
 * COLOR SETTINGS
 * ============================================================ */
#define LV_COLOR_DEPTH 16              /* RGB565 for ILI9341 */

/* ============================================================
 * MEMORY SETTINGS
 * ============================================================ */
#define LV_MEM_CUSTOM 0                /* Use LVGL's built-in allocator */
#define LV_MEM_SIZE (48U * 1024U)      /* 48KB LVGL heap */
#define LV_MEM_ADR 0                   /* Use default location */
#define LV_MEM_BUF_MAX_NUM 16

/* ============================================================
 * DISPLAY SETTINGS
 * ============================================================ */
#define LV_DPI_DEF 130
#define LV_DEF_REFR_PERIOD 33          /* ~30 FPS */
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN 4

/* ============================================================
 * DRAW ENGINE
 * ============================================================ */
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_COMPLEX 1           /* Needed for rounded corners on bars */
#define LV_DRAW_SW_SHADOW_CACHE_SIZE 0
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS 0
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE  /* No NEON/Helium on ESP32 */
#define LV_USE_DRAW_ARM2D 0
#define LV_USE_DRAW_DAVE2D 0
#define LV_USE_DRAW_VG_LITE 0
#define LV_USE_DRAW_PXP 0
#define LV_USE_DRAW_OPENGLES 0
#define LV_USE_DRAW_VDMA 0

/* Layer buffer */
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE (8 * 1024)

/* ============================================================
 * LOGGING
 * ============================================================ */
#define LV_USE_LOG 0                   /* Disable logging to save RAM */

/* ============================================================
 * ASSERTS (disable for production)
 * ============================================================ */
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

/* ============================================================
 * FONTS
 * Only enable what claude_monitor_ui.h actually uses
 * ============================================================ */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 1        /* Section titles, detail text, footer */
#define LV_FONT_MONTSERRAT_12 1        /* Header, percentages, stats */
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

/* Special fonts */
#define LV_FONT_MONTSERRAT_12_SUBPX 0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK 0
#define LV_FONT_UNSCII_8 0
#define LV_FONT_UNSCII_16 0

#define LV_FONT_DEFAULT &lv_font_montserrat_12

/* Font cache */
#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_COMPRESSED 0
#define LV_USE_FONT_SUBPX 0
#define LV_USE_FONT_PLACEHOLDER 1

/* ============================================================
 * WIDGETS - Only enable what the dashboard uses
 * ============================================================ */
/* Core widgets (used by claude_monitor_ui.h) */
#define LV_USE_LABEL 1                 /* All text */
#define LV_USE_BAR 1                   /* Progress bars */
#define LV_USE_OBJ 1                   /* Containers, dividers, status LED */
#define LV_USE_LINE 0
#define LV_USE_IMAGE 0
#define LV_USE_IMG 0

/* Extra widgets - all disabled to save flash */
#define LV_USE_ARC 0
#define LV_USE_ANIMIMG 0
#define LV_USE_BTN 1                   /* Window switcher buttons */
#define LV_USE_BTNMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMGBTN 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

/* ============================================================
 * THEMES
 * ============================================================ */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1        /* Dark theme matches our UI */
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

/* ============================================================
 * ANIMATIONS
 * ============================================================ */
#define LV_USE_ANIM 1                  /* Needed for bar animations */
#define LV_USE_ANIM_TIMELINE 0

/* ============================================================
 * TEXT
 * ============================================================ */
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_)]}"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_COLOR_CMD "#"

/* ============================================================
 * DISPLAY DRIVERS
 * ============================================================ */
#define LV_USE_SDL 0                   /* No SDL on ESP32 */
#define LV_USE_TFT_ESPI 0             /* We manage TFT_eSPI manually in main.cpp */

/* ============================================================
 * OTHER
 * ============================================================ */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

#define LV_USE_OBJ_ID 0
#define LV_USE_OBJ_ID_BUILTIN 0
#define LV_USE_OBJ_PROPERTY 0
#define LV_USE_SNAPSHOT 0
#define LV_USE_SYSMON 0
#define LV_USE_PROFILER 0
#define LV_USE_MONKEY 0
#define LV_USE_GRIDNAV 0
#define LV_USE_FRAGMENT 0
#define LV_USE_OBSERVER 0
#define LV_USE_IME_PINYIN 0
#define LV_USE_FILE_EXPLORER 0

#define LV_BUILD_EXAMPLES 0

#endif /* LV_CONF_H */
