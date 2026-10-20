#include "pdfcli/arena.h"
#include <stdlib.h>
#include <string.h>

struct arena {
    uint8_t *buf;
    size_t cap;
    size_t off;
};

[[nodiscard]] arena *arena_create(size_t capacity) {
    auto a = (arena *)calloc(1, sizeof(arena));
    if (!a) return nullptr;
    if (!(a->buf = malloc(capacity))) {
        free(a);
        return nullptr;
    }
    a->cap = capacity;
    return a;
}

void arena_destroy(arena *a) {
    if (a) {
        free(a->buf);
        free(a);
    }
}

[[nodiscard]] void *arena_alloc(arena *a, size_t size) {
    if (!a || !a->buf) return nullptr;
    size_t aligned = (a->off + 7) & ~7ULL;
    if (aligned + size > a->cap) return nullptr;
    void *ptr = a->buf + aligned;
    a->off = aligned + size;
    return ptr;
}

[[nodiscard]] void *arena_alloc_zero(arena *a, size_t size) {
    void *p = arena_alloc(a, size);
    if (p) memset(p, 0, size);
    return p;
}

[[nodiscard]] char *arena_strdup(arena *a, const char *str) {
    if (!str) return nullptr;
    size_t len = strlen(str) + 1;
    char *p = arena_alloc(a, len);
    if (p) memcpy(p, str, len);
    return p;
}

void arena_reset(arena *a) {
    if (a) a->off = 0;
}

[[nodiscard]] size_t arena_used(const arena *a) {
    return a ? a->off : 0;
}
