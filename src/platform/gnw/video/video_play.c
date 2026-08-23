// Video playback loop — see video_play.h.
//
// OpenLara cutscene mode: demux AVI, HW-decode MJPEG to LCD, feed MP3 to SAI.
// Controls: A = skip; PAUSE/SET = Retro-Go menu (release) and PAUSE+D-pad
// volume/brightness macros (same path as the main game loop).
// No transport OSD, scrub, speed, or video-specific options.

#include "video_play.h"
#include "video_resume.h"
#include "avi.h"
#include "video_decode.h"
#include "video_audio.h"
#include "video_scratch.h"
#include "hw_jpeg_decoder.h"
#include "gw_lcd.h"
#include "gw_audio.h"
#include "main.h"
#include "odroid_system.h"
#include "odroid_overlay.h"
#include <string.h>
#include <stdio.h>
#include "gw_core_bridge.h"

void sd_io_set_poll(void (*fn)(void));

bool gnw_fmv_batch_audio = false;

extern int g_vdec_read_ms;
extern int g_vdec_pf_ms;
extern int g_vdec_st;

static void apply_audio(bool live)
{
    pcm_audio_set(live ? common_emu_sound_get_volume() : 0, live ? 1 : 0);
}


// Pump one AVI audio chunk (MP3) into the decode->ring path.
static void feed_audio(avi_t *a, long sz)
{
    uint8_t buf[2048];
    while (sz > 0) {
        wdog_refresh();
        size_t want = sz > (long)sizeof buf ? sizeof buf : (size_t)sz;
        size_t got = avi_read(a, buf, want);   /* self-heals a slept-out handle */
        if (got == 0) break;
        video_audio_feed(buf, (int)got);
        sz -= (long)got;
    }
}

// ---- Frame prefetch (jitter buffer) ----------------------------------------
// MJPEG frame sizes are bursty: a busy scene's frame can take several times the
// average SD read time and blow the 1/fps budget on its own. The pacing wait of
// the EASY frames is idle time — so the demuxer runs ahead during it, reading
// upcoming video frames into free slots (audio chunks met on the way are fed
// immediately). A burst frame is then already in RAM when its turn comes.
// Reads during the wait go in small steps so the swap never overshoots by more
// than ~one step's worth of milliseconds.
#define PF_DEPTH          1           /* 1 slot: decode→FB then reuse for prefetch */
#define PF_STEP           (4 * 1024)  // bytes per wait-loop / force tick (pump between)
/* Free samples required before demuxing more audio. Lower = keep the ring
 * fuller so a JPEG/SD stall cannot drain it to zero. Gate =
 * VR_SIZE-1-HEADROOM = 5991 with HEADROOM 2200; VR_TARGET 5000 sits under that. */
#define PF_AUDIO_HEADROOM 2200
#define PF_PREROLL        5000        // ~104 ms at 48 kHz; matches VR_TARGET

/* Frames the clip contains that do not fit a slot -- silently undrawable, and
 * indistinguishable from SD or decode judder on screen. `big=` counts them and
 * `max=` is the largest frame seen, so a clip running close to the ceiling shows
 * up BEFORE it starts crossing it. Reset per clip. */
static long g_vid_toobig = 0, g_vid_szmax = 0;

typedef struct { long sz; int slot; } pf_ent_t;   // slot < 0: unreadable/oversized frame

static pf_ent_t pf_q[PF_DEPTH];
static int      pf_n;
static uint8_t  pf_busy;                          // slot-in-use bitmask
static int      pf_ip_slot;                       // in-progress video read...
static long     pf_ip_want = -1, pf_ip_got;       // ...(-1 = none)
static bool     pf_src_end;

static void pf_reset(void)
{
    pf_n = 0;
    pf_busy = 0;
    pf_ip_want = -1;
    pf_src_end = false;
}

static int pf_slot_alloc(void)
{
    for (int s = 0; s < VIDEO_SLOTS; s++)
        if (!(pf_busy & (1 << s))) { pf_busy |= (1 << s); return s; }
    return -1;
}

static void pf_enqueue(long sz, int slot)
{
    pf_q[pf_n].sz = sz;
    pf_q[pf_n].slot = slot;
    pf_n++;
}

