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

struct doc_viewer {
    char *file_path;
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

    // Continuous virtual document layout
    int *page_heights;
    int64_t *page_y_starts;
    int64_t total_doc_h;
    int64_t virtual_y;
    int64_t max_scroll_y;
    double scroll_ratio;

    fit_mode fit;
    view_mode view;
    dual_mode dual;
    half_offset half;
    tint_mode tint;
    double zoom;
    crop_box crop;

    term_state old_term;
    int term_cols, term_rows;
    double cell_w_px, cell_h_px;

    term_gfx_context *gfx;
    page_cache *cache;
    file_watcher *watcher;
    ctl_server *ctl_srv;

    char temp_dir[1024];
    bool want_back;

    page_click_map click_map;
    doc_page_link *current_links;
    size_t current_links_count;

    view_loc back_stack[64];
    size_t back_count;
    view_loc fwd_stack[64];
    size_t fwd_count;

    doc_toc_entry *toc;
    size_t toc_count;

    char status_msg[256];
    uint64_t status_until_ms;
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
    snprintf(v->status_msg, sizeof(v->status_msg), "%s", msg);
    v->status_until_ms = get_time_ms() + duration_ms;
}

static void viewer_find_content_pages(doc_viewer *v) {
    if (!v->doc) return;
    free(v->content_pages);
    v->total_doc_pages = doc_get_page_count(v->doc);
    if (v->total_doc_pages <= 0) return;
    v->content_pages = malloc(sizeof(int) * (size_t)v->total_doc_pages);
    if (!v->content_pages) return;
    for (int i = 0; i < v->total_doc_pages; i++) v->content_pages[i] = i;
    v->content_page_count = (size_t)v->total_doc_pages;
}

static void viewer_update_layout(doc_viewer *v) {
    if (!v->content_pages || v->content_page_count == 0) return;
    free(v->page_heights);
    free(v->page_y_starts);
    v->page_heights = malloc(sizeof(int) * v->content_page_count);
    v->page_y_starts = malloc(sizeof(int64_t) * v->content_page_count);
    if (!v->page_heights || !v->page_y_starts) return;

    int total_w_px = 0, total_h_px = 0;
    term_get_pixel_size(&total_w_px, &total_h_px);
    if (total_w_px <= 0) total_w_px = (int)((double)v->term_cols * v->cell_w_px + 0.5);
    if (total_h_px <= 0) total_h_px = (int)((double)v->term_rows * v->cell_h_px + 0.5);

    int64_t cur_y = 0;
    constexpr int GAP = 12;

    for (size_t i = 0; i < v->content_page_count; i++) {
        v->page_y_starts[i] = cur_y;
        int doc_p = v->content_pages[i];
        if (v->doc) {
            fz_rect b = doc_get_page_bounds(v->doc, doc_p);
            double pw = b.x1 - b.x0, ph = b.y1 - b.y0;
            if (pw <= 0) pw = 612.0;
            if (ph <= 0) ph = 792.0;
            if (v->fit == FIT_WIDTH) {
                double scale = ((double)total_w_px / pw) * v->zoom;
                v->page_heights[i] = (int)(ph * scale + 0.5);
                cur_y += v->page_heights[i] + GAP;
            } else {
                v->page_heights[i] = total_h_px;
                cur_y += total_h_px;
            }
        } else {
            v->page_heights[i] = total_h_px;
            cur_y += total_h_px;
        }
    }
    v->total_doc_h = (v->fit == FIT_WIDTH && v->content_page_count > 0) ? (cur_y - GAP) : cur_y;
    v->max_scroll_y = (v->total_doc_h > total_h_px) ? (v->total_doc_h - total_h_px) : 0;
    if (v->virtual_y > v->max_scroll_y) v->virtual_y = v->max_scroll_y;
    if (v->virtual_y < 0) v->virtual_y = 0;
}

