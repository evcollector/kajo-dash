#if 1  // Guard required by LVGL — keep as 1

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// Color — 16-bit RGB565 for ILI9341.
// LV_COLOR_16_SWAP pre-swaps bytes in the buffer so pushColors(…, false) works.
#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   1
// No transformed widgets anymore (big digits use the real lv_font_speed72
// font), so alpha layers are not needed.
#define LV_COLOR_SCREEN_TRANSP 0

// Use ESP32's runtime heap for LVGL objects/styles. Keeping LVGL's allocator as
// one large static array needlessly consumes the linker-constrained DRAM window
// used by the BLE controller, while the same memory remains available through
// the system heap. This also allows unused UI memory to remain available to BLE.
#define LV_MEM_CUSTOM          1
#define LV_MEM_CUSTOM_INCLUDE  <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC    malloc
#define LV_MEM_CUSTOM_FREE     free
#define LV_MEM_CUSTOM_REALLOC  realloc

// Tick source — uses millis() so no separate task needed
#define LV_TICK_CUSTOM                  1
#define LV_TICK_CUSTOM_INCLUDE          "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR    (millis())

// Poll touch every 10 ms (default 30) so short taps are not missed
#define LV_INDEV_DEF_READ_PERIOD        10

// Resolution (must match TFT_WIDTH/TFT_HEIGHT)
#define LV_HOR_RES_MAX  240
#define LV_VER_RES_MAX  320
#define LV_DPI_DEF      130

// Drawing
#define LV_DRAW_COMPLEX         1
#define LV_SHADOW_CACHE_SIZE    0
#define LV_CIRCLE_CACHE_SIZE    4
#define LV_IMG_CACHE_DEF_SIZE   0
#define LV_GRADIENT_MAX_STOPS   2
// LVGL 8's pointer-based gradient key aliases opposing bell-background halves.
// Keep disabled until cached/uncached output passes dashboard_redraw_test.
#define LV_GRAD_CACHE_DEF_SIZE  0

// GPU — none on ESP32
#define LV_USE_GPU_STM32_DMA2D  0
#define LV_USE_GPU_NXP_PXP      0
#define LV_USE_GPU_NXP_VG_LITE  0
#define LV_USE_GPU_SDL          0

// Logging / asserts
#define LV_USE_LOG              0
#define LV_USE_ASSERT_NULL      1
#define LV_USE_ASSERT_MALLOC    1
#define LV_USE_ASSERT_STYLE     0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ       0

// FPS counter visible on screen — useful for testing
#define LV_USE_PERF_MONITOR         0
#define LV_USE_PERF_MONITOR_POS     LV_ALIGN_BOTTOM_RIGHT
#define LV_USE_MEM_MONITOR          0

// Animations and layouts
#define LV_USE_ANIMATION    1
#define LV_USE_FLEX         1
#define LV_USE_GRID         1

// Core widgets
#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BTN          1
#define LV_USE_BTNMATRIX    1
#define LV_USE_CANVAS       1
#define LV_USE_CHECKBOX     1
#define LV_USE_DROPDOWN     1
#define LV_USE_IMG          1
#define LV_USE_LABEL        1
#define LV_LABEL_TEXT_SELECTION  1
#define LV_LABEL_LONG_TXT_HINT   1
#define LV_USE_LINE         1
#define LV_USE_ROLLER       1
#define LV_ROLLER_INF_PAGES 7
#define LV_USE_SLIDER       1
#define LV_USE_SWITCH       1
#define LV_USE_TEXTAREA     1
#define LV_USE_TABLE        0

// Extra widgets
#define LV_USE_ANIMIMG      0
#define LV_USE_CALENDAR     0
#define LV_USE_CHART        1
#define LV_USE_COLORWHEEL   0
#define LV_USE_IMGBTN       0
#define LV_USE_KEYBOARD     0  // custom btnmatrix keyboard in screens.cpp instead
#define LV_USE_LED          1
#define LV_USE_LIST         1
#define LV_USE_MENU         0
#define LV_USE_METER        1
#define LV_USE_MSGBOX       0
#define LV_USE_SPAN         0
#define LV_USE_SPINBOX      0
#define LV_USE_SPINNER      1
#define LV_USE_TABVIEW      0
#define LV_USE_TILEVIEW     0
#define LV_USE_WIN          0

