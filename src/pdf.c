#include "pdfcli.h"
#include <string.h>

static void dummy_warning_cb(void *user, const char *msg) { (void)user; (void)msg; }

[[nodiscard]] pdf_doc_t *pdf_open(const char *path) {
    if (!path) return nullptr;
    fz_context *ctx = fz_new_context(nullptr, nullptr, 64 * 1024 * 1024);
    if (!ctx) return nullptr;
    fz_set_warning_callback(ctx, dummy_warning_cb, nullptr);
    fz_set_error_callback(ctx, dummy_warning_cb, nullptr);
    fz_register_document_handlers(ctx);

    fz_document *doc = nullptr;
    fz_var(doc);
    fz_try(ctx) { doc = fz_open_document(ctx, path); } fz_catch(ctx) { doc = nullptr; }
    if (!doc) { fz_drop_context(ctx); return nullptr; }

    pdf_doc_t *p = (pdf_doc_t *)calloc(1, sizeof(pdf_doc_t));
    if (!p) { fz_drop_document(ctx, doc); fz_drop_context(ctx); return nullptr; }
    p->ctx = ctx; p->doc = doc;
    fz_var(p);
    fz_try(ctx) { p->page_count = fz_count_pages(ctx, doc); } fz_catch(ctx) { p->page_count = 0; }
    return p;
}

void pdf_close(pdf_doc_t *p) {
    if (!p) return;
    if (p->doc) fz_drop_document(p->ctx, p->doc);
    if (p->ctx) fz_drop_context(p->ctx);
    free(p);
}

[[nodiscard]] fz_rect pdf_page_bounds(pdf_doc_t *p, int page) {
    fz_rect empty = {0};
    if (!p || page < 0 || page >= p->page_count) return empty;
    fz_rect b = empty;
    fz_var(b);
    fz_try(p->ctx) {
        fz_page *pg = fz_load_page(p->ctx, p->doc, page);
        if (pg) { b = fz_bound_page(p->ctx, pg); fz_drop_page(p->ctx, pg); }
    } fz_catch(p->ctx) { b = empty; }
    return b;
}

bool pdf_render_page_slice(pdf_doc_t *p, int page, double scale, int sx, int sy, int vw, int vh, fz_pixmap *dst, int dst_x, int dst_y) {
    if (!p || page < 0 || page >= p->page_count || !dst || !dst->samples || vw <= 0 || vh <= 0) return false;
    if (dst_x < 0 || dst_y < 0 || dst_x + vw > dst->w || dst_y + vh > dst->h) return false;
    bool ok = false;
    fz_page *pg = nullptr; fz_device *dev = nullptr; fz_pixmap *sub = nullptr;
    fz_var(pg); fz_var(dev); fz_var(sub); fz_var(ok);

    fz_try(p->ctx) {
        pg = fz_load_page(p->ctx, p->doc, page);
        if (pg) {
            fz_matrix ctm = fz_concat(fz_scale((float)scale, (float)scale), fz_translate((float)-sx, (float)-sy));
            unsigned char *region = dst->samples + (ptrdiff_t)dst_y * dst->stride + (ptrdiff_t)dst_x * dst->n;
            sub = fz_new_pixmap_with_data(p->ctx, dst->colorspace, vw, vh, nullptr, dst->alpha, (int)dst->stride, region);
            fz_clear_pixmap_with_value(p->ctx, sub, 0xFF);
            dev = fz_new_draw_device(p->ctx, ctm, sub);
            fz_run_page(p->ctx, pg, dev, fz_identity, nullptr);
            fz_close_device(p->ctx, dev);
            ok = true;
        }
    } fz_always(p->ctx) {
        if (dev) { fz_drop_device(p->ctx, dev); dev = nullptr; }
        if (sub) { fz_drop_pixmap(p->ctx, sub); sub = nullptr; }
        if (pg) { fz_drop_page(p->ctx, pg); pg = nullptr; }
    } fz_catch(p->ctx) { ok = false; }
    return ok;
}

