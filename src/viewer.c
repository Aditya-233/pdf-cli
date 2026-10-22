#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "pdfcli/viewer.h"
#include "pdfcli/types.h"
#include "pdfcli/term_raw.h"
#include "pdfcli/term_gfx.h"
#include "pdfcli/doc_engine.h"
#include "pdfcli/img_util.h"
#include "pdfcli/cache.h"
#include "pdfcli/file_watcher.h"
#include "pdfcli/config.h"
#include "pdfcli/ctl_server.h"
#include "pdfcli/ctl_proto.h"
#include "pdfcli/toc_modal.h"
#include "pdfcli/utf8_util.h"
#include "pdfcli/picker.h"
#include "pdfcli/file_search.h"
#include "pdfcli/profile.h"
#include <yyjson.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <malloc.h>
#endif
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <libgen.h>
#include <stdarg.h>
#include <ctype.h>

typedef struct {
    char *file_path;
    char *display_name;
    char *file_type;
    doc_handle *doc;
    fz_pixmap *standalone_image;

    bool is_reflowable;
    bool is_image;
    int html_page_width;

    int total_doc_pages;
    int *content_pages;
    size_t content_page_count;
    int current_page_idx;

    // Memoized PDF page dimensions in points (pre-fetched once in viewer_tab_open)
    float *page_widths;
    float *page_heights;
    size_t page_dims_count;

    // Continuous virtual document layout in screen pixels
    int *page_pixel_heights;
    int64_t *page_y_starts;
    int64_t total_doc_h;
    int64_t virtual_y;
    int64_t max_scroll_y;
    double scroll_ratio;

    // Layout memoization cache to prevent redundant recalculations
    int last_layout_w;
    int last_layout_h;
    double last_layout_zoom;
    fit_mode last_layout_fit;
    bool layout_valid;

    fit_mode fit;
    view_mode view;
    dual_mode dual;
    half_offset half;
    tint_mode tint;
    double zoom;
    crop_box crop;

    file_watcher *watcher;
    page_click_map click_map;
    doc_page_link *current_links;
    size_t current_links_count;

    view_loc back_stack[64];
    size_t back_count;
    view_loc fwd_stack[64];
    size_t fwd_count;

    doc_toc_entry *toc;
    size_t toc_count;
} viewer_tab;

typedef struct {
    int start_col;
    int end_col;
    int close_col;
} tab_hit_box;

struct doc_viewer {
    viewer_tab tabs[MAX_TABS];
    size_t tab_count;
    size_t active_tab_idx;

    term_state old_term;
    int term_cols, term_rows;
    double cell_w_px, cell_h_px;

    term_gfx_context *gfx;
    page_cache *cache;
    ctl_server *ctl_srv;

    char temp_dir[1024];
    bool want_back;

    char status_msg[256];
    uint64_t status_until_ms;
    uint64_t tab_bar_until_ms;

    tab_hit_box hit_boxes[MAX_TABS];
};

static uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void viewer_debug(const char *fmt, ...) {
    FILE *f = fopen("/tmp/pdfcli_debug.log", "a");
    if (!f) return;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    fprintf(f, "%lld.%03ld pid=%d ", (long long)ts.tv_sec, ts.tv_nsec / 1000000L, (int)getpid());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static const char *viewer_fit_name(fit_mode fit) {
    return (fit == FIT_WIDTH) ? "width" : ((fit == FIT_HEIGHT) ? "height" : "auto");
}

static void viewer_set_status(doc_viewer *v, const char *msg, uint64_t duration_ms) {
    if (!v) return;
    if (msg && duration_ms > 0) {
        snprintf(v->status_msg, sizeof(v->status_msg), "%s", msg);
        v->status_until_ms = get_time_ms() + duration_ms;
    } else {
        v->status_msg[0] = '\0';
        v->status_until_ms = 0;
    }
}

[[nodiscard]] bool viewer_is_tab_bar_visible(const doc_viewer *v) {
    if (!v || v->tab_count == 0) return false;
    return get_time_ms() < v->tab_bar_until_ms;
}

void viewer_dismiss_hud(doc_viewer *v) {
    if (!v) return;
    v->tab_bar_until_ms = 0;
    v->status_until_ms = 0;
    v->status_msg[0] = '\0';
    memset(v->hit_boxes, 0, sizeof(v->hit_boxes));
}

void viewer_toggle_hud(doc_viewer *v) {
    if (!v || v->tab_count == 0) return;
    if (viewer_is_tab_bar_visible(v)) {
        viewer_dismiss_hud(v);
    } else {
        uint64_t now = get_time_ms();
        v->tab_bar_until_ms = now + 2500;
        viewer_tab *tab = &v->tabs[v->active_tab_idx];
        char ibuf[256];
        int curr_p = (tab->content_pages && tab->current_page_idx < (int)tab->content_page_count) ? tab->content_pages[tab->current_page_idx] + 1 : 1;
        snprintf(ibuf, sizeof(ibuf), "[%zu/%zu] %s | Page %d/%zu (%d) | Zoom %.0f%% | Fit %s",
                 v->active_tab_idx + 1, v->tab_count, tab->display_name ? tab->display_name : "doc",
                 tab->current_page_idx + 1, tab->content_page_count, curr_p,
                 tab->zoom * 100.0, viewer_fit_name(tab->fit));
        viewer_set_status(v, ibuf, 2500);
    }
}

static void viewer_tab_save_config(const viewer_tab *tab) {
    if (!tab || !tab->file_path) return;
    char abs_buf[PATH_MAX];
    if (!realpath(tab->file_path, abs_buf)) return;
    doc_config cfg = {};
    snprintf(cfg.fit_mode, sizeof(cfg.fit_mode), "%s", viewer_fit_name(tab->fit));
    cfg.scale_factor = tab->zoom;
    snprintf(cfg.dark_mode, sizeof(cfg.dark_mode), "%s",
             (tab->tint == TINT_DARK) ? "dark" : ((tab->tint == TINT_DIM) ? "dim" : ((tab->tint == TINT_INVERT) ? "invert" : "off")));
    cfg.crop = tab->crop;
    config_save(abs_buf, &cfg);
}

static void viewer_tab_find_content_pages(viewer_tab *tab) {
    if (!tab->doc) return;
    free(tab->content_pages);
    tab->total_doc_pages = doc_get_page_count(tab->doc);
    if (tab->total_doc_pages <= 0) return;
    tab->content_pages = malloc(sizeof(int) * (size_t)tab->total_doc_pages);
    if (!tab->content_pages) return;
    for (int i = 0; i < tab->total_doc_pages; i++) tab->content_pages[i] = i;
    tab->content_page_count = (size_t)tab->total_doc_pages;
}

static void viewer_tab_prefetch_page_dims(viewer_tab *tab) {
    if (!tab) return;
    free(tab->page_widths);
    free(tab->page_heights);
    tab->page_widths = nullptr;
    tab->page_heights = nullptr;
    tab->page_dims_count = 0;

    if (tab->content_page_count == 0) return;

    tab->page_widths = (float *)malloc(sizeof(float) * tab->content_page_count);
    tab->page_heights = (float *)malloc(sizeof(float) * tab->content_page_count);
    if (!tab->page_widths || !tab->page_heights) {
        free(tab->page_widths);
        free(tab->page_heights);
        tab->page_widths = tab->page_heights = nullptr;
        return;
    }
    tab->page_dims_count = tab->content_page_count;

    for (size_t i = 0; i < tab->content_page_count; i++) {
        if (tab->is_image && tab->standalone_image) {
            tab->page_widths[i] = (float)tab->standalone_image->w;
            tab->page_heights[i] = (float)tab->standalone_image->h;
        } else if (tab->doc) {
            int doc_p = tab->content_pages[i];
            fz_rect b = doc_get_page_bounds(tab->doc, doc_p);
            float pw = (float)(b.x1 - b.x0);
            float ph = (float)(b.y1 - b.y0);
            if (pw <= 0.0f) pw = 612.0f;
            if (ph <= 0.0f) ph = 792.0f;
            tab->page_widths[i] = pw;
            tab->page_heights[i] = ph;
        } else {
            tab->page_widths[i] = 612.0f;
            tab->page_heights[i] = 792.0f;
        }
    }
}

static void viewer_update_tab_layout(doc_viewer *v, viewer_tab *tab, int total_w_px, int content_h_px) {
    if (!tab->content_pages || tab->content_page_count == 0) return;
    (void)v;

    // Fast O(1) check: if terminal size, zoom, and fit have not changed, layout is already valid!
    if (tab->layout_valid &&
        tab->last_layout_w == total_w_px &&
        tab->last_layout_h == content_h_px &&
        tab->last_layout_zoom == tab->zoom &&
        tab->last_layout_fit == tab->fit &&
        tab->page_pixel_heights &&
        tab->page_y_starts) {
        return;
    }

    if (!tab->page_pixel_heights || tab->content_page_count != tab->page_dims_count) {
        free(tab->page_pixel_heights);
        tab->page_pixel_heights = (int *)malloc(sizeof(int) * tab->content_page_count);
    }
    if (!tab->page_y_starts) {
        tab->page_y_starts = (int64_t *)malloc(sizeof(int64_t) * tab->content_page_count);
    }
    if (!tab->page_pixel_heights || !tab->page_y_starts) return;

    int64_t cur_y = 0;
    constexpr int GAP = 12;

    for (size_t i = 0; i < tab->content_page_count; i++) {
        tab->page_y_starts[i] = cur_y;
        float pw = (tab->page_widths && i < tab->page_dims_count) ? tab->page_widths[i] : 612.0f;
        float ph = (tab->page_heights && i < tab->page_dims_count) ? tab->page_heights[i] : 792.0f;
        if (pw <= 0.0f) pw = 612.0f;
        if (ph <= 0.0f) ph = 792.0f;

        double base_scale;
        if (tab->fit == FIT_WIDTH) {
            base_scale = (double)total_w_px / (double)pw;
        } else if (tab->fit == FIT_HEIGHT) {
            base_scale = (double)content_h_px / (double)ph;
        } else {
            double sw = (double)total_w_px / (double)pw;
            double sh = (double)content_h_px / (double)ph;
            base_scale = (sw < sh) ? sw : sh;
        }
        double scale = base_scale * tab->zoom;
        int page_pixel_h = (int)(ph * scale + 0.5);
        if (page_pixel_h < 1) page_pixel_h = 1;

        if (tab->fit == FIT_WIDTH) {
            tab->page_pixel_heights[i] = page_pixel_h;
            cur_y += page_pixel_h + GAP;
        } else {
            int slot_h = (page_pixel_h < content_h_px) ? content_h_px : (page_pixel_h + GAP);
            tab->page_pixel_heights[i] = slot_h;
            cur_y += slot_h;
        }
    }
    if (tab->fit == FIT_WIDTH && tab->content_page_count > 0 && cur_y >= GAP) {
        cur_y -= GAP;
    } else if (tab->fit != FIT_WIDTH && tab->content_page_count > 0) {
        size_t last_i = tab->content_page_count - 1;
        float last_ph = (tab->page_heights && last_i < tab->page_dims_count) ? tab->page_heights[last_i] : 792.0f;
        if (last_ph <= 0.0f) last_ph = 792.0f;
        double last_scale = ((double)content_h_px / (double)last_ph) * tab->zoom;
        int last_h = (int)(last_ph * last_scale + 0.5);
        if (last_h >= content_h_px && cur_y >= GAP) {
            cur_y -= GAP;
        }
    }
    tab->total_doc_h = cur_y;
    tab->max_scroll_y = (tab->total_doc_h > content_h_px) ? (tab->total_doc_h - content_h_px) : 0;
    if (tab->virtual_y > tab->max_scroll_y) tab->virtual_y = tab->max_scroll_y;
    if (tab->virtual_y < 0) tab->virtual_y = 0;

    tab->last_layout_w = total_w_px;
    tab->last_layout_h = content_h_px;
    tab->last_layout_zoom = tab->zoom;
    tab->last_layout_fit = tab->fit;
    tab->layout_valid = true;
}

static bool viewer_tab_init(viewer_tab *tab, const char *path) {
    if (!tab || !path) return false;
    memset(tab, 0, sizeof(*tab));
    tab->file_path = strdup(path);
    char *pcopy = strdup(path);
    const char *bn = basename(pcopy);
    tab->display_name = strdup(bn ? bn : path);
    free(pcopy);

    const char *dot = strrchr(path, '.');
    tab->file_type = strdup(dot ? dot + 1 : "pdf");
    if (!strcasecmp(tab->file_type, "html") || !strcasecmp(tab->file_type, "htm")) tab->is_reflowable = true;
    if (!strcasecmp(tab->file_type, "png") || !strcasecmp(tab->file_type, "jpg") || !strcasecmp(tab->file_type, "jpeg")) tab->is_image = true;

    tab->fit = FIT_HEIGHT;
    tab->view = VIEW_AUTO;
    tab->dual = DUAL_OFF;
    tab->half = HALF_TOP;
    tab->tint = TINT_OFF;
    tab->zoom = DEFAULT_ZOOM;
    tab->html_page_width = DEFAULT_HTML_PAGE_WIDTH;
    tab->scroll_ratio = 0.35;
    const char *senv = getenv("DOCVIEWER_SCROLL_RATIO");
    if (senv) {
        double r = atof(senv);
        if (r >= 0.05 && r <= 1.0) tab->scroll_ratio = r;
    }

    char abs_buf[PATH_MAX];
    if (realpath(path, abs_buf)) {
        doc_config cfg = config_load(abs_buf);
        if (!strcmp(cfg.fit_mode, "width")) tab->fit = FIT_WIDTH;
        else if (!strcmp(cfg.fit_mode, "auto")) tab->fit = FIT_AUTO;
        if (cfg.scale_factor >= MIN_ZOOM && cfg.scale_factor <= MAX_ZOOM) tab->zoom = cfg.scale_factor;
        if (!strcmp(cfg.dark_mode, "dark")) tab->tint = TINT_DARK;
        else if (!strcmp(cfg.dark_mode, "dim")) tab->tint = TINT_DIM;
        else if (!strcmp(cfg.dark_mode, "invert")) tab->tint = TINT_INVERT;
        tab->crop = cfg.crop;
    }
    return true;
}

static bool viewer_tab_open(viewer_tab *tab) {
    if (!tab || !tab->file_path) return false;
    tab->doc = doc_open(tab->file_path);
    if (!tab->doc) return false;
    if (tab->is_image) {
        tab->standalone_image = doc_load_standalone_image(tab->doc, tab->file_path);
        if (!tab->standalone_image) {
            doc_close(tab->doc);
            tab->doc = nullptr;
            return false;
        }
        tab->total_doc_pages = 1;
        tab->content_pages = malloc(sizeof(int));
        if (tab->content_pages) {
            tab->content_pages[0] = 0;
            tab->content_page_count = 1;
        }
    } else {
        if (tab->is_reflowable) doc_layout_reflowable(tab->doc, (double)tab->html_page_width, (double)tab->html_page_width * 1.414, 12.0);
        viewer_tab_find_content_pages(tab);
        if (tab->content_page_count == 0) {
            doc_close(tab->doc);
            tab->doc = nullptr;
            return false;
        }
        tab->toc = doc_get_toc(tab->doc, &tab->toc_count);
    }
    viewer_tab_prefetch_page_dims(tab);
    tab->watcher = file_watcher_create(tab->file_path);
    return true;
}

static void viewer_tab_free(viewer_tab *tab) {
    if (!tab) return;
    if (tab->watcher) file_watcher_destroy(tab->watcher);
    fz_context *ctx = tab->doc ? doc_get_context(tab->doc) : nullptr;
    if (tab->current_links) doc_free_page_links(tab->current_links, tab->current_links_count);
    if (tab->toc) doc_free_toc(tab->toc, tab->toc_count);
    if (tab->standalone_image && ctx) fz_drop_pixmap(ctx, tab->standalone_image);
    if (tab->doc) doc_close(tab->doc);

    free(tab->file_path);
    free(tab->display_name);
    free(tab->file_type);
    free(tab->content_pages);
    free(tab->page_widths);
    free(tab->page_heights);
    free(tab->page_pixel_heights);
    free(tab->page_y_starts);
    tab->page_widths = nullptr;
    tab->page_heights = nullptr;
    tab->page_pixel_heights = nullptr;
    tab->page_y_starts = nullptr;
    tab->page_dims_count = 0;
    memset(tab, 0, sizeof(*tab));
}

static void viewer_display_current_page(doc_viewer *v);

static void viewer_render_tab_bar(doc_viewer *v) {
    PROFILE_SCOPE("viewer", "viewer_render_tabs");
    if (!v || v->tab_count == 0) return;

    printf("\033[1;1H\033[K");
    int cur_col = 1;
    for (size_t i = 0; i < v->tab_count; i++) {
        viewer_tab *tab = &v->tabs[i];
        bool is_active = (i == v->active_tab_idx);

        char title[32];
        const char *name = tab->display_name ? tab->display_name : "doc";
        if (strlen(name) > 14) {
            snprintf(title, sizeof(title), "%.11s...", name);
        } else {
            snprintf(title, sizeof(title), "%s", name);
        }

        v->hit_boxes[i].start_col = cur_col;
        int tab_num = (i == 9) ? 0 : (int)(i + 1);

        if (is_active) {
            printf("\033[1;37;44m %d: %s \033[1;31;44m×\033[1;37;44m \033[0m", tab_num, title);
            int len = 1 + 2 + 1 + (int)strlen(title) + 1 + 1 + 1; // " %d: %s × "
            v->hit_boxes[i].close_col = cur_col + len - 3;
            v->hit_boxes[i].end_col = cur_col + len - 1;
            cur_col += len;
        } else {
            printf("\033[38;5;248;48;5;236m %d: %s \033[0m", tab_num, title);
            int len = 1 + 2 + 1 + (int)strlen(title) + 1; // " %d: %s "
            v->hit_boxes[i].close_col = -1;
            v->hit_boxes[i].end_col = cur_col + len - 1;
            cur_col += len;
        }

        if (cur_col <= v->term_cols) {
            printf("\033[48;5;234m \033[0m");
            cur_col++;
        }
    }
    for (size_t i = v->tab_count; i < MAX_TABS; i++) {
        v->hit_boxes[i] = (tab_hit_box){0, 0, -1};
    }
    if (cur_col <= v->term_cols) {
        printf("\033[48;5;234m\033[1;%dH\033[K\033[0m", cur_col);
    }
}

static void viewer_display_current_page(doc_viewer *v) {
    PROFILE_SCOPE("viewer", "viewer_render");
    profile_record_frame();
    if (!v || v->tab_count == 0) return;
    viewer_tab *tab = &v->tabs[v->active_tab_idx];

    term_get_size(&v->term_cols, &v->term_rows);
    if (v->term_cols <= 0) v->term_cols = 80;
    if (v->term_rows <= 0) v->term_rows = 24;

    int total_w_px = 0, total_h_px = 0;
    term_get_pixel_size(&total_w_px, &total_h_px);
    if (total_w_px <= 0) total_w_px = (int)((double)v->term_cols * v->cell_w_px + 0.5);
    if (total_h_px <= 0) total_h_px = (int)((double)v->term_rows * v->cell_h_px + 0.5);

    int start_row = 1;
    int content_rows = v->term_rows;
    if (content_rows < 1) content_rows = 1;
    int content_h_px = (int)((double)content_rows * v->cell_h_px + 0.5);
    if (content_h_px <= 0) content_h_px = total_h_px;

    viewer_update_tab_layout(v, tab, total_w_px, content_h_px);

    size_t cur_idx = 0;
    for (size_t i = 0; i < tab->content_page_count; i++) {
        if (tab->page_y_starts[i] <= tab->virtual_y + (content_h_px / 3)) cur_idx = i;
    }
    tab->current_page_idx = (int)cur_idx;
    int current_doc_page = (tab->content_pages && cur_idx < tab->content_page_count) ? tab->content_pages[cur_idx] : 0;

    viewer_debug("DISPLAY tab=%zu/%zu virt_y=%lld/%lld cur_p=%zu doc_p=%d fit=%s zoom=%.2f px=%dx%d",
                 v->active_tab_idx + 1, v->tab_count, (long long)tab->virtual_y, (long long)tab->max_scroll_y,
                 cur_idx + 1, current_doc_page, viewer_fit_name(tab->fit), tab->zoom, total_w_px, content_h_px);

    fz_context *ctx = tab->doc ? doc_get_context(tab->doc) : nullptr;
    if (!ctx) return;

    fz_pixmap *vpix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), fz_make_irect(0, 0, total_w_px, content_h_px), nullptr, 1);
    if (!vpix) return;
    fz_clear_pixmap_with_value(ctx, vpix, 0x18);

    for (size_t i = 0; i < tab->content_page_count; i++) {
        int64_t y0 = tab->page_y_starts[i];
        int slot_h = tab->page_pixel_heights[i];
        int64_t y1 = y0 + slot_h;
        if (y1 <= tab->virtual_y) continue;
        if (y0 >= tab->virtual_y + content_h_px) break;

        int doc_p = tab->content_pages[i];
        float pw = (tab->page_widths && i < tab->page_dims_count) ? tab->page_widths[i] : 612.0f;
        float ph = (tab->page_heights && i < tab->page_dims_count) ? tab->page_heights[i] : 792.0f;
        if (pw <= 0.0f) pw = 612.0f;
        if (ph <= 0.0f) ph = 792.0f;

        double base_scale;
        if (tab->fit == FIT_WIDTH) {
            base_scale = (double)total_w_px / (double)pw;
        } else if (tab->fit == FIT_HEIGHT) {
            base_scale = (double)content_h_px / (double)ph;
        } else {
            double sw = (double)total_w_px / (double)pw;
            double sh = (double)content_h_px / (double)ph;
            base_scale = (sw < sh) ? sw : sh;
        }
        double scale = base_scale * tab->zoom;
        int page_pixel_w = (int)(pw * scale + 0.5);
        int page_pixel_h = (int)(ph * scale + 0.5);
        if (page_pixel_w < 1) page_pixel_w = 1;
        if (page_pixel_h < 1) page_pixel_h = 1;

        int slice_w;
        int sx;
        int dst_x;
        if (page_pixel_w <= total_w_px) {
            slice_w = page_pixel_w;
            sx = 0;
            dst_x = (total_w_px - page_pixel_w) / 2;
        } else {
            slice_w = total_w_px;
            sx = (page_pixel_w - total_w_px) / 2;
            dst_x = 0;
        }

        int64_t page_y0 = y0;
        if (tab->fit != FIT_WIDTH && page_pixel_h < content_h_px) {
            page_y0 += (content_h_px - page_pixel_h) / 2;
        }
        int64_t page_y1 = page_y0 + page_pixel_h;

        int64_t v0 = (tab->virtual_y > page_y0) ? tab->virtual_y : page_y0;
        int64_t v1 = (tab->virtual_y + content_h_px < page_y1) ? (tab->virtual_y + content_h_px) : page_y1;
        if (v1 <= v0) continue;

        int slice_h = (int)(v1 - v0);
        int src_y = (int)(v0 - page_y0);
        int dst_y = (int)(v0 - tab->virtual_y);

        char vkey[256];
        snprintf(vkey, sizeof(vkey), "doc:%p:p%d:s%.4f:sx%d:y%d:w%d:h%d", (void *)tab->doc, doc_p, scale, sx, src_y, slice_w, slice_h);
        fz_pixmap *spix = cache_get(v->cache, ctx, vkey);
        if (!spix) {
            spix = doc_render_page_viewport(tab->doc, doc_p, scale, sx, src_y, slice_w, slice_h);
            if (spix) {
                cache_put(v->cache, ctx, vkey, spix, nullptr);
            }
        }
        if (spix) {
            for (int r = 0; r < spix->h && (dst_y + r) < content_h_px; r++) {
                unsigned char *dst_row = vpix->samples + (size_t)(dst_y + r) * vpix->stride + (size_t)dst_x * vpix->n;
                unsigned char *src_row = spix->samples + (size_t)r * spix->stride;
                memcpy(dst_row, src_row, (size_t)spix->w * spix->n);
            }
            fz_drop_pixmap(ctx, spix);
        }

        if (i == cur_idx) {
            tab->click_map.page_num = doc_p;
            tab->click_map.col_offset = 1;
            tab->click_map.row_offset = start_row;
            tab->click_map.width_chars = v->term_cols;
            tab->click_map.height_lines = content_rows;
            tab->click_map.pixel_width = total_w_px;
            tab->click_map.pixel_height = content_h_px;
            tab->click_map.crop_x0 = (double)sx / ((double)pw * scale);
            tab->click_map.crop_x1 = tab->click_map.crop_x0 + (double)slice_w / ((double)pw * scale);
            tab->click_map.crop_y0 = (double)src_y / ((double)ph * scale);
            tab->click_map.crop_y1 = tab->click_map.crop_y0 + (double)slice_h / ((double)ph * scale);
        }
    }

    if (tab->crop.top > 0.0 || tab->crop.bottom > 0.0 || tab->crop.left > 0.0 || tab->crop.right > 0.0) {
        fz_pixmap *cr = img_crop(ctx, vpix, &tab->crop);
        if (cr) { fz_drop_pixmap(ctx, vpix); vpix = cr; }
    }

    if (tab->tint == TINT_DARK) img_smart_invert(ctx, vpix);
    else if (tab->tint == TINT_INVERT) img_simple_invert(ctx, vpix);
    else if (tab->tint == TINT_DIM) img_dim_tint(ctx, vpix);

    term_begin_sync();
    term_gfx_clear(v->gfx);
    term_clear_screen();

    char temp_png[PATH_MAX];
    snprintf(temp_png, sizeof(temp_png), "%s/view.png", v->temp_dir);
    (void)term_gfx_draw_pixmap(v->gfx, ctx, vpix, temp_png, 1, start_row, v->term_cols, content_rows, total_w_px, content_h_px);
    fz_drop_pixmap(ctx, vpix);

    if (viewer_is_tab_bar_visible(v)) {
        viewer_render_tab_bar(v);
    }

    if (tab->current_links) { doc_free_page_links(tab->current_links, tab->current_links_count); tab->current_links = nullptr; }
    tab->current_links = doc_get_page_links(tab->doc, current_doc_page, &tab->current_links_count);

    if (get_time_ms() < v->status_until_ms && v->status_msg[0]) {
        printf("\033[%d;1H\033[7m %s \033[0m", v->term_rows, v->status_msg);
    }
    term_end_sync();
