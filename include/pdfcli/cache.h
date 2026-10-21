#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <mupdf/fitz.h>
#include "types.h"

typedef struct page_cache page_cache;

[[nodiscard]] page_cache *cache_create(size_t max_entries);
void cache_destroy(page_cache *cache, fz_context *ctx);

// Lookup a cached pixmap. If found, returns a retained fz_pixmap* (caller must fz_drop_pixmap when done)
[[nodiscard]] fz_pixmap *cache_get(page_cache *cache, fz_context *ctx, const char *key);

// Fast O(1) check if a key is present in cache without retaining a pixmap reference
[[nodiscard]] bool cache_contains(page_cache *cache, const char *key);

// Store a pixmap into cache. Cache retains a reference.
void cache_put(page_cache *cache, fz_context *ctx, const char *key, fz_pixmap *pix, const char *png_path);

// Get cached PNG file path if one exists
[[nodiscard]] const char *cache_get_png_path(page_cache *cache, const char *key);

void cache_clear(page_cache *cache, fz_context *ctx);

// Helper to generate a cache key
void cache_make_key(
    char *out_buf,
    size_t buf_size,
    int page_num,
    fit_mode fit,
    tint_mode tint,
    double zoom,
    const crop_box *crop,
    int cols,
    int rows,
    int scroll_y
);