[[nodiscard]] doc_viewer *viewer_create(const char *path) {
    if (!path) return nullptr;
    auto v = (doc_viewer *)calloc(1, sizeof(doc_viewer));
    if (!v) return nullptr;

    v->file_path = strdup(path);
    const char *dot = strrchr(path, '.');
    v->file_type = strdup(dot ? dot + 1 : "pdf");
    if (!strcasecmp(v->file_type, "html") || !strcasecmp(v->file_type, "htm")) v->is_reflowable = true;
    if (!strcasecmp(v->file_type, "png") || !strcasecmp(v->file_type, "jpg") || !strcasecmp(v->file_type, "jpeg")) v->is_image = true;

    snprintf(v->temp_dir, sizeof(v->temp_dir), "/tmp/pdfcli_%d_%lu", getpid(), (unsigned long)time(nullptr));
    mkdir(v->temp_dir, 0700);

    v->fit = FIT_HEIGHT;
    v->view = VIEW_AUTO;
    v->dual = DUAL_OFF;
    v->half = HALF_TOP;
    v->tint = TINT_OFF;
    v->zoom = DEFAULT_ZOOM;
    v->html_page_width = DEFAULT_HTML_PAGE_WIDTH;
    v->scroll_ratio = 0.35; // Default 35% screen jump for browser-like smooth scroll
    const char *senv = getenv("DOCVIEWER_SCROLL_RATIO");
    if (senv) {
        double r = atof(senv);
        if (r >= 0.05 && r <= 1.0) v->scroll_ratio = r;
    }

    char abs_buf[PATH_MAX];
    if (realpath(path, abs_buf)) {
        doc_config cfg = config_load(abs_buf);
        if (!strcmp(cfg.fit_mode, "width")) v->fit = FIT_WIDTH;
        else if (!strcmp(cfg.fit_mode, "auto")) v->fit = FIT_AUTO;
        if (cfg.scale_factor >= MIN_ZOOM && cfg.scale_factor <= MAX_ZOOM) v->zoom = cfg.scale_factor;
        if (!strcmp(cfg.dark_mode, "dark")) v->tint = TINT_DARK;
        else if (!strcmp(cfg.dark_mode, "dim")) v->tint = TINT_DIM;
        else if (!strcmp(cfg.dark_mode, "invert")) v->tint = TINT_INVERT;
        v->crop = cfg.crop;
    }

    v->cache = cache_create(1);
    v->gfx = term_gfx_init();
    return v;
}

[[nodiscard]] bool viewer_open(doc_viewer *v) {
    if (!v) return false;
    v->doc = doc_open(v->file_path);
    if (!v->doc) return false;
    if (v->is_image) {
        v->standalone_image = doc_load_standalone_image(v->doc, v->file_path);
        if (!v->standalone_image) return false;
        v->total_doc_pages = 1;
        v->content_pages = malloc(sizeof(int));
        v->content_pages[0] = 0;
        v->content_page_count = 1;
    } else {
        if (v->is_reflowable) doc_layout_reflowable(v->doc, (double)v->html_page_width, (double)v->html_page_width * 1.414, 12.0);
        viewer_find_content_pages(v);
        if (v->content_page_count == 0) return false;
        v->toc = doc_get_toc(v->doc, &v->toc_count);
    }
    v->watcher = file_watcher_create(v->file_path);
    v->ctl_srv = ctl_server_create(getpid());
    return true;
}

void viewer_destroy(doc_viewer *v) {
    if (!v) return;
    if (v->watcher) file_watcher_destroy(v->watcher);
    if (v->ctl_srv) ctl_server_destroy(v->ctl_srv);
    fz_context *ctx = v->doc ? doc_get_context(v->doc) : nullptr;
    if (v->cache) cache_destroy(v->cache, ctx);
    if (v->current_links) doc_free_page_links(v->current_links, v->current_links_count);
    if (v->toc) doc_free_toc(v->toc, v->toc_count);
    if (v->standalone_image && ctx) fz_drop_pixmap(ctx, v->standalone_image);
    if (v->doc) doc_close(v->doc);
    if (v->gfx) term_gfx_destroy(v->gfx);

    char rm_cmd[2048];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", v->temp_dir);
    (void)system(rm_cmd);

    free(v->file_path);
    free(v->file_type);
    free(v->content_pages);
    free(v->page_heights);
    free(v->page_y_starts);
    free(v);
}

