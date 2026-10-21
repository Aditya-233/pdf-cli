#define _GNU_SOURCE 1
#define _DEFAULT_SOURCE 1
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <sys/stat.h>
#include <limits.h>
#include <time.h>

#include <mupdf/fitz.h>
#include <sixel.h>

#include "pdfcli/types.h"
#include "pdfcli/term_raw.h"
#include "pdfcli/term_gfx.h"
#include "pdfcli/profile.h"

static constexpr char B64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const unsigned char g_probe_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x00, 0x02, 0x00,
    0x00, 0x05, 0x00, 0x01, 0x7a, 0x5e, 0xab, 0x3f, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};

static _Atomic uint32_t g_kitty_image_id = 0;

static char *base64_encode(const unsigned char *src, size_t len, size_t *out_len) {
    if (!src) {
        if (out_len) *out_len = 0;
        return nullptr;
    }
    size_t enc_len = ((len + 2) / 3) * 4;
    char *out = malloc(enc_len + 1);
    if (!out) {
        if (out_len) *out_len = 0;
        return nullptr;
    }
    size_t s = 0, d = 0;

    // Fast path: load 4 bytes (contains 3 bytes to encode) using 32-bit register packing and bit shifts
    while (s + 4 <= len) {
        uint32_t raw;
        memcpy(&raw, src + s, sizeof(uint32_t));
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        uint32_t n = __builtin_bswap32(raw) >> 8;
#else
        uint32_t n = raw >> 8;
#endif
        uint32_t c0 = (uint32_t)(uint8_t)B64_CHARS[(n >> 18) & 0x3F];
        uint32_t c1 = (uint32_t)(uint8_t)B64_CHARS[(n >> 12) & 0x3F];
        uint32_t c2 = (uint32_t)(uint8_t)B64_CHARS[(n >> 6) & 0x3F];
        uint32_t c3 = (uint32_t)(uint8_t)B64_CHARS[n & 0x3F];
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        uint32_t packed = c0 | (c1 << 8) | (c2 << 16) | (c3 << 24);
#else
        uint32_t packed = (c0 << 24) | (c1 << 16) | (c2 << 8) | c3;
#endif
        memcpy(out + d, &packed, sizeof(uint32_t));
        s += 3;
        d += 4;
    }

    // Process remaining full 3-byte block safely without out-of-bounds 32-bit read
    while (s + 3 <= len) {
        uint32_t n = ((uint32_t)src[s] << 16) | ((uint32_t)src[s + 1] << 8) | (uint32_t)src[s + 2];
        uint32_t c0 = (uint32_t)(uint8_t)B64_CHARS[(n >> 18) & 0x3F];
        uint32_t c1 = (uint32_t)(uint8_t)B64_CHARS[(n >> 12) & 0x3F];
        uint32_t c2 = (uint32_t)(uint8_t)B64_CHARS[(n >> 6) & 0x3F];
        uint32_t c3 = (uint32_t)(uint8_t)B64_CHARS[n & 0x3F];
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        uint32_t packed = c0 | (c1 << 8) | (c2 << 16) | (c3 << 24);
#else
        uint32_t packed = (c0 << 24) | (c1 << 16) | (c2 << 8) | c3;
#endif
        memcpy(out + d, &packed, sizeof(uint32_t));
        s += 3;
        d += 4;
    }

    // Tail remainder: 1 or 2 bytes
    if (s < len) {
        size_t rem = len - s;
        uint32_t b0 = src[s];
        uint32_t b1 = (rem > 1) ? src[s + 1] : 0;
        uint32_t n = (b0 << 16) | (b1 << 8);
        out[d++] = B64_CHARS[(n >> 18) & 0x3F];
        out[d++] = B64_CHARS[(n >> 12) & 0x3F];
        out[d++] = (rem > 1) ? B64_CHARS[(n >> 6) & 0x3F] : '=';
        out[d++] = '=';
    }
    out[d] = '\0';
    if (out_len) *out_len = d;
    return out;
}

static unsigned char *read_file_bytes(const char *path, size_t *out_size) {
    if (!path || !out_size) return nullptr;
    *out_size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return nullptr;
    }
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return nullptr;
    }
    *out_size = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[*out_size] = '\0';
    return buf;
}

