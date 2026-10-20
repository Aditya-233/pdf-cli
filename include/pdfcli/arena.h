#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct arena arena;

[[nodiscard]] arena *arena_create(size_t capacity);
void arena_destroy(arena *a);

[[nodiscard]] void *arena_alloc(arena *a, size_t size);
[[nodiscard]] void *arena_alloc_zero(arena *a, size_t size);
[[nodiscard]] char *arena_strdup(arena *a, const char *str);

void arena_reset(arena *a);
[[nodiscard]] size_t arena_used(const arena *a);
