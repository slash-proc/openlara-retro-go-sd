#pragma once

#include <stdint.h>

void video_audio_start(void);
void video_audio_feed(const uint8_t *mp3, int len);
void video_audio_pump(void);
int  video_audio_ring_count(void);
int  video_audio_ring_free(void);
void video_audio_stop(void);
void video_audio_detach(void);