[[nodiscard]] fz_pixmap *pdf_render_viewport(pdf_doc_t *p, int page, double scale, int scroll_y, int vw, int vh, bool dark) {
    if (!p || page < 0 || page >= p->page_count || vw <= 0 || vh <= 0) return nullptr;
    fz_pixmap *pix = fz_new_pixmap_with_bbox(p->ctx, fz_device_rgb(p->ctx), fz_make_irect(0, 0, vw, vh), nullptr, 0);
    if (!pix) return nullptr;
    fz_clear_pixmap_with_value(p->ctx, pix, 0xFF);
    pdf_render_page_slice(p, page, scale, 0, scroll_y, vw, vh, pix, 0, 0);
    if (dark) {
        for (int y = 0; y < pix->h; y++) {
            unsigned char *row = pix->samples + (size_t)y * (size_t)pix->stride;
            int rb = pix->w * 3;
            #pragma GCC ivdep
            for (int x = 0; x < rb; x++) row[x] = 255 - row[x];
        }
    }
    return pix;
}

static void walk_outline(fz_context *ctx, fz_document *doc, fz_outline *o, int lvl, toc_item_t **arr, size_t *cnt, size_t *cap) {
    for (; o; o = o->next) {
        fz_location loc = fz_resolve_link(ctx, doc, o->uri, nullptr, nullptr);
        int page = fz_page_number_from_location(ctx, doc, loc);
        if (page >= 0) {
            if (*cnt >= *cap) {
                *cap = (*cap == 0) ? 32 : (*cap * 2);
                *arr = (toc_item_t *)realloc(*arr, *cap * sizeof(toc_item_t));
            }
            (*arr)[*cnt].title = strdup(o->title ? o->title : "(untitled)");
            (*arr)[*cnt].page = page;
            (*arr)[*cnt].level = lvl;
            (*cnt)++;
        }
        if (o->down) walk_outline(ctx, doc, o->down, lvl + 1, arr, cnt, cap);
    }
}

[[nodiscard]] toc_item_t *pdf_get_toc(pdf_doc_t *p, size_t *out_count) {
    if (!out_count) return nullptr;
    *out_count = 0;
    if (!p) return nullptr;
    toc_item_t *items = nullptr;
    size_t cnt = 0, cap = 0;
    fz_var(items); fz_var(cnt); fz_var(cap);
    fz_try(p->ctx) {
        fz_outline *out = fz_load_outline(p->ctx, p->doc);
        if (out) { walk_outline(p->ctx, p->doc, out, 1, &items, &cnt, &cap); fz_drop_outline(p->ctx, out); }
    } fz_catch(p->ctx) {}
    *out_count = cnt;
    return items;
}

void pdf_free_toc(toc_item_t *items, size_t count) {
    if (!items) return;
    for (size_t i = 0; i < count; i++) free(items[i].title);
    free(items);
}

[[nodiscard]] int *pdf_search_text(pdf_doc_t *p, const char *needle, size_t *out_count) {
    if (!out_count) return nullptr;
    *out_count = 0;
    if (!p || !needle || !*needle) return nullptr;
    int *pages = nullptr;
    size_t cnt = 0, cap = 16;
    pages = (int *)malloc(cap * sizeof(int));
    if (!pages) return nullptr;
    fz_var(pages); fz_var(cnt); fz_var(cap);

    fz_try(p->ctx) {
        for (int pg_idx = 0; pg_idx < p->page_count; pg_idx++) {
            fz_page *pg = fz_load_page(p->ctx, p->doc, pg_idx);
            if (!pg) continue;
            fz_quad quad;
            int n = fz_search_page(p->ctx, pg, needle, nullptr, &quad, 1);
            fz_drop_page(p->ctx, pg);
            if (n > 0) {
                if (cnt >= cap) { cap *= 2; pages = (int *)realloc(pages, cap * sizeof(int)); }
                pages[cnt++] = pg_idx;
            }
        }
    } fz_catch(p->ctx) {}
    *out_count = cnt;
    return pages;
}
