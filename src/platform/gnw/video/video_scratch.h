#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
 * Tight scratch for OpenLara FMV — RAM_EMU only has ~229 KiB free after BSS
 * (and less once TITLE.SCR is resident). Layout (see video_decode.c):
 *   [0    .. 64K)   JPEG slot 0 (reused for prefetch after decode→FB)
 *   [64K  .. 184K)  HW JPEG YCbCr work (120 KiB; 320×240 4:2:0 = 115 KiB)
 */
#define VIDEO_SCRATCH_SIZE (184 * 1024)

extern uint8_t *g_scratch;

bool video_scratch_acquire(void);
void video_scratch_release(void);
