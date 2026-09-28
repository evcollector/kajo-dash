#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Runtime-scaled 5x7 bitmap face. Every source pixel expands to an integer
// square, so these fonts stay perfectly hard-edged without storing several
// large antialiased glyph atlases in flash.
LV_FONT_DECLARE(lv_font_pixel_8)
LV_FONT_DECLARE(lv_font_pixel_16)
LV_FONT_DECLARE(lv_font_pixel_24)
LV_FONT_DECLARE(lv_font_pixel_32)
LV_FONT_DECLARE(lv_font_pixel_64)
LV_FONT_DECLARE(lv_font_pixel_80)
LV_FONT_DECLARE(lv_font_pixel_112)

#ifdef __cplusplus
}
#endif
