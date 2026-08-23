#include "gnw_internal.h"

extern "C" {
#include "gw_lcd.h"
#include "gnw_bridge.h"
}

#include "ol/common.h"

/* RGB555 (GBA order) → RGB565 for the LTDC path. */
static inline uint16_t rgb555_to_565(uint16_t c)
{
    uint16_t r = (uint16_t)(c & 0x1Fu);
    uint16_t g = (uint16_t)((c >> 5) & 0x1Fu);
    uint16_t b = (uint16_t)((c >> 10) & 0x1Fu);
    return (uint16_t)((r << 11) | (g << 6) | b);
}

static uint16_t s_palette565[256];

extern "C" void gnw_set_palette_rgb555(const uint16_t *palette)
{
    for (int i = 0; i < 256; i++)
        s_palette565[i] = rgb555_to_565(palette[i]);
}

extern "C" const uint16_t *gnw_palette(void)
{
    return s_palette565;
}

extern "C" void gnw_present(void)
{
    pixel_t *dst = (pixel_t *)lcd_get_active_buffer();
    const uint8_t *src = (const uint8_t *)fb;
    int y;

    /*
     * Soft-raster is still 240×160. Fill the 320×240 LCD with uniform scale
     * (cover + crop) so X/Y stay the same ratio — non-uniform stretch made
     * far fog / empty clip regions shimmer with weird block artifacts.
     *
     * scale = 240/160 = 1.5 → virtual 360×240, crop 20 px on each side.
     */
    const int virt_w = FRAME_WIDTH * GW_LCD_HEIGHT / FRAME_HEIGHT; /* 360 */
    const int crop_x = (virt_w - GW_LCD_WIDTH) / 2;                 /* 20 */

    for (y = 0; y < GW_LCD_HEIGHT; y++) {
        int sy = (y * FRAME_HEIGHT) / GW_LCD_HEIGHT;
        if (sy >= FRAME_HEIGHT)
            sy = FRAME_HEIGHT - 1;
        const uint8_t *srow = src + sy * FRAME_WIDTH;
        pixel_t *drow = dst + y * GW_LCD_WIDTH;
        int x;

        for (x = 0; x < GW_LCD_WIDTH; x++) {
            int sx = ((x + crop_x) * FRAME_HEIGHT) / GW_LCD_HEIGHT;
            if (sx < 0)
                sx = 0;
            if (sx >= FRAME_WIDTH)
                sx = FRAME_WIDTH - 1;
            drow[x] = (pixel_t)s_palette565[srow[sx]];
        }
    }

    common_ingame_overlay();
}
