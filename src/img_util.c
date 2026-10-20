#define _POSIX_C_SOURCE 200809L
#include "pdfcli/img_util.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void rgb_to_hsl(double r, double g, double b, double *h, double *s, double *l) {
    double max = fmax(r, fmax(g, b)), min = fmin(r, fmin(g, b));
    *l = (max + min) / 2.0;
    if (max == min) { *h = *s = 0.0; return; }
    double d = max - min;
    *s = (*l > 0.5) ? (d / (2.0 - max - min)) : (d / (max + min));
    if (max == r) *h = (g - b) / d + (g < b ? 6.0 : 0.0);
    else if (max == g) *h = (b - r) / d + 2.0;
    else *h = (r - g) / d + 4.0;
    *h /= 6.0;
}

static double hue_to_rgb(double p, double q, double t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0 / 2.0) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}

static void hsl_to_rgb(double h, double s, double l, double *r, double *g, double *b) {
    if (s == 0.0) { *r = *g = *b = l; return; }
    double q = (l < 0.5) ? (l * (1.0 + s)) : (l + s - l * s);
    double p = 2.0 * l - q;
    *r = hue_to_rgb(p, q, h + 1.0 / 3.0);
    *g = hue_to_rgb(p, q, h);
    *b = hue_to_rgb(p, q, h - 1.0 / 3.0);
}

[[nodiscard]] fz_pixmap *img_crop(fz_context *ctx, fz_pixmap *pix, const crop_box *crop) {
    if (!ctx || !pix) return nullptr;
    if (!crop || (crop->top == 0.0 && crop->bottom == 0.0 && crop->left == 0.0 && crop->right == 0.0))
        return fz_clone_pixmap(ctx, pix);
    int w = fz_pixmap_width(ctx, pix), h = fz_pixmap_height(ctx, pix);
    int x0 = (int)(w * crop->left), x1 = w - (int)(w * crop->right);
    int y0 = (int)(h * crop->top), y1 = h - (int)(h * crop->bottom);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x1 <= x0 || y1 <= y0) return fz_clone_pixmap(ctx, pix);
    int nw = x1 - x0, nh = y1 - y0;
    fz_pixmap *dst = nullptr;
    fz_try(ctx) {
        dst = fz_new_pixmap_with_bbox(ctx, fz_pixmap_colorspace(ctx, pix), fz_make_irect(0, 0, nw, nh), nullptr, fz_pixmap_alpha(ctx, pix));
        int n = fz_pixmap_components(ctx, pix);
        int src_stride = fz_pixmap_stride(ctx, pix), dst_stride = fz_pixmap_stride(ctx, dst);
        const unsigned char *src = fz_pixmap_samples(ctx, pix);
        unsigned char *dst_s = fz_pixmap_samples(ctx, dst);
        for (int y = 0; y < nh; y++)
            memcpy(dst_s + y * dst_stride, src + (y0 + y) * src_stride + x0 * n, (size_t)nw * (size_t)n);
    } fz_catch(ctx) { if (dst) fz_drop_pixmap(ctx, dst); dst = nullptr; }
    return dst;
}

static void apply_lut(fz_context *ctx, fz_pixmap *pix, const uint8_t lut[256]) {
    int w = fz_pixmap_width(ctx, pix), h = fz_pixmap_height(ctx, pix);
    int n = fz_pixmap_components(ctx, pix), stride = fz_pixmap_stride(ctx, pix);
    unsigned char *s = fz_pixmap_samples(ctx, pix);
    int channels = (n >= 3) ? 3 : 1;
    for (int y = 0; y < h; y++) {
        unsigned char *row = s + y * stride;
        for (int x = 0; x < w; x++) {
            unsigned char *p = row + x * n;
            for (int c = 0; c < channels; c++) p[c] = lut[p[c]];
        }
    }
}

void img_smart_invert(fz_context *ctx, fz_pixmap *pix) {
    if (!ctx || !pix) return;
    int w = fz_pixmap_width(ctx, pix), h = fz_pixmap_height(ctx, pix);
    int n = fz_pixmap_components(ctx, pix), stride = fz_pixmap_stride(ctx, pix);
    unsigned char *s = fz_pixmap_samples(ctx, pix);
    uint8_t lut[256];
    for (int v = 0; v < 256; v++) lut[v] = (uint8_t)((0.12 + (1.0 - (double)v / 255.0) * 0.88) * 255.0 + 0.5);
    if (n < 3) { apply_lut(ctx, pix, lut); return; }
    for (int y = 0; y < h; y++) {
        unsigned char *row = s + y * stride;
        for (int x = 0; x < w; x++) {
            unsigned char *p = row + x * n;
            if (p[0] == p[1] && p[1] == p[2]) { p[0] = p[1] = p[2] = lut[p[0]]; }
            else {
                double hh, ss, ll, nr, ng, nb;
                rgb_to_hsl(p[0] / 255.0, p[1] / 255.0, p[2] / 255.0, &hh, &ss, &ll);
                hsl_to_rgb(hh, ss, 0.12 + (1.0 - ll) * 0.88, &nr, &ng, &nb);
                p[0] = (uint8_t)(nr * 255.0 + 0.5); p[1] = (uint8_t)(ng * 255.0 + 0.5); p[2] = (uint8_t)(nb * 255.0 + 0.5);
            }
        }
    }
}

void img_simple_invert(fz_context *ctx, fz_pixmap *pix) {
    if (!ctx || !pix) return;
    uint8_t lut[256];
    for (int v = 0; v < 256; v++) lut[v] = (uint8_t)(30 + (255 - v) * 225 / 255);
    apply_lut(ctx, pix, lut);
}

void img_dim_tint(fz_context *ctx, fz_pixmap *pix) {
    if (!ctx || !pix) return;
    uint8_t lut[256];
    for (int v = 0; v < 256; v++) lut[v] = (uint8_t)(120.0 * pow((double)v / 255.0, 2.2) + 0.5);
    apply_lut(ctx, pix, lut);
}

[[nodiscard]] unsigned char *img_save_png_to_memory(fz_context *ctx, fz_pixmap *pix, size_t *out_sz) {
    if (!out_sz) return nullptr;
    *out_sz = 0;
    if (!ctx || !pix) return nullptr;
    fz_buffer *buf = nullptr;
    unsigned char *res = nullptr;
    fz_var(buf);
    fz_var(res);
    fz_try(ctx) {
        buf = fz_new_buffer_from_pixmap_as_png(ctx, pix, fz_default_color_params);
        if (buf) {
            unsigned char *data = nullptr;
            size_t sz = fz_buffer_storage(ctx, buf, &data);
            if (sz > 0 && data) {
                res = (unsigned char *)malloc(sz);
                if (res) {
                    memcpy(res, data, sz);
                    *out_sz = sz;
                }
            }
        }
    } fz_always(ctx) {
        if (buf) fz_drop_buffer(ctx, buf);
    } fz_catch(ctx) {
        free(res);
        res = nullptr;
        *out_sz = 0;
    }
    return res;
}

[[nodiscard]] bool img_save_png_to_file(fz_context *ctx, fz_pixmap *pix, const char *path) {
    if (!ctx || !pix || !path) return false;
    bool ok = true;
    fz_try(ctx) { fz_save_pixmap_as_png(ctx, pix, path); } fz_catch(ctx) { ok = false; }
    return ok;
}
