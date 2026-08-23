#include "gnw_internal.h"

extern "C" {
#include <stdio.h>
#include <string.h>
#ifdef HOST_BUILD
#include <stdlib.h>
#endif
#include "gw_malloc.h"
#include "rom_manager.h"
#include "odroid_system.h"
#include "odroid_overlay.h"
#ifdef HOST_BUILD
#include "host_compat.h"
#else
#include "gw_core_bridge.h"
#include "video_scratch.h"
#endif
}

#include "ol/common.h"

static bool s_game_ready;
static uint8_t *s_level_blob;
static uint32_t s_level_blob_size;
static LevelID s_level_blob_id = LVL_MAX;
static uint8_t *s_title_scr;
static uint32_t s_title_scr_size;
static uint16_t s_palette_rgb555[256];
static uint32_t s_tick_ms;

extern "C" bool gnw_game_ready(void)
{
    return s_game_ready;
}

extern "C" void gnw_set_game_ready(bool ready)
{
    s_game_ready = ready;
}

int32 fps;

void osSetPalette(const uint16 *palette)
{
    memcpy(s_palette_rgb555, palette, sizeof(s_palette_rgb555));
    gnw_set_palette_rgb555(s_palette_rgb555);
}

int32 osGetSystemTimeMS()
{
    return (int32)s_tick_ms;
}

void gnw_os_tick(uint32_t delta_ms)
{
    s_tick_ms += delta_ms;
}

bool osSaveSettings()
{
    return false;
}

bool osLoadSettings()
{
    return false;
}

bool osCheckSave()
{
    return false;
}

bool osSaveGame()
{
    return false;
}

bool osLoadGame()
{
    return false;
}

void osJoyVibrate(int32 index, int32 L, int32 R)
{
    (void)index;
    (void)L;
    (void)R;
}

static void forget_level_blob(void)
{
#ifdef HOST_BUILD
    if (s_level_blob)
        free(s_level_blob);
#endif
    /* Flash cache owns the PKD bytes — drop our pointer only. */
    s_level_blob = NULL;
    s_level_blob_size = 0;
    s_level_blob_id = LVL_MAX;
}

#ifdef HOST_BUILD
extern "C" const char *host_get_data_dir(void);

static const char *const k_host_asset_dirs[] = {
    ".",
    "./data",
    "./openlara",
    "./openlara/data",
    NULL
};
#endif

static const char *const k_asset_dirs[] = {
    "/homebrews/openlara",
    "/homebrews/openlara/fmv",
    "/homebrews/openlara/data",
    "/roms/homebrew/openlara",
    "/roms/homebrew/openlara/fmv",
    "/roms/homebrew/openlara/data",
    NULL
};

static bool path_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

