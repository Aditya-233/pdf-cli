#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <malloc.h>
#include "pdfcli.h"

typedef struct {
    char* path;
    char* name;
    pdf_doc_t* doc;
    int page_count;
    float* page_pw;
    float* page_ph;
    int64_t* page_y;
    int* page_h;
    int64_t total_h;
    int64_t virtual_y;
    int64_t max_scroll;
    int64_t virtual_x;
    int64_t max_scroll_x;
    int zoom_pct;
    fit_mode_t fit;
    bool dark;
    int last_w;
    int last_h;
    int last_zoom_pct;
    fit_mode_t last_fit;
    bool layout_dirty;
    char search_query[64];
    search_hit_t* search_hits;
    size_t search_hit_count;
    size_t search_hit_idx;
} tab_t;

static const char* get_base_name(const char* path) {
    if (!path) return "document.pdf";
    const char* s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static void ensure_page_bounds(tab_t* t, int page) {
    if (!t || page < 0 || page >= t->page_count) return;
    if (t->page_pw[page] > 0.0f && t->page_ph[page] > 0.0f) return;

    fz_rect b = pdf_page_bounds(t->doc, page);
    float w = b.x1 - b.x0;
    float h = b.y1 - b.y0;
    if (w < 1.0f || w > 50000.0f) w = 612.0f;
    if (h < 1.0f || h > 50000.0f) h = 792.0f;
    if (t->page_pw[page] != w || t->page_ph[page] != h) {
        t->layout_dirty = true;
    }
    t->page_pw[page] = w;
    t->page_ph[page] = h;
}

static void tab_set_scroll(tab_t* t, int64_t y) {
    if (!t) return;
    if (y > t->max_scroll) y = t->max_scroll;
    if (y < 0) y = 0;
    t->virtual_y = y;
}

static int get_active_page(const tab_t* t) {
    if (!t || !t->page_y || !t->page_h || t->page_count <= 0) return 0;
    int content_h = t->last_h > 0 ? t->last_h : 600;
    int64_t v_top = t->virtual_y;
    int64_t v_bot = v_top + content_h;

    int best_page = 0;
    int64_t max_visible = -1;

    for (int i = 0; i < t->page_count; i++) {
        int64_t p_top = t->page_y[i];
        int64_t p_bot = p_top + t->page_h[i];
        if (p_bot <= v_top) continue;
        if (p_top >= v_bot) break;

        int64_t visible_top = (p_top > v_top) ? p_top : v_top;
        int64_t visible_bot = (p_bot < v_bot) ? p_bot : v_bot;
        int64_t visible_h = visible_bot - visible_top;
        if (visible_h > max_visible) {
            max_visible = visible_h;
            best_page = i;
        }
    }
    return best_page;
}

static int find_first_visible_page(const tab_t* t, int64_t vy) {
    if (!t || !t->page_y || !t->page_h || t->page_count <= 0) return 0;
    int low = 0, high = t->page_count - 1;
    int ans = 0;
    while (low <= high) {
        int mid = low + (high - low) / 2;
        if (t->page_y[mid] + t->page_h[mid] > vy) {
            ans = mid;
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }
    return ans;
}

static void next_page(tab_t* t) {
    if (!t || !t->page_y || t->page_count <= 0) return;
    for (int i = 0; i < t->page_count; i++) {
        if (t->page_y[i] > t->virtual_y + 10) {
            tab_set_scroll(t, t->page_y[i]);
            return;
        }
    }
    tab_set_scroll(t, t->max_scroll);
}

static void prev_page(tab_t* t) {
    if (!t || !t->page_y || t->page_count <= 0) return;
    for (int i = t->page_count - 1; i >= 0; i--) {
        if (t->page_y[i] < t->virtual_y - 10) {
            tab_set_scroll(t, t->page_y[i]);
            return;
        }
    }
    tab_set_scroll(t, 0);
}

static void render_top_hud(const tab_t* tabs, int tab_count, int active_tab, int cols) {
    if (tab_count <= 0 || active_tab < 0 || active_tab >= tab_count || cols < 10) return;
    printf("\033[1;1H\033[2m\033[K");

    int cur_col = 1;
    for (int i = 0; i < tab_count && cur_col < cols - 32; i++) {
        bool is_act = (i == active_tab);
        char safe_name[64];
        sanitize_terminal_string(tabs[i].name, safe_name, sizeof(safe_name));

        char trunc_name[32];
        utf8_truncate_by_width(safe_name, 14, trunc_name, sizeof(trunc_name));

        char lbl[64];
        snprintf(lbl, sizeof(lbl), " %d: %s%s ", i + 1, trunc_name, is_act ? "*" : "");
        int w = utf8_str_width(lbl);
        if (cur_col + w >= cols - 30) break;

        printf(is_act ? "\033[1;7m%s\033[0m\033[2m" : "\033[0;2m%s\033[0m\033[2m", lbl);
        cur_col += w;
    }

    const tab_t* act = &tabs[active_tab];
    char info[256];
    snprintf(info, sizeof(info), " | Page %d/%d | %d%% | %s%s",
             get_active_page(act) + 1, act->page_count, act->zoom_pct,
             act->fit == FIT_WIDTH ? "Width" : "Page", act->dark ? " | Dark" : "");

    if (act->search_query[0] != '\0') {
        char s_info[128];
        char safe_q[32];
        sanitize_terminal_string(act->search_query, safe_q, sizeof(safe_q));
        char trunc_q[32];
        utf8_truncate_by_width(safe_q, 20, trunc_q, sizeof(trunc_q));
        if (act->search_hit_count > 0) {
            snprintf(s_info, sizeof(s_info), " | [%zu/%zu] \"%s\"",
                     act->search_hit_idx + 1, act->search_hit_count, trunc_q);
        } else {
            snprintf(s_info, sizeof(s_info), " | [No match] \"%s\"", trunc_q);
        }
        size_t cur_len = strlen(info);
        strncat(info, s_info, sizeof(info) - cur_len - 1);
    }

    int info_w = utf8_str_width(info);
    if (cols > info_w) {
        printf("%s\033[0m", info);
    } else {
        char trunc_info[256];
        utf8_truncate_by_width(info, cols - 1, trunc_info, sizeof(trunc_info));
        printf("%s\033[0m", trunc_info);
    }
}

static int find_tab_by_col(const tab_t* tabs, int tab_count, int active_tab, int cols, int col) {
    int cur_col = 1;
    for (int i = 0; i < tab_count && cur_col < cols - 32; i++) {
        char safe_name[64];
        sanitize_terminal_string(tabs[i].name, safe_name, sizeof(safe_name));
        char trunc_name[32];
        utf8_truncate_by_width(safe_name, 14, trunc_name, sizeof(trunc_name));

        char lbl[64];
        snprintf(lbl, sizeof(lbl), " %d: %s%s ", i + 1, trunc_name, (i == active_tab) ? "*" : "");
        int w = utf8_str_width(lbl);
        if (cur_col + w >= cols - 30) break;
        if (col >= cur_col && col < cur_col + w) return i;
        cur_col += w;
    }
    return -1;
}

static bool update_tab_layout(tab_t* t, int total_w, int content_h) {
    if (!t || t->page_count <= 0 || total_w <= 0 || content_h <= 0) return false;

    if (!t->layout_dirty && t->page_y && t->page_h && t->last_w == total_w && t->last_h == content_h &&
        t->last_zoom_pct == t->zoom_pct && t->last_fit == t->fit) {
        tab_set_scroll(t, t->virtual_y);
        return true;
    }
    t->layout_dirty = false;

    if (!t->page_y || !t->page_h) {
        int64_t* new_y = (int64_t*)malloc(sizeof(int64_t) * (size_t)t->page_count);
        int* new_h = (int*)malloc(sizeof(int) * (size_t)t->page_count);
        if (!new_y || !new_h) {
            free(new_y);
            free(new_h);
            return false;
        }
        t->page_y = new_y;
        t->page_h = new_h;
        t->last_w = 0;
        t->last_h = 0;
    }

    int cur_p = 0;
    double rel_offset = 0.0;
    bool has_prev_layout = (t->last_w > 0 && t->last_h > 0 && t->page_h[0] > 0);
    if (has_prev_layout) {
        cur_p = find_first_visible_page(t, t->virtual_y);
        if (t->page_h[cur_p] > 0) {
            rel_offset = (double)(t->virtual_y - t->page_y[cur_p]) / (double)t->page_h[cur_p];
            if (rel_offset < 0.0) rel_offset = 0.0;
            if (rel_offset > 1.0) rel_offset = 1.0;
        }
    }

    int64_t cur_y = 0;
    const int GAP = 12;
    double zoom = (double)t->zoom_pct / 100.0;
    int max_page_pixel_w = 0;

    ensure_page_bounds(t, 0);
    float def_pw = (t->page_pw && t->page_pw[0] > 0.0f) ? t->page_pw[0] : 612.0f;
    float def_ph = (t->page_ph && t->page_ph[0] > 0.0f) ? t->page_ph[0] : 792.0f;

    for (int i = 0; i < t->page_count; i++) {
        float pw = (t->page_pw && t->page_pw[i] > 0.0f) ? t->page_pw[i] : def_pw;
        float ph = (t->page_ph && t->page_ph[i] > 0.0f) ? t->page_ph[i] : def_ph;

        double scale = 1.0;
        if (t->fit == FIT_WIDTH) {
            scale = ((double)total_w / (double)pw) * zoom;
        } else {
            scale = fmin((double)total_w / (double)pw, (double)content_h / (double)ph) * zoom;
        }

        int ph_px = checked_scale_to_int((double)ph, scale, 1, 1000000);
        int pw_px = checked_scale_to_int((double)pw, scale, 1, 1000000);
        if (pw_px > max_page_pixel_w) max_page_pixel_w = pw_px;

        t->page_y[i] = cur_y;
        if (t->fit == FIT_WIDTH) {
            t->page_h[i] = ph_px;
            cur_y += ph_px;
            if (i < t->page_count - 1) cur_y += GAP;
        } else {
            int slot = (ph_px < content_h) ? content_h : ph_px;
            t->page_h[i] = slot;
            cur_y += slot;
            if (ph_px >= content_h && i < t->page_count - 1) cur_y += GAP;
        }
    }

    t->total_h = cur_y;
    t->max_scroll = (t->total_h > content_h) ? (t->total_h - content_h) : 0;
    t->max_scroll_x = (max_page_pixel_w > total_w) ? (max_page_pixel_w - total_w) : 0;

    if (has_prev_layout) {
        if (t->fit != t->last_fit) {
            t->virtual_y = t->page_y[cur_p];
        } else {
            t->virtual_y = t->page_y[cur_p] + (int64_t)(rel_offset * (double)t->page_h[cur_p] + 0.5);
        }
    }

    tab_set_scroll(t, t->virtual_y);
    if (t->virtual_x > t->max_scroll_x) t->virtual_x = t->max_scroll_x;
    if (t->virtual_x < 0) t->virtual_x = 0;

    t->last_w = total_w;
    t->last_h = content_h;
    t->last_zoom_pct = t->zoom_pct;
    t->last_fit = t->fit;
    return true;
}

static fz_pixmap* s_vpix = nullptr;
static int s_vpix_w = 0;
static int s_vpix_h = 0;
static fz_context* s_vpix_ctx = nullptr;

static void main_free_buffers(void) {
    if (s_vpix && s_vpix_ctx) {
        fz_drop_pixmap(s_vpix_ctx, s_vpix);
        s_vpix = nullptr;
        s_vpix_w = 0;
        s_vpix_h = 0;
        s_vpix_ctx = nullptr;
    }
}

static void draw_search_highlights(fz_pixmap* vpix, const tab_t* t, int px_w, int content_h_px) {
    if (!vpix || !vpix->samples || !t || !t->search_hits || t->search_hit_count == 0) return;
    double zoom = (double)t->zoom_pct / 100.0;

    for (size_t h = 0; h < t->search_hit_count; h++) {
        int p = t->search_hits[h].page;
        if (p < 0 || p >= t->page_count) continue;
        int64_t y0 = t->page_y[p], y1 = y0 + t->page_h[p];
        if (y1 <= t->virtual_y || y0 >= t->virtual_y + content_h_px) continue;

        float pw = t->page_pw[p];
        float ph = t->page_ph[p];
        double scale = (t->fit == FIT_WIDTH) ? ((double)px_w / (double)pw) * zoom
                                             : fmin((double)px_w / (double)pw, (double)content_h_px / (double)ph) * zoom;
        int page_pixel_w = checked_scale_to_int((double)pw, scale, 1, 1000000);
        int page_pixel_h = checked_scale_to_int((double)ph, scale, 1, 1000000);

        int slice_w = (page_pixel_w <= px_w) ? page_pixel_w : px_w;
        int sx = 0, dst_x = 0;
        if (page_pixel_w <= px_w) {
            dst_x = (px_w - page_pixel_w) / 2;
        } else {
            sx = (int)t->virtual_x;
            if (sx + slice_w > page_pixel_w) sx = page_pixel_w - slice_w;
            if (sx < 0) sx = 0;
        }

        int64_t page_y0 = (t->fit != FIT_WIDTH && page_pixel_h < content_h_px)
                              ? y0 + (content_h_px - page_pixel_h) / 2
                              : y0;

        fz_rect hr = t->search_hits[h].rect;
        int bx0 = dst_x + (int)((double)hr.x0 * scale) - sx;
        int bx1 = dst_x + (int)((double)hr.x1 * scale + 0.999) - sx;
        int by0 = (int)(page_y0 + (int64_t)((double)hr.y0 * scale) - t->virtual_y);
        int by1 = (int)(page_y0 + (int64_t)((double)hr.y1 * scale + 0.999) - t->virtual_y);

        if (bx0 < 0) bx0 = 0;
        if (bx1 > vpix->w) bx1 = vpix->w;
        if (by0 < 0) by0 = 0;
        if (by1 > vpix->h) by1 = vpix->h;
        if (bx1 <= bx0 || by1 <= by0) continue;

        bool is_active = (h == t->search_hit_idx);
        unsigned char hr_c = is_active ? 255 : 220;
        unsigned char hg_c = is_active ? 215 : 180;
        unsigned char hb_c = 0;

        for (int y = by0; y < by1; y++) {
            unsigned char* row = vpix->samples + (size_t)y * (size_t)vpix->stride;
            for (int x = bx0; x < bx1; x++) {
                bool is_border = is_active && (y <= by0 + 1 || y >= by1 - 2 || x <= bx0 + 1 || x >= bx1 - 2);
                if (is_border) {
                    row[x * 3 + 0] = 255;
                    row[x * 3 + 1] = 69;
                    row[x * 3 + 2] = 0;
                } else {
                    row[x * 3 + 0] = (unsigned char)((row[x * 3 + 0] + hr_c) >> 1);
                    row[x * 3 + 1] = (unsigned char)((row[x * 3 + 1] + hg_c) >> 1);
                    row[x * 3 + 2] = (unsigned char)((row[x * 3 + 2] + hb_c) >> 1);
                }
            }
        }
    }
}

static void scroll_to_hit(tab_t* t, size_t hit_idx, int px_w, int content_h_px) {
    if (!t || !t->search_hits || hit_idx >= t->search_hit_count || !t->page_y || !t->page_h) return;
    int p = t->search_hits[hit_idx].page;
    if (p < 0 || p >= t->page_count) return;
    ensure_page_bounds(t, p);
    float pw = t->page_pw[p];
    float ph = t->page_ph[p];
    double zoom = (double)t->zoom_pct / 100.0;
    double scale = (t->fit == FIT_WIDTH) ? ((double)px_w / (double)pw) * zoom
                                         : fmin((double)px_w / (double)pw, (double)content_h_px / (double)ph) * zoom;
    int page_pixel_h = checked_scale_to_int((double)ph, scale, 1, 1000000);
    int64_t y0 = t->page_y[p];
    int64_t page_y0 = (t->fit != FIT_WIDTH && page_pixel_h < content_h_px)
                          ? y0 + (content_h_px - page_pixel_h) / 2
                          : y0;
    fz_rect r = t->search_hits[hit_idx].rect;
    int64_t hit_mid_y = page_y0 + (int64_t)(((double)r.y0 + (double)r.y1) * 0.5 * scale);
    tab_set_scroll(t, hit_mid_y - content_h_px / 2);

    int page_pixel_w = checked_scale_to_int((double)pw, scale, 1, 1000000);
    if (page_pixel_w > px_w) {
        int hit_mid_x = (int)(((double)r.x0 + (double)r.x1) * 0.5 * scale);
        int64_t vx = hit_mid_x - px_w / 2;
        if (vx < 0) vx = 0;
        if (vx > t->max_scroll_x) vx = t->max_scroll_x;
        t->virtual_x = vx;
    }
}

static void render_frame(tab_t* tabs, int tab_count, int active_tab, bool show_hud) {
    if (tab_count <= 0 || active_tab < 0 || active_tab >= tab_count) return;
    tab_t* t = &tabs[active_tab];

    int cols = 0, rows = 0, px_w = 0, px_h = 0;
    term_get_size(&cols, &rows, &px_w, &px_h);
    if (cols < 10 || rows < 3) {
        printf("\033[1;1HTerminal too small (min 10x3)");
        fflush(stdout);
        return;
    }

    if (px_w > 16384) px_w = 16384;

    int content_rows = rows - (show_hud ? 1 : 0);
    if (content_rows < 1) content_rows = 1;
    int content_h_px = (int)((double)content_rows * ((double)px_h / rows) + 0.5);
    if (content_h_px < 1) content_h_px = 1;
    if (content_h_px > 16384) content_h_px = 16384;

    if (!update_tab_layout(t, px_w, content_h_px)) return;
    if (!t->page_y || !t->page_h) return;

    term_sync_begin();
    printf("\033]11;%s\007", t->dark ? "#000000" : "#181818");

    fz_context* ctx = t->doc->ctx;
    if (!s_vpix || s_vpix_w != px_w || s_vpix_h != content_h_px || s_vpix_ctx != ctx) {
        if (s_vpix && s_vpix_ctx) {
            fz_drop_pixmap(s_vpix_ctx, s_vpix);
            s_vpix = nullptr;
        }
        fz_try(ctx) {
            s_vpix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), fz_make_irect(0, 0, px_w, content_h_px), nullptr, 0);
            s_vpix_w = px_w;
            s_vpix_h = content_h_px;
            s_vpix_ctx = ctx;
        } fz_catch(ctx) {
            s_vpix = nullptr;
            s_vpix_w = 0;
            s_vpix_h = 0;
            s_vpix_ctx = nullptr;
        }
    }

    fz_pixmap* vpix = s_vpix;

    if (vpix) {
        fz_clear_pixmap_with_value(ctx, vpix, t->dark ? 0xFF : 0x18);

        int start_page = find_first_visible_page(t, t->virtual_y);
        double zoom = (double)t->zoom_pct / 100.0;

        for (int i = start_page; i < t->page_count; i++) {
            int64_t y0 = t->page_y[i], y1 = y0 + t->page_h[i];
            if (y1 <= t->virtual_y) continue;
            if (y0 >= t->virtual_y + content_h_px) break;

            ensure_page_bounds(t, i);
            float pw = t->page_pw[i];
            float ph = t->page_ph[i];

            double scale = 1.0;
            if (t->fit == FIT_WIDTH) {
                scale = ((double)px_w / (double)pw) * zoom;
            } else {
                scale = fmin((double)px_w / (double)pw, (double)content_h_px / (double)ph) * zoom;
            }

            int page_pixel_w = checked_scale_to_int((double)pw, scale, 1, 1000000);
            int page_pixel_h = checked_scale_to_int((double)ph, scale, 1, 1000000);

            int slice_w = (page_pixel_w <= px_w) ? page_pixel_w : px_w;
            int sx = 0;
            int dst_x = 0;

            if (page_pixel_w <= px_w) {
                dst_x = (px_w - page_pixel_w) / 2;
                sx = 0;
            } else {
                sx = (int)t->virtual_x;
                if (sx + slice_w > page_pixel_w) sx = page_pixel_w - slice_w;
                if (sx < 0) sx = 0;
                dst_x = 0;
            }

            int64_t page_y0 = (t->fit != FIT_WIDTH && page_pixel_h < content_h_px)
                                  ? y0 + (content_h_px - page_pixel_h) / 2
                                  : y0;
            int64_t page_y1 = page_y0 + page_pixel_h;
            int64_t v0 = (t->virtual_y > page_y0) ? t->virtual_y : page_y0;
            int64_t v1 = (t->virtual_y + content_h_px < page_y1) ? (t->virtual_y + content_h_px) : page_y1;
            if (v1 <= v0) continue;

            pdf_render_page_slice(t->doc, i, scale, sx, (int)(v0 - page_y0), slice_w,
                                  (int)(v1 - v0), vpix, dst_x, (int)(v0 - t->virtual_y));
        }

        draw_search_highlights(vpix, t, px_w, content_h_px);

        if (t->dark) {
            for (int y = 0; y < vpix->h; y++) {
                unsigned char* row = vpix->samples + (size_t)y * (size_t)vpix->stride;
                int rb = vpix->w * 3;
#pragma GCC ivdep
                for (int x = 0; x < rb; x++) row[x] = (unsigned char)(255 - row[x]);
            }
        }

        sixel_draw(vpix, 1, show_hud ? 2 : 1);
    }

    if (show_hud) render_top_hud(tabs, tab_count, active_tab, cols);
    term_sync_end();
}