#if defined(__linux__)
    malloc_trim(0);
#endif
}

static void viewer_goto_page(doc_viewer *v, int page_idx) {
    if (v->tab_count == 0) return;
    viewer_dismiss_hud(v);
    viewer_tab *tab = &v->tabs[v->active_tab_idx];
    if (page_idx < 0) page_idx = 0;
    if (page_idx >= (int)tab->content_page_count) page_idx = (int)tab->content_page_count - 1;
    if (tab->back_count < 64) {
        tab->back_stack[tab->back_count++] = (view_loc){ .page = tab->current_page_idx, .y = (double)tab->virtual_y };
    }
    tab->current_page_idx = page_idx;
    if (tab->page_y_starts) tab->virtual_y = tab->page_y_starts[page_idx];
    viewer_display_current_page(v);
}

static bool viewer_handle_link_click(doc_viewer *v, int col, int row) {
    if (v->tab_count == 0) return false;
    viewer_tab *tab = &v->tabs[v->active_tab_idx];
    if (!tab->current_links || tab->current_links_count == 0) return false;
    if (col < tab->click_map.col_offset || col >= tab->click_map.col_offset + tab->click_map.width_chars ||
        row < tab->click_map.row_offset || row >= tab->click_map.row_offset + tab->click_map.height_lines) return false;

    double norm_x = (double)(col - tab->click_map.col_offset) / (double)tab->click_map.width_chars;
    double norm_y = (double)(row - tab->click_map.row_offset) / (double)tab->click_map.height_lines;
    double cy = tab->click_map.crop_y0 + norm_y * (tab->click_map.crop_y1 - tab->click_map.crop_y0);
    float pw = 612.0f, ph = 792.0f;
    size_t click_p_idx = 0;
    for (size_t i = 0; i < tab->content_page_count; i++) {
        if (tab->content_pages[i] == tab->click_map.page_num) {
            click_p_idx = i;
            break;
        }
    }
    if (tab->page_widths && click_p_idx < tab->page_dims_count) pw = tab->page_widths[click_p_idx];
    if (tab->page_heights && click_p_idx < tab->page_dims_count) ph = tab->page_heights[click_p_idx];
    double pdf_x = norm_x * (double)pw, pdf_y = cy * (double)ph;

    for (size_t i = 0; i < tab->current_links_count; i++) {
        auto l = &tab->current_links[i];
        if (pdf_x >= l->x0 - 4.0 && pdf_x <= l->x1 + 4.0 && pdf_y >= l->y0 - 8.0 && pdf_y <= l->y1 + 8.0) {
            if (l->is_external && l->uri) {
                char cmd[1024]; snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" >/dev/null 2>&1 &", l->uri);
                (void)system(cmd);
                viewer_set_status(v, "Opened external link", 1500);
                viewer_display_current_page(v);
                return true;
            } else if (l->dest_page >= 0) {
                for (size_t p = 0; p < tab->content_page_count; p++) {
                    if (tab->content_pages[p] == l->dest_page) {
                        viewer_goto_page(v, (int)p);
                        return true;
                    }
                }
            }
            return true;
        }
    }
    return false;
}

