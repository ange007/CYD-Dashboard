/**
 * @file lv_conf.h
 * Configuration file for LVGL v9.5.0
 * Migrated from v8.3.2 config
 */

/* clang-format off */
#if 1 /*Set it to "1" to enable content*/

#ifndef LV_CONF_H
#define LV_CONF_H

/* If you need to include anything here, do it inside the `__ASSEMBLY__` guard */
#if  0 && defined(__ASSEMBLY__)
#include "my_include.h"
#endif

/*====================
   COLOR SETTINGS
 *====================*/

/** Color depth: 1 (I1), 8 (L8), 16 (RGB565), 24 (RGB888), 32 (XRGB8888) */
#define LV_COLOR_DEPTH 16

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/

#ifdef BOARD_HAS_PSRAM
    /** PSRAM-backed pool: frees ~48 KB of internal SRAM (DMA, WiFi, stack).
     *  LVGL object trees are CPU-only (no DMA) so PSRAM latency is acceptable. */
    #define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
    #define LV_MEM_SIZE (128U * 1024U)
    #define LV_MEM_POOL_EXPAND_SIZE 0
    #define LV_MEM_ADR 0
    #define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>
    #define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
    /** No PSRAM: use the system heap (malloc/free) instead of the 48 KB static
     *  pool — LV_STDLIB_BUILTIN links in a full pool as BSS at build time which
     *  overflows DRAM on plain-ESP32 boards (no PSRAM, ~180 KB usable DRAM).
     *  System heap is the same DRAM but populated on demand, not reserved. */
    #define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#endif
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN

/*====================
   HAL SETTINGS
 *====================*/

/** Default display refresh, input device read and animation step period. */
#define LV_DEF_REFR_PERIOD  30      /**< [ms] */

/** Default Dots Per Inch. Used to initialize default sizes such as widgets sized, style paddings.
 * (Not so important, you can adjust it to modify default sizes and spaces.) */
#define LV_DPI_DEF 130              /**< [px/inch] */

/*=================
 * OPERATING SYSTEM
 *=================*/

#define LV_USE_OS   LV_OS_NONE

/*========================
 * RENDERING CONFIGURATION
 *========================*/

/** Align stride of all layers and images to this bytes */
#define LV_DRAW_BUF_STRIDE_ALIGN                1

/** Align start address of draw_buf addresses to this bytes */
#define LV_DRAW_BUF_ALIGN                       4

#define LV_DRAW_TRANSFORM_USE_MATRIX            0

/** The target buffer size for simple layer chunks. */
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE    (24 * 1024)    /**< [bytes] */

#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW == 1
    #define LV_DRAW_SW_SUPPORT_RGB565       1
    #define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED       1
    #define LV_DRAW_SW_SUPPORT_RGB565A8     1
    #define LV_DRAW_SW_SUPPORT_RGB888       1
    #define LV_DRAW_SW_SUPPORT_XRGB8888     1
    #define LV_DRAW_SW_SUPPORT_ARGB8888     1
    #define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 1
    #define LV_DRAW_SW_SUPPORT_L8           1
    #define LV_DRAW_SW_SUPPORT_AL88         1
    #define LV_DRAW_SW_SUPPORT_A8           1
    #define LV_DRAW_SW_SUPPORT_I1           1

    #define LV_DRAW_SW_DRAW_UNIT_CNT    1

    /**
     * - 0: Use a simple renderer capable of drawing only simple rectangles with gradient, images, text, and straight lines only.
     * - 1: Use a complex renderer capable of drawing rounded corners, shadow, skew lines, and arcs too. */
    #define LV_DRAW_SW_COMPLEX          1

    #if LV_DRAW_SW_COMPLEX == 1
        /* Shadow cache is a static uint8_t[N*N] array living in lv_global (.bss,
         * internal DRAM on every board). At 256 it reserved 65536 B — the single
         * largest DRAM consumer, blocking NimBLE on no-PSRAM boards. Shadows still
         * render (LV_DRAW_SW_COMPLEX=1); only the precomputed-blur cache is gone,
         * which costs a one-off recompute when a shadowed widget is redrawn.
         * 0 = disabled (frees 64 KB). Our UI has few, mostly-static shadowed cards. */
        #define LV_DRAW_SW_SHADOW_CACHE_SIZE 0
        #define LV_DRAW_SW_CIRCLE_CACHE_SIZE 4
    #endif

    #define LV_USE_DRAW_SW_ASM     LV_DRAW_SW_ASM_NONE
#endif

/*-------------
 * Logging
 *-----------*/

#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 0
    #define LV_LOG_USE_TIMESTAMP 1
    #define LV_LOG_USE_FILE_LINE 1
#endif  /*LV_USE_LOG*/

/*-------------
 * Asserts
 *-----------*/

#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

#define LV_ASSERT_HANDLER_INCLUDE <esp_system.h>
#define LV_ASSERT_HANDLER { esp_system_abort("LVGL assert"); }   /*Log + restart instead of infinite loop*/

/*-------------
 * Others
 *-----------*/