static void show_toc_modal(tab_t* tabs, int tab_count, int active_tab, bool show_hud) {
    if (tab_count <= 0 || active_tab < 0 || active_tab >= tab_count) return;
    tab_t* t = &tabs[active_tab];
    size_t toc_cnt = 0;
    toc_item_t* toc = pdf_get_toc(t->doc, &toc_cnt);
    if (!toc || toc_cnt == 0) {
        pdf_free_toc(toc, toc_cnt);
        return;
    }

    int sel = 0, best_p = -1, cur_p = get_active_page(t);
    for (size_t i = 0; i < toc_cnt; i++) {
        if (toc[i].page <= cur_p && toc[i].page > best_p) {
            best_p = toc[i].page;
            sel = (int)i;
        }
    }
    if (sel < 0) sel = 0;
    if (sel >= (int)toc_cnt) sel = (int)toc_cnt - 1;

    int offset = 0;
    bool needs_redraw = true;
    bool needs_clear = true;

    while (true) {
        int cols = 0, rows = 0;
        term_get_size(&cols, &rows, nullptr, nullptr);
        if (cols < 10) cols = 10;
        if (rows < 3) rows = 3;

        int top = (rows >= 2) ? 2 : 1;
        int bottom = (rows >= 3) ? (rows - 1) : rows;
        int max_v = bottom - top + 1;
        if (max_v < 1) max_v = 1;

        if (sel < 0) sel = 0;
        if (sel >= (int)toc_cnt) sel = (int)toc_cnt - 1;
        if (sel < offset) offset = sel;
        if (sel >= offset + max_v) offset = sel - max_v + 1;
        if (offset + max_v > (int)toc_cnt) offset = (int)toc_cnt - max_v;
        if (offset < 0) offset = 0;

        if (needs_redraw) {
            bool dark = t->dark;
            const char* hdr_bg = dark ? "\033[48;2;34;34;37m" : "\033[48;2;230;232;236m";
            const char* hdr_title_fg = dark ? "\033[1;38;2;255;255;255m" : "\033[1;38;2;20;20;20m";
            const char* hdr_hint_fg = dark ? "\033[0;38;2;160;160;165m" : "\033[0;38;2;85;85;90m";

            const char* norm_bg = dark ? "\033[48;2;18;18;18m" : "\033[48;2;255;255;255m";
            const char* norm_text_fg = dark ? "\033[38;2;230;230;230m" : "\033[38;2;24;24;24m";
            const char* norm_page_fg = dark ? "\033[38;2;140;140;140m" : "\033[38;2;110;110;110m";

            const char* sel_bg = dark ? "\033[48;2;42;72;118m" : "\033[48;2;30;95;185m";
            const char* sel_text_fg = "\033[1;38;2;255;255;255m";
            const char* sel_page_fg = dark ? "\033[1;38;2;190;220;255m" : "\033[1;38;2;220;235;255m";

            const char* ftr_bg = dark ? "\033[48;2;26;26;29m" : "\033[48;2;236;238;242m";
            const char* ftr_info_fg = dark ? "\033[38;2;210;210;215m" : "\033[38;2;50;50;55m";
            const char* ftr_hint_fg = dark ? "\033[38;2;140;140;145m" : "\033[38;2;100;100;105m";

            term_sync_begin();
            if (needs_clear) {
                printf("\033]11;%s\007\033[2J\033[H", dark ? "#000000" : "#ffffff");
                needs_clear = false;
            }

            // Header
            if (rows >= 2) {
                char h_left[128];
                snprintf(h_left, sizeof(h_left), "  Table of Contents (%zu items)", toc_cnt);
                const char* h_right = "Enter: Jump | Esc/q: Cancel  ";
                int l_len = utf8_str_width(h_left);
                int r_len = utf8_str_width(h_right);
                if (l_len + r_len > cols) {
                    snprintf(h_left, sizeof(h_left), "  Table of Contents");
                    h_right = "Esc: Cancel  ";
                    l_len = utf8_str_width(h_left);
                    r_len = utf8_str_width(h_right);
                }
                int h_pad = cols - l_len - r_len;
                if (h_pad < 0) h_pad = 0;

                printf("\033[1;1H%s%s%s%*s%s%s\033[0m",
                       hdr_bg, hdr_title_fg, h_left,
                       h_pad, "",
                       hdr_hint_fg, h_right);
            }

            // Items
            for (int i = 0; i < max_v; i++) {
                int cur_row = top + i;
                int idx = offset + i;
                printf("\033[%d;1H", cur_row);
                if (idx < (int)toc_cnt) {
                    bool is_sel = (idx == sel);
                    int indent = (toc[idx].level - 1) * 2;
                    if (indent < 0) indent = 0;
                    if (indent > 16) indent = 16;

                    char pbuf[32];
                    snprintf(pbuf, sizeof(pbuf), " (p. %d)  ", toc[idx].page + 1);
                    int p_len = utf8_str_width(pbuf);

                    int avail = cols - 3 - indent - p_len;
                    if (avail < 0) avail = 0;

                    char safe_title[512];
                    sanitize_terminal_string(toc[idx].title, safe_title, sizeof(safe_title));

                    char trunc_title[512];
                    utf8_truncate_by_width(safe_title, avail, trunc_title, sizeof(trunc_title));
                    int t_w = utf8_str_width(trunc_title);

                    int row_pad = cols - 3 - indent - t_w - p_len;
                    if (row_pad < 0) row_pad = 0;

                    if (is_sel) {
                        printf("%s%s > %*s%s%*s%s%s\033[0m",
                               sel_bg, sel_text_fg,
                               indent, "",
                               trunc_title,
                               row_pad, "",
                               sel_page_fg, pbuf);
                    } else {
                        printf("%s%s   %*s%s%*s%s%s\033[0m",
                               norm_bg, norm_text_fg,
                               indent, "",
                               trunc_title,
                               row_pad, "",
                               norm_page_fg, pbuf);
                    }
                } else {
                    printf("%s%*s\033[0m", norm_bg, cols, "");
                }
            }

            // Footer
            if (rows >= 3) {
                char f_left[128];
                int pct = (toc_cnt > 1) ? (int)((double)sel / (double)(toc_cnt - 1) * 100.0 + 0.5) : 100;
                snprintf(f_left, sizeof(f_left), "  Item %d of %zu (%d%%)", sel + 1, toc_cnt, pct);
                const char* f_right = "j/k, Wheel: Select | PgUp/PgDn: Scroll  ";
                int fl_len = utf8_str_width(f_left);
                int fr_len = utf8_str_width(f_right);
                if (fl_len + fr_len > cols) {
                    snprintf(f_left, sizeof(f_left), "  %d/%zu", sel + 1, toc_cnt);
                    f_right = "Enter: Jump  ";
                    fl_len = utf8_str_width(f_left);
                    fr_len = utf8_str_width(f_right);
                }
                int f_pad = cols - fl_len - fr_len;
                if (f_pad < 0) f_pad = 0;

                printf("\033[%d;1H%s%s%s%*s%s%s\033[0m",
                       rows,
                       ftr_bg, ftr_info_fg, f_left,
                       f_pad, "",
                       ftr_hint_fg, f_right);
            }

            term_sync_end();
            needs_redraw = false;
        }

        int mouse_col = 0, mouse_row = 0;
        int k = term_read_key(500, &mouse_col, &mouse_row);
        if (k == KEY_NONE) continue;
        if (k == KEY_EOF || k == 'q' || k == 't' || k == 'T' || k == KEY_ESC || k == KEY_CTRL_C || k == 3) break;

        if (k == 'j' || k == KEY_DOWN || k == KEY_WHEEL_DOWN) {
            if (sel < (int)toc_cnt - 1) { sel++; needs_redraw = true; }
        } else if (k == 'k' || k == KEY_UP || k == KEY_WHEEL_UP) {
            if (sel > 0) { sel--; needs_redraw = true; }
        } else if (k == KEY_PGDN || k == ' ' || k == 'd') {
            sel = (sel + max_v < (int)toc_cnt) ? (sel + max_v) : (int)toc_cnt - 1;
            needs_redraw = true;
        } else if (k == KEY_PGUP || k == 'u') {
            sel = (sel >= max_v) ? (sel - max_v) : 0;
            needs_redraw = true;
        } else if (k == KEY_HOME || k == 'g') {
            sel = 0;
            needs_redraw = true;
        } else if (k == KEY_END || k == 'G') {
            sel = (int)toc_cnt - 1;
            needs_redraw = true;
        } else if (k == 'D') {
            t->dark = !t->dark;
            needs_clear = true;
            needs_redraw = true;
        } else if (k == 'r') {
            needs_clear = true;
            needs_redraw = true;
        } else if (k == KEY_MOUSE_LEFT) {
            if (mouse_row >= top && mouse_row <= bottom) {
                int clicked = offset + (mouse_row - top);
                if (clicked >= 0 && clicked < (int)toc_cnt) {
                    if (clicked == sel) {
                        int p = toc[sel].page;
                        if (p >= 0 && p < t->page_count && t->page_y) tab_set_scroll(t, t->page_y[p]);
                        break;
                    } else {
                        sel = clicked;
                        needs_redraw = true;
                    }
                }
            } else if (mouse_row == 1 && rows >= 2 && mouse_col > cols - 16) {
                break;
            }
        } else if (k == '\r' || k == '\n') {
            int p = toc[sel].page;
            if (p >= 0 && p < t->page_count && t->page_y) tab_set_scroll(t, t->page_y[p]);
            break;
        }
    }

    term_sync_begin();
    printf("\033[2J\033[H");
    term_sync_end();

    pdf_free_toc(toc, toc_cnt);
    render_frame(tabs, tab_count, active_tab, show_hud);
}