[[nodiscard]] doc_viewer *viewer_create(const char *path) {
    if (!path) return nullptr;
    auto v = (doc_viewer *)calloc(1, sizeof(doc_viewer));
    if (!v) return nullptr;

    snprintf(v->temp_dir, sizeof(v->temp_dir), "/tmp/pdfcli_%d_%lu", getpid(), (unsigned long)time(nullptr));
    mkdir(v->temp_dir, 0700);

    if (!viewer_tab_init(&v->tabs[0], path)) {
        free(v);
        return nullptr;
    }
    v->tab_count = 1;
    v->active_tab_idx = 0;

    v->cache = cache_create(4);
    v->gfx = term_gfx_init();
    return v;
}

[[nodiscard]] bool viewer_open(doc_viewer *v) {
    if (!v || v->tab_count == 0) return false;
    if (!viewer_tab_open(&v->tabs[0])) return false;
    v->ctl_srv = ctl_server_create(getpid());
    return true;
}

[[nodiscard]] bool viewer_add_tab(doc_viewer *v, const char *path) {
    if (!v || !path) return false;
    if (v->tab_count >= MAX_TABS) {
        viewer_set_status(v, "Tab limit reached (max 10 tabs)", 1500);
        return false;
    }
    size_t idx = v->tab_count;
    if (!viewer_tab_init(&v->tabs[idx], path)) return false;
    if (!viewer_tab_open(&v->tabs[idx])) {
        viewer_tab_free(&v->tabs[idx]);
        return false;
    }
    v->tab_count++;
    v->active_tab_idx = idx;
    term_set_title(v->tabs[idx].file_path);
    char msg[128];
    snprintf(msg, sizeof(msg), "Opened Tab %zu: %s", v->tab_count, v->tabs[idx].display_name);
    viewer_set_status(v, msg, 1500);
    return true;
}

void viewer_switch_tab(doc_viewer *v, size_t index) {
    PROFILE_SCOPE("viewer", "viewer_switch_tab");
    if (!v || index >= v->tab_count || index == v->active_tab_idx) return;
    size_t old_idx = v->active_tab_idx;
    viewer_tab_save_config(&v->tabs[v->active_tab_idx]);

    v->active_tab_idx = index;
    term_set_title(v->tabs[v->active_tab_idx].file_path);
    char msg[128];
    snprintf(msg, sizeof(msg), "Tab %zu/%zu: %s", index + 1, v->tab_count, v->tabs[index].display_name);
    viewer_set_status(v, msg, 1000);
    term_log_keystroke("TAB SWITCH: from tab %zu to tab %zu (\"%s\", %zu pages)",
                       old_idx + 1, index + 1, v->tabs[index].display_name, v->tabs[index].content_page_count);
    viewer_display_current_page(v);
}

void viewer_set_active_tab(doc_viewer *v, size_t index) {
    if (!v || index >= v->tab_count) return;
    v->active_tab_idx = index;
}

void viewer_close_tab(doc_viewer *v, size_t index) {
    if (!v || index >= v->tab_count) return;
    char closed_name[128] = {};
    if (v->tabs[index].display_name) snprintf(closed_name, sizeof(closed_name), "%s", v->tabs[index].display_name);
    viewer_tab_save_config(&v->tabs[index]);
    if (v->cache && v->tabs[index].doc) {
        cache_clear(v->cache, doc_get_context(v->tabs[index].doc));
    }
    viewer_tab_free(&v->tabs[index]);

    for (size_t i = index; i + 1 < v->tab_count; i++) {
        v->tabs[i] = v->tabs[i + 1];
    }
    memset(&v->tabs[v->tab_count - 1], 0, sizeof(viewer_tab));
    v->tab_count--;
    term_log_keystroke("TAB CLOSE: closed tab %zu (\"%s\"), remaining tabs: %zu", index + 1, closed_name, v->tab_count);

    if (v->tab_count == 0) {
        v->want_back = false;
        return;
    }

    if (v->active_tab_idx >= v->tab_count) {
        v->active_tab_idx = v->tab_count - 1;
    } else if (v->active_tab_idx > index) {
        v->active_tab_idx--;
    }
    term_set_title(v->tabs[v->active_tab_idx].file_path);
    viewer_display_current_page(v);
}

void viewer_next_tab(doc_viewer *v) {
    if (!v || v->tab_count <= 1) return;
    viewer_switch_tab(v, (v->active_tab_idx + 1) % v->tab_count);
}

void viewer_prev_tab(doc_viewer *v) {
    if (!v || v->tab_count <= 1) return;
    viewer_switch_tab(v, (v->active_tab_idx + v->tab_count - 1) % v->tab_count);
}

[[nodiscard]] size_t viewer_get_tab_count(const doc_viewer *v) {
    return v ? v->tab_count : 0;
}

[[nodiscard]] size_t viewer_get_active_tab(const doc_viewer *v) {
    return v ? v->active_tab_idx : 0;
}

void viewer_destroy(doc_viewer *v) {
    if (!v) return;
    if (v->ctl_srv) ctl_server_destroy(v->ctl_srv);

    fz_context *ctx = (v->tab_count > 0 && v->tabs[0].doc) ? doc_get_context(v->tabs[0].doc) : nullptr;
    if (v->cache) {
        cache_destroy(v->cache, ctx);
        v->cache = nullptr;
    }

    for (size_t i = 0; i < v->tab_count; i++) {
        viewer_tab_free(&v->tabs[i]);
    }
    if (v->gfx) term_gfx_destroy(v->gfx);

    char rm_cmd[2048];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", v->temp_dir);
    (void)system(rm_cmd);
    free(v);
}

