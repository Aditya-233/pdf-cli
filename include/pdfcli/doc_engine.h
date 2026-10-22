#pragma once

#include "pdfcli/types.h"
#include <mupdf/fitz.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct doc_handle doc_handle;
typedef struct doc_page_link doc_page_link;
typedef struct doc_toc_entry doc_toc_entry;
typedef struct doc_search_hit doc_search_hit;

struct doc_page_link {
    char *uri;
    double x0, y0, x1, y1; // PDF coordinate bounding box
    bool is_external;
    int dest_page;         // 0-indexed destination page (-1 if external)
    double dest_x, dest_y;
};

struct doc_toc_entry {
    int level;
    char *title;
    int page;              // 0-indexed destination page
    double x, y;           // Destination anchor point
};

struct doc_search_hit {
    int page;
    fz_rect rect;
};

[[nodiscard]] doc_handle *doc_open(const char *path);
void doc_close(doc_handle *doc);
[[nodiscard]] fz_context *doc_get_context(doc_handle *doc);
[[nodiscard]] int doc_get_page_count(doc_handle *doc);
[[nodiscard]] bool doc_page_has_visual_content(doc_handle *doc, int page_num);
[[nodiscard]] fz_rect doc_get_page_bounds(doc_handle *doc, int page_num);
[[nodiscard]] fz_pixmap *doc_render_page_pixmap(doc_handle *doc, int page_num, double zoom, double dpi);
[[nodiscard]] fz_pixmap *doc_render_page_viewport(
    doc_handle *doc,
    int page_num,
    double scale,
    int src_x,
    int src_y,
    int view_w,
    int view_h
);

// Render the page slice (source offset src_x,src_y at 'scale', size view_w*view_h)
// directly into the destination region (dst_x,dst_y) of an existing composite
// pixmap. Avoids the intermediate full-size slice pixmap plus the row copy.
// Returns true on success; false if bounds are violated or rendering fails
// (caller can fall back to doc_render_page_viewport + row copy).
[[nodiscard]] bool doc_render_page_into(
    doc_handle *doc,
    int page_num,
    double scale,
    int src_x,
    int src_y,
    int view_w,
    int view_h,
    fz_pixmap *dst,
    int dst_x,
    int dst_y
);
void doc_layout_reflowable(doc_handle *doc, double width_pts, double height_pts, double em_size);
[[nodiscard]] doc_page_link *doc_get_page_links(doc_handle *doc, int page_num, size_t *out_count);
void doc_free_page_links(doc_page_link *links, size_t count);
[[nodiscard]] doc_toc_entry *doc_get_toc(doc_handle *doc, size_t *out_count);
void doc_free_toc(doc_toc_entry *toc, size_t count);
[[nodiscard]] doc_search_hit *doc_search_text(doc_handle *doc, const char *needle, size_t *out_count);
void doc_free_search_hits(doc_search_hit *hits, size_t count);
void doc_empty_store(doc_handle *doc);
[[nodiscard]] fz_pixmap *doc_load_standalone_image(doc_handle *doc, const char *path);

#ifdef __cplusplus
}
#endif