// Advance the prefetcher a little. force=false (pacing wait): bounded work per
// call, honours the audio-ring gate. force=true (consumer starving): big reads,
// no gate — this is exactly the old synchronous behaviour.
// Returns false when there is nothing (more) to do right now.
static bool pf_step(avi_t *a, int spd, bool paused, int *na_seen, bool force)
{
    if (pf_ip_want >= 0) {                       // continue the in-progress frame
        long left = pf_ip_want - pf_ip_got;
        /* Always step: a single 40KiB avi_read blocks FatFs for the whole
         * transfer. Chunking lets sd_io_poll (video_audio_pump) run between
         * sectors and keeps the watchdog fed. New AVI audio still cannot
         * appear mid-chunk — depth of the PCM ring covers that stall. */
        long take = (left > PF_STEP) ? PF_STEP : left;
        uint32_t t0 = HAL_GetTick();
        size_t got = avi_read(a, video_slot(pf_ip_slot) + pf_ip_got, (size_t)take);
        video_audio_pump();
        // Attribute the read time honestly: a forced read (consumer starving) is
        // what the frame actually waited on -> rd=; a wait-time read is overlapped
        // with pacing and hidden from the frame budget -> pf=.
        int dt = (int)(HAL_GetTick() - t0);
        if (force) g_vdec_read_ms += dt; else g_vdec_pf_ms += dt;
        pf_ip_got += (long)got;
        if (got < (size_t)take) {                // short read even after self-heal
            pf_busy &= ~(1 << pf_ip_slot);
            pf_enqueue(-1, -1);                  // surfaces as a decode failure
            pf_ip_want = -1;
            return true;
        }
        if (pf_ip_got >= pf_ip_want) {
            pf_enqueue(pf_ip_want, pf_ip_slot);
            pf_ip_want = -1;
        }
        return true;
    }

    if (pf_src_end || pf_n >= PF_DEPTH)
        return false;
    /* Feeding audio early presses on the PCM ring; only run ahead when it has
     * room for another chunk (the forced path keeps today's behaviour). */
    if (!force && spd == 1 && !paused && video_audio_ring_free() < PF_AUDIO_HEADROOM)
        return false;

    long sz;
    avi_kind_t k = avi_next(a, &sz);
    if (k == AVI_END) { pf_src_end = true; return false; }
    if (k == AVI_AUDIO) {
        (*na_seen)++;
        if (spd == 1 && !paused) feed_audio(a, sz);
        return true;
    }
    // video frame: unreadable sizes pass through as failure markers (the
    // demuxer skips the unread payload by itself on the next avi_next)
    /* A frame larger than a slot cannot be read at all, so it is enqueued as a
     * failure marker and never drawn. That is the right behaviour and it was
     * completely silent: at a high encoder quality a busy scene can cross the
     * slot size, and every one of those frames is a dropped frame that looks
     * exactly like judder from SD or decode. Count them, and remember the
     * biggest frame the clip actually contains, so the HUD can say which of the
     * three it is -- and so the slot size is chosen from a measurement instead
     * of from the number that happened to be there. */
    if (sz > g_vid_szmax) g_vid_szmax = sz;
    if (sz < 2 || sz > VIDEO_FRAME_MAX) {
        if (sz > VIDEO_FRAME_MAX) g_vid_toobig++;
        pf_enqueue(sz, -1);
        return true;
    }

    int slot = pf_slot_alloc();
    if (slot < 0) return false;                  // shouldn't happen with pf_n < depth
    pf_ip_slot = slot;
    pf_ip_want = sz;
    pf_ip_got = 0;
    return true;
}

/* lcd_swap() only takes effect at vblank, but lcd_sleep_while_swap_pending()
 * WFI-s without feeding the PCM ring — SAI keeps draining and the ISR inserts
 * zeros (crackles). Keep demuxing audio until the reload bit clears. */
static void pf_wait_swap(avi_t *a, int spd, bool paused, int *na_seen)
{
    while (lcd_is_swap_pending()) {
        wdog_refresh();
        if (!pf_step(a, spd, paused, na_seen, false)) {
            video_audio_pump();
            __WFI();
        }
    }
}

static void pf_preroll(avi_t *a, int spd, bool paused, int *na_seen)
{
    uint32_t t_lim = HAL_GetTick() + 400;
    while (video_audio_ring_count() < PF_PREROLL &&
           (int32_t)(HAL_GetTick() - t_lim) < 0) {
        wdog_refresh();
        if (!pf_step(a, spd, paused, na_seen, true))
            break;
    }
}

static avi_t *s_poll_a;
static int s_poll_spd;
static bool s_poll_paused;
static int *s_poll_na;

