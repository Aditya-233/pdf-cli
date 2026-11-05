#include "pdfcli.h"
#include <string.h>
#include <math.h>

int utf8_codepoint_width(uint32_t cp) {
    if (cp < 32 || (cp >= 0x7F && cp < 0xA0)) return 0;
    // Zero-width combining characters
    if ((cp >= 0x0300 && cp <= 0x036F) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) ||
        (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE20 && cp <= 0xFE2F))
        return 0;
    // East Asian Wide / Fullwidth characters & Emoji
    if ((cp >= 0x1100 && cp <= 0x115F) ||
        cp == 0x2329 || cp == 0x232A ||
        (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE10 && cp <= 0xFE19) ||
        (cp >= 0xFE30 && cp <= 0xFE6F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1F64F) ||
        (cp >= 0x1F900 && cp <= 0x1F9FF) ||
        (cp >= 0x20000 && cp <= 0x3FFFD))
        return 2;
    return 1;
}

static int utf8_next_codepoint(const char *s, size_t len, size_t *idx, uint32_t *out_cp) {
    if (*idx >= len) return 0;
    unsigned char c = (unsigned char)s[*idx];
    if (c < 0x80) {
        *out_cp = c;
        (*idx)++;
        return 1;
    } else if ((c & 0xE0) == 0xC0) {
        if (*idx + 1 >= len) { (*idx)++; *out_cp = '?'; return 1; }
        unsigned char c2 = (unsigned char)s[*idx + 1];
        if ((c2 & 0xC0) != 0x80) { (*idx)++; *out_cp = '?'; return 1; }
        *out_cp = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(c2 & 0x3F);
        *idx += 2;
        return 2;
    } else if ((c & 0xF0) == 0xE0) {
        if (*idx + 2 >= len) { (*idx)++; *out_cp = '?'; return 1; }
        unsigned char c2 = (unsigned char)s[*idx + 1];
        unsigned char c3 = (unsigned char)s[*idx + 2];
        if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) { (*idx)++; *out_cp = '?'; return 1; }
        *out_cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(c2 & 0x3F) << 6) | (uint32_t)(c3 & 0x3F);
        *idx += 3;
        return 3;
    } else if ((c & 0xF8) == 0xF0) {
        if (*idx + 3 >= len) { (*idx)++; *out_cp = '?'; return 1; }
        unsigned char c2 = (unsigned char)s[*idx + 1];
        unsigned char c3 = (unsigned char)s[*idx + 2];
        unsigned char c4 = (unsigned char)s[*idx + 3];
        if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80 || (c4 & 0xC0) != 0x80) { (*idx)++; *out_cp = '?'; return 1; }
        *out_cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(c2 & 0x3F) << 12) | ((uint32_t)(c3 & 0x3F) << 6) | (uint32_t)(c4 & 0x3F);
        *idx += 4;
        return 4;
    }
    (*idx)++;
    *out_cp = '?';
    return 1;
}

int utf8_str_width(const char *s) {
    if (!s) return 0;
    size_t len = strlen(s);
    size_t idx = 0;
    int total_w = 0;
    while (idx < len) {
        uint32_t cp = 0;
        utf8_next_codepoint(s, len, &idx, &cp);
        total_w += utf8_codepoint_width(cp);
    }
    return total_w;
}

size_t sanitize_terminal_string(const char *src, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return 0;
    if (!src) { dst[0] = '\0'; return 0; }

    size_t in_len = strlen(src);
    size_t in_idx = 0;
    size_t out_idx = 0;

    while (in_idx < in_len && out_idx + 1 < dst_size) {
        size_t prev_idx = in_idx;
        uint32_t cp = 0;
        utf8_next_codepoint(src, in_len, &in_idx, &cp);

        // Sanitize control characters (including ESC 0x1B, BEL 0x07, \r, \n, \t, etc.)
        if (cp < 32 || cp == 127 || (cp >= 0x7F && cp < 0xA0)) {
            dst[out_idx++] = ' ';
        } else if (cp < 0x80) {
            dst[out_idx++] = (char)cp;
        } else {
            // Valid multibyte UTF-8 codepoint
            size_t cp_bytes = in_idx - prev_idx;
            if (out_idx + cp_bytes < dst_size) {
                memcpy(dst + out_idx, src + prev_idx, cp_bytes);
                out_idx += cp_bytes;
            } else {
                break;
            }
        }
    }
    dst[out_idx] = '\0';
    return out_idx;
}

size_t utf8_truncate_by_width(const char *src, int max_cols, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return 0;
    if (!src || max_cols <= 0) { dst[0] = '\0'; return 0; }

    size_t in_len = strlen(src);
    size_t in_idx = 0;
    size_t out_idx = 0;
    int cols_used = 0;

    while (in_idx < in_len && out_idx + 1 < dst_size) {
        size_t prev_idx = in_idx;
        uint32_t cp = 0;
        utf8_next_codepoint(src, in_len, &in_idx, &cp);
        int w = utf8_codepoint_width(cp);
        if (cols_used + w > max_cols) break;
        cols_used += w;

        size_t bytes = in_idx - prev_idx;
        if (out_idx + bytes < dst_size) {
            memcpy(dst + out_idx, src + prev_idx, bytes);
            out_idx += bytes;
        } else {
            break;
        }
    }
    dst[out_idx] = '\0';
    return out_idx;
}

int checked_scale_to_int(double base, double scale, int min_val, int max_val) {
    if (!isnan(base) && !isinf(base) && !isnan(scale) && !isinf(scale)) {
        double val = base * scale + 0.5;
        if (val < (double)min_val) return min_val;
        if (val > (double)max_val) return max_val;
        return (int)val;
    }
    return min_val;
}
