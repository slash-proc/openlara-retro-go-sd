/*
 * OpenLara audio for G&W: GBA ADPCM4 music + PCM SFX → Retro-Go mono int16 DMA.
 *
 * Music: TRACKS.AD4 streamed from SD (flash cache holds the active PKD only).
 * SFX: PCM blobs inside the level PKD already mapped in flash.
 */
#include "gnw_internal.h"

extern "C" {
#include <stdio.h>
#include <string.h>
#include "gw_audio.h"
#include "odroid_overlay.h"
#include "gnw_bridge.h"
}

#include "ol/common.h"

/* Present for any code that checks the symbol; music uses the FILE* below. */
const void *TRACKS_AD4;

uint8 ADPCM4_ADAPT[] = {
    192, 192, 136, 136, 128, 128, 128, 128,
    112, 128, 128, 128, 128, 136, 136, 192,
};

#define DECODE_ADPCM4(n)                                                       \
    tap = zM2 + tap - (tap >> 3);                                              \
    *buffer++ = SND_ENCODE(X_CLAMP(tap >> 8, SND_MIN, SND_MAX));               \
    res = ((n & 0xF) ^ 8) - 8;                                                 \
    out = res * quant + (zM1 - zM2);                                           \
    zM2 = zM1;                                                                 \
    zM1 = out;                                                                 \
    quant = (quant * (int32)ADPCM4_ADAPT[res + 8] + 127) >> 7;

static void sndADPCM4_c(ADPCM4_STATE &state, int8 *buffer, const uint8 *data, int32 size)
{
    int32 zM1 = state.zM1;
    int32 zM2 = state.zM2;
    int32 tap = state.tap;
    int32 quant = state.quant;
    int32 res, out;

    for (int32 i = 0; i < size; i++) {
        uint32 n = *data++;
        DECODE_ADPCM4(n);
        n >>= 4;
        DECODE_ADPCM4(n);
    }

    state.zM1 = zM1;
    state.zM2 = zM2;
    state.tap = tap;
    state.quant = quant;
}

static int32 sndPCM_c(int32 pos, int32 inc, int32 size, int32 volume, const uint8 *data,
                      int8 *buffer)
{
    int32 last = pos + SND_SAMPLES * inc;
    if (last > size)
        last = size;

    while (pos < last) {
        int32 amp = SND_DECODE(*(uint8 *)buffer) +
                    ((SND_DECODE(data[pos >> SND_FIXED_SHIFT]) * volume) >> SND_VOL_SHIFT);
        *buffer++ = SND_ENCODE(X_CLAMP(amp, SND_MIN, SND_MAX));
        pos += inc;
    }
    return pos;
}

struct TrackInfo {
    int32 offset;
    int32 size;
};

struct Music {
    FILE *fp;
    int32 base; /* file offset of ADPCM payload */
    int32 size; /* ADPCM bytes remaining from start */
    int32 pos;  /* ADPCM bytes consumed */
    bool active;
    ADPCM4_STATE state;

    void fill(int8 *buffer)
    {
        /* Same as GBA: one ADPCM byte → two PCM samples at SND_OUTPUT_FREQ. */
        uint8 chunk[SND_SAMPLES >> 1];
        int32 len = X_MIN(size - pos, SND_SAMPLES >> 1);
        size_t got;

        if (!fp || len <= 0) {
            active = false;
            dmaFill(buffer, (uint8)SND_ENCODE(0), (uint32)(SND_SAMPLES * sizeof(buffer[0])));
            return;
        }

        if (fseek(fp, (long)(base + pos), SEEK_SET) != 0) {
            active = false;
            dmaFill(buffer, (uint8)SND_ENCODE(0), (uint32)(SND_SAMPLES * sizeof(buffer[0])));
            return;
        }

        got = fread(chunk, 1, (size_t)len, fp);
        wdog_refresh();
        if ((int32)got < len)
            len = (int32)got;

        if (len > 0)
            sndADPCM4_c(state, buffer, chunk, len);
        pos += len;

        if (pos >= size || len == 0) {
            active = false;
            memset(buffer + (len << 1), 0, (SND_SAMPLES - (len << 1)) * sizeof(buffer[0]));
        }
    }
};

struct Sample {
    int32 pos;
    int32 inc;
    int32 size;
    int32 volume;
    const uint8 *data;

    void mix(int8 *buffer)
    {
        pos = sndPCM_c(pos, inc, size, volume, data, buffer);
        if (pos >= size)
            data = NULL;
    }

    void fill(int8 *buffer)
    {
        pos = sndPCM_c(pos, inc, size, volume, data, buffer);
        if (pos >= size)
            data = NULL;
    }
};

static Music music;
static Sample channels[SND_CHANNELS];
static int32 channelsCount;
static FILE *s_tracks_fp;

#define CALC_INC (((SND_SAMPLE_FREQ << SND_FIXED_SHIFT) / SND_OUTPUT_FREQ) * pitch >> SND_PITCH_SHIFT)

void sndInit() {}
void sndInitSamples() {}
void sndFreeSamples() {}

