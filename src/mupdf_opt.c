#include <stddef.h>
#include <string.h>
#include "pdfcli.h"

#include <mupdf/fitz/font.h>
#include <mupdf/fitz/story.h>
#include <mupdf/fitz/archive.h>
#include <mupdf/fitz/structured-text.h>

// Forward types for internal MuPDF structures
typedef struct pdf_document_s pdf_document;
typedef struct pdf_obj_s pdf_obj;
typedef struct pdf_cmap_s pdf_cmap;

// Upstream Fitz/MuPDF symbols referenced by the minimal CMap and Font handlers
extern void pdf_drop_cmap_imp(fz_context *ctx, void *item);
extern void *pdf_find_item(fz_context *ctx, void (*drop)(fz_context*, void*), pdf_obj *key);
extern void pdf_store_item(fz_context *ctx, pdf_obj *key, void *val, size_t itemsize);
extern fz_stream *pdf_open_stream(fz_context *ctx, pdf_obj *stm);
extern pdf_cmap *pdf_load_cmap(fz_context *ctx, fz_stream *stm);
extern void pdf_drop_cmap(fz_context *ctx, pdf_cmap *cmap);
extern pdf_cmap *pdf_new_cmap(fz_context *ctx);
extern void pdf_add_codespace(fz_context *ctx, pdf_cmap *cmap, unsigned int low, unsigned int high, int n);
extern void pdf_map_range_to_range(fz_context *ctx, pdf_cmap *cmap, unsigned int srclow, unsigned int srchigh, int dstlow);
extern void pdf_sort_cmap(fz_context *ctx, pdf_cmap *cmap);
extern void pdf_set_cmap_wmode(fz_context *ctx, pdf_cmap *cmap, int wmode);

// --- Minimal CMap Stubs & Embedded CMap Loader ---

[[nodiscard]] void *pdf_load_builtin_cmap(fz_context *ctx, const char *name);
[[nodiscard]] void *pdf_load_system_cmap(fz_context *ctx, const char *name);
[[nodiscard]] pdf_cmap *pdf_new_identity_cmap(fz_context *ctx, int wmode, int bytes);
[[nodiscard]] pdf_cmap *pdf_load_embedded_cmap(fz_context *ctx, pdf_document *doc, pdf_obj *stm);

void *pdf_load_builtin_cmap(fz_context *ctx, const char *name) {
    (void)ctx;
    (void)name;
    return nullptr;
}

void *pdf_load_system_cmap(fz_context *ctx, const char *name) {
    (void)ctx;
    (void)name;
    return nullptr;
}

pdf_cmap *pdf_new_identity_cmap(fz_context *ctx, int wmode, int bytes) {
    if (!ctx || bytes <= 0 || bytes > 4) return nullptr;
    pdf_cmap *cmap = pdf_new_cmap(ctx);
    if (!cmap) return nullptr;

    unsigned int max = (bytes == 4) ? 0xFFFFFFFFU : ((1U << (bytes * 8)) - 1U);
    fz_strlcpy((char *)(void *)cmap + 0x18, wmode ? "Identity-V" : "Identity-H", 32);
    pdf_add_codespace(ctx, cmap, 0, max, bytes);
    pdf_map_range_to_range(ctx, cmap, 0, max, 0);
    pdf_sort_cmap(ctx, cmap);
    pdf_set_cmap_wmode(ctx, cmap, wmode);
    return cmap;
}

pdf_cmap *pdf_load_embedded_cmap(fz_context *ctx, pdf_document *doc, pdf_obj *stm) {
    (void)doc;
    if (!ctx || !stm) return nullptr;
    pdf_cmap *cmap = (pdf_cmap *)pdf_find_item(ctx, pdf_drop_cmap_imp, stm);
    if (cmap) return cmap;

    fz_stream *file = nullptr;
    fz_var(file);
    fz_var(cmap);

    fz_try(ctx) {
        file = pdf_open_stream(ctx, stm);
        cmap = pdf_load_cmap(ctx, file);
        pdf_store_item(ctx, stm, cmap, 256);
    } fz_always(ctx) {
        if (file) fz_drop_stream(ctx, file);
    } fz_catch(ctx) {
        if (cmap) pdf_drop_cmap(ctx, cmap);
        cmap = nullptr;
    }
    return cmap;
}

// --- Dead Code & Subsystem Stubs (JavaScript, Story, Archives, Image Loaders) ---

