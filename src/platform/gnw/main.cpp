/*
 * OpenLara — Retro-Go SD GWHB homebrew entry (Cortex-M7 / STM32H7B0).
 *
 * Uses OpenLara's fixed-point engine + GBA MODE4 software rasterizer (C path).
 * Levels are PKD sidecars on the SD card (see README).
 */

#include "gnw_internal.h"

extern "C" {
#include <string.h>
#include <stdio.h>
#include "gw_lcd.h"
#include "gw_audio.h"
#include "rom_manager.h"
#include "odroid_system.h"
#include "odroid_overlay.h"
#include "odroid_settings.h"
#include "gw_malloc.h"
/* porting/common.h comes in via the ABI header — before OpenLara headers. */
#include "gnw_bridge.h"
}

/* OpenLara fixed engine (ol/ → third_party/.../fixed; avoids #include "common.h" clash). */
#include "ol/game.h"

#define APP_ID       14 /* APPID_HOMEBREW */
#define FPS          30
/* Closest firmware rate to GBA DirectSound (10512); must match SND_OUTPUT_FREQ. */
#define SAMPLE_RATE  11025
#define AUDIO_LENGTH 368

extern void gnw_os_tick(uint32_t delta_ms);
bool gnw_pkd_exists(const char *name);

static odroid_gamepad_state_t s_pad;
static int32 s_fps_counter;
static int32 s_fps_window_start;

static bool LoadState(const char *savePathName)
{
    (void)savePathName;
    return false;
}

static bool SaveState(const char *savePathName)
{
    (void)savePathName;
    return false;
}

static void *Screenshot(void)
{
    lcd_wait_for_vblank();
    if (gnw_game_ready())
        gnw_present();
    return lcd_get_active_buffer();
}

static void Shutdown(void) {}

static void SleepWake(void)
{
    odroid_audio_init(SAMPLE_RATE);
    audio_clear_buffers();
    audio_start_playing(AUDIO_LENGTH);
}

static void SramSave(void) {}

static void blit_fallback(void)
{
    pixel_t *fb_lcd = (pixel_t *)lcd_get_active_buffer();
    int y = 40;

    memset(fb_lcd, 0, GW_LCD_WIDTH * GW_LCD_HEIGHT * sizeof(pixel_t));
    odroid_overlay_draw_text(16, y, 0, "OpenLara", 0xFFFF, 0x0000);
    y += 20;
    odroid_overlay_draw_text(16, y, 0, "Put PKD levels in:", 0xFFFF, 0x0000);
    y += 16;
#ifdef HOST_BUILD
    odroid_overlay_draw_text(16, y, 0, "./data/ or OPENLARA_DATA", 0xFFE0, 0x0000);
#else
    odroid_overlay_draw_text(16, y, 0, "/homebrews/openlara/", 0xFFE0, 0x0000);
#endif
    y += 16;
    odroid_overlay_draw_text(16, y, 0, "TITLE.PKD  LEVEL1.PKD ...", 0xFFFF, 0x0000);
    y += 24;
    odroid_overlay_draw_text(16, y, 0, "(from OpenLara GBA data/)", 0xC618, 0x0000);
    common_ingame_overlay();
}

static void blit(void)
{
    if (gnw_game_ready())
        gnw_present();
    else
        blit_fallback();
}

static bool try_boot_game(void)
{
    /* Probe with fopen only — never pre-cache into flash/RAM before gameInit.
     * startLevel() → osLoadLevel() maps the PKD once via flash cache. */
    if (!gnw_pkd_exists("TITLE")) {
        printf("openlara: no TITLE.PKD — fallback UI\n");
        return false;
    }

    (void)gnw_load_tracks(); /* STREAM TRACKS.AD4; optional — SFX still work */

    gLevelID = LVL_TR1_TITLE;
    gameInit();
    return true;
}

extern "C" void app_main(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    odroid_gamepad_state_t joystick;
    odroid_dialog_choice_t options[1];
    int32 last_frame = 0;
    int32 frame_acc = 0;

    gw_core_bridge_init();
    ram_start = (uint32_t)(uintptr_t)&__CORE_BSS_END__;
    gnw_set_game_ready(false);
    memset(&s_pad, 0, sizeof(s_pad));
    s_fps_counter = 0;
    s_fps_window_start = 0;
    fps = 0;

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }
    common_emu_state.frame_time_10us = (uint16_t)(100000 / FPS + 0.5f);
    lcd_set_refresh_rate(FPS);

    odroid_system_init(APP_ID, SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot,
                           &Shutdown, &SleepWake, &SramSave, NULL);

    options[0] = (odroid_dialog_choice_t)ODROID_DIALOG_CHOICE_LAST;
    audio_start_playing(AUDIO_LENGTH);

    if (try_boot_game()) {
        gnw_set_game_ready(true);
        printf("openlara: gameInit OK\n");
    }

    if (load_state)
        odroid_system_emu_load_state(save_slot);
    else
        lcd_clear_buffers();

    while (1) {
        wdog_refresh();

        bool draw_frame = common_emu_frame_loop();

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &blit);
        common_emu_input_loop_handle_turbo(&joystick);
        s_pad = joystick;

        if (gnw_game_ready()) {
            gnw_input_update();
            frame_acc++;
            gnw_os_tick(1000u / (uint32_t)FPS);
            int32 delta = 1;
            if (frame_acc - last_frame > 0) {
                delta = frame_acc - last_frame;
                last_frame = frame_acc;
            }
            gameUpdate(delta);
            if (draw_frame)
                gameRender();

            /*
             * FPS over the last FPS loop ticks. The main loop is paced by
             * audio sync (~FPS Hz), so this is draws per wall-clock second
             * without depending on HAL_GetTick (which fired the 1s window
             * immediately on boot and reported 0/1).
             */
            if (draw_frame)
                s_fps_counter++;
            if (frame_acc - s_fps_window_start >= FPS) {
                fps = s_fps_counter;
                s_fps_counter = 0;
                s_fps_window_start = frame_acc;
            }
        }

        if (draw_frame) {
            blit();
            lcd_swap();
        }

        if (gnw_game_ready())
            gnw_submit_audio();
        else {
            int16_t *buf = audio_get_active_buffer();
            uint16_t len = audio_get_buffer_length();
            if (buf && len && !common_emu_sound_loop_is_muted())
                memset(buf, 0, len * sizeof(int16_t));
        }
        common_emu_sound_sync(false);
    }
}
