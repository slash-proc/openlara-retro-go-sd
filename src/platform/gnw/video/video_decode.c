// One-frame JPEG decode into the framebuffer — see video_decode.h.
//
// Uses the STM32 HARDWARE JPEG codec instead of software tjpgd: the JPEG core
// decodes to YCbCr and DMA2D converts straight to RGB565 in the LCD framebuffer.
// Decode runs from a local HAL copy (src/hw_jpeg_decoder.c) so we own SrcSize
// and error handling without a firmware ABI round-trip.
//
// The frame is read into RAM, its size is read cheaply from the JPEG header
// (centered as a letterbox), then decoded directly to the framebuffer. The HW
// codec cannot downscale, so a frame larger than the screen is skipped (the
// player keeps the previous frame) — the encoder always targets 320x240 anyway.

#include "video_decode.h"
#include "avi.h"
#include "video_scratch.h"
#include "hw_jpeg_decoder.h"
#include "gw_lcd.h"
#include "main.h"               // wdog_refresh + CMSIS cache ops
#include <string.h>

// g_scratch (184 KiB) for OpenLara FMV:
//   [0    .. 64K)     frame slot 0 (JPEG source; reused after blit for prefetch)
//   [64K  .. 184K)    JPEG core YCbCr work (120 KiB; 320x240 4:2:0 = 115 KiB)
#define FRAME_MAX  VIDEO_FRAME_MAX
#define JWORK_SZ   (120 * 1024)

uint8_t *video_slot(int i)
{
    (void)i;
    return g_scratch;
}

// Diagnostics for the last decode attempt (shown on screen when a clip won't play):
// st 0=ok 1=bad-args/too-big 2=fread-fail 3=jpeg_dims-fail 4=larger-than-screen
// 5=HW-decode-rc-nonzero. w/h = parsed dims, rc = video_jpeg_decode return.
int  g_vdec_st = 0, g_vdec_w = 0, g_vdec_h = 0;
long g_vdec_sz = 0, g_vdec_rc = 0;
unsigned char g_vdec_b0 = 0, g_vdec_b1 = 0;   // first 2 bytes of the frame (FFD8 = JPEG SOI)

// Split timing (ms) so the HUD can tell where a busy frame's cost went:
//   g_vdec_read_ms  BLOCKING SD read — bytes the player had to wait on because the
//                   frame was NOT already prefetched (the read that actually cost
//                   the frame budget). ~0 when the jitter buffer absorbed the burst.
//   g_vdec_pf_ms    read hidden during the pacing wait (prefetch overlap) — the
//                   work that made the read_ms above small. High pf_ms + low read_ms
//                   is the overlap paying off.
//   g_vdec_jpeg_ms  HW JPEG decode of the frame just shown.
int g_vdec_read_ms = 0, g_vdec_pf_ms = 0, g_vdec_jpeg_ms = 0;

void video_decode_init(void)
{
    /* Release the launcher's JPEG handle before we HAL_JPEG_Init our own. */
    JPEG_DecodeDeInit();
    if (!g_scratch)
        return;
    video_jpeg_init((uint32_t)(g_scratch + FRAME_MAX), JWORK_SZ);
}

void video_decode_deinit(void)
{
    video_jpeg_deinit();
}

// Read the image size from the JPEG SOF marker — cheap (a header walk) versus a
// full pre-decode, and we need it before placing the letterbox.
static bool jpeg_dims(const uint8_t *p, long n, int *w, int *h)
{
    long i = 2;                                 // past SOI (FF D8)
    while (i + 9 < n) {
        if (p[i] != 0xFF) { i++; continue; }
        uint8_t m = p[i + 1];
        if (m == 0xC0 || m == 0xC1 || m == 0xC2) {        // SOF0 / SOF1 / SOF2
            *h = (p[i + 5] << 8) | p[i + 6];
            *w = (p[i + 7] << 8) | p[i + 8];
            return *w > 0 && *h > 0;
        }
        if (m == 0xD8 || m == 0xD9 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
            i += 2;                              // standalone marker, no length
            continue;
        }
        i += 2 + ((p[i + 2] << 8) | p[i + 3]);   // skip this length-prefixed segment
    }
    return false;
}

bool video_decode_slot(const uint8_t *src, long size, uint16_t *fb, int fb_w, int fb_h)
{
    g_vdec_sz = size; g_vdec_st = 0; g_vdec_w = g_vdec_h = 0; g_vdec_rc = 0;
    if (!src || size < 2 || size > FRAME_MAX || !fb) { g_vdec_st = 1; return false; }

    wdog_refresh();
    g_vdec_b0 = src[0]; g_vdec_b1 = src[1];

    int w, h;
    if (!jpeg_dims(src, size, &w, &h)) { g_vdec_st = 3; return false; }
    g_vdec_w = w; g_vdec_h = h;
    if (w > fb_w || h > fb_h) { g_vdec_st = 4; return false; }   // HW codec can't downscale
    int x = (fb_w - w) / 2, y = (fb_h - h) / 2;

    if (w < fb_w || h < fb_h) {
        size_t n = (size_t)fb_w * (size_t)fb_h * sizeof(uint16_t);
        uint8_t *p = (uint8_t *)fb;
        while (n) {
            size_t chunk = n > 8192u ? 8192u : n;
            memset(p, 0, chunk);
            p += chunk;
            n -= chunk;
            video_jpeg_poll();
            wdog_refresh();
        }
    }

    /* Flush the JPEG source so the peripheral's FIFO reads fresh bytes. */
    SCB_CleanDCache_by_Addr((uint32_t *)src, (int32_t)((size + 31) & ~31L));

    uint32_t t_jpeg0 = HAL_GetTick();
    g_vdec_rc = (long)video_jpeg_decode((uint32_t)src, (uint32_t)size, (uint32_t)fb,
                                        (uint16_t)x, (uint16_t)y, 255);
    g_vdec_jpeg_ms = (int)(HAL_GetTick() - t_jpeg0);
    if (g_vdec_rc != 0) { g_vdec_st = 5; return false; }
    return true;
}
