#include "pdfcli/arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

typedef struct arena_chunk arena_chunk;
struct arena_chunk {
    arena_chunk *next;
    size_t cap;
    size_t off;
    alignas(max_align_t) uint8_t data[];
};

struct arena {
    arena_chunk *first;
    arena_chunk *current;
    size_t default_chunk_size;
};

[[nodiscard]] arena *arena_create(size_t capacity) {
    auto a = (arena *)calloc(1, sizeof(arena));
    if (!a) return nullptr;

    size_t init_cap = capacity > 0 ? capacity : 4096;
    auto first = (arena_chunk *)malloc(sizeof(arena_chunk) + init_cap);
    if (!first) {
        free(a);
        return nullptr;
    }
    first->next = nullptr;
    first->cap = init_cap;
    first->off = 0;

    a->first = first;
    a->current = first;
    a->default_chunk_size = init_cap;
    return a;
}

void arena_destroy(arena *a) {
    if (!a) return;
    arena_chunk *c = a->first;
    while (c) {
        arena_chunk *next = c->next;
        free(c);
        c = next;
    }
    free(a);
}

static void *arena_alloc_grow(arena *a, size_t sz) {
    // Scan subsequent pooled chunks retained across arena_reset
    arena_chunk *prev = a->current;
    arena_chunk *curr = prev ? prev->next : a->first;

    while (curr) {
        if (curr->cap >= sz) {
            if (prev && prev != a->current) {
                prev->next = curr->next;
                curr->next = a->current->next;
                a->current->next = curr;
            }
            curr->off = sz;
            a->current = curr;
            return curr->data;
        }
        prev = curr;
        curr = curr->next;
    }

    // Allocate a new chunk and add it to the pool
    size_t chunk_cap = a->default_chunk_size > sz ? a->default_chunk_size : sz;
    auto new_chunk = (arena_chunk *)malloc(sizeof(arena_chunk) + chunk_cap);
    if (!new_chunk) return nullptr;
    new_chunk->cap = chunk_cap;
    new_chunk->off = sz;

    if (a->current) {
        new_chunk->next = a->current->next;
        a->current->next = new_chunk;
        a->current = new_chunk;
    } else {
        new_chunk->next = a->first;
        a->first = new_chunk;
        a->current = new_chunk;
    }

    return new_chunk->data;
}

[[nodiscard]] void *arena_alloc(arena *a, size_t size) {
    if (!a || size > SIZE_MAX - 7) return nullptr;
    size_t sz = (size + 7) & ~(size_t)7;

    arena_chunk *c = a->current;
    if (__builtin_expect(c != nullptr && c->off + sz <= c->cap, 1)) {
        void *ptr = c->data + c->off;
        c->off += sz;
        return ptr;
    }

    return arena_alloc_grow(a, sz);
}

[[nodiscard]] void *arena_alloc_zero(arena *a, size_t size) {
    void *p = arena_alloc(a, size);
    if (p) memset(p, 0, size);
    return p;
}

[[nodiscard]] char *arena_strdup(arena *a, const char *str) {
    if (!str) return nullptr;
    size_t len = strlen(str) + 1;
    char *p = (char *)arena_alloc(a, len);
    if (p) memcpy(p, str, len);
    return p;
}

void arena_reset(arena *a) {
    if (!a) return;
    for (arena_chunk *c = a->first; c; c = c->next) {
        c->off = 0;
    }
    a->current = a->first;
}

[[nodiscard]] size_t arena_used(const arena *a) {
    if (!a) return 0;
    size_t total = 0;
    for (const arena_chunk *c = a->first; c; c = c->next) {
        total += c->off;
        if (c == a->current) break;
    }
    return total;
}