typedef enum {
    PROMPT_CANCELLED = 0,
    PROMPT_SUBMITTED = 1
} prompt_result_t;

static prompt_result_t prompt_input(const char* prompt, char* buf, size_t maxlen) {
    if (!prompt || !buf || maxlen == 0) return PROMPT_CANCELLED;
    int rows = 0;
    term_get_size(nullptr, &rows, nullptr, nullptr);
    if (rows < 1) rows = 24;

    size_t len = 0;
    buf[0] = '\0';
    fputs("\033[?25h", stdout);

    prompt_result_t res = PROMPT_CANCELLED;
    while (true) {
        printf("\033[%d;1H\033[7m %s%s \033[0m\033[K", rows, prompt, buf);
        fflush(stdout);

        int ch = term_read_key(500, nullptr, nullptr);
        if (ch == KEY_NONE) continue;
        if (ch == KEY_RESIZE) {
            term_get_size(nullptr, &rows, nullptr, nullptr);
            if (rows < 1) rows = 24;
            continue;
        }
        if (ch == KEY_EOF || ch == KEY_ESC || ch == KEY_CTRL_C || ch == 3) {
            buf[0] = '\0';
            res = PROMPT_CANCELLED;
            break;
        }
        if (ch == '\r' || ch == '\n') {
            res = PROMPT_SUBMITTED;
            break;
        }
        if ((ch == 127 || ch == 8) && len > 0) {
            // Delete previous UTF-8 codepoint
            size_t i = len;
            while (i > 0 && ((unsigned char)buf[i - 1] & 0xC0) == 0x80) i--;
            if (i > 0) i--;
            len = i;
            buf[len] = '\0';
        } else if (ch >= 32 && ch <= 126 && len + 1 < maxlen) {
            buf[len++] = (char)ch;
            buf[len] = '\0';
        } else if (ch >= 0xC2 && ch <= 0xF4) {
            int need = (ch < 0xE0) ? 1 : ((ch < 0xF0) ? 2 : 3);
            if (len + (size_t)need + 1 < maxlen) {
                char seq[4];
                seq[0] = (char)ch;
                bool valid = true;
                for (int b = 1; b <= need; b++) {
                    int c_byte = term_read_key(25, nullptr, nullptr);
                    if (c_byte >= 0x80 && c_byte <= 0xBF) {
                        seq[b] = (char)c_byte;
                    } else {
                        valid = false;
                        break;
                    }
                }
                if (valid) {
                    for (int b = 0; b <= need; b++) buf[len++] = seq[b];
                    buf[len] = '\0';
                }
            }
        }
    }

    fputs("\033[?25l", stdout);
    fflush(stdout);
    return res;
}

