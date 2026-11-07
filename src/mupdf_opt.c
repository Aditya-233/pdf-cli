#include <stddef.h>
#include <string.h>
#include "pdfcli.h"

#include <mupdf/fitz/font.h>
#include <mupdf/fitz/story.h>
#include <mupdf/fitz/archive.h>
#include <mupdf/fitz/structured-text.h>

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
