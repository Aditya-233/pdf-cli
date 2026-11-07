#pragma once
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <mupdf/fitz.h>

#define MAX_TABS 10
#define MIN_ZOOM_PCT 20
#define MAX_ZOOM_PCT 500
#define DEFAULT_ZOOM_PCT 100
#define PDFCLI_STORE_BUDGET (512 * 1024)

typedef enum {
    FIT_WIDTH = 0,
    FIT_PAGE = 1
} fit_mode_t;

typedef enum {
    SEARCH_OK = 0,
    SEARCH_TRUNCATED,
    SEARCH_ERROR
} search_status_t;

enum {
    KEY_EOF = -1,
    KEY_NONE = 0,
    KEY_UP = 1000,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_PGUP,
    KEY_PGDN,
    KEY_HOME,
    KEY_END,
    KEY_WHEEL_UP,
    KEY_WHEEL_DOWN,
    KEY_MOUSE_LEFT,
    KEY_CTRL_TAB,
    KEY_CTRL_SHIFT_TAB,
    KEY_CTRL_1 = 1101,
    KEY_CTRL_2,
    KEY_CTRL_3,
    KEY_CTRL_4,
    KEY_CTRL_5,
    KEY_CTRL_6,
    KEY_CTRL_7,
    KEY_CTRL_8,
    KEY_CTRL_9,
    KEY_CTRL_0,
    KEY_RESIZE = 1200,
    KEY_CTRL_W = 23,
    KEY_CTRL_C = 3,
    KEY_ESC = 27
};

typedef struct {
    char *title;
    int page;
    int level;
} toc_item_t;

typedef struct {
    int page;
    fz_rect rect;
} search_hit_t;

typedef struct {
    fz_context *ctx;
    fz_document *doc;
    int page_count;
} pdf_doc_t;

// PDF Engine (Fitz)
[[nodiscard]] pdf_doc_t *pdf_open(const char *path);
void pdf_close(pdf_doc_t *doc);
[[nodiscard]] fz_rect pdf_page_bounds(pdf_doc_t *doc, int page);
bool pdf_render_page_slice(pdf_doc_t *doc, int page, double scale, int sx, int sy, int vw, int vh, fz_pixmap *dst, int dst_x, int dst_y);
[[nodiscard]] fz_pixmap *pdf_render_viewport(pdf_doc_t *doc, int page, double scale, int scroll_y, int vw, int vh, bool dark);
[[nodiscard]] toc_item_t *pdf_get_toc(pdf_doc_t *doc, size_t *out_count);
void pdf_free_toc(toc_item_t *items, size_t count);
[[nodiscard]] search_hit_t *pdf_search_text(pdf_doc_t *doc, const char *needle, size_t *out_count, search_status_t *out_status);
void pdf_free_search(search_hit_t *hits);

// Terminal I/O & Key Parsing (termios, foot SGR & sync)
bool term_enter_raw(void);
void term_restore(void);
void term_sync_begin(void);
void term_sync_end(void);
void term_get_size(int *cols, int *rows, int *px_w, int *px_h);
int  term_read_key(int timeout_ms, int *mouse_col, int *mouse_row);

// Sixel Graphics (Foot Terminal)
bool sixel_draw(const fz_pixmap *pix, int col, int row);
void sixel_free_buffers(void);

// Utility functions (Terminal sanitation, UTF-8 display width, arithmetic)
size_t sanitize_terminal_string(const char *src, char *dst, size_t dst_size);
int utf8_codepoint_width(uint32_t cp);
int utf8_str_width(const char *s);
size_t utf8_truncate_by_width(const char *src, int max_cols, char *dst, size_t dst_size);
int checked_scale_to_int(double base, double scale, int min_val, int max_val);