static void viewer_handle_ipc(doc_viewer *v) {
    PROFILE_SCOPE("ipc", "viewer_handle_ipc");
    int cfd = -1; char cmd[64] = {}, args[2048] = {};
    if (!ctl_server_recv(v->ctl_srv, &cfd, cmd, sizeof(cmd), args, sizeof(args))) return;

    if (v->tab_count == 0) {
        char *r = ctl_serialize_simple_response(false, "no open tabs");
        ctl_server_send_response(cfd, r);
        free(r);
        return;
    }
    viewer_tab *tab = &v->tabs[v->active_tab_idx];

    if (!strcmp(cmd, "status")) {
        ctl_viewer_state st = {
            .pid = getpid(),
            .page = tab->current_page_idx + 1,
            .pages = (int)tab->content_page_count,
            .zoom = tab->zoom,
            .crop = tab->crop,
            .is_image = tab->is_image,
            .tab_count = (int)v->tab_count,
            .active_tab = (int)v->active_tab_idx + 1
        };
        snprintf(st.path, sizeof(st.path), "%s", tab->file_path);
        snprintf(st.name, sizeof(st.name), "%s", tab->display_name);
        snprintf(st.file_type, sizeof(st.file_type), "%s", tab->file_type);
        snprintf(st.fit, sizeof(st.fit), "%s", viewer_fit_name(tab->fit));
        snprintf(st.tint, sizeof(st.tint), "%s", (tab->tint == TINT_DARK) ? "dark" : ((tab->tint == TINT_DIM) ? "dim" : ((tab->tint == TINT_INVERT) ? "invert" : "white")));
        ctl_get_socket_path(st.pid, st.socket, sizeof(st.socket));
        char *r = ctl_serialize_state_response(true, nullptr, &st);
        ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "page")) {
        char val[64];
        if (ctl_args_get(args, "page", val, sizeof(val)) || ctl_args_get(args, "to", val, sizeof(val))) {
            int p = atoi(val);
            if (p >= 1 && p <= (int)tab->content_page_count) viewer_goto_page(v, p - 1);
        }
        char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "next")) {
        viewer_goto_page(v, tab->current_page_idx + 1);
        char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "prev")) {
        viewer_goto_page(v, tab->current_page_idx - 1);
        char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "zoom")) {
        char val[64];
        if (ctl_args_get(args, "level", val, sizeof(val))) {
            double z = atof(val);
            if (z >= MIN_ZOOM && z <= MAX_ZOOM) {
                int cur_p = tab->current_page_idx;
                double frac = 0.0;
                if (tab->page_pixel_heights && tab->page_y_starts && cur_p < (int)tab->content_page_count && tab->page_pixel_heights[cur_p] > 0) {
                    frac = (double)(tab->virtual_y - tab->page_y_starts[cur_p]) / (double)tab->page_pixel_heights[cur_p];
                    if (frac < 0.0) frac = 0.0;
                    if (frac > 1.0) frac = 1.0;
                }
                tab->zoom = z;
                int total_w_px = 0, total_h_px = 0;
                term_get_pixel_size(&total_w_px, &total_h_px);
                if (total_w_px <= 0) total_w_px = (int)((double)v->term_cols * v->cell_w_px + 0.5);
                int c_rows = v->term_rows;
                int c_h = (int)((double)c_rows * v->cell_h_px + 0.5);
                if (c_h <= 0) c_h = total_h_px;
                viewer_update_tab_layout(v, tab, total_w_px, c_h);
                if (tab->page_pixel_heights && tab->page_y_starts && cur_p < (int)tab->content_page_count) {
                    tab->virtual_y = tab->page_y_starts[cur_p] + (int64_t)(frac * tab->page_pixel_heights[cur_p]);
                    if (tab->virtual_y > tab->max_scroll_y) tab->virtual_y = tab->max_scroll_y;
                    if (tab->virtual_y < 0) tab->virtual_y = 0;
                }
                viewer_display_current_page(v);
            }
        }
        char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "fit")) {
        char val[64];
        if (ctl_args_get(args, "mode", val, sizeof(val))) {
            tab->fit = !strcmp(val, "width") ? FIT_WIDTH : (!strcmp(val, "auto") ? FIT_AUTO : FIT_HEIGHT);
            viewer_display_current_page(v);
        }
        char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "tint")) {
        char val[64];
        if (ctl_args_get(args, "mode", val, sizeof(val))) {
            tab->tint = !strcmp(val, "dark") ? TINT_DARK : (!strcmp(val, "dim") ? TINT_DIM : (!strcmp(val, "invert") ? TINT_INVERT : TINT_OFF));
            viewer_display_current_page(v);
        }
        char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "toc")) {
        char *r = ctl_serialize_toc_response(true, nullptr, tab->toc, tab->toc_count);
        ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "tab")) {
        char action[64] = {}, param[512] = {};
        (void)ctl_args_get(args, "action", action, sizeof(action));
        (void)ctl_args_get(args, "param", param, sizeof(param));

        if (!strcmp(action, "next")) {
            viewer_next_tab(v);
            char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
        } else if (!strcmp(action, "prev")) {
            viewer_prev_tab(v);
            char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
        } else if (!strcmp(action, "switch") || (isdigit((unsigned char)action[0]) && atoi(action) > 0)) {
            int target = isdigit((unsigned char)action[0]) ? atoi(action) : atoi(param);
            if (target >= 1 && target <= (int)v->tab_count) {
                viewer_switch_tab(v, (size_t)(target - 1));
                char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
            } else {
                char *r = ctl_serialize_simple_response(false, "tab index out of range"); ctl_server_send_response(cfd, r); free(r);
            }
        } else if (!strcmp(action, "open")) {
            if (param[0] && viewer_add_tab(v, param)) {
                viewer_display_current_page(v);
                char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
            } else {
                char *r = ctl_serialize_simple_response(false, "failed to open tab"); ctl_server_send_response(cfd, r); free(r);
            }
        } else if (!strcmp(action, "close")) {
            size_t c_idx = param[0] ? (size_t)(atoi(param) - 1) : v->active_tab_idx;
            viewer_close_tab(v, c_idx);
            char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
        } else {
            // "list" or default: return JSON tab info
            yyjson_mut_doc *tdoc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val *troot = yyjson_mut_obj(tdoc);
            yyjson_mut_doc_set_root(tdoc, troot);
            yyjson_mut_obj_add_bool(tdoc, troot, "ok", true);
            yyjson_mut_obj_add_int(tdoc, troot, "tab_count", (int)v->tab_count);
            yyjson_mut_obj_add_int(tdoc, troot, "active_tab", (int)v->active_tab_idx + 1);
            yyjson_mut_val *tarr = yyjson_mut_arr(tdoc);
            for (size_t i = 0; i < v->tab_count; i++) {
                yyjson_mut_val *item = yyjson_mut_obj(tdoc);
                yyjson_mut_obj_add_int(tdoc, item, "index", (int)i + 1);
                yyjson_mut_obj_add_str(tdoc, item, "name", v->tabs[i].display_name);
                yyjson_mut_obj_add_str(tdoc, item, "path", v->tabs[i].file_path);
                yyjson_mut_obj_add_int(tdoc, item, "pages", (int)v->tabs[i].content_page_count);
                yyjson_mut_arr_append(tarr, item);
            }
            yyjson_mut_obj_add_val(tdoc, troot, "tabs", tarr);
            char *r = yyjson_mut_write(tdoc, 0, nullptr);
            yyjson_mut_doc_free(tdoc);
            ctl_server_send_response(cfd, r); free(r);
        }
    } else if (!strcmp(cmd, "quit")) {
        char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
        exit(0);
    } else {
        char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
    }
}