static inline void move_cursor(int col, int row) {
    if (row > 0 && col > 0) printf("\033[%d;%dH", row, col);
    else if (col > 0) printf("\033[%dG", col);
}


[[nodiscard]] term_gfx_context *term_gfx_init(void) {
    auto ctx = (term_gfx_context *)malloc(sizeof(term_gfx_context));
    if (!ctx) return nullptr;
    *ctx = (term_gfx_context){
        .proto = term_gfx_detect_protocol(),
        .kitty_mode = KITTY_XFER_DIRECT,
        .last_image_id = 0,
        .kitty_probed = false,
    };
    return ctx;
}

static sixel_output_t *s_sixel_out = nullptr;
static sixel_dither_t *s_sixel_dither = nullptr;

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} sixel_buf_t;

static sixel_buf_t s_cached_sixel = {};
static unsigned char *s_indexed_buf = nullptr;
static size_t s_indexed_cap = 0;

static int sixel_buf_write_cb(char *data, int size, void *priv) {
    if (size <= 0 || !data || !priv) return 0;
    sixel_buf_t *b = (sixel_buf_t *)priv;
    if (b->len + (size_t)size > b->cap) {
        size_t ncap = (b->cap == 0) ? 65536 : b->cap * 2;
        while (ncap < b->len + (size_t)size) ncap *= 2;
        char *nb = (char *)realloc(b->buf, ncap);
        if (!nb) return -1;
        b->buf = nb;
        b->cap = ncap;
    }
    memcpy(b->buf + b->len, data, (size_t)size);
    b->len += (size_t)size;
    return size;
}

static void term_gfx_sixel_cleanup(void) {
    if (s_sixel_dither) {
        sixel_dither_unref(s_sixel_dither);
        s_sixel_dither = nullptr;
    }
    if (s_sixel_out) {
        sixel_output_unref(s_sixel_out);
        s_sixel_out = nullptr;
    }
    free(s_cached_sixel.buf);
    s_cached_sixel.buf = nullptr;
    s_cached_sixel.len = 0;
    s_cached_sixel.cap = 0;
    free(s_indexed_buf);
    s_indexed_buf = nullptr;
    s_indexed_cap = 0;
}

void term_gfx_destroy(term_gfx_context *ctx) {
    if (!ctx) return;
    if (ctx->proto == GFX_KITTY && ctx->last_image_id != 0) {
        term_gfx_kitty_delete_image(ctx->last_image_id);
    }
    term_gfx_sixel_cleanup();
    free(ctx);
}

[[nodiscard]] term_gfx_proto term_gfx_detect_protocol(void) {
    const char *e = getenv("PDFCLI_GFX");
    if (!e) e = getenv("DOCVIEWER_GFX");
    if (e) {
        if (!strcasecmp(e, "kitty")) return GFX_KITTY;
        if (!strcasecmp(e, "sixel")) return GFX_SIXEL;
        if (!strcasecmp(e, "iterm2") || !strcasecmp(e, "iterm")) return GFX_ITERM2;
        if (!strcasecmp(e, "blocks") || !strcasecmp(e, "halfblocks")) return GFX_BLOCKS;
        if (!strcasecmp(e, "none")) return GFX_NONE;
    }
    if (getenv("KITTY_WINDOW_ID") || getenv("KITTY_PID") || getenv("GHOSTTY_RESOURCES_DIR")) return GFX_KITTY;
    const char *tp = getenv("TERM_PROGRAM"), *te = getenv("TERM");
    if (tp) {
        if (!strcasecmp(tp, "ghostty") || !strcasecmp(tp, "kitty") || !strcasecmp(tp, "WezTerm")) return GFX_KITTY;
        if (!strcasecmp(tp, "iTerm.app")) return GFX_ITERM2;
        if (!strcasecmp(tp, "foot")) return GFX_SIXEL;
        if (!strcasecmp(tp, "Apple_Terminal")) return GFX_BLOCKS;
    }
    if (te) {
        if (strstr(te, "kitty") || strstr(te, "ghostty") || strstr(te, "wezterm")) return GFX_KITTY;
        if (strstr(te, "foot") || strstr(te, "sixel") || strstr(te, "mlterm") || strstr(te, "xterm")) return GFX_SIXEL;
        if (strstr(te, "iterm")) return GFX_ITERM2;
    }
    return GFX_BLOCKS;
}

