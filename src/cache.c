#include "pdfcli/cache.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#if defined(__linux__)
#include <malloc.h>
#endif

struct cache_node {
    char *key;
    char *png_path;
    fz_pixmap *pix;
    struct cache_node *prev;
    struct cache_node *next;
};

struct page_cache {
    size_t max_entries;
    size_t count;
    struct cache_node *head;
    struct cache_node *tail;
    pthread_mutex_t lock;
};

[[nodiscard]] page_cache *cache_create(size_t max_entries) {
    page_cache *c = calloc(1, sizeof(page_cache));
    if (!c) return nullptr;
    c->max_entries = (max_entries > 0) ? max_entries : 16;
    pthread_mutex_init(&c->lock, nullptr);
    return c;
}

static void remove_node(page_cache *c, struct cache_node *n) {
    if (n->prev) n->prev->next = n->next; else c->head = n->next;
    if (n->next) n->next->prev = n->prev; else c->tail = n->prev;
    n->prev = n->next = nullptr;
}

static void push_front(page_cache *c, struct cache_node *n) {
    n->next = c->head;
    n->prev = nullptr;
    if (c->head) c->head->prev = n;
    c->head = n;
    if (!c->tail) c->tail = n;
}

static void free_node(fz_context *ctx, struct cache_node *n) {
    if (!n) return;
    free(n->key);
    free(n->png_path);
    if (n->pix && ctx) fz_drop_pixmap(ctx, n->pix);
    free(n);
}

void cache_clear(page_cache *c, fz_context *ctx) {
    if (!c) return;
    pthread_mutex_lock(&c->lock);
    for (struct cache_node *n = c->head, *nxt; n; n = nxt) {
        nxt = n->next;
        free_node(ctx, n);
    }
    c->head = c->tail = nullptr;
    c->count = 0;
#if defined(__linux__)
    malloc_trim(0);
#endif
    pthread_mutex_unlock(&c->lock);
}

void cache_destroy(page_cache *c, fz_context *ctx) {
    if (!c) return;
    cache_clear(c, ctx);
    pthread_mutex_destroy(&c->lock);
    free(c);
}

static struct cache_node *find_and_promote(page_cache *c, const char *key) {
    for (struct cache_node *curr = c->head; curr; curr = curr->next) {
        if (strcmp(curr->key, key) == 0) {
            if (curr != c->head) {
                remove_node(c, curr);
                push_front(c, curr);
            }
            return curr;
        }
    }
    return nullptr;
}

[[nodiscard]] fz_pixmap *cache_get(page_cache *c, fz_context *ctx, const char *key) {
    if (!c || !key) return nullptr;
    pthread_mutex_lock(&c->lock);
    struct cache_node *n = find_and_promote(c, key);
    fz_pixmap *ret = (n && n->pix && ctx) ? fz_keep_pixmap(ctx, n->pix) : nullptr;
    pthread_mutex_unlock(&c->lock);
    return ret;
}

[[nodiscard]] const char *cache_get_png_path(page_cache *c, const char *key) {
    if (!c || !key) return nullptr;
    pthread_mutex_lock(&c->lock);
    struct cache_node *n = find_and_promote(c, key);
    const char *ret = n ? n->png_path : nullptr;
    pthread_mutex_unlock(&c->lock);
    return ret;
}

void cache_put(page_cache *c, fz_context *ctx, const char *key, fz_pixmap *pix, const char *png_path) {
    if (!c || !key) return;
    pthread_mutex_lock(&c->lock);
    struct cache_node *n = find_and_promote(c, key);
    if (n) {
        if (n->pix && ctx) fz_drop_pixmap(ctx, n->pix);
        n->pix = (pix && ctx) ? fz_keep_pixmap(ctx, pix) : nullptr;
        free(n->png_path);
        n->png_path = png_path ? strdup(png_path) : nullptr;
        pthread_mutex_unlock(&c->lock);
        return;
    }
    if (c->count >= c->max_entries && c->tail) {
        struct cache_node *old = c->tail;
        remove_node(c, old);
        free_node(ctx, old);
        c->count--;
#if defined(__linux__)
        malloc_trim(0);
#endif
    }
    n = calloc(1, sizeof(struct cache_node));
    if (!n) {
        pthread_mutex_unlock(&c->lock);
        return;
    }
    n->key = strdup(key);
    n->pix = (pix && ctx) ? fz_keep_pixmap(ctx, pix) : nullptr;
    n->png_path = png_path ? strdup(png_path) : nullptr;
    push_front(c, n);
    c->count++;
    pthread_mutex_unlock(&c->lock);
}

void cache_make_key(char *out_buf, size_t buf_size, int page_num, fit_mode fit, tint_mode tint, double zoom, const crop_box *crop, int cols, int rows, int scroll_y) {
    double ct = crop ? crop->top : 0.0, cb = crop ? crop->bottom : 0.0;
    double cl = crop ? crop->left : 0.0, cr = crop ? crop->right : 0.0;
    snprintf(out_buf, buf_size, "p%d:f%u:t%u:z%.2f:c%.2f,%.2f,%.2f,%.2f:%dx%d:s%d",
             page_num, (unsigned)fit, (unsigned)tint, zoom, ct, cb, cl, cr, cols, rows, scroll_y);
}
