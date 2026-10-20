#pragma once

#include "pdfcli/types.h"
#include <mupdf/fitz.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

[[nodiscard]] fz_pixmap *img_crop(fz_context *ctx, fz_pixmap *pix, const crop_box *crop);
void img_smart_invert(fz_context *ctx, fz_pixmap *pix);
void img_simple_invert(fz_context *ctx, fz_pixmap *pix);
void img_dim_tint(fz_context *ctx, fz_pixmap *pix);
[[nodiscard]] unsigned char *img_save_png_to_memory(fz_context *ctx, fz_pixmap *pix, size_t *out_size);
[[nodiscard]] bool img_save_png_to_file(fz_context *ctx, fz_pixmap *pix, const char *path);

#ifdef __cplusplus
}
#endif