static void viewer_save_config(const doc_viewer *v) {
    if (!v || !v->file_path) return;
    char abs_buf[PATH_MAX];
    if (!realpath(v->file_path, abs_buf)) return;
    doc_config cfg = {};
    snprintf(cfg.fit_mode, sizeof(cfg.fit_mode), "%s", viewer_fit_name(v->fit));
    cfg.scale_factor = v->zoom;
    snprintf(cfg.dark_mode, sizeof(cfg.dark_mode), "%s", (v->tint == TINT_DARK) ? "dark" : ((v->tint == TINT_DIM) ? "dim" : ((v->tint == TINT_INVERT) ? "invert" : "off")));
    cfg.crop = v->crop;
    config_save(abs_buf, &cfg);
}

static void viewer_display_current_page(doc_viewer *v) {
    if (!v) return;
    term_get_size(&v->term_cols, &v->term_rows);
    if (v->term_cols <= 0) v->term_cols = 80;
    if (v->term_rows <= 0) v->term_rows = 24;

    int total_w_px = 0, total_h_px = 0;
    term_get_pixel_size(&total_w_px, &total_h_px);
    if (total_w_px <= 0) total_w_px = (int)((double)v->term_cols * v->cell_w_px + 0.5);
    if (total_h_px <= 0) total_h_px = (int)((double)v->term_rows * v->cell_h_px + 0.5);

    viewer_update_layout(v);

    size_t cur_idx = 0;
    for (size_t i = 0; i < v->content_page_count; i++) {
        if (v->page_y_starts[i] <= v->virtual_y + (total_h_px / 3)) cur_idx = i;
    }
    v->current_page_idx = (int)cur_idx;
    int current_doc_page = (v->content_pages && cur_idx < v->content_page_count) ? v->content_pages[cur_idx] : 0;

    viewer_debug("DISPLAY virt_y=%lld/%lld cur_p=%zu doc_p=%d fit=%s zoom=%.2f term=%dx%d px=%dx%d",
                 (long long)v->virtual_y, (long long)v->max_scroll_y, cur_idx + 1, current_doc_page,
                 viewer_fit_name(v->fit), v->zoom, v->term_cols, v->term_rows, total_w_px, total_h_px);

    fz_context *ctx = v->doc ? doc_get_context(v->doc) : nullptr;
    if (!ctx) return;

    fz_pixmap *vpix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), fz_make_irect(0, 0, total_w_px, total_h_px), nullptr, 1);
    if (!vpix) return;
    fz_clear_pixmap_with_value(ctx, vpix, 0x18);

    for (size_t i = 0; i < v->content_page_count; i++) {
        int64_t y0 = v->page_y_starts[i];
        int64_t y1 = y0 + v->page_heights[i];
        if (y1 <= v->virtual_y) continue;
        if (y0 >= v->virtual_y + total_h_px) break;

        int64_t v0 = (v->virtual_y > y0) ? v->virtual_y : y0;
        int64_t v1 = (v->virtual_y + total_h_px < y1) ? (v->virtual_y + total_h_px) : y1;
        int slice_h = (int)(v1 - v0);
        int src_y = (int)(v0 - y0);
        int dst_y = (int)(v0 - v->virtual_y);

        int doc_p = v->content_pages[i];
        fz_rect b = doc_get_page_bounds(v->doc, doc_p);
        double pw = b.x1 - b.x0, ph = b.y1 - b.y0;
        if (pw <= 0) pw = 612.0;
        if (ph <= 0) ph = 792.0;

        double scale = (v->fit == FIT_WIDTH) ? (((double)total_w_px / pw) * v->zoom) : (((double)total_h_px / ph) * v->zoom);
        int slice_w = total_w_px;
        if (v->fit != FIT_WIDTH) {
            slice_w = (int)(pw * scale + 0.5);
            if (slice_w > total_w_px) slice_w = total_w_px;
        }
        int dst_x = (total_w_px > slice_w) ? (total_w_px - slice_w) / 2 : 0;

        fz_pixmap *spix = doc_render_page_viewport(v->doc, doc_p, scale, 0, src_y, slice_w, slice_h);
        if (spix) {
            for (int r = 0; r < spix->h && (dst_y + r) < total_h_px; r++) {
                unsigned char *dst_row = vpix->samples + (size_t)(dst_y + r) * vpix->stride + (size_t)dst_x * vpix->n;
                unsigned char *src_row = spix->samples + (size_t)r * spix->stride;
                memcpy(dst_row, src_row, (size_t)spix->w * spix->n);
            }
            fz_drop_pixmap(ctx, spix);
        }

        if (i == cur_idx) {
            v->click_map.page_num = doc_p;
            v->click_map.col_offset = 1;
            v->click_map.row_offset = 1;
            v->click_map.width_chars = v->term_cols;
            v->click_map.height_lines = v->term_rows;
            v->click_map.pixel_width = total_w_px;
            v->click_map.pixel_height = total_h_px;
            v->click_map.crop_x0 = 0.0; v->click_map.crop_x1 = 1.0;
            v->click_map.crop_y0 = (double)src_y / (ph * scale);
            v->click_map.crop_y1 = v->click_map.crop_y0 + (double)slice_h / (ph * scale);
        }
    }

    if (v->crop.top > 0.0 || v->crop.bottom > 0.0 || v->crop.left > 0.0 || v->crop.right > 0.0) {
        fz_pixmap *cr = img_crop(ctx, vpix, &v->crop);
        if (cr) { fz_drop_pixmap(ctx, vpix); vpix = cr; }
    }

    if (v->tint == TINT_DARK) img_smart_invert(ctx, vpix);
    else if (v->tint == TINT_INVERT) img_simple_invert(ctx, vpix);
    else if (v->tint == TINT_DIM) img_dim_tint(ctx, vpix);

    term_begin_sync();
    term_gfx_clear(v->gfx);
    term_clear_screen();

    char temp_png[PATH_MAX];
    snprintf(temp_png, sizeof(temp_png), "%s/view.png", v->temp_dir);
    (void)term_gfx_draw_pixmap(v->gfx, ctx, vpix, temp_png, 1, 1, v->term_cols, v->term_rows, total_w_px, total_h_px);
    fz_drop_pixmap(ctx, vpix);

    if (v->current_links) { doc_free_page_links(v->current_links, v->current_links_count); v->current_links = nullptr; }
    v->current_links = doc_get_page_links(v->doc, current_doc_page, &v->current_links_count);

    if (get_time_ms() < v->status_until_ms && v->status_msg[0]) {
        printf("\033[%d;1H\033[7m %s \033[0m", v->term_rows, v->status_msg);
    }
    term_end_sync();