#ifdef BOARD_HAS_PSRAM
#define LV_USE_PERF_MONITOR 1
#else
/* Plain-ESP32 (no PSRAM): perf monitor costs ~1-2 KB state + label in lv_global. */
#define LV_USE_PERF_MONITOR 0
#endif
#define LV_USE_MEM_MONITOR 0
#define LV_USE_REFR_DEBUG 0

#define LV_USE_USER_DATA 1

#define LV_ENABLE_GLOBAL_CUSTOM 0

/*==================
 *   FONT USAGE
 *===================*/

#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK            0

#define LV_FONT_UNSCII_8  0
#define LV_FONT_UNSCII_16 0

#define LV_FONT_CUSTOM_DECLARE

#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_COMPRESSED 0
#define LV_USE_FONT_SUBPX 0
#define LV_USE_FONT_PLACEHOLDER 1

/*=================
 *  TEXT SETTINGS
 *=================*/

#define LV_TXT_ENC LV_TXT_ENC_UTF8

#define LV_TXT_BREAK_CHARS " ,.;:-_"

#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3

#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*==================
 *  WIDGET USAGE
 *================*/

#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BUTTON     1
#define LV_USE_BUTTONMATRIX  1
#ifdef BOARD_HAS_PSRAM
#define LV_USE_CANVAS     1
#else
#define LV_USE_CANVAS     0
#endif
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMAGE      1
#define LV_USE_LABEL      1
#if LV_USE_LABEL
#ifdef BOARD_HAS_PSRAM
    #define LV_LABEL_TEXT_SELECTION 1
    #define LV_LABEL_LONG_TXT_HINT 1
#else
    #define LV_LABEL_TEXT_SELECTION 0
    #define LV_LABEL_LONG_TXT_HINT 0
#endif
#endif
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#if LV_USE_TEXTAREA != 0
    #define LV_TEXTAREA_DEF_PWD_SHOW_TIME 1500    /*ms*/
#endif
#define LV_USE_TABLE      1

/*==================
 * EXTRA COMPONENTS
 *==================*/

#ifdef BOARD_HAS_PSRAM
#define LV_USE_ANIMIMAGE    1
#define LV_USE_IMAGEBUTTON  1
#define LV_USE_KEYBOARD   1
#define LV_USE_LED        1
#define LV_USE_LIST       1
#define LV_USE_MENU       1
#define LV_USE_MSGBOX     1
#define LV_USE_SPAN       1
#define LV_USE_TILEVIEW   1
#define LV_USE_WIN        1
#define LV_USE_SCALE      1
#else
/* Plain-ESP32 (no PSRAM): drop widgets not referenced by this app. */
#define LV_USE_ANIMIMAGE    0
#define LV_USE_IMAGEBUTTON  0
#define LV_USE_KEYBOARD   1
#define LV_USE_LED        0
#define LV_USE_LIST       0
#define LV_USE_MENU       0
#define LV_USE_MSGBOX     0
#define LV_USE_SPAN       0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0
#define LV_USE_SCALE      0
#endif
#if LV_USE_SPAN
    #define LV_SPAN_SNIPPET_STACK_SIZE 64
#endif
#define LV_USE_CALENDAR   0
#define LV_USE_CHART      1
#define LV_USE_SPINBOX    1
#define LV_USE_SPINNER    1
#define LV_USE_TABVIEW    1

/*-----------
 * Themes
 *----------*/

#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 0
    #define LV_THEME_DEFAULT_GROW 0
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif /*LV_USE_THEME_DEFAULT*/

#define LV_USE_THEME_SIMPLE 1
#define LV_USE_THEME_MONO 0

/*-----------
 * Layouts
 *----------*/

#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*---------------------
 * 3rd party libraries
 *--------------------*/

#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0

#define LV_USE_LODEPNG 0
#define LV_USE_BMP 0
#define LV_USE_TJPGD 1
#define LV_USE_GIF 0

#ifdef BOARD_HAS_PSRAM
#define LV_USE_QRCODE 1
#else
#define LV_USE_QRCODE 0
#endif
#define LV_USE_BARCODE 0

#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0
#define LV_USE_RLOTTIE 0
#define LV_USE_FFMPEG 0

/*-----------
 * Others
 *----------*/

#ifdef ENABLE_SCREENSHOT_ENDPOINT
#define LV_USE_SNAPSHOT 1   /* temporary: /api/screenshot capture (build flag) */
#else
#define LV_USE_SNAPSHOT 0
#endif
#define LV_USE_MONKEY 0
#define LV_USE_GRIDNAV 0
#define LV_USE_FRAGMENT 0
#define LV_USE_IMGFONT 0
#ifdef BOARD_HAS_PSRAM
#define LV_USE_OBSERVER 1
#else
#define LV_USE_OBSERVER 0
#endif
#define LV_USE_IME_PINYIN 0

#define LV_BUILD_EXAMPLES 0

/*--END OF LV_CONF_H--*/

#endif /*LV_CONF_H*/

#endif /*End of "Content enable"*/
