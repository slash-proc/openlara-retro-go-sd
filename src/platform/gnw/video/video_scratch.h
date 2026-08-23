#pragma once

#include <stdint.h>
#include <stdbool.h>

/*
 * FMV scratch in RAM_EMU:
 *   [0 .. 64K)   compressed JPEG slot
 *   [64K .. 80K) YCbCr MCU strip for chunked HW decode (~2 MCU rows @ 320 4:2:0)
 * Full-frame YCbCr (~115 KiB) is not kept — strips convert via DMA2D into the
 * LCD back-buffer. After boot FMV, TITLE.SCR (38 KiB) may reuse this block.
 */
#define VIDEO_JPEG_SLOT_SIZE   (64 * 1024)
#define VIDEO_YCBCR_STRIP_SIZE (16 * 1024)
#define VIDEO_SCRATCH_SIZE     (VIDEO_JPEG_SLOT_SIZE + VIDEO_YCBCR_STRIP_SIZE)

extern uint8_t *g_scratch;

bool video_scratch_acquire(void);
void video_scratch_release(void);
