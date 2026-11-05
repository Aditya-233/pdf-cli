#include "pdfcli.h"
#include <string.h>
#include <math.h>
#include <malloc.h>

extern const fz_document_handler pdf_document_handler;

static void dummy_warning_cb(void *user, const char *msg) {
    (void)user;
    (void)msg;
}

[[nodiscard]] pdf_doc_t *pdf_open(const char *path) {
    if (!path || !*path) return nullptr;
    // Ultra low-footprint MuPDF context store: 0 KB cache store
    fz_context *ctx = fz_new_context(nullptr, nullptr, 0);
    if (!ctx) return nullptr;

    fz_set_warning_callback(ctx, dummy_warning_cb, nullptr);
    fz_set_error_callback(ctx, dummy_warning_cb, nullptr);

    fz_document *doc = nullptr;
    fz_var(doc);

    fz_try(ctx) {
        fz_register_document_handler(ctx, &pdf_document_handler);
        doc = fz_open_document(ctx, path);
    } fz_catch(ctx) {
        doc = nullptr;
    }

    if (!doc) {
        fz_drop_context(ctx);
        return nullptr;
    }

    // Check encryption / password requirements
    int needs_pwd = 0;
    fz_var(needs_pwd);
    fz_try(ctx) {
        needs_pwd = fz_needs_password(ctx, doc);
        if (needs_pwd) {
            // Attempt blank password authentication
            if (fz_authenticate_password(ctx, doc, "")) {
                needs_pwd = 0;
            }
        }
    } fz_catch(ctx) {
        needs_pwd = 1;
    }

    if (needs_pwd) {
        char safe_path[256];
        sanitize_terminal_string(path, safe_path, sizeof(safe_path));
        fprintf(stderr, "Error: PDF '%s' is encrypted and requires a password.\n", safe_path);
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        return nullptr;
    }

    // Check page count
    int page_count = 0;
    fz_var(page_count);
    fz_try(ctx) {
        page_count = fz_count_pages(ctx, doc);
    } fz_catch(ctx) {
        page_count = 0;
    }

    if (page_count <= 0) {
        char safe_path[256];
        sanitize_terminal_string(path, safe_path, sizeof(safe_path));
        fprintf(stderr, "Error: PDF '%s' has 0 readable pages or corrupted page tree.\n", safe_path);
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        return nullptr;
    }

    pdf_doc_t *p = (pdf_doc_t *)calloc(1, sizeof(pdf_doc_t));
    if (!p) {
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        return nullptr;
    }

    p->ctx = ctx;
    p->doc = doc;
    p->page_count = page_count;
    return p;
}

void pdf_close(pdf_doc_t *p) {
    if (!p) return;
    if (p->doc && p->ctx) fz_drop_document(p->ctx, p->doc);
    if (p->ctx) fz_drop_context(p->ctx);
    free(p);
}

[[nodiscard]] fz_rect pdf_page_bounds(pdf_doc_t *p, int page) {
    const fz_rect fallback_rect = { .x0 = 0.0f, .y0 = 0.0f, .x1 = 612.0f, .y1 = 792.0f };
    if (!p || !p->ctx || !p->doc || page < 0 || page >= p->page_count) return (fz_rect){0};

    fz_rect b = fallback_rect;
    fz_page *pg = nullptr;
    fz_var(pg);
    fz_var(b);

    fz_try(p->ctx) {
        pg = fz_load_page(p->ctx, p->doc, page);
        if (pg) {
            b = fz_bound_page(p->ctx, pg);
        }
    } fz_always(p->ctx) {
        if (pg) {
            fz_drop_page(p->ctx, pg);
            pg = nullptr;
        }
    } fz_catch(p->ctx) {
        b = fallback_rect;
    }

    // Validate bounds for finiteness, positive size, and reasonable limits
    if (!isnan(b.x0) && !isinf(b.x0) && !isnan(b.x1) && !isinf(b.x1) &&
        !isnan(b.y0) && !isinf(b.y0) && !isnan(b.y1) && !isinf(b.y1) &&
        b.x1 > b.x0 && b.y1 > b.y0) {
        float w = b.x1 - b.x0;
        float h = b.y1 - b.y0;
        if (w >= 1.0f && w <= 50000.0f && h >= 1.0f && h <= 50000.0f) {
            return b;
        }
    }

    return fallback_rect;
}

