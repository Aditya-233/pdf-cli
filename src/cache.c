#include "pdfcli/cache.h"
#include "pdfcli/profile.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#if defined(__linux__)
#include <malloc.h>
#endif

#define CACHE_BUCKET_COUNT 128
#define CACHE_BUCKET_MASK (CACHE_BUCKET_COUNT - 1)

struct cache_node {
    char *key;
    uint64_t hash;
    char *png_path;
    fz_pixmap *pix;
    fz_context *ctx;
    size_t bytes;
    struct cache_node *prev;
    struct cache_node *next;
    struct cache_node *hash_prev;
    struct cache_node *hash_next;
};

struct page_cache {
    size_t max_entries;
    size_t count;
    size_t total_bytes;
    struct cache_node *head;
    struct cache_node *tail;
    struct cache_node *buckets[CACHE_BUCKET_COUNT];
    pthread_mutex_t lock;
};

// Fast 64-bit FNV-1a hash with Murmur3 finalizer mixer
static inline uint64_t hash_str(const char *str) {
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        hash ^= (uint64_t)(*p);
        hash *= 1099511628211ULL;
    }
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    return hash;
}

[[nodiscard]] page_cache *cache_create(size_t max_entries) {
    page_cache *c = calloc(1, sizeof(page_cache));
    if (!c) return nullptr;
    c->max_entries = (max_entries > 0) ? max_entries : 16;
    pthread_mutex_init(&c->lock, nullptr);
    return c;
}

static void remove_lru_node(page_cache *c, struct cache_node *n) {
    if (n->prev) n->prev->next = n->next;
    else c->head = n->next;

    if (n->next) n->next->prev = n->prev;
    else c->tail = n->prev;

    n->prev = nullptr;
    n->next = nullptr;
}

static void push_lru_front(page_cache *c, struct cache_node *n) {
    n->next = c->head;
    n->prev = nullptr;
    if (c->head) {
        c->head->prev = n;
    }
    c->head = n;
    if (!c->tail) {
        c->tail = n;
    }
}

static void hash_insert(page_cache *c, struct cache_node *n) {
    size_t b = (size_t)(n->hash & CACHE_BUCKET_MASK);
    n->hash_prev = nullptr;
    n->hash_next = c->buckets[b];
    if (c->buckets[b]) {
        c->buckets[b]->hash_prev = n;
    }
    c->buckets[b] = n;
}

static void hash_remove(page_cache *c, struct cache_node *n) {
    size_t b = (size_t)(n->hash & CACHE_BUCKET_MASK);
    if (n->hash_prev) {
        n->hash_prev->hash_next = n->hash_next;
    } else {
        c->buckets[b] = n->hash_next;
    }
    if (n->hash_next) {
        n->hash_next->hash_prev = n->hash_prev;
    }
    n->hash_prev = nullptr;
    n->hash_next = nullptr;
}

static struct cache_node *hash_lookup(page_cache *c, const char *key, uint64_t hash) {
    size_t b = (size_t)(hash & CACHE_BUCKET_MASK);
    for (struct cache_node *curr = c->buckets[b]; curr; curr = curr->hash_next) {
        if (curr->hash == hash && strcmp(curr->key, key) == 0) {
            return curr;
        }
    }
    return nullptr;
}

static struct cache_node *find_and_promote(page_cache *c, const char *key, uint64_t hash) {
    struct cache_node *n = hash_lookup(c, key, hash);
    if (n && n != c->head) {
        remove_lru_node(c, n);
        push_lru_front(c, n);
    }
    return n;
}