void pdf_drop_js(void *doc);
void pdf_js_execute(void *doc, const char *str);
void pdf_js_event_init(void *ctx, void *doc, void *target, const char *val, int vtype);
[[nodiscard]] const char *pdf_js_event_value(void *ctx, void *doc);
[[nodiscard]] int pdf_js_event_result_validate(void *ctx, void *doc, const char **out);
[[nodiscard]] const char *pdf_js_event_result(void *ctx, void *doc);
[[nodiscard]] int pdf_is_image_stream(void *ctx, void *obj);
[[nodiscard]] void *js_regcomp(const char *p, int f);
[[nodiscard]] int js_regexec(void *r, const char *b, void *m, int f);
void js_regfree(void *r);
[[nodiscard]] void *fz_load_psd(void *a, void *b, void *c, void *d);
[[nodiscard]] void *fz_load_bmp(void *a, void *b, void *c, void *d);
[[nodiscard]] void *fz_load_gif(void *a, void *b, void *c, void *d);
[[nodiscard]] void *fz_load_jxr(void *a, void *b, void *c, void *d);
[[nodiscard]] void *fz_load_pnm(void *a, void *b, void *c, void *d);
[[nodiscard]] void *fz_load_tiff(void *a, void *b, void *c, void *d);

void pdf_drop_js(void *doc) { (void)doc; }
void pdf_js_execute(void *doc, const char *str) { (void)doc; (void)str; }
void pdf_js_event_init(void *ctx, void *doc, void *target, const char *val, int vtype) {
    (void)ctx; (void)doc; (void)target; (void)val; (void)vtype;
}
const char *pdf_js_event_value(void *ctx, void *doc) { (void)ctx; (void)doc; return ""; }
int pdf_js_event_result_validate(void *ctx, void *doc, const char **out) {
    (void)ctx; (void)doc; if (out) *out = ""; return 1;
}
const char *pdf_js_event_result(void *ctx, void *doc) { (void)ctx; (void)doc; return ""; }
fz_story *fz_new_story(fz_context *ctx, fz_buffer *buf, const char *user_css, float em, fz_archive *dir) {
    (void)ctx; (void)buf; (void)user_css; (void)em; (void)dir; return nullptr;
}
int fz_place_story(fz_context *ctx, fz_story *story, fz_rect where, fz_rect *filled) {
    (void)ctx; (void)story; (void)where; if (filled) *filled = (fz_rect){0}; return 1;
}
void fz_draw_story(fz_context *ctx, fz_story *story, fz_device *dev, fz_matrix ctm) {
    (void)ctx; (void)story; (void)dev; (void)ctm;
}
void fz_drop_story(fz_context *ctx, fz_story *story) {
    (void)ctx; (void)story;
}
int pdf_is_image_stream(void *ctx, void *obj) { (void)ctx; (void)obj; return 0; }
void *js_regcomp(const char *p, int f) { (void)p; (void)f; return nullptr; }
int js_regexec(void *r, const char *b, void *m, int f) { (void)r; (void)b; (void)m; (void)f; return 1; }
void js_regfree(void *r) { (void)r; }
int fz_is_cfb_archive(fz_context *ctx, fz_stream *file) { (void)ctx; (void)file; return 0; }
fz_archive *fz_open_cfb_archive_with_stream(fz_context *ctx, fz_stream *file) {
    (void)ctx; (void)file; return nullptr;
}
int fz_is_tar_archive(fz_context *ctx, fz_stream *file) { (void)ctx; (void)file; return 0; }
fz_archive *fz_open_tar_archive_with_stream(fz_context *ctx, fz_stream *file) {
    (void)ctx; (void)file; return nullptr;
}
void *fz_load_psd(void *a, void *b, void *c, void *d) { (void)a; (void)b; (void)c; (void)d; return nullptr; }
void *fz_load_bmp(void *a, void *b, void *c, void *d) { (void)a; (void)b; (void)c; (void)d; return nullptr; }
void *fz_load_gif(void *a, void *b, void *c, void *d) { (void)a; (void)b; (void)c; (void)d; return nullptr; }
void *fz_load_jxr(void *a, void *b, void *c, void *d) { (void)a; (void)b; (void)c; (void)d; return nullptr; }
void *fz_load_pnm(void *a, void *b, void *c, void *d) { (void)a; (void)b; (void)c; (void)d; return nullptr; }
void *fz_load_tiff(void *a, void *b, void *c, void *d) { (void)a; (void)b; (void)c; (void)d; return nullptr; }
int fz_segment_stext_page(fz_context *ctx, fz_stext_page *page) { (void)ctx; (void)page; return 0; }
void fz_table_hunt(fz_context *ctx, fz_stext_page *page) { (void)ctx; (void)page; }
