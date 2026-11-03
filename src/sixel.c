#include "pdfcli.h"
#include <sixel.h>
#include <limits.h>
#include <string.h>

static int sixel_write_cb(char *data, int size, void *priv) {
    return (int)fwrite(data, 1, (size_t)size, (FILE *)priv);
}

bool sixel_draw(const fz_pixmap *pix, int col, int row) {
    if (!pix || !pix->samples || pix->w <= 0 || pix->h <= 0) return false;

    // Position cursor on terminal
    if (row > 0 && col > 0) printf("\033[%d;%dH", row, col);
    else if (col > 0) printf("\033[%dG", col);
    fflush(stdout);

    int fmt = (pix->n >= 3) ? SIXEL_PIXELFORMAT_RGB888 : SIXEL_PIXELFORMAT_G8;
    size_t comp = (size_t)pix->n;
    size_t total_px = (size_t)pix->w * (size_t)pix->h;

    // Ensure contiguous scanlines
    const unsigned char *samples = pix->samples;
    unsigned char *cbuf = nullptr;
    ptrdiff_t exp_stride = (ptrdiff_t)pix->w * (ptrdiff_t)pix->n;
    if (pix->stride != exp_stride) {
        cbuf = (unsigned char *)malloc(total_px * comp);
        if (!cbuf) return false;
        for (int y = 0; y < pix->h; y++) {
            memcpy(cbuf + (size_t)y * (size_t)exp_stride, pix->samples + (size_t)y * (size_t)pix->stride, (size_t)exp_stride);
        }
        samples = cbuf;
    }

    sixel_output_t *out = nullptr;
    if (!SIXEL_SUCCEEDED(sixel_output_new(&out, sixel_write_cb, stdout, nullptr))) {
        free(cbuf);
        return false;
    }

    sixel_dither_t *dither = nullptr;
    if (!SIXEL_SUCCEEDED(sixel_dither_new(&dither, 256, nullptr))) {
        sixel_output_unref(out);
        free(cbuf);
        return false;
    }

    if (!SIXEL_SUCCEEDED(sixel_dither_initialize(dither, (unsigned char *)samples, pix->w, pix->h, fmt, SIXEL_LARGE_AUTO, SIXEL_REP_AUTO, SIXEL_QUALITY_LOW))) {
        sixel_dither_unref(dither);
        sixel_output_unref(out);
        free(cbuf);
        return false;
    }

    unsigned char *palette = sixel_dither_get_palette(dither);
    int num_colors = sixel_dither_get_num_of_palette_colors(dither);
    if (!palette || num_colors <= 0) {
        bool ok = SIXEL_SUCCEEDED(sixel_encode((unsigned char *)samples, pix->w, pix->h, 0, dither, out));
        sixel_dither_unref(dither);
        sixel_output_unref(out);
        free(cbuf);
        fflush(stdout);
        return ok;
    }

    unsigned char *indexed = (unsigned char *)malloc(total_px);
    if (!indexed) {
        sixel_dither_unref(dither);
        sixel_output_unref(out);
        free(cbuf);
        return false;
    }

    // Fast color quantization cache (15-bit RGB direct-mapped table, 64KB on stack)
    uint16_t lut[32768];
    memset(lut, 0, sizeof(lut));

    uint8_t bg_r = samples[0];
    uint8_t bg_g = (comp >= 3) ? samples[1] : samples[0];
    uint8_t bg_b = (comp >= 3) ? samples[2] : samples[0];
    uint8_t bg_idx = 0;
    int min_d = INT_MAX;
    for (int i = 0; i < num_colors; i++) {
        int dr = (int)bg_r - (int)palette[i * 3];
        int dg = (int)bg_g - (int)palette[i * 3 + 1];
        int db = (int)bg_b - (int)palette[i * 3 + 2];
        int dist = dr * dr + dg * dg + db * db;
        if (dist < min_d) {
            min_d = dist;
            bg_idx = (uint8_t)i;
            if (dist == 0) break;
        }
    }
    uint16_t bg_key = (uint16_t)((((uint16_t)(bg_r >> 3)) << 10) | (((uint16_t)(bg_g >> 3)) << 5) | ((uint16_t)(bg_b >> 3)));
    lut[bg_key] = (uint16_t)(0x8000 | bg_idx);

    for (size_t i = 0; i < total_px; i++) {
        uint8_t r = samples[i * comp];
        uint8_t g = (comp >= 3) ? samples[i * comp + 1] : r;
        uint8_t b = (comp >= 3) ? samples[i * comp + 2] : r;

        if (r == bg_r && g == bg_g && b == bg_b) {
            indexed[i] = bg_idx;
            continue;
        }

        uint16_t key = (uint16_t)((((uint16_t)(r >> 3)) << 10) | (((uint16_t)(g >> 3)) << 5) | ((uint16_t)(b >> 3)));
        if (lut[key] & 0x8000) {
            indexed[i] = (uint8_t)(lut[key] & 0xFF);
        } else {
            int best_dist = INT_MAX;
            uint8_t best_idx = 0;
            for (int c = 0; c < num_colors; c++) {
                int dr = (int)r - (int)palette[c * 3];
                int dg = (int)g - (int)palette[c * 3 + 1];
                int db = (int)b - (int)palette[c * 3 + 2];
                int dist = dr * dr + dg * dg + db * db;
                if (dist < best_dist) {
                    best_dist = dist;
                    best_idx = (uint8_t)c;
                    if (dist == 0) break;
                }
            }
            lut[key] = (uint16_t)(0x8000 | best_idx);
            indexed[i] = best_idx;
        }
    }

    sixel_dither_set_pixelformat(dither, SIXEL_PIXELFORMAT_PAL8);
    sixel_dither_set_diffusion_type(dither, SIXEL_DIFFUSE_NONE);
    bool ok = SIXEL_SUCCEEDED(sixel_encode(indexed, pix->w, pix->h, 0, dither, out));

    sixel_dither_unref(dither);
    sixel_output_unref(out);
    free(indexed);
    free(cbuf);
    fflush(stdout);
    return ok;
}
