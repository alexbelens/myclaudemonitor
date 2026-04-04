/**
 * main.c — Claude Monitor CYD
 *
 * Drop-in replacement for lv_port_pc_vscode's main.c.
 * Launches the Claude Monitor dashboard at 320×240.
 */

#include "lvgl.h"
#include "claude_monitor_ui.h"

/* SDL display driver setup (provided by lv_port_pc_vscode) */
#if LV_USE_SDL
#include "lv_sdl_window.h"
#include "lv_sdl_mouse.h"
#endif

/* Resolution — must match CYD hardware */
#ifndef SDL_HOR_RES
#define SDL_HOR_RES 320
#endif
#ifndef SDL_VER_RES
#define SDL_VER_RES 240
#endif

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* Initialize LVGL */
    lv_init();

    /* Create the SDL display + input (the simulator handles this) */
    lv_display_t * disp = lv_sdl_window_create(SDL_HOR_RES, SDL_VER_RES);
    (void)disp;

    lv_indev_t * mouse = lv_sdl_mouse_create();
    (void)mouse;

    /* Build our Claude Monitor UI */
    claude_monitor_create_ui();

    printf("Claude Monitor CYD simulator running at %dx%d\n", SDL_HOR_RES, SDL_VER_RES);
    printf("Reading data from: %s\n", DATA_FILE);
    printf("Press Ctrl+C to quit.\n");

    /* Main loop */
    while (1) {
        uint32_t time_till_next = lv_timer_handler();
        lv_delay_ms(time_till_next);
    }

    return 0;
}