static void viewer_show_help(doc_viewer *v) {
    term_clear_screen();
    int r = 2;
    printf("\033[%d;4H\033[1;36mpdf-cli (C23 ISO/IEC 9899:2024) — Keyboard & Mouse Shortcuts\033[0m\033[K\r\n", r++);
    printf("\033[%d;4H\033[2m─────────────────────────────────────────────────────────────────────────────\033[0m\033[K\r\n", r++);
    r++;
    printf("\033[%d;4H\033[1;33mTab Navigation\033[0m\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mCtrl+1 .. Ctrl+9\033[0m (Alt+1..9) Switch directly to tab 1 through 9\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mCtrl+0\033[0m           (Alt+0)     Switch to tab 10 / last tab\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mCtrl+Tab\033[0m / \033[1mgt\033[0m                Cycle to next tab\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mCtrl+Shift+Tab\033[0m / \033[1mgT\033[0m          Cycle to previous tab\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mCtrl+T\033[0m                       Open document in new tab\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mCtrl+W\033[0m                       Close current tab\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mClick Tab / Click ×\033[0m          Switch to tab / close tab\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mMouse Wheel on Tab Bar\033[0m       Cycle through tabs\033[K\r\n", r++);
    r++;
    printf("\033[%d;4H\033[1;33mPage Navigation\033[0m\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mRight\033[0m / \033[1mj\033[0m / \033[1mJ\033[0m       Next page\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mLeft\033[0m  / \033[1mk\033[0m / \033[1mK\033[0m       Previous page\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mg\033[0m                       Go to specific PDF page number\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mT\033[0m                       Table of contents modal (fuzzy search, Enter jumps)\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mb\033[0m                       Back to file picker\033[K\r\n", r++);
    r++;
    printf("\033[%d;4H\033[1;33mContinuous Scrolling\033[0m\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mPgDn\033[0m / \033[1mSpace\033[0m           Smooth continuous scroll down (~35%% screen, overlap)\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mPgUp\033[0m                    Smooth continuous scroll up (~35%% screen, overlap)\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mWheelDown\033[0m / \033[1mWheelUp\033[0m   Mouse wheel smooth scroll\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mDown\033[0m / \033[1mUp\033[0m             Fine scroll (fit-width) / Page (fit-height)\033[K\r\n", r++);
    printf("\033[%d;6H\033[1m[\033[0m    / \033[1m]\033[0m              Decrease / increase scroll step ratio\033[K\r\n", r++);
    r++;
    printf("\033[%d;4H\033[1;33mDisplay & Adjustments\033[0m\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mf\033[0m                       Toggle fit mode (Fit width / Fit height)\033[K\r\n", r++);
    printf("\033[%d;6H\033[1m+\033[0m    / \033[1m-\033[0m              Zoom in / out\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mD\033[0m                       Cycle tint (white / dim / dark / invert)\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mi\033[0m                       Toggle document info & tab bar HUD\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mr\033[0m                       Refresh display & re-detect cell size\033[K\r\n", r++);
    printf("\033[%d;6H\033[1mq\033[0m                       Quit viewer\033[K\r\n", r++);
    r++;
    printf("\033[%d;4H\033[2mPress any key, 'h', or Esc to return to document...\033[0m\033[K\r\n", r);
    fflush(stdout);
    char buf[32];
    term_read_key(STDIN_FILENO, buf, sizeof(buf), nullptr, nullptr);
    viewer_display_current_page(v);
}