void *sndPlaySample(int32 index, int32 volume, int32 pitch, int32 mode)
{
    if (!gSettings.audio_sfx)
        return NULL;
    if (!level.soundData || !level.soundOffsets)
        return NULL;

    const uint8 *data = level.soundData + level.soundOffsets[index];
    int32 size = *(int32 *)data;
    data += 4;

    if (mode == UNIQUE || mode == REPLAY) {
        for (int32 i = 0; i < channelsCount; i++) {
            Sample *sample = channels + i;
            if (sample->data != data)
                continue;
            sample->inc = CALC_INC;
            sample->volume = volume;
            if (mode == REPLAY)
                sample->pos = 0;
            return sample;
        }
    }

    if (channelsCount >= SND_CHANNELS)
        return NULL;

    Sample *sample = channels + channelsCount++;
    sample->data = data;
    sample->size = size << SND_FIXED_SHIFT;
    sample->pos = 0;
    sample->inc = CALC_INC;
    sample->volume = volume;
    return sample;
}

void sndPlayTrack(int32 track)
{
    TrackInfo info;

    if (!gSettings.audio_music)
        return;
    if (track == gCurTrack)
        return;

    gCurTrack = track;
    if (track == -1) {
        sndStopTrack();
        return;
    }
    if (!s_tracks_fp)
        return;

    if (fseek(s_tracks_fp, (long)track * (long)sizeof(TrackInfo), SEEK_SET) != 0)
        return;
    if (fread(&info, sizeof(info), 1, s_tracks_fp) != 1)
        return;
    if (!info.size)
        return;

    music.fp = s_tracks_fp;
    music.base = info.offset;
    music.size = info.size;
    music.pos = 0;
    music.active = true;
    music.state.zM1 = 0;
    music.state.zM2 = 0;
    music.state.tap = 0;
    music.state.quant = 0x0800;
}

void sndStopTrack()
{
    music.active = false;
    music.fp = NULL;
    music.size = 0;
    music.pos = 0;
}

bool sndTrackIsPlaying()
{
    return music.active;
}

void sndStopSample(int32 index)
{
    if (!level.soundData || !level.soundOffsets)
        return;
    const uint8 *data = level.soundData + level.soundOffsets[index] + 4;
    int32 i = channelsCount;
    while (--i >= 0) {
        if (channels[i].data == data)
            channels[i] = channels[--channelsCount];
    }
}

void sndStop()
{
    channelsCount = 0;
    music.active = false;
    music.fp = NULL;
    gCurTrack = -1;
}

/*
 * Flash PKD loads can take hundreds of ms with no sndFill — DMA would loop the
 * last two halves. Stop the mixer and zero both halves first.
 */
extern "C" void gnw_audio_silence_for_load(void)
{
    sndStop();
    audio_clear_buffers();
}

void sndFill(int8 *buffer)
{
    bool mix = music.active;

    if (mix)
        music.fill(buffer);
    else
        dmaFill(buffer, (uint8)SND_ENCODE(0), (uint32)(SND_SAMPLES * sizeof(buffer[0])));

    int32 ch = channelsCount;
    while (ch--) {
        Sample *sample = channels + ch;
        if (mix)
            sample->mix(buffer);
        else
            sample->fill(buffer);
        if (!sample->data)
            channels[ch] = channels[--channelsCount];
        mix = true;
    }
}

/* --- platform: open TRACKS.AD4 + submit to Retro-Go DMA ---------------- */

struct tracks_open_ctx {
    bool ok;
};

static bool tracks_open_cb(const char *path, void *vctx)
{
    tracks_open_ctx *ctx = (tracks_open_ctx *)vctx;
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    s_tracks_fp = f;
    TRACKS_AD4 = (const void *)1; /* non-NULL = present */
    ctx->ok = true;
    printf("openlara: %s open for streaming\n", path);
    return true;
}

extern "C" bool gnw_load_tracks(void)
{
    tracks_open_ctx ctx = { false };
    if (s_tracks_fp) {
        fclose(s_tracks_fp);
        s_tracks_fp = NULL;
    }
    TRACKS_AD4 = NULL;
    if (gnw_foreach_asset_path("TRACKS", "AD4", tracks_open_cb, &ctx))
        return true;
    printf("openlara: missing TRACKS.AD4 — music silent (SFX still ok)\n");
    return false;
}

extern "C" void gnw_submit_audio(void)
{
    int16_t *out;
    uint16_t len;
    int8 mix[SND_SAMPLES];
    int32 vol;
    uint16_t i;
    uint16_t n;

    if (common_emu_sound_loop_is_muted())
        return;

    out = audio_get_active_buffer();
    len = audio_get_buffer_length();
    if (!out || !len)
        return;

    sndFill(mix);

    vol = common_emu_sound_get_volume();
    n = len < (uint16_t)SND_SAMPLES ? len : (uint16_t)SND_SAMPLES;

    for (i = 0; i < n; i++) {
        int32 s = SND_DECODE((uint8)mix[i]) << 8;
        out[i] = (int16_t)((s * vol) / 255);
    }
    for (; i < len; i++)
        out[i] = 0;
}