void term_gfx_probe_kitty_mode(term_gfx_context *ctx, const char *temp_dir) {
    if (!ctx || ctx->kitty_probed) return;
    const char *ov = getenv("DOCVIEWER_KITTY_XFER");
    if (ov) {
        ctx->kitty_mode = (strcmp(ov, "file") == 0) ? KITTY_XFER_FILE : KITTY_XFER_DIRECT;
        ctx->kitty_probed = true;
        return;
    }
    if (ctx->proto != GFX_KITTY) {
        ctx->kitty_probed = true;
        return;
    }
    const char *dir = (temp_dir && temp_dir[0]) ? temp_dir : "/tmp";
    mkdir(dir, 0755);
    char pp[PATH_MAX] = {}, ap[PATH_MAX] = {};
    snprintf(pp, sizeof(pp), "%s/probe.png", dir);
    FILE *fp = fopen(pp, "wb");
    if (fp) {
        fwrite(g_probe_png, 1, sizeof(g_probe_png), fp);
        fclose(fp);
    }
    if (!realpath(pp, ap)) {
        ctx->kitty_mode = KITTY_XFER_DIRECT;
        ctx->kitty_probed = true;
        return;
    }
    char *b64 = base64_encode((const unsigned char *)ap, strlen(ap), nullptr);
    if (!b64) {
        ctx->kitty_mode = KITTY_XFER_DIRECT;
        ctx->kitty_probed = true;
        return;
    }
    int fd = open("/dev/tty", O_RDWR | O_NOCTTY);
    if (fd < 0) {
        free(b64);
        ctx->kitty_mode = KITTY_XFER_DIRECT;
        ctx->kitty_probed = true;
        return;
    }
    struct termios oldt = {}, rawt = {};
    tcgetattr(fd, &oldt);
    rawt = oldt;
    cfmakeraw(&rawt);
    rawt.c_cc[VMIN] = 0; rawt.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &rawt);
    uint32_t pid = term_gfx_next_image_id();
    dprintf(fd, "\033_Ga=q,f=100,t=f,i=%u;%s\033\\", pid, b64);
    tcdrain(fd);
    free(b64);
    char resp[512] = {};
    struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
    if (poll(&pfd, 1, 300) > 0 && (pfd.revents & POLLIN)) {
        ssize_t n = read(fd, resp, sizeof(resp) - 1);
        if (n > 0) resp[n] = '\0';
    }
    tcsetattr(fd, TCSANOW, &oldt);
    close(fd);
    ctx->kitty_mode = strstr(resp, ";OK") ? KITTY_XFER_FILE : KITTY_XFER_DIRECT;
    ctx->kitty_probed = true;
}

[[nodiscard]] uint32_t term_gfx_next_image_id(void) {
    uint32_t cur = atomic_load_explicit(&g_kitty_image_id, memory_order_relaxed);
    if (cur == 0) {
        uint32_t seed = ((uint32_t)getpid() << 16) | ((uint32_t)time(nullptr) & 0xFFFF) | 1U;
        uint32_t exp = 0;
        atomic_compare_exchange_strong_explicit(&g_kitty_image_id, &exp, seed, memory_order_acq_rel, memory_order_relaxed);
    }
    return atomic_fetch_add_explicit(&g_kitty_image_id, 1, memory_order_acq_rel) + 1;
}

[[nodiscard]] term_gfx_proto term_gfx_get_proto(const term_gfx_context *ctx) { return ctx ? ctx->proto : GFX_NONE; }
void term_gfx_set_proto(term_gfx_context *ctx, term_gfx_proto proto) { if (ctx) ctx->proto = proto; }
[[nodiscard]] kitty_xfer_mode term_gfx_get_kitty_mode(const term_gfx_context *ctx) { return ctx ? ctx->kitty_mode : KITTY_XFER_DIRECT; }
void term_gfx_set_kitty_mode(term_gfx_context *ctx, kitty_xfer_mode mode) { if (ctx) ctx->kitty_mode = mode; }
[[nodiscard]] uint32_t term_gfx_get_last_image_id(const term_gfx_context *ctx) { return ctx ? ctx->last_image_id : 0; }