static void play_jpeg_poll(void)
{
    if (s_poll_a)
        pf_step(s_poll_a, s_poll_spd, s_poll_paused, s_poll_na, false);
    video_audio_pump();
}

// Blocking: hand out the next video frame in display order (prefetched or read
// now), feeding interleaved audio chunks along the way. false = end of stream.
static bool pf_fetch(avi_t *a, pf_ent_t *out, int spd, bool paused, int *na_seen)
{
    g_vdec_read_ms = 0;              // blocking read time for the frame we deliver
    for (;;) {
        wdog_refresh();
        if (pf_n > 0) {
            *out = pf_q[0];
            pf_n--;
            return true;
        }
        if (pf_src_end)
            return false;
        pf_step(a, spd, paused, na_seen, true);
    }
}


// --- playback ---------------------------------------------------------------

extern int  g_vdec_w, g_vdec_h;
extern long g_vdec_sz, g_vdec_rc;
extern unsigned char g_vdec_b0, g_vdec_b1;
extern uint32_t g_jpeg_hal, g_jpeg_err, g_jpeg_rej, g_jpeg_sub, g_jpeg_need;

static char s_diag[256];
const char *video_last_diag(void) { return s_diag[0] ? s_diag : "unsupported / unreadable"; }

static void build_diag(const avi_t *a, int nv, int na)
{
    int fps = a->usec_per_frame > 0 ? (1000000 + a->usec_per_frame / 2) / a->usec_per_frame : 0;
    snprintf(s_diag, sizeof s_diag,
             "open %dx%d %dfps f=%d|frame %02X%02X sz=%ld|decode st=%d %dx%d rc=%ld|"
             "hal=%lu err=%lx rej=%lu sub=%lu need=%lu|chunks v=%d a=%d",
             a->width, a->height, fps, a->total_frames,
             g_vdec_b0, g_vdec_b1, g_vdec_sz,
             g_vdec_st, g_vdec_w, g_vdec_h, g_vdec_rc,
             (unsigned long)g_jpeg_hal, (unsigned long)g_jpeg_err,
             (unsigned long)g_jpeg_rej, (unsigned long)g_jpeg_sub, (unsigned long)g_jpeg_need,
             nv, na);
}

static odroid_dialog_choice_t s_game_options[] = {
    ODROID_DIALOG_CHOICE_LAST
};

/* Firmware pause/menu may clear the active buffer; restore the last presented
 * frame (inactive after lcd_swap) so the dialog has something under it. */
static void video_repaint(void)
{
    pixel_t *active;
    pixel_t *inactive;

    wdog_refresh();
    video_audio_pump();
    active = (pixel_t *)lcd_get_active_buffer();
    inactive = (pixel_t *)lcd_get_inactive_buffer();
    if (active && inactive && active != inactive)
        memcpy(active, inactive, GW_LCD_FRAME_SIZE);
    common_ingame_overlay();
}

