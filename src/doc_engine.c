#define _DEFAULT_SOURCE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pdfcli/doc_engine.h"
#include "pdfcli/profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>

struct doc_handle {
    fz_context *ctx;
    fz_document *doc;
    pthread_mutex_t mu;
    int page_count;
};

[[nodiscard]] doc_handle *doc_open(const char *path) {
    PROFILE_SCOPE("doc_engine", "doc_engine_open");
    if (!path) return nullptr;
    fz_context *ctx = fz_new_context(profile_get_fitz_alloc(), nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return nullptr;
    fz_register_document_handlers(ctx);
    fz_document *doc = nullptr;
    fz_var(doc);
    fz_try(ctx) { doc = fz_open_document(ctx, path); } fz_catch(ctx) { doc = nullptr; }
    if (!doc) { fz_drop_context(ctx); return nullptr; }
    auto h = (doc_handle *)calloc(1, sizeof(doc_handle));
    if (!h) { fz_drop_document(ctx, doc); fz_drop_context(ctx); return nullptr; }
    h->ctx = ctx; h->doc = doc;
    fz_var(h);
    pthread_mutex_init(&h->mu, nullptr);
    fz_try(ctx) { h->page_count = fz_count_pages(ctx, doc); } fz_catch(ctx) { h->page_count = 0; }
    return h;
}

void doc_close(doc_handle *h) {
    PROFILE_SCOPE("doc_engine", "doc_engine_close");
    if (!h) return;
    pthread_mutex_lock(&h->mu);
    if (h->doc) { fz_drop_document(h->ctx, h->doc); h->doc = nullptr; }
    if (h->ctx) { fz_drop_context(h->ctx); h->ctx = nullptr; }
    pthread_mutex_unlock(&h->mu);
    pthread_mutex_destroy(&h->mu);
    free(h);
}

[[nodiscard]] fz_context *doc_get_context(doc_handle *h) { return h ? h->ctx : nullptr; }
[[nodiscard]] int doc_get_page_count(doc_handle *h) {
    PROFILE_SCOPE("doc_engine", "doc_engine_page_count");
    return h ? h->page_count : 0;
}

[[nodiscard]] bool doc_page_has_visual_content(doc_handle *h, int page_num) {
    PROFILE_SCOPE("doc_engine", "doc_engine_has_visual_content");
    if (!h || !h->ctx || !h->doc || page_num < 0 || page_num >= h->page_count) return false;
    pthread_mutex_lock(&h->mu);
    bool has_content = false;
    fz_var(has_content);
    fz_try(h->ctx) {
        fz_page *p = fz_load_page(h->ctx, h->doc, page_num);
        if (p) {
            fz_rect b = fz_bound_page(h->ctx, p);
            has_content = (b.x1 > b.x0 && b.y1 > b.y0);
            fz_drop_page(h->ctx, p);
        }
    } fz_catch(h->ctx) { has_content = false; }
    pthread_mutex_unlock(&h->mu);
    return has_content;
}

[[nodiscard]] fz_rect doc_get_page_bounds(doc_handle *h, int page_num) {
    PROFILE_SCOPE("doc_engine", "doc_engine_page_dims");
    fz_rect empty = { 0, 0, 0, 0 };
    if (!h || !h->ctx || !h->doc || page_num < 0 || page_num >= h->page_count) return empty;
    pthread_mutex_lock(&h->mu);
    fz_rect b = empty;
    fz_var(b);
    fz_try(h->ctx) {
        fz_page *p = fz_load_page(h->ctx, h->doc, page_num);
        if (p) {
            b = fz_bound_page(h->ctx, p);
            fz_drop_page(h->ctx, p);
            PROFILE_EVENT("doc_engine", "page_dim_w", "w", (int64_t)(b.x1 - b.x0));
            PROFILE_EVENT("doc_engine", "page_dim_h", "h", (int64_t)(b.y1 - b.y0));
        }
    } fz_catch(h->ctx) { b = empty; }
    pthread_mutex_unlock(&h->mu);
    return b;
}

[[nodiscard]] fz_pixmap *doc_render_page_pixmap(doc_handle *h, int page_num, double zoom, double dpi) {
    PROFILE_SCOPE("raster", "doc_engine_render_page");
    if (!h || !h->ctx || !h->doc || page_num < 0 || page_num >= h->page_count) return nullptr;
    float scale = (dpi > 0 ? (float)dpi / 72.0f : 1.0f) * (zoom > 0 ? (float)zoom : 1.0f);
    PROFILE_EVENT("raster", "render_page_num", "page", page_num);
    PROFILE_EVENT("raster", "render_scale_pct", "scale_pct", (int64_t)(scale * 100.0f));
    pthread_mutex_lock(&h->mu);
    fz_pixmap *pix = nullptr;
    fz_var(pix);
    fz_try(h->ctx) {
        fz_page *p = fz_load_page(h->ctx, h->doc, page_num);
        if (p) {
            fz_matrix ctm = fz_scale(scale, scale);
            pix = fz_new_pixmap_from_page(h->ctx, p, ctm, fz_device_rgb(h->ctx), 1);
            fz_drop_page(h->ctx, p);
        }
    } fz_catch(h->ctx) { pix = nullptr; }
    pthread_mutex_unlock(&h->mu);
    if (pix) {
        PROFILE_EVENT("raster", "render_dims_w", "w", pix->w);
        PROFILE_EVENT("raster", "render_dims_h", "h", pix->h);
    }
    return pix;
}

[[nodiscard]] fz_pixmap *doc_render_page_viewport(doc_handle *h, int page_num, double scale, int sx, int sy, int vw, int vh) {
    PROFILE_SCOPE("raster", "doc_engine_render_viewport");
    if (!h || !h->ctx || !h->doc || page_num < 0 || vw <= 0 || vh <= 0) return nullptr;
    if (scale <= 0.0) scale = 1.0;
    PROFILE_EVENT("raster", "viewport_page_num", "page", page_num);
    PROFILE_EVENT("raster", "viewport_scale_pct", "scale_pct", (int64_t)(scale * 100.0));
    PROFILE_EVENT("raster", "viewport_target_w", "vw", vw);
    PROFILE_EVENT("raster", "viewport_target_h", "vh", vh);
    pthread_mutex_lock(&h->mu);
    fz_pixmap *pix = nullptr;
    fz_page *p = nullptr;
    fz_device *dev = nullptr;
    fz_var(pix);
    fz_var(p);
    fz_var(dev);
    fz_try(h->ctx) {
        p = fz_load_page(h->ctx, h->doc, page_num);
        if (p) {
            fz_matrix ctm = fz_concat(fz_scale((float)scale, (float)scale), fz_translate((float)-sx, (float)-sy));
            fz_irect bbox = fz_make_irect(0, 0, vw, vh);
            pix = fz_new_pixmap_with_bbox(h->ctx, fz_device_rgb(h->ctx), bbox, nullptr, 1);
            fz_clear_pixmap_with_value(h->ctx, pix, 0xff);
            dev = fz_new_draw_device(h->ctx, ctm, pix);
            fz_run_page(h->ctx, p, dev, fz_identity, nullptr);
            fz_close_device(h->ctx, dev);
        }
    } fz_always(h->ctx) {
        if (dev) fz_drop_device(h->ctx, dev);
        if (p) fz_drop_page(h->ctx, p);
    } fz_catch(h->ctx) { if (pix) fz_drop_pixmap(h->ctx, pix); pix = nullptr; }
    pthread_mutex_unlock(&h->mu);
    if (pix) {
        PROFILE_EVENT("raster", "viewport_dims_w", "w", pix->w);
        PROFILE_EVENT("raster", "viewport_dims_h", "h", pix->h);
    }
    return pix;
}

void doc_layout_reflowable(doc_handle *h, double w, double height, double em) {
    if (!h || !h->ctx || !h->doc) return;
    pthread_mutex_lock(&h->mu);
    fz_try(h->ctx) {
        if (fz_is_document_reflowable(h->ctx, h->doc)) {
            fz_layout_document(h->ctx, h->doc, (float)w, (float)height, (float)em);
            h->page_count = fz_count_pages(h->ctx, h->doc);
        }
    } fz_catch(h->ctx) {}
    pthread_mutex_unlock(&h->mu);
}

[[nodiscard]] doc_page_link *doc_get_page_links(doc_handle *h, int page_num, size_t *out_cnt) {
    if (!out_cnt) return nullptr;
    *out_cnt = 0;
    if (!h || !h->ctx || !h->doc || page_num < 0 || page_num >= h->page_count) return nullptr;
    pthread_mutex_lock(&h->mu);
    doc_page_link *res = nullptr; size_t count = 0, cap = 8;
    fz_try(h->ctx) {
        fz_page *p = fz_load_page(h->ctx, h->doc, page_num);
        if (p) {
            fz_link *l = fz_load_links(h->ctx, p);
            res = (doc_page_link *)calloc(cap, sizeof(doc_page_link));
            for (fz_link *it = l; it; it = it->next) {
                if (count >= cap) { cap *= 2; res = (doc_page_link *)realloc(res, cap * sizeof(doc_page_link)); }
                res[count].uri = it->uri ? strdup(it->uri) : nullptr;
                res[count].x0 = it->rect.x0; res[count].y0 = it->rect.y0;
                res[count].x1 = it->rect.x1; res[count].y1 = it->rect.y1;
                res[count].is_external = !fz_is_external_link(h->ctx, it->uri) ? false : true;
                res[count].dest_page = -1;
                count++;
            }
            fz_drop_page(h->ctx, p);
        }
    } fz_catch(h->ctx) {}
    pthread_mutex_unlock(&h->mu);
    *out_cnt = count; return res;
}

void doc_free_page_links(doc_page_link *links, size_t count) {
    if (!links) return;
    for (size_t i = 0; i < count; i++) free(links[i].uri);
    free(links);
}

static void walk_outline(fz_context *ctx, fz_document *doc, fz_outline *o, int lvl, doc_toc_entry **arr, size_t *cnt, size_t *cap) {
    for (; o; o = o->next) {
        fz_location loc = fz_resolve_link(ctx, doc, o->uri, nullptr, nullptr);
        int page = fz_page_number_from_location(ctx, doc, loc);
        if (page >= 0) {
            if (*cnt >= *cap) { *cap = (*cap ? *cap * 2 : 16); *arr = (doc_toc_entry *)realloc(*arr, *cap * sizeof(doc_toc_entry)); }
            (*arr)[*cnt].level = lvl;
            (*arr)[*cnt].title = strdup(o->title ? o->title : "");
            (*arr)[*cnt].page = page;
            (*arr)[*cnt].x = 0; (*arr)[*cnt].y = 0;
            (*cnt)++;
        }
        if (o->down) walk_outline(ctx, doc, o->down, lvl + 1, arr, cnt, cap);
    }
}

[[nodiscard]] doc_toc_entry *doc_get_toc(doc_handle *h, size_t *out_cnt) {
    PROFILE_SCOPE("doc_engine", "doc_engine_get_toc");
    if (!out_cnt) return nullptr;
    *out_cnt = 0;
    if (!h || !h->ctx || !h->doc) return nullptr;
    pthread_mutex_lock(&h->mu);
    doc_toc_entry *res = nullptr; size_t count = 0, cap = 0;
    fz_try(h->ctx) {
        fz_outline *out = fz_load_outline(h->ctx, h->doc);
        if (out) {
            walk_outline(h->ctx, h->doc, out, 1, &res, &count, &cap);
            fz_drop_outline(h->ctx, out);
        }
    } fz_catch(h->ctx) {}
    pthread_mutex_unlock(&h->mu);
    *out_cnt = count; return res;
}

void doc_free_toc(doc_toc_entry *toc, size_t count) {
    if (!toc) return;
    for (size_t i = 0; i < count; i++) free(toc[i].title);
    free(toc);
}

[[nodiscard]] doc_search_hit *doc_search_text(doc_handle *h, const char *needle, size_t *out_cnt) {
    PROFILE_SCOPE("doc_engine", "doc_engine_search_text");
    if (!out_cnt) return nullptr;
    *out_cnt = 0;
    if (!h || !h->ctx || !h->doc || !needle || !*needle) return nullptr;
    pthread_mutex_lock(&h->mu);
    doc_search_hit *hits = nullptr; size_t count = 0, cap = 16;
    hits = (doc_search_hit *)calloc(cap, sizeof(doc_search_hit));
    fz_var(hits);
    fz_var(count);
    fz_var(cap);
    fz_try(h->ctx) {
        for (int p = 0; p < h->page_count; p++) {
            fz_page *page = fz_load_page(h->ctx, h->doc, p);
            if (!page) continue;
            fz_quad quads[64];
            int n = fz_search_page(h->ctx, page, needle, nullptr, quads, 64);
            for (int i = 0; i < n; i++) {
                if (count >= cap) { cap *= 2; hits = (doc_search_hit *)realloc(hits, cap * sizeof(doc_search_hit)); }
                hits[count].page = p; hits[count].rect = fz_rect_from_quad(quads[i]); count++;
            }
            fz_drop_page(h->ctx, page);
        }
    } fz_catch(h->ctx) {}
    pthread_mutex_unlock(&h->mu);
    *out_cnt = count; return hits;
}

void doc_free_search_hits(doc_search_hit *hits, size_t count) { (void)count; free(hits); }

[[nodiscard]] fz_pixmap *doc_load_standalone_image(doc_handle *h, const char *path) {
    if (!h || !h->ctx || !path) return nullptr;
    pthread_mutex_lock(&h->mu);
    fz_pixmap *pix = nullptr;
    fz_var(pix);
    fz_try(h->ctx) {
        fz_image *img = fz_new_image_from_file(h->ctx, path);
        if (img) {
            pix = fz_get_pixmap_from_image(h->ctx, img, nullptr, nullptr, nullptr, nullptr);
            fz_drop_image(h->ctx, img);
        }
    } fz_catch(h->ctx) { pix = nullptr; }
    pthread_mutex_unlock(&h->mu);
    return pix;
}
