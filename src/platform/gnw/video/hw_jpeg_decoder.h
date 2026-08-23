#ifndef __HW_JPEG_DECODER_H
#define __HW_JPEG_DECODER_H

#include <stdint.h>

/* Local HW JPEG + DMA2D path (not the firmware ABI). Polling HAL, no IRQs. */
extern uint32_t g_jpeg_hal, g_jpeg_err, g_jpeg_rej, g_jpeg_sub, g_jpeg_need;

/* Firmware ABI: drop the launcher's JPEG handle before video_jpeg_init. */
uint32_t JPEG_DecodeDeInit(void);

uint32_t video_jpeg_init(uint32_t work, uint32_t work_size);
/* Point YCbCr output at another pool (e.g. LCD back-buffer) without re-init. */
void video_jpeg_set_work(uint32_t work, uint32_t work_size);
uint32_t video_jpeg_decode(uint32_t src, uint32_t src_size, uint32_t dst,
                           uint16_t x, uint16_t y, uint8_t luma_alpha);
uint32_t video_jpeg_deinit(void);

/* Optional: called ~every ms from the polling JPEG/DMA2D loops so the player
 * can keep feeding the PCM ring. NULL = no-op. */
void video_jpeg_set_poll(void (*fn)(void));
void video_jpeg_poll(void);

#endif