void term_gfx_clear_last_image(term_gfx_context *ctx) {
    if (!ctx) return;
    if (ctx->proto == GFX_KITTY && ctx->last_image_id != 0) term_gfx_kitty_delete_image(ctx->last_image_id);
    ctx->last_image_id = 0;
}

[[nodiscard]] bool term_gfx_draw_kitty_png_mem(uint32_t id, const unsigned char *bytes, size_t sz, int col, int row, int wc, int hc) {
    PROFILE_SCOPE("gfx", "term_gfx_kitty_png_mem");
    if (!bytes || sz == 0) return false;
    PROFILE_EVENT("gfx", "kitty_png_bytes", "bytes", (int64_t)sz);
    size_t b64_len = 0;
    char *b64 = base64_encode(bytes, sz, &b64_len);
    if (!b64) return false;
    PROFILE_EVENT("gfx", "kitty_b64_bytes", "bytes", (int64_t)b64_len);
    move_cursor(col, row);
    constexpr size_t CHUNK = 4096;
    for (size_t off = 0; off < b64_len; off += CHUNK) {
        size_t clen = (b64_len - off > CHUNK) ? CHUNK : (b64_len - off);
        int more = (off + clen < b64_len) ? 1 : 0;
        if (off == 0) printf("\033_Ga=T,f=100,i=%u,c=%d,r=%d,q=2,m=%d;%.*s\033\\", id, wc, hc, more, (int)clen, b64 + off);
        else printf("\033_Gm=%d,q=2;%.*s\033\\", more, (int)clen, b64 + off);
    }
    PROFILE_START("gfx", "stdout_flush");
    fflush(stdout);
    PROFILE_END("gfx", "stdout_flush");
    free(b64);
    return true;
}

[[nodiscard]] bool term_gfx_draw_kitty_png_file(uint32_t id, const char *path, int col, int row, int wc, int hc, kitty_xfer_mode mode) {
    PROFILE_SCOPE("gfx", "term_gfx_kitty_png_file");
    if (!path || !*path) return false;
    if (mode == KITTY_XFER_FILE) {
        char rpath[PATH_MAX];
        if (realpath(path, rpath)) {
            char *b64 = base64_encode((const unsigned char *)rpath, strlen(rpath), nullptr);
            if (b64) {
                move_cursor(col, row);
                printf("\033_Ga=T,f=100,t=f,c=%d,r=%d,i=%u,q=2;%s\033\\", wc, hc, id, b64);
                PROFILE_START("gfx", "stdout_flush");
                fflush(stdout);
                PROFILE_END("gfx", "stdout_flush");
                free(b64);
                return true;
            }
        }
    }
    size_t sz = 0;
    unsigned char *b = read_file_bytes(path, &sz);
    if (!b) return false;
    bool ok = term_gfx_draw_kitty_png_mem(id, b, sz, col, row, wc, hc);
    free(b);
    return ok;
}

void term_gfx_kitty_delete_image(uint32_t id) {
    if (id != 0) {
        printf("\033_Ga=d,d=I,i=%u\033\\", id);
        fflush(stdout);
    }
}