vid_result_t video_play(const char *path)
{
    avi_t a;
    if (!avi_open(&a, path, NULL, 0)) {
        snprintf(s_diag, sizeof s_diag, "avi_open FAILED");
        return VID_UNPLAYABLE;
    }
    if (!video_scratch_acquire()) {
        snprintf(s_diag, sizeof s_diag, "scratch alloc FAILED");
        avi_close(&a);
        return VID_UNPLAYABLE;
    }
    int nv_seen = 0, na_seen = 0;
    g_vid_toobig = 0;
    g_vid_szmax = 0;
    s_diag[0] = '\0';

    video_decode_init();

    const int resume_at = video_resume_get(path);
    if (resume_at > 0 && resume_at < a.total_frames)
        avi_seek_frame(&a, resume_at);

    odroid_gamepad_state_t joy, prev;
    odroid_input_read_gamepad(&prev);

    common_emu_state.skip_frames = 0;
    common_emu_state.pause_frames = 0;
    video_audio_start();
    apply_audio(true);

    pf_reset();
    s_poll_a = &a;
    s_poll_spd = 1;
    s_poll_paused = false;
    s_poll_na = &na_seen;
    video_jpeg_set_poll(play_jpeg_poll);
    sd_io_set_poll(video_audio_pump);
    pf_preroll(&a, 1, false, &na_seen);
    audio_start_playing(AUDIO_BUFFER_LENGTH);
    pcm_audio_enable(1);

    int  dec_ok = 0;
    bool decoded_any = false, stopped = false;
    bool anchored = false;
    uint32_t t0 = 0;
    int      frame_idx = 0;

    pf_ent_t ent;
    while (pf_fetch(&a, &ent, 1, false, &na_seen)) {
        wdog_refresh();
        odroid_input_read_gamepad(&joy);
        #define HIT(b) (joy.values[b] && !prev.values[b])

        /* A alone skips; VOLUME+A is the firmware save-state macro. */
        if (HIT(ODROID_INPUT_A) && !joy.values[ODROID_INPUT_VOLUME]) {
            stopped = true;
            prev = joy;
            break;
        }

        /*
         * Firmware UX while PAUSE/SET is held (volume/brightness macros) or on
         * release (pause menu). Still decode this frame — only skipping here
         * freezes FMV for the whole hold. Re-anchor after a release (menu may
         * have blocked); do not reset timing on every held poll.
         */
        if (joy.values[ODROID_INPUT_VOLUME] || prev.values[ODROID_INPUT_VOLUME]) {
            if (!lcd_is_swap_pending()) {
                bool pause_release = prev.values[ODROID_INPUT_VOLUME] &&
                                     !joy.values[ODROID_INPUT_VOLUME];
                apply_audio(false);
                common_emu_input_loop(&joy, s_game_options, &video_repaint);
                apply_audio(true);
                if (pause_release)
                    anchored = false;
                odroid_input_read_gamepad(&prev);
            }
        } else {
            prev = joy;
        }

        /* Firmware only runs overlay timeout inside common_emu_input_loop.
         * FMV skips that most frames — expire the quick-access HUD here. */
        if (common_emu_state.overlay != 0 &&
            get_elapsed_time_since(common_emu_state.last_overlay_time) > 1000u) {
            common_emu_state.overlay = 0; /* INGAME_OVERLAY_NONE */
        }

        nv_seen++;
        uint32_t fr_us = (uint32_t)a.usec_per_frame;
        if (!anchored) {
            t0 = HAL_GetTick();
            frame_idx = 0;
            anchored = true;
        }
        uint32_t due = t0 + (uint32_t)((uint64_t)frame_idx * fr_us / 1000ULL);

        if ((int32_t)(HAL_GetTick() - due) > (int32_t)(fr_us / 1000)) {
            frame_idx++;
            if (ent.slot >= 0)
                pf_busy &= ~(1 << ent.slot);
            continue;
        }

        s_poll_spd = 1;
        s_poll_paused = false;
        pf_wait_swap(&a, 1, false, &na_seen);

        /* RGB565 + HUD on the back buffer, then swap (classic emu path). */
        bool dec_ok_now = (ent.slot >= 0) &&
            video_decode_slot(video_slot(ent.slot), ent.sz,
                              lcd_get_active_buffer(), GW_LCD_WIDTH, GW_LCD_HEIGHT);
        if (ent.slot < 0)
            g_vdec_st = (ent.sz >= 2 && ent.sz <= VIDEO_FRAME_MAX) ? 2 : 1;
        else
            pf_busy &= ~(1 << ent.slot);

        if (dec_ok_now) {
            decoded_any = true;
            dec_ok++;
            common_ingame_overlay();
            lcd_swap();
        } else if (nv_seen >= 30 && !decoded_any) {
            build_diag(&a, nv_seen, na_seen);
            stopped = true;
            break;
        }

        g_vdec_pf_ms = 0;
        while ((int32_t)(HAL_GetTick() - due) < 0) {
            wdog_refresh();
            if (!pf_step(&a, 1, false, &na_seen, false)) {
                video_audio_pump();
                HAL_Delay(1);
            }
        }
        frame_idx++;
        (void)dec_ok;
    }

    pcm_audio_set(0, 0);
    video_jpeg_set_poll(NULL);
    sd_io_set_poll(NULL);
    s_poll_a = NULL;
    if (!gnw_fmv_batch_audio) {
        video_audio_detach();
        pcm_audio_enable(0);
        audio_stop_playing();
    } else {
        video_audio_stop();
    }
    video_decode_deinit();
    video_resume_put(path, a.cur_frame, a.total_frames);
    if (!decoded_any && !s_diag[0])
        build_diag(&a, nv_seen, na_seen);
    avi_close(&a);
    video_scratch_release();
    if (!decoded_any)
        return VID_UNPLAYABLE;
    return stopped ? VID_STOPPED : VID_OK;
}
