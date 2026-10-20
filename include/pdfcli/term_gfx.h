#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <mupdf/fitz.h>
#include <sixel.h>
#include "pdfcli/types.h"
#include "pdfcli/term_raw.h"

// Terminal Graphics Context structure
typedef struct term_gfx_context {
    term_gfx_proto proto;
    kitty_xfer_mode kitty_mode;
    uint32_t last_image_id;
    bool kitty_probed;
} term_gfx_context;

// Context lifecycle
[[nodiscard]] term_gfx_context *term_gfx_init(void);
void term_gfx_destroy(term_gfx_context *ctx);

// Protocol detection and probing
[[nodiscard]] term_gfx_proto term_gfx_detect_protocol(void);
void term_gfx_probe_kitty_mode(term_gfx_context *ctx, const char *temp_dir);
[[nodiscard]] uint32_t term_gfx_next_image_id(void);

// Context accessors
[[nodiscard]] term_gfx_proto term_gfx_get_proto(const term_gfx_context *ctx);
void term_gfx_set_proto(term_gfx_context *ctx, term_gfx_proto proto);
[[nodiscard]] kitty_xfer_mode term_gfx_get_kitty_mode(const term_gfx_context *ctx);
void term_gfx_set_kitty_mode(term_gfx_context *ctx, kitty_xfer_mode mode);
[[nodiscard]] uint32_t term_gfx_get_last_image_id(const term_gfx_context *ctx);
void term_gfx_clear_last_image(term_gfx_context *ctx);

// Kitty graphics protocol
[[nodiscard]] bool term_gfx_draw_kitty_png_file(
    uint32_t image_id,
    const char *file_path,
    int col,
    int row,
    int width_cells,
    int height_cells,
    kitty_xfer_mode mode
);

[[nodiscard]] bool term_gfx_draw_kitty_png_mem(
    uint32_t image_id,
    const unsigned char *png_bytes,
    size_t png_size,
    int col,
    int row,
    int width_cells,
    int height_cells
);

void term_gfx_kitty_delete_image(uint32_t image_id);

// Sixel graphics protocol
[[nodiscard]] bool term_gfx_draw_sixel(
    const fz_pixmap *pix,
    int col,
    int row,
    int width_px,
    int height_px
);

// iTerm2 inline protocol
[[nodiscard]] bool term_gfx_draw_iterm2(
    const unsigned char *png_bytes,
    size_t png_size,
    int col,
    int row,
    int width_cells,
    int height_cells
);

// Halfblocks 24-bit truecolor fallback (Unicode ▀)
[[nodiscard]] bool term_gfx_draw_halfblocks(
    const fz_pixmap *pix,
    int col,
    int row,
    int max_cols,
    int max_rows
);

// Clear any lingering graphics attachments (e.g. Kitty placements)
void term_gfx_clear(term_gfx_context *ctx);

// Universal dispatch
[[nodiscard]] bool term_gfx_draw_pixmap(
    term_gfx_context *ctx,
    fz_context *fzctx,
    fz_pixmap *pix,
    const char *temp_png_path,
    int col,
    int row,
    int width_cells,
    int height_cells,
    int width_px,
    int height_px
);