// Theme
#define LV_USE_THEME_DEFAULT            1
#define LV_THEME_DEFAULT_DARK           1
#define LV_THEME_DEFAULT_GROW           1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80
#define LV_USE_THEME_SIMPLE             1
#define LV_USE_THEME_MONO               0

// Fonts — Rajdhani (futuristic semi-condensed, SIL OFL) replaces Montserrat
// as the UI font; see src/lvgl_app/lv_font_rajdhani_*.c. The Montserrat
// names are aliased to Rajdhani so all existing references (and LVGL's
// default theme) pick it up without code changes.
#define LV_FONT_MONTSERRAT_10   0
#define LV_FONT_MONTSERRAT_12   0
#define LV_FONT_MONTSERRAT_14   0
#define LV_FONT_MONTSERRAT_16   0
#define LV_FONT_MONTSERRAT_18   0
#define LV_FONT_MONTSERRAT_20   0
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   0
#define LV_FONT_MONTSERRAT_26   0
#define LV_FONT_MONTSERRAT_28   0
#define LV_FONT_MONTSERRAT_30   0
#define LV_FONT_MONTSERRAT_32   0
#define LV_FONT_MONTSERRAT_34   0
#define LV_FONT_MONTSERRAT_36   0
#define LV_FONT_MONTSERRAT_38   0
#define LV_FONT_MONTSERRAT_40   0
#define LV_FONT_MONTSERRAT_42   0
#define LV_FONT_MONTSERRAT_44   0
#define LV_FONT_MONTSERRAT_46   0
#define LV_FONT_MONTSERRAT_48   0
#define LV_FONT_MONTSERRAT_12_SUBPX         0
#define LV_FONT_MONTSERRAT_28_COMPRESSED    0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW    0
#define LV_FONT_SIMSUN_16_CJK               0
#define LV_FONT_UNSCII_8                    0
#define LV_FONT_UNSCII_16                   0

#define LV_FONT_CUSTOM_DECLARE \
  LV_FONT_DECLARE(lv_font_rajdhani_10) \
  LV_FONT_DECLARE(lv_font_rajdhani_12) \
  LV_FONT_DECLARE(lv_font_rajdhani_14) \
  LV_FONT_DECLARE(lv_font_rajdhani_16) \
  LV_FONT_DECLARE(lv_font_rajdhani_20) \
  LV_FONT_DECLARE(lv_font_rajdhani_24) \
  LV_FONT_DECLARE(lv_font_rajdhani_36)


#define LV_FONT_DEFAULT  &lv_font_rajdhani_14

// File system — not needed for this demo
#define LV_USE_FS_STDIO     0
#define LV_USE_FS_POSIX     0
#define LV_USE_FS_WIN32     0
#define LV_USE_FS_FATFS     0

// Image decoders — not needed
#define LV_USE_PNG      0
#define LV_USE_BMP      0
#define LV_USE_SJPG     0
#define LV_USE_GIF      0
#define LV_USE_QRCODE   0
#define LV_USE_FREETYPE 0
#define LV_USE_RLOTTIE  0
#define LV_USE_FFMPEG   0

// Misc
#define LV_USE_SNAPSHOT     0
#define LV_USE_MONKEY       0
#define LV_USE_GRIDNAV      0
#define LV_USE_FRAGMENT     0
#define LV_USE_IMGFONT      0
#define LV_USE_MSG          0
#define LV_USE_IME_PINYIN   0

// Built-in demos — disabled (we build our own in main_lvgl.cpp)
#define LV_BUILD_EXAMPLES           0
#define LV_USE_DEMO_WIDGETS         0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK       0
#define LV_USE_DEMO_STRESS          0
#define LV_USE_DEMO_MUSIC           0

#endif  // LV_CONF_H
#endif  // Guard
