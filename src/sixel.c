#include "pdfcli.h"
#include <sixel.h>
#include <limits.h>
#include <string.h>

static int sixel_write_cb(char *data, int size, void *priv) {
    return (int)fwrite(data, 1, (size_t)size, (FILE *)priv);
}

bool sixel_draw(const fz_pixmap *pix, int col, int row) {
    if (!pix || !pix->samples || pix->w <= 0 || pix->h <= 0) return false;
    if (row > 0 && col > 0) printf("\033[%d;%dH", row, col);
    else if (col > 0) printf("\033[%dG", col);
    fflush(stdout);

    int fmt = (pix->n >= 3) ? SIXEL_PIXELFORMAT_RGB888 : SIXEL_PIXELFORMAT_G8;
    size_t comp = (size_t)pix->n, total_px = (size_t)pix->w * (size_t)pix->h;
    const unsigned char *samples = pix->samples;
    unsigned char *cbuf = nullptr, *indexed = nullptr;
    sixel_output_t *out = nullptr;
    sixel_dither_t *dither = nullptr;
    bool ok = false;

    ptrdiff_t exp_stride = (ptrdiff_t)pix->w * (ptrdiff_t)pix->n;
    if (pix->stride != exp_stride) {
        if (!(cbuf = (unsigned char *)malloc(total_px * comp))) goto cleanup;
        for (int y = 0; y < pix->h; y++)
            memcpy(cbuf + (size_t)y * exp_stride, pix->samples + (size_t)y * (size_t)pix->stride, exp_stride);
        samples = cbuf;
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

    if (!(indexed = (unsigned char *)malloc(total_px))) goto cleanup;

    uint16_t lut[32768] = {0};
    uint8_t bg_r = samples[0], bg_g = (comp >= 3) ? samples[1] : bg_r, bg_b = (comp >= 3) ? samples[2] : bg_r;
    uint8_t bg_idx = 0;
    int min_d = INT_MAX;
    for (int i = 0; i < num_colors; i++) {
        int dr = (int)bg_r - palette[i * 3], dg = (int)bg_g - palette[i * 3 + 1], db = (int)bg_b - palette[i * 3 + 2];
        int d = dr * dr + dg * dg + db * db;
        if (d < min_d) { min_d = d; bg_idx = (uint8_t)i; if (d == 0) break; }
    }
    lut[((uint16_t)(bg_r >> 3) << 10) | ((uint16_t)(bg_g >> 3) << 5) | (bg_b >> 3)] = (uint16_t)(0x8000 | bg_idx);

    for (size_t i = 0; i < total_px; i++) {
        uint8_t r = samples[i * comp], g = (comp >= 3) ? samples[i * comp + 1] : r, b = (comp >= 3) ? samples[i * comp + 2] : r;
        if (r == bg_r && g == bg_g && b == bg_b) { indexed[i] = bg_idx; continue; }
        uint16_t key = (uint16_t)(((uint16_t)(r >> 3) << 10) | ((uint16_t)(g >> 3) << 5) | (b >> 3));
        if (lut[key] & 0x8000) {
            indexed[i] = (uint8_t)(lut[key] & 0xFF);
        } else {
            int bd = INT_MAX; uint8_t bi = 0;
            for (int c = 0; c < num_colors; c++) {
                int dr = (int)r - palette[c * 3], dg = (int)g - palette[c * 3 + 1], db = (int)b - palette[c * 3 + 2];
                int d = dr * dr + dg * dg + db * db;
                if (d < bd) { bd = d; bi = (uint8_t)c; if (d == 0) break; }
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