#if defined(__linux__)
    malloc_trim(0);
#endif
}

static void viewer_goto_page(doc_viewer *v, int page_idx) {
    if (page_idx < 0) page_idx = 0;
    if (page_idx >= (int)v->content_page_count) page_idx = (int)v->content_page_count - 1;
    if (v->back_count < 64) {
        v->back_stack[v->back_count++] = (view_loc){ .page = v->current_page_idx, .y = (double)v->virtual_y };
    }
    v->current_page_idx = page_idx;
    if (v->page_y_starts) v->virtual_y = v->page_y_starts[page_idx];
    viewer_display_current_page(v);
}

static void viewer_handle_link_click(doc_viewer *v, int col, int row) {
    if (!v->current_links || v->current_links_count == 0) return;
    if (col < v->click_map.col_offset || col >= v->click_map.col_offset + v->click_map.width_chars ||
        row < v->click_map.row_offset || row >= v->click_map.row_offset + v->click_map.height_lines) return;

    double norm_x = (double)(col - v->click_map.col_offset) / (double)v->click_map.width_chars;
    double norm_y = (double)(row - v->click_map.row_offset) / (double)v->click_map.height_lines;
    double cy = v->click_map.crop_y0 + norm_y * (v->click_map.crop_y1 - v->click_map.crop_y0);
    fz_rect b = doc_get_page_bounds(v->doc, v->click_map.page_num);
    double pdf_x = b.x0 + norm_x * (b.x1 - b.x0), pdf_y = b.y0 + cy * (b.y1 - b.y0);

    for (size_t i = 0; i < v->current_links_count; i++) {
        auto l = &v->current_links[i];
        if (pdf_x >= l->x0 - 4.0 && pdf_x <= l->x1 + 4.0 && pdf_y >= l->y0 - 8.0 && pdf_y <= l->y1 + 8.0) {
            if (l->is_external && l->uri) {
                char cmd[1024]; snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" >/dev/null 2>&1 &", l->uri);
                (void)system(cmd);
                viewer_set_status(v, "Opened external link", 1500);
            } else if (l->dest_page >= 0) {
                for (size_t p = 0; p < v->content_page_count; p++) {
                    if (v->content_pages[p] == l->dest_page) { viewer_goto_page(v, (int)p); return; }
                }
            }
            break;
        }
    }
}