static void show_search_prompt(tab_t* tabs, int tab_count, int active_tab, bool show_hud) {
    tab_t* t = &tabs[active_tab];
    char query[64] = {0};
    prompt_result_t res = prompt_input("Search:", query, sizeof(query));
    if (res == PROMPT_SUBMITTED) {
        if (query[0] == '\0') {
            // Clear search on empty submission
            t->search_query[0] = '\0';
            if (t->search_hits) {
                pdf_free_search(t->search_hits);
                t->search_hits = nullptr;
            }
            t->search_hit_count = 0;
            t->search_hit_idx = 0;
        } else {
            char safe_q[sizeof(t->search_query)];
            sanitize_terminal_string(query, safe_q, sizeof(safe_q));
            snprintf(t->search_query, sizeof(t->search_query), "%s", safe_q);

            if (t->search_hits) {
                pdf_free_search(t->search_hits);
                t->search_hits = nullptr;
            }
            search_status_t status = SEARCH_OK;
            t->search_hits = pdf_search_text(t->doc, query, &t->search_hit_count, &status);
            t->search_hit_idx = 0;

            if (t->search_hit_count > 0) {
                int cur_p = get_active_page(t);
                for (size_t i = 0; i < t->search_hit_count; i++) {
                    if (t->search_hits[i].page >= cur_p) {
                        t->search_hit_idx = i;
                        break;
                    }
                }
                int cols = 0, rows = 0, px_w = 0, px_h = 0;
                term_get_size(&cols, &rows, &px_w, &px_h);
                int content_rows = rows - (show_hud ? 1 : 0);
                if (content_rows < 1) content_rows = 1;
                int content_h_px = (int)((double)content_rows * ((double)px_h / (rows > 0 ? rows : 24)) + 0.5);
                scroll_to_hit(t, t->search_hit_idx, px_w, content_h_px);
            }
        }
    }
    render_frame(tabs, tab_count, active_tab, show_hud);
}

