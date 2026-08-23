#ifndef GNW_INTERNAL_H
#define GNW_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void gnw_present(void);
void gnw_input_update(void);
void gnw_set_palette_rgb555(const uint16_t *palette);
const uint16_t *gnw_palette(void);

/* True after a successful TITLE (or first) level load + gameInit. */
bool gnw_game_ready(void);
void gnw_set_game_ready(bool ready);

/* Open TRACKS.AD4 for SD streaming; fill DMA half from OpenLara mixer. */
bool gnw_load_tracks(void);
void gnw_submit_audio(void);
/* Stop mixer + clear DMA halves before a long flash/SD load. */
void gnw_audio_silence_for_load(void);

/* Try /homebrews/openlara/<name>.<ext> and sibling paths; stop on first hit. */
bool gnw_foreach_asset_path(const char *name, const char *ext,
                            bool (*fn)(const char *path, void *ctx), void *ctx);

/* Play /homebrews/openlara/fmv/<clip>.AVI if present (MJPEG+MP3). No-op if missing. */
void gnw_play_fmv(const char *clip_name);
void gnw_play_fmv_for_level(int level_id);

#ifdef __cplusplus
}
#endif

#endif