static void viewer_handle_ipc(doc_viewer *v) {
    int cfd = -1; char cmd[64] = {}, args[2048] = {};
    if (!ctl_server_recv(v->ctl_srv, &cfd, cmd, sizeof(cmd), args, sizeof(args))) return;

    if (!strcmp(cmd, "status")) {
        ctl_viewer_state st = { .pid = getpid(), .page = v->current_page_idx + 1, .pages = (int)v->content_page_count, .zoom = v->zoom, .crop = v->crop, .is_image = v->is_image };
        snprintf(st.path, sizeof(st.path), "%s", v->file_path);
        snprintf(st.name, sizeof(st.name), "%s", basename(v->file_path));
        snprintf(st.file_type, sizeof(st.file_type), "%s", v->file_type);
        snprintf(st.fit, sizeof(st.fit), "%s", viewer_fit_name(v->fit));
        snprintf(st.tint, sizeof(st.tint), "%s", (v->tint == TINT_DARK) ? "dark" : ((v->tint == TINT_DIM) ? "dim" : ((v->tint == TINT_INVERT) ? "invert" : "white")));
        ctl_get_socket_path(st.pid, st.socket, sizeof(st.socket));
        char *r = ctl_serialize_state_response(true, nullptr, &st);
        ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "page")) {
        char val[64];
        if (ctl_args_get(args, "page", val, sizeof(val))) {
            int p = atoi(val);
            if (p >= 1 && p <= (int)v->content_page_count) viewer_goto_page(v, p - 1);
        }
        char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "next")) {
        viewer_goto_page(v, v->current_page_idx + 1);
        char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "prev")) {
        viewer_goto_page(v, v->current_page_idx - 1);
        char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "zoom")) {
        char val[64];
        if (ctl_args_get(args, "level", val, sizeof(val))) {
            double z = atof(val); if (z >= MIN_ZOOM && z <= MAX_ZOOM) { v->zoom = z; viewer_display_current_page(v); }
        }
        char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "fit")) {
        char val[64];
        if (ctl_args_get(args, "mode", val, sizeof(val))) {
            v->fit = !strcmp(val, "width") ? FIT_WIDTH : (!strcmp(val, "auto") ? FIT_AUTO : FIT_HEIGHT);
            viewer_update_layout(v); viewer_display_current_page(v);
        }
        char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "tint")) {
        char val[64];
        if (ctl_args_get(args, "mode", val, sizeof(val))) {
            v->tint = !strcmp(val, "dark") ? TINT_DARK : (!strcmp(val, "dim") ? TINT_DIM : (!strcmp(val, "invert") ? TINT_INVERT : TINT_OFF));
            viewer_display_current_page(v);
        }
        char *r = ctl_serialize_simple_response(true, nullptr); ctl_server_send_response(cfd, r); free(r);
    } else if (!strcmp(cmd, "toc")) {
        char *r = ctl_serialize_toc_response(true, nullptr, v->toc, v->toc_count);
        ctl_server_send_response(cfd, r); free(r);
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
    printf("\033[%d;6H\033[1mi\033[0m                       Show document info toast\033[K\r\n", r++);
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
    if (!v) return false;
    if (!term_enter_raw(&v->old_term)) return false;

    term_enter_alt_screen();
    term_detect_cell_size(&v->cell_w_px, &v->cell_h_px);
    term_gfx_probe_kitty_mode(v->gfx, v->temp_dir);
    term_get_size(&v->term_cols, &v->term_rows);

    if (term_gfx_get_proto(v->gfx) == GFX_KITTY) {
        term_enable_kitty_keyboard();
        term_enable_mouse();
    }
    term_hide_cursor();
    term_set_title(v->file_path);

    viewer_update_layout(v);
    viewer_display_current_page(v);

    uint64_t last_wheel_ms = 0;
    struct pollfd pfds[3] = {};
    pfds[0].fd = STDIN_FILENO; pfds[0].events = POLLIN;
    pfds[1].fd = file_watcher_get_fd(v->watcher); pfds[1].events = POLLIN;
    pfds[2].fd = ctl_server_get_fd(v->ctl_srv); pfds[2].events = POLLIN;

    while (true) {
        if (term_poll_resize()) {
            term_get_size(&v->term_cols, &v->term_rows);
            term_detect_cell_size(&v->cell_w_px, &v->cell_h_px);
            viewer_update_layout(v);
            viewer_display_current_page(v);
        }

        if (poll(pfds, 3, 500) < 0) continue;

        if (pfds[0].revents & POLLIN) {
            char key_buf[32] = {};
            int mouse_col = 0, mouse_row = 0;
            int key = term_read_key(STDIN_FILENO, key_buf, sizeof(key_buf), &mouse_col, &mouse_row);

            int total_w_px = 0, total_h_px = 0;
            term_get_pixel_size(&total_w_px, &total_h_px);
            if (total_h_px <= 0) total_h_px = (int)((double)v->term_rows * v->cell_h_px + 0.5);

            int step_pg = (int)((double)total_h_px * v->scroll_ratio);
            if (step_pg < 80) step_pg = 80;
            int step_line = (int)(v->cell_h_px * 3.0);
            if (step_line < 40) step_line = 40;

            viewer_debug("KEY code=%d(0x%X) virt_y=%lld/%lld step_pg=%d fit=%s",
                         key, key, (long long)v->virtual_y, (long long)v->max_scroll_y, step_pg, viewer_fit_name(v->fit));

            if (key == 'q' || key == KEY_CTRL_C) {
                v->want_back = false;
                break;
            } else if (key == 'b') {
                v->want_back = true;
                break;
            } else if (key == KEY_PAGE_DOWN || key == ' ' || key == 'd' || key == 4) {
                if (v->fit == FIT_WIDTH) {
                    v->virtual_y = (v->virtual_y + step_pg <= v->max_scroll_y) ? (v->virtual_y + step_pg) : v->max_scroll_y;
                    viewer_display_current_page(v);
                } else {
                    viewer_goto_page(v, v->current_page_idx + 1);
                }
            } else if (key == KEY_PAGE_UP || key == 'u' || key == 21) {
                if (v->fit == FIT_WIDTH) {
                    v->virtual_y = (v->virtual_y >= step_pg) ? (v->virtual_y - step_pg) : 0;
                    viewer_display_current_page(v);
                } else {
                    viewer_goto_page(v, v->current_page_idx - 1);
                }
            } else if (key == KEY_MOUSE_WHEEL_DOWN) {
                if (v->fit == FIT_WIDTH) {
                    int wstep = (int)((double)total_h_px * 0.15);
                    if (wstep < 50) wstep = 50;
                    v->virtual_y = (v->virtual_y + wstep <= v->max_scroll_y) ? (v->virtual_y + wstep) : v->max_scroll_y;
                    viewer_display_current_page(v);
                } else {
                    uint64_t now = get_time_ms();
                    if (now - last_wheel_ms >= 200) { last_wheel_ms = now; viewer_goto_page(v, v->current_page_idx + 1); }
                }
            } else if (key == KEY_MOUSE_WHEEL_UP) {
                if (v->fit == FIT_WIDTH) {
                    int wstep = (int)((double)total_h_px * 0.15);
                    if (wstep < 50) wstep = 50;
                    v->virtual_y = (v->virtual_y >= wstep) ? (v->virtual_y - wstep) : 0;
                    viewer_display_current_page(v);
                } else {
                    uint64_t now = get_time_ms();
                    if (now - last_wheel_ms >= 200) { last_wheel_ms = now; viewer_goto_page(v, v->current_page_idx - 1); }
                }
            } else if (key == KEY_ARROW_DOWN) {
                if (v->fit == FIT_WIDTH) {
                    v->virtual_y = (v->virtual_y + step_line <= v->max_scroll_y) ? (v->virtual_y + step_line) : v->max_scroll_y;
                    viewer_display_current_page(v);
                } else {
                    viewer_goto_page(v, v->current_page_idx + 1);
                }
            } else if (key == KEY_ARROW_UP) {
                if (v->fit == FIT_WIDTH) {
                    v->virtual_y = (v->virtual_y >= step_line) ? (v->virtual_y - step_line) : 0;
                    viewer_display_current_page(v);
                } else {
                    viewer_goto_page(v, v->current_page_idx - 1);
                }
            } else if (key == KEY_ARROW_RIGHT || key == 'j' || key == 'J') {
                viewer_goto_page(v, v->current_page_idx + 1);
            } else if (key == KEY_ARROW_LEFT || key == 'k' || key == 'K') {
                viewer_goto_page(v, v->current_page_idx - 1);
            } else if (key == '[') {
                v->scroll_ratio = fmax(0.10, v->scroll_ratio - 0.05);
                char msg[64]; snprintf(msg, sizeof(msg), "Scroll step: %.0f%% screen", v->scroll_ratio * 100.0);
                viewer_set_status(v, msg, 1000);
                viewer_display_current_page(v);
            } else if (key == ']') {
                v->scroll_ratio = fmin(0.80, v->scroll_ratio + 0.05);
                char msg[64]; snprintf(msg, sizeof(msg), "Scroll step: %.0f%% screen", v->scroll_ratio * 100.0);
                viewer_set_status(v, msg, 1000);
                viewer_display_current_page(v);
            } else if (key == 'g') {
                char input[32] = {};
                size_t input_len = 0;
                term_show_cursor();
                while (true) {
                    printf("\033[%d;1H\033[7m Go to PDF page (1-%d): %s\033[0m\033[K", v->term_rows, v->total_doc_pages, input);
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
                    if (p >= 1 && p <= v->total_doc_pages) {
                        int t_idx = -1;
                        for (size_t i = 0; i < v->content_page_count; i++) {
                            if (v->content_pages[i] == p - 1) { t_idx = (int)i; break; }
                        }
                        if (t_idx >= 0) viewer_goto_page(v, t_idx);
                    }
                }
                viewer_display_current_page(v);
            } else if (key == 'f') {
                int cur_p = v->current_page_idx;
                v->fit = (v->fit == FIT_WIDTH) ? FIT_HEIGHT : FIT_WIDTH;
                viewer_update_layout(v);
                if (v->page_y_starts) v->virtual_y = v->page_y_starts[cur_p];
                viewer_set_status(v, (v->fit == FIT_WIDTH) ? "Fit: width (continuous scroll)" : "Fit: height (fit page)", 1200);
                viewer_display_current_page(v);
            } else if (key == '+' || key == '=') {
                v->zoom = fmin(v->zoom + 0.1, MAX_ZOOM);
                char zbuf[64]; snprintf(zbuf, sizeof(zbuf), "Zoom: %.0f%%", v->zoom * 100.0);
                viewer_set_status(v, zbuf, 1000);
                viewer_display_current_page(v);
            } else if (key == '-') {
                v->zoom = fmax(v->zoom - 0.1, MIN_ZOOM);
                char zbuf[64]; snprintf(zbuf, sizeof(zbuf), "Zoom: %.0f%%", v->zoom * 100.0);
                viewer_set_status(v, zbuf, 1000);
                viewer_display_current_page(v);
            } else if (key == 'D') {
                v->tint = (tint_mode)(((unsigned)v->tint + 1) % 4);
                char tbuf[64]; snprintf(tbuf, sizeof(tbuf), "Tint: %s", (v->tint == TINT_DARK) ? "dark" : ((v->tint == TINT_DIM) ? "dim" : ((v->tint == TINT_INVERT) ? "invert" : "white")));
                viewer_set_status(v, tbuf, 1000);
                viewer_display_current_page(v);
            } else if (key == 'i') {
                char ibuf[256];
                int curr_p = (v->content_pages && v->current_page_idx < (int)v->content_page_count) ? v->content_pages[v->current_page_idx] + 1 : 1;
                snprintf(ibuf, sizeof(ibuf), "%s | Page %d/%zu (%d) | Zoom %.0f%% | Fit %s",
                         basename(v->file_path), v->current_page_idx + 1, v->content_page_count, curr_p,
                         v->zoom * 100.0, viewer_fit_name(v->fit));
                viewer_set_status(v, ibuf, 2500);
                viewer_display_current_page(v);
            } else if (key == 'T') {
                if (v->toc && v->toc_count > 0) {
                    int cur_doc_p = v->content_pages ? v->content_pages[v->current_page_idx] : 0;
                    int target = toc_modal_run(v->toc, v->toc_count, cur_doc_p);
                    if (target >= 0) {
                        for (size_t p = 0; p < v->content_page_count; p++) {
                            if (v->content_pages[p] == target) { viewer_goto_page(v, (int)p); break; }
                        }
                    }
                    viewer_display_current_page(v);
                }
            } else if (key == 'h') {
                viewer_show_help(v);
            } else if (key == 'r') {
                term_detect_cell_size(&v->cell_w_px, &v->cell_h_px);
                viewer_update_layout(v);
                viewer_display_current_page(v);
            } else if (key == KEY_HISTORY_BACK) {
                if (v->back_count > 0) {
                    view_loc loc = v->back_stack[--v->back_count];
                    if (v->fwd_count < 64) v->fwd_stack[v->fwd_count++] = (view_loc){ .page = v->current_page_idx, .y = (double)v->virtual_y };
                    viewer_goto_page(v, loc.page);
                }
            } else if (key == KEY_HISTORY_FORWARD) {
                if (v->fwd_count > 0) {
                    view_loc loc = v->fwd_stack[--v->fwd_count];
                    if (v->back_count < 64) v->back_stack[v->back_count++] = (view_loc){ .page = v->current_page_idx, .y = (double)v->virtual_y };
                    viewer_goto_page(v, loc.page);
                }
            } else if (key == KEY_MOUSE_CTRL_CLICK || key == KEY_MOUSE_LEFT) {
                viewer_handle_link_click(v, mouse_col, mouse_row);
            }
        }

        if ((pfds[1].revents & POLLIN) || file_watcher_check_modified(v->watcher)) {
            if (v->doc) {
                doc_close(v->doc);
                v->doc = doc_open(v->file_path);
                if (v->doc) {
                    if (v->is_reflowable) doc_layout_reflowable(v->doc, (double)v->html_page_width, (double)v->html_page_width * 1.414, 12.0);
                    viewer_find_content_pages(v);
                    cache_clear(v->cache, doc_get_context(v->doc));
                    viewer_update_layout(v);
                    viewer_display_current_page(v);
                    viewer_set_status(v, "Reloaded document", 1500);
                }
            }
        }

        if (pfds[2].revents & POLLIN) {
            viewer_handle_ipc(v);
        }
    }

    viewer_save_config(v);
    term_leave_alt_screen();
    term_restore(&v->old_term);
    term_show_cursor();
    if (term_gfx_get_proto(v->gfx) == GFX_KITTY) {
        term_disable_mouse();
        term_disable_kitty_keyboard();
    }
    term_clear_title();
    return v->want_back;
}
