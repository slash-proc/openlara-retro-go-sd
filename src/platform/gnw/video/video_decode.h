#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "avi.h"

#define VIDEO_FRAME_MAX (64 * 1024)
#define VIDEO_SLOTS     1

uint8_t *video_slot(int i);

void video_decode_init(void);
void video_decode_deinit(void);
bool video_decode_slot(const uint8_t *src, long size, uint16_t *fb, int fb_w, int fb_h);
