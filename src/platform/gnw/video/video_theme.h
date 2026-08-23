#pragma once

#include <stdint.h>

/* Prefer firmware colors_t when available; fallback matches gui.h layout. */
#ifndef COLORS_T_DEFINED
typedef struct {
    uint16_t bg_c;
    uint16_t main_c;
    uint16_t sel_c;
    uint16_t dis_c;
} colors_t;
#endif

int i18n_get_text_width(const char *text);
int i18n_draw_text_line(uint16_t x_pos, uint16_t y_pos, uint16_t width,
                        const char *text, uint16_t color, uint16_t color_bg,
                        char transparent);
