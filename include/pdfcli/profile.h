#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <mupdf/fitz.h>

#ifdef __cplusplus
extern "C" {
#endif

// Core Profiler Lifecycle
void profile_init(bool enabled, const char *trace_json_path);
[[nodiscard]] bool profile_is_enabled(void);
void profile_shutdown(void);
void profile_print_summary(FILE *out);

// High-resolution clock helper (nanosecond precision)
static inline uint64_t profile_get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Scoped duration measurement helper (RAII via cleanup attribute)
typedef struct {
    const char *category;
    const char *name;
    uint64_t start_ns;
} profile_scope_guard_t;

void profile_scope_record(const char *category, const char *name, uint64_t start_ns, uint64_t end_ns);

static inline profile_scope_guard_t profile_scope_enter(const char *category, const char *name) {
    if (!profile_is_enabled()) {
        return (profile_scope_guard_t){ .category = nullptr, .name = nullptr, .start_ns = 0 };
    }
    return (profile_scope_guard_t){
        .category = category,
        .name = name,
        .start_ns = profile_get_time_ns()
    };
}

static inline void profile_scope_exit(profile_scope_guard_t *guard) {
    if (guard->start_ns) {
        uint64_t end_ns = profile_get_time_ns();
        profile_scope_record(guard->category, guard->name, guard->start_ns, end_ns);
    }
}

#define _PROFILE_CONCAT_INNER(a, b) a ## b
#define _PROFILE_CONCAT(a, b) _PROFILE_CONCAT_INNER(a, b)

#define PROFILE_SCOPE(category, name) \
    profile_scope_guard_t _PROFILE_CONCAT(_prof_scope_, __COUNTER__) \
        __attribute__((cleanup(profile_scope_exit))) = \
        profile_scope_enter((category), (name))

// Explicit start/end markers for non-lexical or multi-threaded scopes
void profile_start(const char *category, const char *name);
void profile_end(const char *category, const char *name);

#define PROFILE_START(category, name) profile_start((category), (name))
#define PROFILE_END(category, name) profile_end((category), (name))

// Instant event / metric recording
void profile_event(const char *category, const char *name, const char *arg_name, int64_t arg_val);

#define PROFILE_EVENT(category, name, arg_name, arg_val) \
    profile_event((category), (name), (arg_name), (int64_t)(arg_val))

// Frame and Cache Tracking
void profile_record_frame(void);
void profile_record_cache(size_t used_bytes, size_t max_bytes, size_t item_count);

// MuPDF Fitz Allocator Hook
[[nodiscard]] const fz_alloc_context *profile_get_fitz_alloc(void);

// Kernel ProcFS Memory Reader
bool profile_get_proc_memory(size_t *rss_bytes, size_t *hwm_bytes, size_t *virt_bytes);

#ifdef __cplusplus
}
#endif