extern "C" bool gnw_foreach_asset_path(const char *name, const char *ext,
                                       bool (*fn)(const char *path, void *ctx),
                                       void *ctx)
{
    char path[160];
    int i;
    const char *slash;
    size_t dir_len;

#ifdef HOST_BUILD
    const char *root = host_get_data_dir();

    if (root && root[0]) {
        snprintf(path, sizeof(path), "%s/%s.%s", root, name, ext);
        if (fn(path, ctx))
            return true;
        snprintf(path, sizeof(path), "%s/data/%s.%s", root, name, ext);
        if (fn(path, ctx))
            return true;
    }
    for (i = 0; k_host_asset_dirs[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s.%s", k_host_asset_dirs[i], name, ext);
        if (fn(path, ctx))
            return true;
    }
#endif

    for (i = 0; k_asset_dirs[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s.%s", k_asset_dirs[i], name, ext);
        if (fn(path, ctx))
            return true;
    }

    if (ACTIVE_FILE && ACTIVE_FILE->path[0]) {
        slash = strrchr(ACTIVE_FILE->path, '/');
        dir_len = slash ? (size_t)(slash - ACTIVE_FILE->path) : 0;
        if (dir_len > 0 && dir_len < sizeof(path) - 40) {
            memcpy(path, ACTIVE_FILE->path, dir_len);
            snprintf(path + dir_len, sizeof(path) - dir_len, "/%s.%s", name, ext);
            if (fn(path, ctx))
                return true;
            snprintf(path + dir_len, sizeof(path) - dir_len, "/openlara/%s.%s", name, ext);
            if (fn(path, ctx))
                return true;
            snprintf(path + dir_len, sizeof(path) - dir_len, "/data/%s.%s", name, ext);
            if (fn(path, ctx))
                return true;
        }
    }
    return false;
}

static bool probe_path_cb(const char *path, void *ctx)
{
    (void)ctx;
    return path_exists(path);
}

bool gnw_pkd_exists(const char *name)
{
    return gnw_foreach_asset_path(name, "PKD", probe_path_cb, NULL);
}

struct flash_load_ctx {
    uint8_t *buf;
    uint32_t size;
};

static bool flash_load_cb(const char *path, void *vctx)
{
    flash_load_ctx *ctx = (flash_load_ctx *)vctx;
    uint32_t size = 0;
    uint8_t *buf;

    wdog_refresh();
    buf = odroid_overlay_cache_file_in_flash(path, &size, false);
    wdog_refresh();
    if (!buf || size == 0)
        return false;

    printf("openlara: %s → flash @ %p (%lu)\n", path, (void *)buf, (unsigned long)size);
    ctx->buf = buf;
    ctx->size = size;
    return true;
}

/*
 * TITLE.SCR is only 38 KiB (MODE4 framebuffer). Copy into RAM_EMU once so the
 * single flash-cache slot can hold the multi-hundred-KiB PKD afterwards.
 */
static bool title_scr_load_cb(const char *path, void *vctx)
{
    flash_load_ctx tmp;
    uint8_t *ram;
    (void)vctx;

    if (s_title_scr)
        return true;

    if (!flash_load_cb(path, &tmp))
        return false;
    if (tmp.size == 0 || tmp.size > (uint32_t)(FRAME_WIDTH * FRAME_HEIGHT))
        return false;

    ram = (uint8_t *)ram_malloc(tmp.size);
    if (!ram) {
#ifndef HOST_BUILD
        /* Boot FMV keeps ~80 KiB scratch via ram_malloc (no free). TITLE.SCR
         * is 38 KiB — reuse that block once FMV is done. */
        if (g_scratch && tmp.size <= VIDEO_SCRATCH_SIZE) {
            ram = g_scratch;
            printf("openlara: TITLE.SCR → reuse video scratch\n");
        } else
#endif
        {
            printf("openlara: TITLE.SCR ram_malloc(%lu) failed\n", (unsigned long)tmp.size);
            return false;
        }
    }
    memcpy(ram, tmp.buf, tmp.size);
    s_title_scr = ram;
    s_title_scr_size = tmp.size;
    printf("openlara: TITLE.SCR → RAM copy (%lu bytes)\n", (unsigned long)tmp.size);
    return true;
}

static bool load_named_pkd(const char *name, uint8_t **out, uint32_t *out_size)
{
    flash_load_ctx ctx = { NULL, 0 };

    /* DMA keeps playing the last filled halves during the flash write. */
    gnw_audio_silence_for_load();

    if (!gnw_foreach_asset_path(name, "PKD", flash_load_cb, &ctx)) {
        printf("openlara: missing %s.PKD (flash; tried /homebrews/openlara/)\n", name);
        return false;
    }
    *out = ctx.buf;
    *out_size = ctx.size;
    return true;
}

const void *osLoadScreen(LevelID id)
{
    (void)id;

    if (!s_title_scr) {
        if (!gnw_foreach_asset_path("TITLE", "SCR", title_scr_load_cb, NULL)) {
            printf("openlara: missing TITLE.SCR — title bg will be noise\n");
            return NULL;
        }
    }
    (void)s_title_scr_size;
    return s_title_scr;
}

const void *osLoadLevel(LevelID id)
{
    const char *name;
    uint8_t *buf = NULL;
    uint32_t size = 0;

    if (id < 0 || id >= LVL_MAX)
        return NULL;

    if (s_level_blob && s_level_blob_id == id)
        return s_level_blob;

    name = (const char *)gLevelInfo[id].data;
    if (!name || !name[0])
        return NULL;

    /*
     * FMV before TITLE.SCR / PKD. Scratch is ~80 KiB (JPEG + YCbCr strip);
     * TITLE.SCR (~38 KiB) reuses that block if ram_malloc is tight.
     */
    gnw_play_fmv_for_level((int)id);

    /* Copy title screen to RAM before flash cache is reused for the PKD. */
    if (!s_title_scr)
        (void)osLoadScreen(LVL_TR1_TITLE);

    forget_level_blob();
    if (!load_named_pkd(name, &buf, &size))
        return NULL;

    s_level_blob = buf;
    s_level_blob_size = size;
    s_level_blob_id = id;
    (void)s_level_blob_size;
    return s_level_blob;
}