static void free_node(fz_context *fallback_ctx, struct cache_node *n) {
    if (!n) return;
    free(n->key);
    free(n->png_path);
    fz_context *c = n->ctx ? n->ctx : fallback_ctx;
    if (n->pix && c) {
        fz_drop_pixmap(c, n->pix);
        n->pix = nullptr;
    }
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
    memset(c->buckets, 0, sizeof(c->buckets));
    c->count = 0;
    c->total_bytes = 0;
    profile_record_cache(0, c->max_entries * 4 * 1024 * 1024, 0);
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

[[nodiscard]] fz_pixmap *cache_get(page_cache *c, fz_context *ctx, const char *key) {
    PROFILE_SCOPE("cache", "cache_get");
    if (!c || !key) return nullptr;
    uint64_t hash = hash_str(key);
    pthread_mutex_lock(&c->lock);
    struct cache_node *n = find_and_promote(c, key, hash);
    fz_pixmap *ret = (n && n->pix && ctx) ? fz_keep_pixmap(ctx, n->pix) : nullptr;
    if (ret) {
        PROFILE_EVENT("cache", "cache_hit", "hit", 1);
    } else {
        PROFILE_EVENT("cache", "cache_miss", "miss", 1);
    }
    pthread_mutex_unlock(&c->lock);
    return ret;
}

[[nodiscard]] bool cache_contains(page_cache *c, const char *key) {
    if (!c || !key) return false;
    uint64_t hash = hash_str(key);
    pthread_mutex_lock(&c->lock);
    struct cache_node *n = hash_lookup(c, key, hash);
    bool ret = (n != nullptr);
    pthread_mutex_unlock(&c->lock);
    return ret;
}

[[nodiscard]] const char *cache_get_png_path(page_cache *c, const char *key) {
    if (!c || !key) return nullptr;
    uint64_t hash = hash_str(key);
    pthread_mutex_lock(&c->lock);
    struct cache_node *n = find_and_promote(c, key, hash);
    const char *ret = n ? n->png_path : nullptr;
    pthread_mutex_unlock(&c->lock);
    return ret;
}

void cache_put(page_cache *c, fz_context *ctx, const char *key, fz_pixmap *pix, const char *png_path) {
    PROFILE_SCOPE("cache", "cache_put");
    if (!c || !key) return;
    uint64_t hash = hash_str(key);
    pthread_mutex_lock(&c->lock);
    size_t pix_bytes = (pix && ctx) ? fz_pixmap_size(ctx, pix) : 0;
    struct cache_node *n = find_and_promote(c, key, hash);
    if (n) {
        if (n->pix) {
            fz_context *old_c = n->ctx ? n->ctx : ctx;
            if (old_c) fz_drop_pixmap(old_c, n->pix);
            c->total_bytes = (c->total_bytes >= n->bytes) ? (c->total_bytes - n->bytes) : 0;
        }
        n->ctx = ctx;
        n->pix = (pix && ctx) ? fz_keep_pixmap(ctx, pix) : nullptr;
        n->bytes = pix_bytes;
        c->total_bytes += pix_bytes;
        free(n->png_path);
        n->png_path = png_path ? strdup(png_path) : nullptr;
        profile_record_cache(c->total_bytes, c->max_entries * 4 * 1024 * 1024, c->count);
        pthread_mutex_unlock(&c->lock);
        return;
    }
    while ((c->count >= c->max_entries || (c->total_bytes > 32 * 1024 * 1024 && c->count > 1)) && c->tail) {
        PROFILE_START("cache", "cache_eviction");
        struct cache_node *old = c->tail;
        remove_lru_node(c, old);
        hash_remove(c, old);
        c->total_bytes = (c->total_bytes >= old->bytes) ? (c->total_bytes - old->bytes) : 0;
        free_node(ctx, old);
        c->count--;
        PROFILE_END("cache", "cache_eviction");
        PROFILE_EVENT("cache", "cache_eviction_event", "evicted", 1);
    }
    n = calloc(1, sizeof(struct cache_node));
    if (!n) {
        pthread_mutex_unlock(&c->lock);
        return;
    }
    n->key = strdup(key);
    if (!n->key) {
        free(n);
        pthread_mutex_unlock(&c->lock);
        return;
    }
    n->hash = hash;
    n->ctx = ctx;
    n->pix = (pix && ctx) ? fz_keep_pixmap(ctx, pix) : nullptr;
    n->bytes = pix_bytes;
    c->total_bytes += pix_bytes;
    n->png_path = png_path ? strdup(png_path) : nullptr;
    push_lru_front(c, n);
    hash_insert(c, n);
    c->count++;
    profile_record_cache(c->total_bytes, c->max_entries * 4 * 1024 * 1024, c->count);
    pthread_mutex_unlock(&c->lock);
}

void cache_make_key(char *out_buf, size_t buf_size, int page_num, fit_mode fit, tint_mode tint, double zoom, const crop_box *crop, int cols, int rows, int scroll_y) {
    double ct = crop ? crop->top : 0.0, cb = crop ? crop->bottom : 0.0;
    double cl = crop ? crop->left : 0.0, cr = crop ? crop->right : 0.0;
    snprintf(out_buf, buf_size, "p%d:f%u:t%u:z%.2f:c%.2f,%.2f,%.2f,%.2f:%dx%d:s%d",
             page_num, (unsigned)fit, (unsigned)tint, zoom, ct, cb, cl, cr, cols, rows, scroll_y);
}