static void show_page_jump_prompt(tab_t* tabs, int tab_count, int active_tab, bool show_hud) {
    tab_t* t = &tabs[active_tab];
    char num[16] = {0}, prompt[32];
    snprintf(prompt, sizeof(prompt), "Go to page (1-%d):", t->page_count);
    prompt_result_t res = prompt_input(prompt, num, sizeof(num));
    if (res == PROMPT_SUBMITTED && num[0] != '\0') {
        errno = 0;
        char* endptr = nullptr;
        long p = strtol(num, &endptr, 10);
        if (errno == 0 && endptr != num && *endptr == '\0' && p >= 1 && p <= t->page_count && t->page_y) {
            tab_set_scroll(t, t->page_y[p - 1]);
        }
    }
    render_frame(tabs, tab_count, active_tab, show_hud);
}

static void free_tab(tab_t* t) {
    if (!t) return;
    if (t->doc) pdf_close(t->doc);
    free(t->path);
    free(t->name);
    free(t->page_pw);
    free(t->page_ph);
    free(t->page_y);
    free(t->page_h);
    if (t->search_hits) pdf_free_search(t->search_hits);
    memset(t, 0, sizeof(*t));
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
}

int main(int argc, char** argv) {
    bool dark_default = false;
    int first_file_idx = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            puts(
                "pdf-cli (C23) - Fast Sixel Terminal PDF Viewer\n\n"
                "Usage: pdf-cli [options] [--] <file1.pdf> [file2.pdf ...]\n\n"
                "Options:\n"
                "  -h, --help             Show this help message and exit\n"
                "  -v, --version          Show version and license information\n"
                "  -d, --dark             Start in dark mode (inverted colors)\n"
                "  --                     End of options marker\n\n"
                "Controls:\n"
                "  j, Down, WheelDown     Continuous scroll down (line / wheel step)\n"
                "  k, Up, WheelUp         Continuous scroll up (line / wheel step)\n"
                "  d, Space, PgDn         Scroll down screen step (~35%)\n"
                "  u, PgUp                Scroll up screen step (~35%)\n"
                "  h, l, Left, Right      Pan horizontally when zoomed\n"
                "  Home, < / End, >, G    Jump to start / Jump to end of document\n"
                "  n, J                   Next page / Next search match\n"
                "  p, K, N                Previous page / Previous search match\n"
                "  i                      Toggle top Tab bar HUD\n"
                "  g                      Jump to page number\n"
                "  f                      Toggle fit mode (Fit Width vs Fit Page)\n"
                "  D                      Toggle dark mode / color invert\n"
                "  +, - / 0               Zoom in / Zoom out / Reset zoom (20%-500%)\n"
                "  T                      Table of Contents overlay\n"
                "  /                      In-document text search\n"
                "  1..9, 0                Switch to tab 1..9, tab 10\n"
                "  [, ] / Ctrl+Tab        Cycle tabs\n"
                "  Ctrl+W                 Close tab (exits on last tab)\n"
                "  r                      Force re-render / window resize\n"
                "  q, Ctrl+C              Quit viewer");
            return 0;
        } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            puts("pdf-cli version 0.2.0 (C23 ISO/IEC 9899:2024)");
            puts("Static libraries: MuPDF 1.28.0 (AGPLv3), libsixel 1.10.5 (MIT)");
            puts("License: MIT (pdf-cli original work)");
            return 0;
        } else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--dark")) {
            dark_default = true;
            first_file_idx = i + 1;
        } else if (!strcmp(argv[i], "--")) {
            first_file_idx = i + 1;
            break;
        } else if (argv[i][0] == '-') {
            char safe_opt[64];
            sanitize_terminal_string(argv[i], safe_opt, sizeof(safe_opt));
            fprintf(stderr, "pdf-cli: unrecognized option '%s'\nTry 'pdf-cli --help' for more information.\n", safe_opt);
            return 1;
        } else {
            first_file_idx = i;
            break;
        }
    }

    if (first_file_idx >= argc) {
        fprintf(stderr, "pdf-cli: missing PDF file argument.\nTry 'pdf-cli --help' for more information.\n");
        return 1;
    }

    tab_t tabs[MAX_TABS] = {0};
    int tab_count = 0;

    for (int i = first_file_idx; i < argc; i++) {
        if (tab_count >= MAX_TABS) {
            char safe_arg[128];
            sanitize_terminal_string(argv[i], safe_arg, sizeof(safe_arg));
            fprintf(stderr, "pdf-cli: warning: maximum of %d tabs reached; skipping '%s'\n", MAX_TABS, safe_arg);
            continue;
        }

        pdf_doc_t* doc = pdf_open(argv[i]);
        if (!doc) {
            char safe_path[256];
            sanitize_terminal_string(argv[i], safe_path, sizeof(safe_path));
            fprintf(stderr, "Warning: failed to open PDF '%s'\n", safe_path);
            continue;
        }

        tab_t* t = &tabs[tab_count];
        t->path = strdup(argv[i]);
        t->name = strdup(get_base_name(argv[i]));
        t->doc = doc;
        t->page_count = doc->page_count;
        t->page_pw = (float*)calloc((size_t)t->page_count, sizeof(float));
        t->page_ph = (float*)calloc((size_t)t->page_count, sizeof(float));

        if (!t->path || !t->name || !t->page_pw || !t->page_ph) {
            char safe_path[256];
            sanitize_terminal_string(argv[i], safe_path, sizeof(safe_path));
            fprintf(stderr, "Error: out of memory initializing tab for '%s'\n", safe_path);
            free_tab(t);
            continue;
        }

        t->zoom_pct = DEFAULT_ZOOM_PCT;
        t->fit = FIT_WIDTH;
        t->dark = dark_default;
        tab_count++;
    }

    if (tab_count == 0) {
        fprintf(stderr, "Error: no valid PDF documents opened.\n");
        return 1;
    }

    if (!term_enter_raw()) {
        fprintf(stderr, "Error: failed to initialize raw terminal mode.\n");
        for (int i = 0; i < tab_count; i++) free_tab(&tabs[i]);
        return 1;
    }
    atexit(term_restore);

    int active_tab = 0;
    bool show_hud = false;
    render_frame(tabs, tab_count, active_tab, show_hud);

    while (tab_count > 0) {
        int mouse_col = 0, mouse_row = 0;
        int key = term_read_key(500, &mouse_col, &mouse_row);
        if (key == KEY_NONE) continue;
        if (key == KEY_EOF || key == 'q' || key == KEY_CTRL_C || key == 3) break;

        tab_t* t = &tabs[active_tab];
        int cols = 0, rows = 0, px_w = 0, px_h = 0;
        term_get_size(&cols, &rows, &px_w, &px_h);
        int content_h_px = (int)((double)(rows - (show_hud ? 1 : 0)) * ((double)px_h / rows) + 0.5);
        if (content_h_px < 1) content_h_px = 1;

        int step_line = (int)(((double)px_h / rows) * 3.0);
        if (step_line < 24) step_line = 24;
        int wstep = (int)(content_h_px * 0.15);
        if (wstep < 40) wstep = 40;
        int step_pg = (int)(content_h_px * 0.35);
        if (step_pg < 50) step_pg = 50;

        bool redraw = true;
        if (key == KEY_ESC) {
            if (show_hud) {
                show_hud = false;
            } else {
                redraw = false;
            }
        } else if (key == 'i') {
            show_hud = !show_hud;
        } else if (key == KEY_MOUSE_LEFT) {
            if (show_hud && mouse_row == 1) {
                int hit = find_tab_by_col(tabs, tab_count, active_tab, cols, mouse_col);
                if (hit >= 0 && hit < tab_count && hit != active_tab)
                    active_tab = hit;
                else
                    redraw = false;
            } else if (show_hud) {
                show_hud = false;
            } else {
                redraw = false;
            }
        } else if (key >= KEY_CTRL_1 && key <= KEY_CTRL_9) {
            int target = key - KEY_CTRL_1;
            if (target < tab_count)
                active_tab = target;
            else
                redraw = false;
        } else if (key == KEY_CTRL_0) {
            if (tab_count >= 10)
                active_tab = 9;
            else
                redraw = false;
        } else if (tab_count > 1 && key >= '1' && key <= '9') {
            int target = key - '1';
            if (target < tab_count)
                active_tab = target;
            else
                redraw = false;
        } else if (key == KEY_CTRL_TAB || key == ']' || key == KEY_PGDN) {
            if (key == KEY_PGDN && t->virtual_y < t->max_scroll) {
                tab_set_scroll(t, t->virtual_y + step_pg);
            } else if (tab_count > 1 && (key == KEY_CTRL_TAB || key == ']')) {
                active_tab = (active_tab + 1) % tab_count;
            } else {
                tab_set_scroll(t, t->virtual_y + step_pg);
            }
        } else if (key == KEY_CTRL_SHIFT_TAB || key == '[') {
            if (tab_count > 1) {
                active_tab = (active_tab + tab_count - 1) % tab_count;
            } else {
                redraw = false;
            }
        } else if (key == KEY_CTRL_W) {
            main_free_buffers();
            free_tab(&tabs[active_tab]);
            for (int i = active_tab; i < tab_count - 1; i++) tabs[i] = tabs[i + 1];
            memset(&tabs[tab_count - 1], 0, sizeof(tab_t));
            if (--tab_count == 0) break;
            if (active_tab >= tab_count) active_tab = tab_count - 1;
        } else if (key == KEY_WHEEL_DOWN) {
            if (show_hud && mouse_row == 1 && tab_count > 1)
                active_tab = (active_tab + 1) % tab_count;
            else
                tab_set_scroll(t, t->virtual_y + wstep);
        } else if (key == KEY_WHEEL_UP) {
            if (show_hud && mouse_row == 1 && tab_count > 1)
                active_tab = (active_tab + tab_count - 1) % tab_count;
            else
                tab_set_scroll(t, t->virtual_y - wstep);
        } else if (key == 'j' || key == KEY_DOWN) {
            tab_set_scroll(t, t->virtual_y + step_line);
        } else if (key == 'k' || key == KEY_UP) {
            tab_set_scroll(t, t->virtual_y - step_line);
        } else if (key == 'h' || key == KEY_LEFT) {
            if (t->max_scroll_x > 0) {
                t->virtual_x -= 40;
                if (t->virtual_x < 0) t->virtual_x = 0;
            } else {
                prev_page(t);
            }
        } else if (key == 'l' || key == KEY_RIGHT) {
            if (t->max_scroll_x > 0) {
                t->virtual_x += 40;
                if (t->virtual_x > t->max_scroll_x) t->virtual_x = t->max_scroll_x;
            } else {
                next_page(t);
            }
        } else if (key == ' ' || key == 'd') {
            tab_set_scroll(t, t->virtual_y + step_pg);
        } else if (key == KEY_PGUP || key == 'u') {
            tab_set_scroll(t, t->virtual_y - step_pg);
        } else if (key == KEY_HOME || key == '<') {
            tab_set_scroll(t, 0);
        } else if (key == KEY_END || key == 'G' || key == '>') {
            tab_set_scroll(t, t->max_scroll);
        } else if (key == 'n' || key == 'J') {
            if (t->search_hit_count > 0) {
                t->search_hit_idx = (t->search_hit_idx + 1) % t->search_hit_count;
                scroll_to_hit(t, t->search_hit_idx, px_w, content_h_px);
            } else {
                next_page(t);
            }
        } else if (key == 'p' || key == 'N' || key == 'K') {
            if (t->search_hit_count > 0) {
                t->search_hit_idx = (t->search_hit_idx + t->search_hit_count - 1) % t->search_hit_count;
                scroll_to_hit(t, t->search_hit_idx, px_w, content_h_px);
            } else {
                prev_page(t);
            }
        } else if (key == 'f') {
            t->fit = (t->fit == FIT_WIDTH) ? FIT_PAGE : FIT_WIDTH;
        } else if (key == 'D') {
            t->dark = !t->dark;
        } else if (key == '+' || key == '=') {
            if (t->zoom_pct < MAX_ZOOM_PCT) t->zoom_pct += 10;
        } else if (key == '-' || key == '_') {
            if (t->zoom_pct > MIN_ZOOM_PCT) t->zoom_pct -= 10;
        } else if (key == '0' || key == 'z' || key == 'Z') {
            t->zoom_pct = DEFAULT_ZOOM_PCT;
        } else if (key == 'g') {
            show_page_jump_prompt(tabs, tab_count, active_tab, show_hud);
            redraw = false;
        } else if (key == 'T') {
            show_toc_modal(tabs, tab_count, active_tab, show_hud);
            redraw = false;
        } else if (key == '/') {
            show_search_prompt(tabs, tab_count, active_tab, show_hud);
            redraw = false;
        } else if (key != 'r') {
            redraw = false;
        }

        tab_set_scroll(t, t->virtual_y);
        if (redraw) render_frame(tabs, tab_count, active_tab, show_hud);
    }

    term_restore();

    main_free_buffers();
    for (int i = 0; i < tab_count; i++) {
        free_tab(&tabs[i]);
    }
    sixel_free_buffers();
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
    return 0;
}
