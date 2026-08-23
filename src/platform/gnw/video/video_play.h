// OpenLara cutscene player: demux MJPEG+MP3 AVI to the LCD.
// A skips; PAUSE/SET = Retro-Go menu + PAUSE+D-pad volume/brightness.
// No transport OSD.
#pragma once

typedef enum {
    VID_OK = 0,        // reached the end of the movie
    VID_STOPPED,       // user pressed Back
    VID_UNPLAYABLE,    // not a usable AVI / no decodable video frames
} vid_result_t;

vid_result_t video_play(const char *path);

// Diagnostic text describing why the last clip was VID_UNPLAYABLE (temporary).
const char *video_last_diag(void);