[[nodiscard]] bool viewer_run(doc_viewer *v) {
    if (!v || v->tab_count == 0) return false;
    if (!term_enter_raw(&v->old_term)) return false;

    term_enter_alt_screen();
    term_detect_cell_size(&v->cell_w_px, &v->cell_h_px);
    term_gfx_probe_kitty_mode(v->gfx, v->temp_dir);
    term_get_size(&v->term_cols, &v->term_rows);

    term_enable_kitty_keyboard();
    term_enable_mouse();
    term_hide_cursor();
    term_set_title(v->tabs[v->active_tab_idx].file_path);

    term_log_keystroke("================================================================================");
    term_log_keystroke("=== pdf-cli Viewer Session Started (PID %d) ===", getpid());
    term_log_keystroke("================================================================================");
    term_log_keystroke("Terminal: TERM='%s' TERM_PROGRAM='%s' GFX_PROTO=%d (Cols=%d, Rows=%d, Cell=%.1fx%.1f)",
                       getenv("TERM") ? getenv("TERM") : "",
                       getenv("TERM_PROGRAM") ? getenv("TERM_PROGRAM") : "",
                       term_gfx_get_proto(v->gfx), v->term_cols, v->term_rows, v->cell_w_px, v->cell_h_px);
    term_log_keystroke("Open tabs (%zu total):", v->tab_count);
    for (size_t i = 0; i < v->tab_count; i++) {
        term_log_keystroke("  [%zu] %s (path: %s, pages: %zu)",
                           i + 1, v->tabs[i].display_name, v->tabs[i].file_path, v->tabs[i].content_page_count);
    }
    term_log_keystroke("Active initial tab: %zu (\"%s\")",
                       v->active_tab_idx + 1, v->tabs[v->active_tab_idx].display_name);

    viewer_display_current_page(v);

    [[maybe_unused]] uint64_t last_wheel_ms = 0;

    while (v->tab_count > 0) {
        if (term_poll_resize()) {
            term_get_size(&v->term_cols, &v->term_rows);
            term_detect_cell_size(&v->cell_w_px, &v->cell_h_px);
            term_log_keystroke("WINDOW RESIZE: %dx%d (cell: %.1fx%.1f)", v->term_cols, v->term_rows, v->cell_w_px, v->cell_h_px);
            viewer_display_current_page(v);
        }

        // Build poll fds array: STDIN, ctl_server, plus each open tab's file watcher
        struct pollfd pfds[2 + MAX_TABS] = {};
        pfds[0].fd = STDIN_FILENO; pfds[0].events = POLLIN;
        pfds[1].fd = ctl_server_get_fd(v->ctl_srv); pfds[1].events = POLLIN;
        nfds_t n_pfds = 2;
        for (size_t i = 0; i < v->tab_count; i++) {
            int wfd = file_watcher_get_fd(v->tabs[i].watcher);
            if (wfd >= 0) {
                pfds[n_pfds].fd = wfd;
                pfds[n_pfds].events = POLLIN;
                n_pfds++;
            }
        }

        uint64_t now_before = get_time_ms();
        int poll_timeout = 500;
        uint64_t next_expire = 0;
        if (v->tab_bar_until_ms > now_before) {
            next_expire = v->tab_bar_until_ms;
        }
        if (v->status_until_ms > now_before) {
            if (next_expire == 0 || v->status_until_ms < next_expire) {
                next_expire = v->status_until_ms;
            }
        }
        if (next_expire > now_before) {
            int rem = (int)(next_expire - now_before) + 1;
            if (rem < poll_timeout) poll_timeout = rem;
        }

        if (poll(pfds, n_pfds, poll_timeout) < 0) continue;

        uint64_t now_after = get_time_ms();
        bool hud_expired = false;
        if (v->tab_bar_until_ms > 0 && now_after >= v->tab_bar_until_ms) {
            v->tab_bar_until_ms = 0;
            memset(v->hit_boxes, 0, sizeof(v->hit_boxes));
            hud_expired = true;
        }
        if (v->status_until_ms > 0 && now_after >= v->status_until_ms) {
            v->status_until_ms = 0;
            v->status_msg[0] = '\0';
            hud_expired = true;
        }
        if (hud_expired && !(pfds[0].revents & POLLIN)) {
            viewer_display_current_page(v);
        }

        if (pfds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            break;
        }

        if (pfds[0].revents & POLLIN) {
            PROFILE_START("viewer", "viewer_handle_key");
            char key_buf[32] = {};
            int mouse_col = 0, mouse_row = 0;
            int key = term_read_key(STDIN_FILENO, key_buf, sizeof(key_buf), &mouse_col, &mouse_row);
            if (key < 0) {
                PROFILE_END("viewer", "viewer_handle_key");
                break;
            }

            viewer_tab *tab = &v->tabs[v->active_tab_idx];

            int total_w_px = 0, total_h_px = 0;
            term_get_pixel_size(&total_w_px, &total_h_px);
            if (total_h_px <= 0) total_h_px = (int)((double)v->term_rows * v->cell_h_px + 0.5);

            int content_h_px = total_h_px;

            int step_pg = (int)((double)content_h_px * tab->scroll_ratio);
            if (step_pg < 80) step_pg = 80;
            int step_line = (int)(v->cell_h_px * 3.0);
            if (step_line < 40) step_line = 40;

            viewer_debug("KEY code=%d(0x%X) mouse=(%d,%d) tab=%zu/%zu virt_y=%lld/%lld step_pg=%d fit=%s",
                         key, key, mouse_col, mouse_row, v->active_tab_idx + 1, v->tab_count,
                         (long long)tab->virtual_y, (long long)tab->max_scroll_y, step_pg, viewer_fit_name(tab->fit));

            // Ctrl+1 .. Ctrl+9 direct tab switching
            if (key >= KEY_CTRL_1 && key <= KEY_CTRL_9) {
                size_t target = (size_t)(key - KEY_CTRL_1);
                term_log_keystroke("ACTION: Ctrl+%zu (or Alt+%zu) pressed. Target tab: %zu (current active: %zu/%zu)",
                                   target + 1, target + 1, target + 1, v->active_tab_idx + 1, v->tab_count);
                if (target < v->tab_count) {
                    viewer_switch_tab(v, target);
                } else {
                    char tmsg[64];
                    snprintf(tmsg, sizeof(tmsg), "No tab %zu (only %zu open)", target + 1, v->tab_count);
                    term_log_keystroke("ACTION: Target tab %zu out of range (only %zu tabs open)", target + 1, v->tab_count);
                    viewer_set_status(v, tmsg, 1000);
                    viewer_display_current_page(v);
                }
            } else if (v->tab_count > 1 && key >= '1' && key <= '9') {
                size_t target = (size_t)(key - '1');
                term_log_keystroke("ACTION: Plain digit '%c' pressed with %zu tabs open -> Target tab: %zu",
                                   (char)key, v->tab_count, target + 1);
                if (target < v->tab_count) {
                    viewer_switch_tab(v, target);
                } else {
                    char tmsg[64];
                    snprintf(tmsg, sizeof(tmsg), "No tab %zu (only %zu open)", target + 1, v->tab_count);
                    term_log_keystroke("ACTION: Target tab %zu out of range (only %zu tabs open)", target + 1, v->tab_count);
                    viewer_set_status(v, tmsg, 1000);
                    viewer_display_current_page(v);
                }
            } else if (key == KEY_CTRL_0 || (v->tab_count > 1 && key == '0')) {
                term_log_keystroke("ACTION: Key %s pressed (Tab 10 / Last tab)", term_key_name(key));
                if (v->tab_count >= 10) {
                    viewer_switch_tab(v, 9);
                } else if (v->tab_count > 0) {
                    viewer_switch_tab(v, v->tab_count - 1);
                }
            } else if (key == KEY_CTRL_TAB) {
                term_log_keystroke("ACTION: Ctrl+Tab pressed -> Next tab");
                viewer_next_tab(v);
            } else if (key == KEY_CTRL_SHIFT_TAB) {
                term_log_keystroke("ACTION: Ctrl+Shift+Tab pressed -> Previous tab");
                viewer_prev_tab(v);
            } else if (key == KEY_CTRL_W) {
                term_log_keystroke("ACTION: Ctrl+W pressed -> Closing active tab %zu", v->active_tab_idx + 1);
                viewer_close_tab(v, v->active_tab_idx);
                if (v->tab_count == 0) break;
            } else if (key == KEY_CTRL_T) {
                term_log_keystroke("ACTION: Ctrl+T pressed -> Open new tab file picker");
                if (v->tab_count >= MAX_TABS) {
                    viewer_set_status(v, "Tab limit reached (max 10 tabs)", 1500);
                    viewer_display_current_page(v);
                } else {
                    char *cur_copy = strdup(tab->file_path);
                    char *dir = dirname(cur_copy);
                    file_list *files = file_search_dir(dir);
                    if (!files || files->count == 0) {
                        if (files) file_list_free(files);
                        files = file_search_broad();
                    }
                    if (files && files->count > 0) {
                        char *sel = picker_run(files);
                        file_list_free(files);
                        if (sel) {
                            term_log_keystroke("ACTION: Selected file from picker: '%s'", sel);
                            (void)viewer_add_tab(v, sel);
                            free(sel);
                        }
                    }
                    free(cur_copy);
                    viewer_display_current_page(v);
                }
            } else if (key == 'q' || key == KEY_CTRL_C) {
                v->want_back = false;
                break;
            } else if (key == 'b') {
                v->want_back = true;
                break;
            } else if (key == KEY_PAGE_DOWN || key == ' ' || key == 'd' || key == 4) {
                viewer_dismiss_hud(v);
                tab->virtual_y = (tab->virtual_y + step_pg <= tab->max_scroll_y) ? (tab->virtual_y + step_pg) : tab->max_scroll_y;
                viewer_display_current_page(v);
            } else if (key == KEY_PAGE_UP || key == 'u' || key == 21) {
                viewer_dismiss_hud(v);
                tab->virtual_y = (tab->virtual_y >= step_pg) ? (tab->virtual_y - step_pg) : 0;
                viewer_display_current_page(v);
            } else if (key == KEY_MOUSE_WHEEL_DOWN) {
                if (viewer_is_tab_bar_visible(v) && mouse_row == 1) {
                    if (v->tab_count > 1) viewer_next_tab(v);
                } else {
                    viewer_dismiss_hud(v);
                    int wstep = (int)((double)content_h_px * 0.15);
                    if (wstep < 50) wstep = 50;
                    tab->virtual_y = (tab->virtual_y + wstep <= tab->max_scroll_y) ? (tab->virtual_y + wstep) : tab->max_scroll_y;
                    viewer_display_current_page(v);
                }
            } else if (key == KEY_MOUSE_WHEEL_UP) {
                if (viewer_is_tab_bar_visible(v) && mouse_row == 1) {
                    if (v->tab_count > 1) viewer_prev_tab(v);
                } else {
                    viewer_dismiss_hud(v);
                    int wstep = (int)((double)content_h_px * 0.15);
                    if (wstep < 50) wstep = 50;
                    tab->virtual_y = (tab->virtual_y >= wstep) ? (tab->virtual_y - wstep) : 0;
                    viewer_display_current_page(v);
                }
            } else if (key == KEY_ARROW_DOWN || key == 'j' || key == 'J') {
                viewer_dismiss_hud(v);
                tab->virtual_y = (tab->virtual_y + step_line <= tab->max_scroll_y) ? (tab->virtual_y + step_line) : tab->max_scroll_y;
                viewer_display_current_page(v);
            } else if (key == KEY_ARROW_UP || key == 'k' || key == 'K') {
                viewer_dismiss_hud(v);
                tab->virtual_y = (tab->virtual_y >= step_line) ? (tab->virtual_y - step_line) : 0;
                viewer_display_current_page(v);
            } else if (key == KEY_ARROW_RIGHT || key == 'n' || key == 'N') {
                viewer_dismiss_hud(v);
                viewer_goto_page(v, tab->current_page_idx + 1);
            } else if (key == KEY_ARROW_LEFT || key == 'p' || key == 'P') {
                viewer_dismiss_hud(v);
                viewer_goto_page(v, tab->current_page_idx - 1);
            } else if (key == '[') {
                viewer_dismiss_hud(v);
                tab->scroll_ratio = fmax(0.10, tab->scroll_ratio - 0.05);
                char msg[64]; snprintf(msg, sizeof(msg), "Scroll step: %.0f%% screen", tab->scroll_ratio * 100.0);
                viewer_set_status(v, msg, 1000);
                viewer_display_current_page(v);
            } else if (key == ']') {
                viewer_dismiss_hud(v);
                tab->scroll_ratio = fmin(0.80, tab->scroll_ratio + 0.05);
                char msg[64]; snprintf(msg, sizeof(msg), "Scroll step: %.0f%% screen", tab->scroll_ratio * 100.0);
                viewer_set_status(v, msg, 1000);
                viewer_display_current_page(v);
            } else if (key == 'g') {
                viewer_dismiss_hud(v);
                // Peek next key for Vim-style tab commands ('gt' -> next tab, 'gT' -> prev tab)
                struct pollfd g_pfd = { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };
                char peek_buf[16] = {};
                int peek_c = 0, peek_r = 0;
                int peek_k = 0;
                if (poll(&g_pfd, 1, 250) > 0) {
                    peek_k = term_read_key(STDIN_FILENO, peek_buf, sizeof(peek_buf), &peek_c, &peek_r);
                }

                if (peek_k == 't') {
                    viewer_next_tab(v);
                } else if (peek_k == 'T') {
                    viewer_prev_tab(v);
                } else {
                    // Standard 'g' page jump prompt
                    char input[32] = {};
                    size_t input_len = 0;
                    if (peek_k >= '0' && peek_k <= '9') {
                        input[input_len++] = (char)peek_k;
                        input[input_len] = '\0';
                    }
                    term_show_cursor();
                    while (true) {
                        printf("\033[%d;1H\033[7m Go to PDF page (1-%d): %s\033[0m\033[K", v->term_rows, tab->total_doc_pages, input);
                        fflush(stdout);
                        char ch_buf[16] = {};
                        int ch = term_read_key(STDIN_FILENO, ch_buf, sizeof(ch_buf), nullptr, nullptr);
                        if (ch == 27 || ch == KEY_CTRL_C) { input_len = 0; break; }
                        if (ch == 10 || ch == 13) break;
                        if (ch == 127 || ch == 8) { if (input_len > 0) input[--input_len] = '\0'; }
                        else if (ch >= '0' && ch <= '9' && input_len < sizeof(input) - 1) {
                            input[input_len++] = (char)ch; input[input_len] = '\0';
                        }
                    }
                    term_hide_cursor();
                    if (input_len > 0) {
                        int p = atoi(input);
                        if (p >= 1 && p <= tab->total_doc_pages) {
                            int t_idx = -1;
                            for (size_t i = 0; i < tab->content_page_count; i++) {
                                if (tab->content_pages[i] == p - 1) { t_idx = (int)i; break; }
                            }
                            if (t_idx >= 0) viewer_goto_page(v, t_idx);
                        }
                    }
                    viewer_display_current_page(v);
                }
            } else if (key == 'f') {
                viewer_dismiss_hud(v);
                int cur_p = tab->current_page_idx;
                tab->fit = (tab->fit == FIT_WIDTH) ? FIT_HEIGHT : FIT_WIDTH;
                int c_rows = v->term_rows;
                int c_h = (int)((double)c_rows * v->cell_h_px + 0.5);
                if (c_h <= 0) c_h = total_h_px;
                viewer_update_tab_layout(v, tab, total_w_px, c_h);
                if (tab->page_y_starts) tab->virtual_y = tab->page_y_starts[cur_p];
                viewer_set_status(v, (tab->fit == FIT_WIDTH) ? "Fit: width (continuous scroll)" : "Fit: height (fit page)", 1200);
                viewer_display_current_page(v);
            } else if (key == '+' || key == '=') {
                viewer_dismiss_hud(v);
                int cur_p = tab->current_page_idx;
                double frac = 0.0;
                if (tab->page_pixel_heights && tab->page_y_starts && cur_p < (int)tab->content_page_count && tab->page_pixel_heights[cur_p] > 0) {
                    frac = (double)(tab->virtual_y - tab->page_y_starts[cur_p]) / (double)tab->page_pixel_heights[cur_p];
                    if (frac < 0.0) frac = 0.0;
                    if (frac > 1.0) frac = 1.0;
                }
                tab->zoom = fmin(tab->zoom + 0.1, MAX_ZOOM);
                int c_rows = v->term_rows;
                int c_h = (int)((double)c_rows * v->cell_h_px + 0.5);
                if (c_h <= 0) c_h = total_h_px;
                viewer_update_tab_layout(v, tab, total_w_px, c_h);
                if (tab->page_pixel_heights && tab->page_y_starts && cur_p < (int)tab->content_page_count) {
                    tab->virtual_y = tab->page_y_starts[cur_p] + (int64_t)(frac * tab->page_pixel_heights[cur_p]);
                    if (tab->virtual_y > tab->max_scroll_y) tab->virtual_y = tab->max_scroll_y;
                    if (tab->virtual_y < 0) tab->virtual_y = 0;
                }
                char zbuf[64]; snprintf(zbuf, sizeof(zbuf), "Zoom: %.0f%%", tab->zoom * 100.0);
                viewer_set_status(v, zbuf, 1000);
                viewer_display_current_page(v);
            } else if (key == '-' || key == '_') {
                viewer_dismiss_hud(v);
                int cur_p = tab->current_page_idx;
                double frac = 0.0;
                if (tab->page_pixel_heights && tab->page_y_starts && cur_p < (int)tab->content_page_count && tab->page_pixel_heights[cur_p] > 0) {
                    frac = (double)(tab->virtual_y - tab->page_y_starts[cur_p]) / (double)tab->page_pixel_heights[cur_p];
                    if (frac < 0.0) frac = 0.0;
                    if (frac > 1.0) frac = 1.0;
                }
                tab->zoom = fmax(tab->zoom - 0.1, MIN_ZOOM);
                int c_rows = v->term_rows;
                int c_h = (int)((double)c_rows * v->cell_h_px + 0.5);
                if (c_h <= 0) c_h = total_h_px;
                viewer_update_tab_layout(v, tab, total_w_px, c_h);
                if (tab->page_pixel_heights && tab->page_y_starts && cur_p < (int)tab->content_page_count) {
                    tab->virtual_y = tab->page_y_starts[cur_p] + (int64_t)(frac * tab->page_pixel_heights[cur_p]);
                    if (tab->virtual_y > tab->max_scroll_y) tab->virtual_y = tab->max_scroll_y;
                    if (tab->virtual_y < 0) tab->virtual_y = 0;
                }
                char zbuf[64]; snprintf(zbuf, sizeof(zbuf), "Zoom: %.0f%%", tab->zoom * 100.0);
                viewer_set_status(v, zbuf, 1000);
                viewer_display_current_page(v);
            } else if (key == 'D') {
                viewer_dismiss_hud(v);
                tab->tint = (tint_mode)(((unsigned)tab->tint + 1) % 4);
                char tbuf[64]; snprintf(tbuf, sizeof(tbuf), "Tint: %s", (tab->tint == TINT_DARK) ? "dark" : ((tab->tint == TINT_DIM) ? "dim" : ((tab->tint == TINT_INVERT) ? "invert" : "white")));
                viewer_set_status(v, tbuf, 1000);
                viewer_display_current_page(v);
            } else if (key == 'i') {
                viewer_toggle_hud(v);
                viewer_display_current_page(v);
            } else if (key == KEY_ESC || key == 27) {
                if (viewer_is_tab_bar_visible(v) || (get_time_ms() < v->status_until_ms)) {
                    viewer_dismiss_hud(v);
                    viewer_display_current_page(v);
                }
            } else if (key == 'T') {
                if (tab->toc && tab->toc_count > 0) {
                    int cur_doc_p = tab->content_pages ? tab->content_pages[tab->current_page_idx] : 0;
                    int target = toc_modal_run(tab->toc, tab->toc_count, cur_doc_p);
                    if (target >= 0) {
                        for (size_t p = 0; p < tab->content_page_count; p++) {
                            if (tab->content_pages[p] == target) { viewer_goto_page(v, (int)p); break; }
                        }
                    }
                    viewer_display_current_page(v);
                }
            } else if (key == 'h') {
                viewer_show_help(v);
            } else if (key == 'r') {
                term_detect_cell_size(&v->cell_w_px, &v->cell_h_px);
                viewer_display_current_page(v);
            } else if (key == KEY_HISTORY_BACK) {
                if (tab->back_count > 0) {
                    view_loc loc = tab->back_stack[--tab->back_count];
                    if (tab->fwd_count < 64) tab->fwd_stack[tab->fwd_count++] = (view_loc){ .page = tab->current_page_idx, .y = (double)tab->virtual_y };
                    viewer_goto_page(v, loc.page);
                }
            } else if (key == KEY_HISTORY_FORWARD) {
                if (tab->fwd_count > 0) {
                    view_loc loc = tab->fwd_stack[--tab->fwd_count];
                    if (tab->back_count < 64) tab->back_stack[tab->back_count++] = (view_loc){ .page = tab->current_page_idx, .y = (double)tab->virtual_y };
                    viewer_goto_page(v, loc.page);
                }
            } else if (key == KEY_MOUSE_LEFT || key == KEY_MOUSE_CTRL_CLICK) {
                term_log_keystroke("ACTION: Mouse click at col=%d, row=%d (tab_count=%zu, active_tab=%zu)",
                                   mouse_col, mouse_row, v->tab_count, v->active_tab_idx + 1);
                if (viewer_is_tab_bar_visible(v) && mouse_row == 1) {
                    bool hit = false;
                    for (size_t i = 0; i < v->tab_count; i++) {
                        if (mouse_col >= v->hit_boxes[i].start_col && mouse_col <= v->hit_boxes[i].end_col) {
                            hit = true;
                            if (v->hit_boxes[i].close_col > 0 &&
                                mouse_col >= v->hit_boxes[i].close_col - 1 &&
                                mouse_col <= v->hit_boxes[i].close_col + 1) {
                                term_log_keystroke("ACTION: Clicked [x] to close tab %zu (\"%s\")", i + 1, v->tabs[i].display_name);
                                viewer_close_tab(v, i);
                                if (v->tab_count == 0) break;
                            } else {
                                term_log_keystroke("ACTION: Clicked tab label to switch to tab %zu (\"%s\")", i + 1, v->tabs[i].display_name);
                                viewer_switch_tab(v, i);
                            }
                            break;
                        }
                    }
                    if (v->tab_count == 0) break;
                    if (!hit) {
                        term_log_keystroke("ACTION: Mouse click on row 1 was outside tab labels");
                    }
                } else {
                    bool hud_was_active = viewer_is_tab_bar_visible(v) || (get_time_ms() < v->status_until_ms);
                    viewer_dismiss_hud(v);
                    bool link_hit = viewer_handle_link_click(v, mouse_col, mouse_row);
                    if (hud_was_active && !link_hit) {
                        viewer_display_current_page(v);
                    }
                }
            }
            PROFILE_END("viewer", "viewer_handle_key");
        }

        // Check file modification for all tabs
        for (size_t i = 0; i < v->tab_count; i++) {
            if (file_watcher_check_modified(v->tabs[i].watcher)) {
                if (v->tabs[i].doc) {
                    doc_close(v->tabs[i].doc);
                    v->tabs[i].doc = doc_open(v->tabs[i].file_path);
                    if (v->tabs[i].doc) {
                        if (v->tabs[i].is_reflowable)
                            doc_layout_reflowable(v->tabs[i].doc, (double)v->tabs[i].html_page_width, (double)v->tabs[i].html_page_width * 1.414, 12.0);
                        viewer_tab_find_content_pages(&v->tabs[i]);
                        viewer_tab_prefetch_page_dims(&v->tabs[i]);
                        v->tabs[i].layout_valid = false;
                        cache_clear(v->cache, doc_get_context(v->tabs[i].doc));
                        if (i == v->active_tab_idx) {
                            viewer_display_current_page(v);
                            viewer_set_status(v, "Reloaded document", 1500);
                        }
                    }
                }
            }
        }

        if (pfds[1].revents & POLLIN) {
            viewer_handle_ipc(v);
        }
    }

    for (size_t i = 0; i < v->tab_count; i++) {
        viewer_tab_save_config(&v->tabs[i]);
    }
    term_leave_alt_screen();
    term_restore(&v->old_term);
    term_show_cursor();
    term_disable_mouse();
    term_disable_kitty_keyboard();
    term_clear_title();
    profile_shutdown();
    return v->want_back;
}
