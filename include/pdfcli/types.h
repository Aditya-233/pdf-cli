#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ISO/IEC 9899:2024 (C23) Core Types and Constants
constexpr double MIN_ZOOM = 0.1;
constexpr double MAX_ZOOM = 2.0;
constexpr double DEFAULT_ZOOM = 1.0;

constexpr int DEFAULT_HTML_PAGE_WIDTH = 1000;
constexpr int MIN_HTML_PAGE_WIDTH = 200;
constexpr int MAX_HTML_PAGE_WIDTH = 3000;

typedef enum fit_mode : uint8_t {
    FIT_AUTO   = 0,
    FIT_HEIGHT = 1,
    FIT_WIDTH  = 2
} fit_mode;

typedef enum view_mode : uint8_t {
    VIEW_AUTO  = 0,
    VIEW_TEXT  = 1,
    VIEW_IMAGE = 2
} view_mode;

typedef enum dual_mode : uint8_t {
    DUAL_OFF        = 0,
    DUAL_HALF       = 1,
    DUAL_HORIZONTAL = 2,
    DUAL_VERTICAL   = 3
} dual_mode;

typedef enum half_offset : uint8_t {
    HALF_TOP    = 0,
    HALF_BOTTOM = 1
} half_offset;

typedef enum tint_mode : uint8_t {
    TINT_OFF    = 0,
    TINT_DIM    = 1,
    TINT_DARK   = 2,
    TINT_INVERT = 3
} tint_mode;

typedef enum term_gfx_proto : uint8_t {
    GFX_NONE   = 0,
    GFX_KITTY  = 1,
    GFX_SIXEL  = 2,
    GFX_ITERM2 = 3,
    GFX_BLOCKS = 4
} term_gfx_proto;

typedef enum kitty_xfer_mode : uint8_t {
    KITTY_XFER_DIRECT = 0,
    KITTY_XFER_FILE   = 1
} kitty_xfer_mode;

typedef struct crop_box {
    double top;
    double bottom;
    double left;
    double right;
} crop_box;

typedef struct view_loc {
    int page;       // 0-indexed page
    double x;       // PDF point x
    double y;       // PDF point y
} view_loc;

// Maps clicked screen character cell back to PDF document coordinates
typedef struct page_click_map {
    int page_num;
    int col_offset;
    int row_offset;
    int height_lines;
    int width_chars;
    int pixel_width;
    int pixel_height;
    double crop_x0;
    double crop_x1;
    double crop_y0;
    double crop_y1;
    double page_width_pts;
    double page_height_pts;
} page_click_map;
