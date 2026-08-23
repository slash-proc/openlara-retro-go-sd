/*
 * OpenLara cutscene playback via the file-manager MJPEG AVI player.
 *
 * Game audio runs at 11025 Hz (OpenLara mixer → DMA halves). The video player
 * needs 48 kHz + pcm_attach. Switch around video_play(), then restore cleanly.
 */
#include "gnw_internal.h"

extern "C" {
#include <stdio.h>
#include <string.h>
#include "gw_audio.h"
#include "gw_malloc.h"
#include "odroid_system.h"
#include "gw_core_bridge.h"
#include "video_play.h"
#include "video_audio.h"

extern bool gnw_fmv_batch_audio;
}

#include "ol/common.h"

#define GAME_SAMPLE_RATE  11025
#define GAME_AUDIO_LENGTH 368

struct fmv_path_ctx {
    char path[160];
};

static bool store_path_cb(const char *path, void *vctx)
{
    fmv_path_ctx *ctx = (fmv_path_ctx *)vctx;
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fclose(f);
    snprintf(ctx->path, sizeof(ctx->path), "%s", path);
    return true;
}

/* Matches TR::getGameVideo() for TR1 (fixed-engine LevelID). */
static const char *fmv_clip_for_level(LevelID id)
{
    switch (id) {
    case LVL_TR1_GYM:    return "MANSION";
    case LVL_TR1_1:      return "SNOW";
    case LVL_TR1_4:      return "LIFT";
    case LVL_TR1_8A:     return "VISION";
    case LVL_TR1_10A:    return "CANYON";
    case LVL_TR1_10B:    return "PYRAMID";
    case LVL_TR1_CUT_4:  return "PRISON";
    case LVL_TR1_EGYPT:  return "END";
    default:             return NULL;
    }
}

static LevelID s_fmv_prev_level = LVL_MAX;

static void fmv_audio_begin(void)
{
    gnw_audio_silence_for_load();
    audio_stop_playing();
    odroid_audio_init(AUDIO_SAMPLE_RATE);
}

static void fmv_audio_end(void)
{
    pcm_audio_set(0, 0);
    pcm_audio_enable(0);
    video_audio_detach();
    audio_stop_playing();
    odroid_audio_init(GAME_SAMPLE_RATE);
    audio_clear_buffers();
    audio_start_playing(GAME_AUDIO_LENGTH);
    /* Re-align DMA half-buffer pacing with the main loop after a rate change. */
    common_emu_sound_sync(false);
    common_emu_sound_sync(false);
}

static bool fmv_resolve_path(const char *clip_name, char *out, size_t out_sz)
{
    fmv_path_ctx ctx;

    if (!clip_name || !clip_name[0])
        return false;

    ctx.path[0] = '\0';
    if (!gnw_foreach_asset_path(clip_name, "AVI", store_path_cb, &ctx) &&
        !gnw_foreach_asset_path(clip_name, "avi", store_path_cb, &ctx)) {
        printf("openlara: FMV %s.AVI not found (tried /homebrews/openlara[/fmv]/)\n",
               clip_name);
        return false;
    }

    snprintf(out, out_sz, "%s", ctx.path);
    return true;
}

static vid_result_t fmv_play_resolved(const char *path)
{
    vid_result_t r;

    printf("openlara: FMV %s (ram free %lu)\n", path, (unsigned long)ram_get_free_size());
    r = video_play(path);
    if (r == VID_UNPLAYABLE)
        printf("openlara: FMV unplayable: %s\n", video_last_diag());
    else
        printf("openlara: FMV done (%d)\n", (int)r);
    return r;
}

extern "C" void gnw_play_fmv(const char *clip_name)
{
    char path[160];

    if (!fmv_resolve_path(clip_name, path, sizeof(path)))
        return;

    fmv_audio_begin();
    fmv_play_resolved(path);
    fmv_audio_end();
}

static void fmv_title_boot(void)
{
    char core[160], cafe[160];
    bool have_core = fmv_resolve_path("CORE", core, sizeof(core));
    bool have_cafe = fmv_resolve_path("CAFE", cafe, sizeof(cafe));

    if (!have_core && !have_cafe)
        return;

    fmv_audio_begin();
    gnw_fmv_batch_audio = true;
    if (have_core)
        fmv_play_resolved(core);
    if (have_cafe)
        fmv_play_resolved(cafe);
    gnw_fmv_batch_audio = false;
    fmv_audio_end();
}

extern "C" void gnw_play_fmv_for_level(int level_id)
{
    const char *clip;
    LevelID id, prev;

    if (level_id < 0 || level_id >= (int)LVL_MAX)
        return;

    id = (LevelID)level_id;
    prev = s_fmv_prev_level;
    s_fmv_prev_level = id;

    if (id == LVL_TR1_TITLE) {
        if (prev == LVL_MAX)
            fmv_title_boot();
        return;
    }

    clip = fmv_clip_for_level(id);
    if (clip)
        gnw_play_fmv(clip);
}