[[nodiscard]] bool term_gfx_draw_sixel(const fz_pixmap *pix, int col, int row, [[maybe_unused]] int wp, [[maybe_unused]] int hp) {
    PROFILE_SCOPE("gfx", "term_gfx_draw_sixel");
    if (!pix || !pix->samples || pix->w <= 0 || pix->h <= 0) return false;
    PROFILE_EVENT("gfx", "sixel_pixels", "pixels", (int64_t)pix->w * pix->h);

    move_cursor(col, row);
    PROFILE_START("gfx", "stdout_flush");
    fflush(stdout);
    PROFILE_END("gfx", "stdout_flush");

    int fmt = (pix->n == 4) ? SIXEL_PIXELFORMAT_RGBA8888 : ((pix->n == 3) ? SIXEL_PIXELFORMAT_RGB888 : ((pix->n == 2) ? SIXEL_PIXELFORMAT_GA88 : SIXEL_PIXELFORMAT_G8));
    const unsigned char *samples = pix->samples;
    unsigned char *cbuf = nullptr;
    ptrdiff_t exp_stride = (ptrdiff_t)pix->w * (ptrdiff_t)pix->n;
    if (pix->stride != exp_stride) {
        cbuf = malloc((size_t)pix->w * (size_t)pix->h * (size_t)pix->n);
        if (!cbuf) return false;
        for (int y = 0; y < pix->h; y++) {
            memcpy(cbuf + (size_t)y * exp_stride, pix->samples + (size_t)y * (size_t)pix->stride, exp_stride);
        }
        samples = cbuf;
    }

    s_cached_sixel.len = 0;
    if (s_sixel_out) {
        sixel_output_unref(s_sixel_out);
        s_sixel_out = nullptr;
    }
    if (!SIXEL_SUCCEEDED(sixel_output_new(&s_sixel_out, sixel_buf_write_cb, &s_cached_sixel, nullptr))) {
        free(cbuf);
        return false;
    }
    if (!s_sixel_dither) {
        if (!SIXEL_SUCCEEDED(sixel_dither_new(&s_sixel_dither, 256, nullptr))) {
            free(cbuf);
            return false;
        }
    }

    if (!SIXEL_SUCCEEDED(sixel_dither_initialize(s_sixel_dither, (unsigned char *)samples, pix->w, pix->h, fmt, SIXEL_LARGE_AUTO, SIXEL_REP_AUTO, SIXEL_QUALITY_LOW))) {
        free(cbuf);
        return false;
    }

    unsigned char *palette = sixel_dither_get_palette(s_sixel_dither);
    int num_colors = sixel_dither_get_num_of_palette_colors(s_sixel_dither);
    if (!palette || num_colors <= 0) {
        PROFILE_START("gfx", "sixel_encode");
        bool ok = SIXEL_SUCCEEDED(sixel_encode((unsigned char *)samples, pix->w, pix->h, 0, s_sixel_dither, s_sixel_out));
        PROFILE_END("gfx", "sixel_encode");
        free(cbuf);
        if (ok && s_cached_sixel.len > 0) {
            PROFILE_START("gfx", "stdout_flush");
            fwrite(s_cached_sixel.buf, 1, s_cached_sixel.len, stdout);
            fflush(stdout);
            PROFILE_END("gfx", "stdout_flush");
        }
        return ok;
    }

    size_t total_px = (size_t)pix->w * (size_t)pix->h;
    if (total_px > s_indexed_cap) {
        size_t ncap = total_px + 65536;
        unsigned char *nb = (unsigned char *)realloc(s_indexed_buf, ncap);
        if (!nb) {
            free(cbuf);
            return false;
        }
        s_indexed_buf = nb;
        s_indexed_cap = ncap;
    }
    unsigned char *indexed = s_indexed_buf;

    // Fast color quantization lookup cache (15-bit RGB direct-mapped table, 64KB on stack)
    uint16_t lut[32768];
    memset(lut, 0, sizeof(lut));

    // Detect background color at top-left corner (0,0)
    uint8_t bg_r = samples[0];
    uint8_t bg_g = (pix->n >= 3) ? samples[1] : samples[0];
    uint8_t bg_b = (pix->n >= 3) ? samples[2] : samples[0];
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

    size_t comp = (size_t)pix->n;
    for (size_t i = 0; i < total_px; i++) {
        uint8_t r, g, b;
        if (comp >= 3) {
            r = samples[i * comp];
            g = samples[i * comp + 1];
            b = samples[i * comp + 2];
        } else {
            r = g = b = samples[i * comp];
        }

        // Fast path: repeated background / margin pixels bypass Floyd-Steinberg diffusion
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

    sixel_dither_set_pixelformat(s_sixel_dither, SIXEL_PIXELFORMAT_PAL8);
    sixel_dither_set_diffusion_type(s_sixel_dither, SIXEL_DIFFUSE_NONE);
    PROFILE_START("gfx", "sixel_encode");
    bool ok = SIXEL_SUCCEEDED(sixel_encode(indexed, pix->w, pix->h, 0, s_sixel_dither, s_sixel_out));
    PROFILE_END("gfx", "sixel_encode");
    free(cbuf);

    if (ok && s_cached_sixel.len > 0) {
        PROFILE_START("gfx", "stdout_flush");
        fwrite(s_cached_sixel.buf, 1, s_cached_sixel.len, stdout);
        fflush(stdout);
        PROFILE_END("gfx", "stdout_flush");
    }
    return ok;
}

[[nodiscard]] bool term_gfx_draw_iterm2(const unsigned char *bytes, size_t sz, int col, int row, int wc, int hc) {
    if (!bytes || sz == 0) return false;
    char *b64 = base64_encode(bytes, sz, nullptr);
    if (!b64) return false;
    move_cursor(col, row);
    if (wc > 0 && hc > 0) printf("\033]1337;File=inline=1;width=%d;height=%d:%s\a", wc, hc, b64);
    else if (wc > 0) printf("\033]1337;File=inline=1;width=%d:%s\a", wc, b64);
    else if (hc > 0) printf("\033]1337;File=inline=1;height=%d:%s\a", hc, b64);
    else printf("\033]1337;File=inline=1:%s\a", b64);
    fflush(stdout);
    free(b64);
    return true;
}

[[nodiscard]] bool term_gfx_draw_halfblocks(const fz_pixmap *pix, int col, int row, int max_cols, int max_rows) {
    PROFILE_SCOPE("gfx", "term_gfx_halfblocks");
    if (!pix || !pix->samples || pix->w <= 0 || pix->h <= 0) return false;
    PROFILE_EVENT("gfx", "halfblocks_pixels", "pixels", (int64_t)pix->w * pix->h);
    int nc = (max_cols > 0 && pix->w > max_cols) ? max_cols : pix->w;
    int nr = (pix->h + 1) / 2;
    if (max_rows > 0 && nr > max_rows) nr = max_rows;
    if (nc <= 0 || nr <= 0) return false;

    int *px_map = (int *)malloc((size_t)nc * sizeof(int));
    if (!px_map) return false;
    for (int cx = 0; cx < nc; cx++) {
        px_map[cx] = (int)((int64_t)cx * pix->w / nc);
    }

    // Row buffer: each cell takes at most ~40 bytes when colors change, or 3 bytes (▀) when colors repeat.
    // Plus cursor move and reset escape sequences.
    size_t buf_cap = (size_t)nc * 48 + 512;
    char *line_buf = (char *)malloc(buf_cap);
    if (!line_buf) {
        free(px_map);
        return false;
    }

    for (int cy = 0; cy < nr; cy++) {
        int py_t = (int)((int64_t)(cy * 2) * pix->h / (nr * 2));
        int py_b = (int)((int64_t)(cy * 2 + 1) * pix->h / (nr * 2));
        if (py_b >= pix->h) py_b = pix->h - 1;

        const unsigned char *row_t = pix->samples + ((size_t)py_t * (size_t)pix->stride);
        const unsigned char *row_b = pix->samples + ((size_t)py_b * (size_t)pix->stride);

        int cur_col = col > 0 ? col : 1;
        int cur_row = row > 0 ? row + cy : cy + 1;

        size_t len = 0;
        if (cur_row > 0 && cur_col > 0) {
            len += (size_t)snprintf(line_buf + len, buf_cap - len, "\033[%d;%dH", cur_row, cur_col);
        } else if (cur_col > 0) {
            len += (size_t)snprintf(line_buf + len, buf_cap - len, "\033[%dG", cur_col);
        }

        uint32_t cur_fg = 0xFFFFFFFF; // Invalid 24-bit RGB sentinel
        uint32_t cur_bg = 0xFFFFFFFF;

        for (int cx = 0; cx < nc; cx++) {
            int px = px_map[cx];
            uint8_t tr, tg, tb, br, bg, bb;
            if (pix->n >= 3) {
                size_t off = (size_t)px * (size_t)pix->n;
                tr = row_t[off];     tg = row_t[off + 1]; tb = row_t[off + 2];
                br = row_b[off];     bg = row_b[off + 1]; bb = row_b[off + 2];
            } else {
                size_t off = (size_t)px * (size_t)pix->n;
                tr = tg = tb = row_t[off];
                br = bg = bb = row_b[off];
            }

            uint32_t fg = ((uint32_t)tr << 16) | ((uint32_t)tg << 8) | tb;
            uint32_t bg_val = ((uint32_t)br << 16) | ((uint32_t)bg << 8) | bb;

            // Only emit foreground RGB escape sequence if color actually changed
            if (fg != cur_fg) {
                len += (size_t)snprintf(line_buf + len, buf_cap - len, "\033[38;2;%u;%u;%um", (unsigned int)tr, (unsigned int)tg, (unsigned int)tb);
                cur_fg = fg;
            }
            // Only emit background RGB escape sequence if color actually changed
            if (bg_val != cur_bg) {
                len += (size_t)snprintf(line_buf + len, buf_cap - len, "\033[48;2;%u;%u;%um", (unsigned int)br, (unsigned int)bg, (unsigned int)bb);
                cur_bg = bg_val;
            }
            // Emit Unicode upper half block ▀ (UTF-8: 0xE2 0x96 0x80)
            if (len + 3 < buf_cap) {
                line_buf[len++] = (char)0xE2;
                line_buf[len++] = (char)0x96;
                line_buf[len++] = (char)0x80;
            }
        }

        // Reset attributes at end of row
        if (len + 6 < buf_cap) {
            memcpy(line_buf + len, "\033[0m\n", 5);
            len += 5;
        }
        fwrite(line_buf, 1, len, stdout);
    }

    free(line_buf);
    free(px_map);
    PROFILE_START("gfx", "stdout_flush");
    fflush(stdout);
    PROFILE_END("gfx", "stdout_flush");
    return true;
}

void term_gfx_clear(term_gfx_context *ctx) {
    if (!ctx) return;
    if (ctx->proto == GFX_KITTY) {
        fputs("\033_Ga=d,d=a\033\\", stdout);
        fflush(stdout);
        ctx->last_image_id = 0;
    }
}

[[nodiscard]] bool term_gfx_draw_pixmap(term_gfx_context *ctx, fz_context *fzctx, fz_pixmap *pix, const char *path, int col, int row, int wc, int hc, int wp, int hp) {
    PROFILE_SCOPE("gfx", "term_gfx_draw_pixmap");
    if (!ctx) return false;
    PROFILE_EVENT("gfx", "draw_pixmap_proto", "proto", (int64_t)ctx->proto);
    if (pix) {
        PROFILE_EVENT("gfx", "draw_pixmap_w", "w", pix->w);
        PROFILE_EVENT("gfx", "draw_pixmap_h", "h", pix->h);
    }
    if (ctx->proto == GFX_KITTY) {
        uint32_t nid = term_gfx_next_image_id();
        bool ok = false;
        if (path && path[0] && access(path, R_OK) == 0) ok = term_gfx_draw_kitty_png_file(nid, path, col, row, wc, hc, ctx->kitty_mode);
        if (!ok && fzctx && pix) {
            fz_buffer *buf = fz_new_buffer_from_pixmap_as_png(fzctx, pix, fz_default_color_params);
            if (buf) {
                unsigned char *data = nullptr;
                size_t sz = fz_buffer_storage(fzctx, buf, &data);
                ok = term_gfx_draw_kitty_png_mem(nid, data, sz, col, row, wc, hc);
                fz_drop_buffer(fzctx, buf);
            }
        }
        if (ok) {
            if (ctx->last_image_id != 0 && ctx->last_image_id != nid) term_gfx_kitty_delete_image(ctx->last_image_id);
            ctx->last_image_id = nid;
            return true;
        }
        return false;
    }
    if (ctx->proto == GFX_SIXEL && pix) return term_gfx_draw_sixel(pix, col, row, wp, hp);
    if (ctx->proto == GFX_ITERM2) {
        bool ok = false;
        if (path && path[0]) {
            size_t fsz = 0; unsigned char *b = read_file_bytes(path, &fsz);
            if (b) { ok = term_gfx_draw_iterm2(b, fsz, col, row, wc, hc); free(b); }
        } else if (fzctx && pix) {
            fz_buffer *buf = fz_new_buffer_from_pixmap_as_png(fzctx, pix, fz_default_color_params);
            if (buf) {
                unsigned char *data = nullptr;
                size_t sz = fz_buffer_storage(fzctx, buf, &data);
                ok = term_gfx_draw_iterm2(data, sz, col, row, wc, hc);
                fz_drop_buffer(fzctx, buf);
            }
        }
        return ok;
    }
    if (ctx->proto == GFX_BLOCKS && pix) return term_gfx_draw_halfblocks(pix, col, row, wc, hc);
    return false;
}