bool pdf_render_page_slice(pdf_doc_t *p, int page, double scale, int sx, int sy, int vw, int vh, fz_pixmap *dst, int dst_x, int dst_y) {
    if (!p || !p->ctx || !p->doc || page < 0 || page >= p->page_count || !dst || !dst->samples || vw <= 0 || vh <= 0)
        return false;

    // Checked bounds without signed integer overflow
    if (dst->w <= 0 || dst->h <= 0 || dst_x < 0 || dst_y < 0) return false;
    if (dst_x > dst->w || vw > dst->w - dst_x) return false;
    if (dst_y > dst->h || vh > dst->h - dst_y) return false;

    // Scale range & finiteness check
    if (isnan(scale) || isinf(scale) || scale <= 0.0001 || scale > 10000.0) return false;

    bool ok = false;
    fz_page *pg = nullptr;
    fz_device *dev = nullptr;
    fz_pixmap *sub = nullptr;
    fz_var(pg);
    fz_var(dev);
    fz_var(sub);
    fz_var(ok);

    fz_try(p->ctx) {
        pg = fz_load_page(p->ctx, p->doc, page);
        if (pg) {
            fz_rect b = fz_bound_page(p->ctx, pg);
            if (isnan(b.x0) || isinf(b.x0) || isnan(b.x1) || isinf(b.x1) ||
                isnan(b.y0) || isinf(b.y0) || isnan(b.y1) || isinf(b.y1)) {
                b = (fz_rect){ .x0 = 0.0f, .y0 = 0.0f, .x1 = 612.0f, .y1 = 792.0f };
            }

            fz_matrix ctm = fz_concat(fz_translate(-b.x0, -b.y0),
                                      fz_concat(fz_scale((float)scale, (float)scale),
                                                fz_translate((float)-sx, (float)-sy)));

            unsigned char *region = dst->samples + (ptrdiff_t)dst_y * dst->stride + (ptrdiff_t)dst_x * dst->n;
            sub = fz_new_pixmap_with_data(p->ctx, dst->colorspace, vw, vh, nullptr, dst->alpha, (int)dst->stride, region);
            if (sub) {
                fz_clear_pixmap_with_value(p->ctx, sub, 0xFF);
                dev = fz_new_draw_device(p->ctx, ctm, sub);
                if (dev) {
                    fz_run_page(p->ctx, pg, dev, fz_identity, nullptr);
                    fz_close_device(p->ctx, dev);
                    ok = true;
                }
            }
        }
    } fz_always(p->ctx) {
        if (dev) { fz_drop_device(p->ctx, dev); dev = nullptr; }
        if (sub) { fz_drop_pixmap(p->ctx, sub); sub = nullptr; }
        if (pg) { fz_drop_page(p->ctx, pg); pg = nullptr; }
    } fz_catch(p->ctx) {
        ok = false;
    }

    return ok;
}

[[nodiscard]] fz_pixmap *pdf_render_viewport(pdf_doc_t *p, int page, double scale, int scroll_y, int vw, int vh, bool dark) {
    if (!p || !p->ctx || !p->doc || page < 0 || page >= p->page_count || vw <= 0 || vh <= 0) return nullptr;
    if (vw > 16384 || vh > 16384) return nullptr;

    fz_pixmap *pix = nullptr;
    fz_var(pix);

    fz_try(p->ctx) {
        pix = fz_new_pixmap_with_bbox(p->ctx, fz_device_rgb(p->ctx), fz_make_irect(0, 0, vw, vh), nullptr, 0);
    } fz_catch(p->ctx) {
        pix = nullptr;
    }

    if (!pix) return nullptr;

    fz_clear_pixmap_with_value(p->ctx, pix, 0xFF);
    bool slice_ok = pdf_render_page_slice(p, page, scale, 0, scroll_y, vw, vh, pix, 0, 0);
    if (!slice_ok) {
        fz_drop_pixmap(p->ctx, pix);
        return nullptr;
    }

    if (dark) {
        for (int y = 0; y < pix->h; y++) {
            unsigned char *row = pix->samples + (size_t)y * (size_t)pix->stride;
            int rb = pix->w * 3;
            #pragma GCC ivdep
            for (int x = 0; x < rb; x++) row[x] = (unsigned char)(255 - row[x]);
        }
    }

    return pix;
}

