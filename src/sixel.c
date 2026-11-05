#include "pdfcli.h"
#include <sixel.h>
#include <limits.h>
#include <string.h>

static int sixel_write_cb(char *data, int size, void *priv) {
    if (size <= 0) return 0;
    return (int)fwrite(data, 1, (size_t)size, (FILE *)priv);
}

bool sixel_draw(const fz_pixmap *pix, int col, int row) {
    if (!pix || !pix->samples || pix->w <= 0 || pix->h <= 0) return false;
    if (pix->w > 16384 || pix->h > 16384) return false;
    if (pix->n <= 0 || pix->n > 4) return false;

    // Check multiplication overflow
    size_t total_px = (size_t)pix->w * (size_t)pix->h;
    if (total_px > SIZE_MAX / 4) return false;

    if (row > 0 && col > 0) printf("\033[%d;%dH", row, col);
    else if (col > 0) printf("\033[%dG", col);
    fflush(stdout);

    // Normalize input to RGB888 (comp = 3) or G8 (comp = 1)
    size_t comp = (pix->n >= 3) ? 3 : 1;
    int fmt = (comp == 3) ? SIXEL_PIXELFORMAT_RGB888 : SIXEL_PIXELFORMAT_G8;

    unsigned char *cbuf = nullptr;
    unsigned char *indexed = nullptr;
    sixel_output_t *out = nullptr;
    sixel_dither_t *dither = nullptr;
    bool ok = false;

    const unsigned char *samples = nullptr;
    bool needs_conversion = (pix->n == 4 || pix->n == 2 || pix->stride != (ptrdiff_t)pix->w * (ptrdiff_t)pix->n);

    if (needs_conversion) {
        cbuf = (unsigned char *)malloc(total_px * comp);
        if (!cbuf) goto cleanup;

        for (int y = 0; y < pix->h; y++) {
            const unsigned char *src_row = pix->samples + (size_t)y * (size_t)pix->stride;
            unsigned char *dst_row = cbuf + (size_t)y * ((size_t)pix->w * comp);

            if (pix->n == 4) {
                // RGBA -> RGB (drop alpha)
                for (int x = 0; x < pix->w; x++) {
                    dst_row[x * 3 + 0] = src_row[x * 4 + 0];
                    dst_row[x * 3 + 1] = src_row[x * 4 + 1];
                    dst_row[x * 3 + 2] = src_row[x * 4 + 2];
                }
            } else if (pix->n == 3) {
                // RGB with stride padding
                memcpy(dst_row, src_row, (size_t)pix->w * 3);
            } else if (pix->n == 2) {
                // Gray+Alpha -> Gray (drop alpha)
                for (int x = 0; x < pix->w; x++) {
                    dst_row[x] = src_row[x * 2 + 0];
                }
            } else {
                // Gray with stride padding
                memcpy(dst_row, src_row, (size_t)pix->w);
            }
        }
        samples = cbuf;
    } else {
        samples = pix->samples;
    }

    if (!SIXEL_SUCCEEDED(sixel_output_new(&out, sixel_write_cb, stdout, nullptr)) ||
        !SIXEL_SUCCEEDED(sixel_dither_new(&dither, 256, nullptr)) ||
        !SIXEL_SUCCEEDED(sixel_dither_initialize(dither, (unsigned char *)samples, pix->w, pix->h, fmt, SIXEL_LARGE_AUTO, SIXEL_REP_AUTO, SIXEL_QUALITY_LOW)))
        goto cleanup;

    unsigned char *palette = sixel_dither_get_palette(dither);
    int num_colors = sixel_dither_get_num_of_palette_colors(dither);
    if (!palette || num_colors <= 0) {
        ok = SIXEL_SUCCEEDED(sixel_encode((unsigned char *)samples, pix->w, pix->h, 0, dither, out));
        goto cleanup;
    }

    indexed = (unsigned char *)malloc(total_px);
    if (!indexed) goto cleanup;

    /*
     * Fast 15-bit RGB Color Quantization LUT:
     * High-speed nearest-color caching dividing 24-bit RGB space into 32x32x32 bins.
     * The first pixel mapping into a bin seeds the bin with the closest palette index,
     * providing significant acceleration for dense text and document rendering.
     */
    uint16_t lut[32768] = {0};
    uint8_t bg_r = samples[0];
    uint8_t bg_g = (comp >= 3) ? samples[1] : bg_r;
    uint8_t bg_b = (comp >= 3) ? samples[2] : bg_r;
    uint8_t bg_idx = 0;
    int min_d = INT_MAX;

    for (int i = 0; i < num_colors; i++) {
        int dr = (int)bg_r - palette[i * 3];
        int dg = (int)bg_g - palette[i * 3 + 1];
        int db = (int)bg_b - palette[i * 3 + 2];
        int d = dr * dr + dg * dg + db * db;
        if (d < min_d) {
            min_d = d;
            bg_idx = (uint8_t)i;
            if (d == 0) break;
        }
    }
    lut[((uint16_t)(bg_r >> 3) << 10) | ((uint16_t)(bg_g >> 3) << 5) | (bg_b >> 3)] = (uint16_t)(0x8000 | bg_idx);

    for (size_t i = 0; i < total_px; i++) {
        uint8_t r = samples[i * comp];
        uint8_t g = (comp >= 3) ? samples[i * comp + 1] : r;
        uint8_t b = (comp >= 3) ? samples[i * comp + 2] : r;

        if (r == bg_r && g == bg_g && b == bg_b) {
            indexed[i] = bg_idx;
            continue;
        }

        uint16_t key = (uint16_t)(((uint16_t)(r >> 3) << 10) | ((uint16_t)(g >> 3) << 5) | (b >> 3));
        if (lut[key] & 0x8000) {
            indexed[i] = (uint8_t)(lut[key] & 0xFF);
        } else {
            int bd = INT_MAX;
            uint8_t bi = 0;
            for (int c = 0; c < num_colors; c++) {
                int dr = (int)r - palette[c * 3];
                int dg = (int)g - palette[c * 3 + 1];
                int db = (int)b - palette[c * 3 + 2];
                int d = dr * dr + dg * dg + db * db;
                if (d < bd) {
                    bd = d;
                    bi = (uint8_t)c;
                    if (d == 0) break;
                }
            }
            lut[key] = (uint16_t)(0x8000 | bi);
            indexed[i] = bi;
        }
    }

    sixel_dither_set_pixelformat(dither, SIXEL_PIXELFORMAT_PAL8);
    sixel_dither_set_diffusion_type(dither, SIXEL_DIFFUSE_NONE);
    ok = SIXEL_SUCCEEDED(sixel_encode(indexed, pix->w, pix->h, 0, dither, out));

cleanup:
    if (dither) sixel_dither_unref(dither);
    if (out) sixel_output_unref(out);
    free(indexed);
    free(cbuf);
    fflush(stdout);
    return ok;
}