static void walk_outline(fz_context *ctx, fz_document *doc, fz_outline *o, int lvl, toc_item_t **arr, size_t *cnt, size_t *cap, int depth) {
    const int MAX_OUTLINE_DEPTH = 32;
    const size_t MAX_OUTLINE_ITEMS = 10000;

    if (depth > MAX_OUTLINE_DEPTH || *cnt >= MAX_OUTLINE_ITEMS) return;

    for (; o && *cnt < MAX_OUTLINE_ITEMS; o = o->next) {
        fz_location loc = fz_resolve_link(ctx, doc, o->uri, nullptr, nullptr);
        int page = fz_page_number_from_location(ctx, doc, loc);
        if (page >= 0) {
            if (*cnt >= *cap) {
                size_t new_cap = (*cap == 0) ? 32 : (*cap * 2);
                if (new_cap < *cap || new_cap > MAX_OUTLINE_ITEMS) new_cap = MAX_OUTLINE_ITEMS;
                if (new_cap <= *cnt) return;

                toc_item_t *new_arr = (toc_item_t *)realloc(*arr, new_cap * sizeof(toc_item_t));
                if (!new_arr) {
                    return;
                }
                *arr = new_arr;
                *cap = new_cap;
            }

            const char *raw_title = o->title ? o->title : "(untitled)";
            char *title_copy = strdup(raw_title);
            if (!title_copy) {
                title_copy = strdup("(untitled)");
            }

            if (title_copy) {
                (*arr)[*cnt].title = title_copy;
                (*arr)[*cnt].page = page;
                (*arr)[*cnt].level = (lvl < 16) ? lvl : 16;
                (*cnt)++;
            }
        }

        if (o->down && depth < MAX_OUTLINE_DEPTH) {
            walk_outline(ctx, doc, o->down, lvl + 1, arr, cnt, cap, depth + 1);
        }
    }
}

[[nodiscard]] toc_item_t *pdf_get_toc(pdf_doc_t *p, size_t *out_count) {
    if (!out_count) return nullptr;
    *out_count = 0;
    if (!p || !p->ctx || !p->doc) return nullptr;

    toc_item_t *items = nullptr;
    size_t cnt = 0, cap = 0;
    fz_outline *out = nullptr;
    fz_var(items);
    fz_var(cnt);
    fz_var(cap);
    fz_var(out);

    fz_try(p->ctx) {
        out = fz_load_outline(p->ctx, p->doc);
        if (out) {
            walk_outline(p->ctx, p->doc, out, 1, &items, &cnt, &cap, 0);
        }
    } fz_always(p->ctx) {
        if (out) {
            fz_drop_outline(p->ctx, out);
            out = nullptr;
        }
    } fz_catch(p->ctx) {
        pdf_free_toc(items, cnt);
        items = nullptr;
        cnt = 0;
    }

    *out_count = cnt;
    return items;
}

void pdf_free_toc(toc_item_t *items, size_t count) {
    if (!items) return;
    for (size_t i = 0; i < count; i++) {
        free(items[i].title);
    }
    free(items);
}

[[nodiscard]] search_hit_t *pdf_search_text(pdf_doc_t *p, const char *needle, size_t *out_count) {
    if (!out_count) return nullptr;
    *out_count = 0;
    if (!p || !p->ctx || !p->doc || !needle || !*needle) return nullptr;

    const size_t MAX_SEARCH_HITS = 2000;
    size_t cap = 32;
    search_hit_t *hits = (search_hit_t *)malloc(cap * sizeof(search_hit_t));
    if (!hits) return nullptr;

    size_t cnt = 0;
    fz_page *pg = nullptr;
    fz_var(hits);
    fz_var(cap);
    fz_var(cnt);
    fz_var(pg);

    fz_try(p->ctx) {
        for (int pg_idx = 0; pg_idx < p->page_count && cnt < MAX_SEARCH_HITS; pg_idx++) {
            pg = fz_load_page(p->ctx, p->doc, pg_idx);
            if (!pg) continue;

            fz_quad quads[32];
            int n = fz_search_page(p->ctx, pg, needle, nullptr, quads, 32);
            for (int m = 0; m < n && cnt < MAX_SEARCH_HITS; m++) {
                if (cnt >= cap) {
                    size_t new_cap = cap * 2;
                    if (new_cap < cap || new_cap > MAX_SEARCH_HITS) new_cap = MAX_SEARCH_HITS;
                    if (new_cap <= cnt) break;

                    search_hit_t *new_hits = (search_hit_t *)realloc(hits, new_cap * sizeof(search_hit_t));
                    if (!new_hits) break;
                    hits = new_hits;
                    cap = new_cap;
                }
                hits[cnt].page = pg_idx;
                hits[cnt].rect = fz_rect_from_quad(quads[m]);
                cnt++;
            }

            fz_drop_page(p->ctx, pg);
            pg = nullptr;

            // Empty context store periodically to keep RAM under strict control
            if ((pg_idx % 5) == 0) {
                fz_empty_store(p->ctx);
                fz_purge_glyph_cache(p->ctx);
                malloc_trim(0);
            }
        }
    } fz_always(p->ctx) {
        if (pg) {
            fz_drop_page(p->ctx, pg);
            pg = nullptr;
        }
        fz_empty_store(p->ctx);
        fz_purge_glyph_cache(p->ctx);
        malloc_trim(0);
    } fz_catch(p->ctx) {
        // Retain any hits found up to the error point
    }

    *out_count = cnt;
    return hits;
}

void pdf_free_search(search_hit_t *hits) {
    free(hits);
}
