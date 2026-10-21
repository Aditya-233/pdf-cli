#define _GNU_SOURCE 1
#define _DEFAULT_SOURCE 1
#define _POSIX_C_SOURCE 200809L

#include "pdfcli/profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

#define RING_BUFFER_SIZE 65536
#define MAX_STAT_ENTRIES 256
#define MAX_THREAD_STACK_DEPTH 64
#define FITZ_MEM_MAGIC 0xF172C0DE

// ---------------------------------------------------------------------------
// Trace Ring Buffer Data Structure
// ---------------------------------------------------------------------------

typedef struct {
    const char *name;
    const char *category;
    const char *arg_name;
    int64_t arg_val;
    uint64_t ts_us;
    uint64_t dur_us;
    int pid;
    uint32_t tid;
    char phase; // 'X', 'B', 'E', 'i'
} trace_event_t;

static trace_event_t g_ring_buffer[RING_BUFFER_SIZE];
static atomic_size_t g_event_index = 0;

// ---------------------------------------------------------------------------
// Thread-Local Stack for Explicit Start/End Scopes
// ---------------------------------------------------------------------------

typedef struct {
    const char *name;
    const char *category;
    uint64_t start_ns;
} thread_active_scope_t;

static _Thread_local thread_active_scope_t tl_scope_stack[MAX_THREAD_STACK_DEPTH];
static _Thread_local size_t tl_scope_depth = 0;

// ---------------------------------------------------------------------------
// Aggregated Statistics Engine
// ---------------------------------------------------------------------------

typedef struct {
    const char *category;
    const char *name;
    atomic_uint_least64_t call_count;
    atomic_uint_least64_t total_ns;
    atomic_uint_least64_t min_ns;
    atomic_uint_least64_t max_ns;
} stat_entry_t;

static stat_entry_t g_stat_entries[MAX_STAT_ENTRIES];
static atomic_size_t g_stat_entry_count = 0;
static pthread_mutex_t g_stat_mutex = PTHREAD_MUTEX_INITIALIZER;

// ---------------------------------------------------------------------------
// Fitz Memory Allocator State
// ---------------------------------------------------------------------------

typedef struct {
    size_t size;
    uint32_t tag;
    uint32_t magic;
} fitz_mem_header_t;

static_assert(sizeof(fitz_mem_header_t) == 16, "fitz_mem_header_t must be exactly 16 bytes for alignment");

static atomic_size_t g_fitz_current_bytes = 0;
static atomic_size_t g_fitz_peak_bytes = 0;
static atomic_size_t g_fitz_total_allocs = 0;
static atomic_size_t g_fitz_total_frees = 0;

// ---------------------------------------------------------------------------
// Cache and Frame Tracking State
// ---------------------------------------------------------------------------

static atomic_size_t g_cache_used_bytes = 0;
static atomic_size_t g_cache_max_bytes = 0;
static atomic_size_t g_cache_peak_used_bytes = 0;
static atomic_size_t g_cache_item_count = 0;
static atomic_size_t g_cache_peak_items = 0;

static atomic_size_t g_frame_count = 0;

// ---------------------------------------------------------------------------
// Global Profiler State
// ---------------------------------------------------------------------------

static atomic_bool g_profile_enabled = false;
static atomic_bool g_profile_shutdown_done = false;
static uint64_t g_session_start_ns = 0;
static uint64_t g_session_end_ns = 0;
static char g_trace_json_path[PATH_MAX] = "trace.json";
static int g_main_pid = 0;
static uint32_t g_main_tid = 0;

// ---------------------------------------------------------------------------
// Stats Tracking Implementation
// ---------------------------------------------------------------------------

static void stats_record_duration(const char *category, const char *name, uint64_t dur_ns) {
    if (!name) return;

    // Fast lock-free lookup
    size_t count = atomic_load_explicit(&g_stat_entry_count, memory_order_acquire);
    stat_entry_t *entry = nullptr;
    for (size_t i = 0; i < count; i++) {
        if (g_stat_entries[i].name == name || strcmp(g_stat_entries[i].name, name) == 0) {
            entry = &g_stat_entries[i];
            break;
        }
    }

    if (!entry) {
        pthread_mutex_lock(&g_stat_mutex);
        count = atomic_load_explicit(&g_stat_entry_count, memory_order_relaxed);
        for (size_t i = 0; i < count; i++) {
            if (g_stat_entries[i].name == name || strcmp(g_stat_entries[i].name, name) == 0) {
                entry = &g_stat_entries[i];
                break;
            }
        }
        if (!entry && count < MAX_STAT_ENTRIES) {
            entry = &g_stat_entries[count];
            entry->category = category ? category : "general";
            entry->name = name;
            atomic_init(&entry->call_count, 0);
            atomic_init(&entry->total_ns, 0);
            atomic_init(&entry->min_ns, UINT64_MAX);
            atomic_init(&entry->max_ns, 0);
            atomic_store_explicit(&g_stat_entry_count, count + 1, memory_order_release);
        }
        pthread_mutex_unlock(&g_stat_mutex);
    }
    if (!entry) return;

    atomic_fetch_add_explicit(&entry->call_count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&entry->total_ns, dur_ns, memory_order_relaxed);

    uint64_t cur_min = atomic_load_explicit(&entry->min_ns, memory_order_relaxed);
    while (dur_ns < cur_min && !atomic_compare_exchange_weak_explicit(&entry->min_ns, &cur_min, dur_ns, memory_order_relaxed, memory_order_relaxed)) {}

    uint64_t cur_max = atomic_load_explicit(&entry->max_ns, memory_order_relaxed);
    while (dur_ns > cur_max && !atomic_compare_exchange_weak_explicit(&entry->max_ns, &cur_max, dur_ns, memory_order_relaxed, memory_order_relaxed)) {}
}

// ---------------------------------------------------------------------------
// Ring Buffer Storage Helper
// ---------------------------------------------------------------------------

static inline void ring_buffer_push(char phase, const char *cat, const char *name, uint64_t ts_us, uint64_t dur_us, const char *arg_name, int64_t arg_val) {
    size_t idx = atomic_fetch_add_explicit(&g_event_index, 1, memory_order_relaxed);
    size_t slot = idx % RING_BUFFER_SIZE;
    trace_event_t *ev = &g_ring_buffer[slot];
    ev->phase = phase;
    ev->category = cat ? cat : "default";
    ev->name = name ? name : "unnamed";
    ev->ts_us = ts_us;
    ev->dur_us = dur_us;
    ev->arg_name = arg_name;
    ev->arg_val = arg_val;
    ev->pid = g_main_pid ? g_main_pid : (int)getpid();
    ev->tid = (uint32_t)gettid();
}

// ---------------------------------------------------------------------------
// Profiler Public Core APIs
// ---------------------------------------------------------------------------

void profile_init(bool enabled, const char *trace_json_path) {
    if (!enabled) {
        const char *env = getenv("PDFCLI_PROFILE");
        if (env && (strcmp(env, "1") == 0 || strcasecmp(env, "true") == 0)) {
            enabled = true;
        }
    }

    if (!enabled) {
        atomic_store_explicit(&g_profile_enabled, false, memory_order_release);
        return;
    }

    if (trace_json_path && *trace_json_path) {
        snprintf(g_trace_json_path, sizeof(g_trace_json_path), "%s", trace_json_path);
    } else {
        snprintf(g_trace_json_path, sizeof(g_trace_json_path), "trace.json");
    }

    g_main_pid = (int)getpid();
    g_main_tid = (uint32_t)gettid();
    g_session_start_ns = profile_get_time_ns();
    atomic_store_explicit(&g_profile_shutdown_done, false, memory_order_release);
    atomic_store_explicit(&g_profile_enabled, true, memory_order_release);
}

[[nodiscard]] bool profile_is_enabled(void) {
    return atomic_load_explicit(&g_profile_enabled, memory_order_relaxed);
}

void profile_scope_record(const char *category, const char *name, uint64_t start_ns, uint64_t end_ns) {
    if (!profile_is_enabled()) return;
    uint64_t dur_ns = (end_ns >= start_ns) ? (end_ns - start_ns) : 0;
    stats_record_duration(category, name, dur_ns);
    ring_buffer_push('X', category, name, start_ns / 1000ULL, dur_ns / 1000ULL, nullptr, 0);
}

void profile_start(const char *category, const char *name) {
    if (!profile_is_enabled()) return;
    uint64_t now_ns = profile_get_time_ns();
    ring_buffer_push('B', category, name, now_ns / 1000ULL, 0, nullptr, 0);

    if (tl_scope_depth < MAX_THREAD_STACK_DEPTH) {
        tl_scope_stack[tl_scope_depth++] = (thread_active_scope_t){
            .name = name,
            .category = category,
            .start_ns = now_ns
        };
    }
}

void profile_end(const char *category, const char *name) {
    if (!profile_is_enabled()) return;
    uint64_t now_ns = profile_get_time_ns();
    ring_buffer_push('E', category, name, now_ns / 1000ULL, 0, nullptr, 0);

    for (size_t i = tl_scope_depth; i > 0; i--) {
        if (tl_scope_stack[i - 1].name == name || strcmp(tl_scope_stack[i - 1].name, name) == 0) {
            uint64_t start_ns = tl_scope_stack[i - 1].start_ns;
            uint64_t dur_ns = (now_ns >= start_ns) ? (now_ns - start_ns) : 0;
            stats_record_duration(category ? category : tl_scope_stack[i - 1].category, name, dur_ns);
            for (size_t j = i - 1; j + 1 < tl_scope_depth; j++) {
                tl_scope_stack[j] = tl_scope_stack[j + 1];
            }
            tl_scope_depth--;
            break;
        }
    }
}

void profile_event(const char *category, const char *name, const char *arg_name, int64_t arg_val) {
    if (!profile_is_enabled()) return;
    uint64_t now_ns = profile_get_time_ns();
    ring_buffer_push('i', category, name, now_ns / 1000ULL, 0, arg_name, arg_val);
}

void profile_record_frame(void) {
    if (!profile_is_enabled()) return;
    atomic_fetch_add_explicit(&g_frame_count, 1, memory_order_relaxed);
}

void profile_record_cache(size_t used_bytes, size_t max_bytes, size_t item_count) {
    atomic_store_explicit(&g_cache_used_bytes, used_bytes, memory_order_relaxed);
    atomic_store_explicit(&g_cache_max_bytes, max_bytes, memory_order_relaxed);
    atomic_store_explicit(&g_cache_item_count, item_count, memory_order_relaxed);

    size_t peak = atomic_load_explicit(&g_cache_peak_used_bytes, memory_order_relaxed);
    while (used_bytes > peak && !atomic_compare_exchange_weak_explicit(&g_cache_peak_used_bytes, &peak, used_bytes, memory_order_relaxed, memory_order_relaxed)) {}

    size_t peak_items = atomic_load_explicit(&g_cache_peak_items, memory_order_relaxed);
    while (item_count > peak_items && !atomic_compare_exchange_weak_explicit(&g_cache_peak_items, &peak_items, item_count, memory_order_relaxed, memory_order_relaxed)) {}

    if (profile_is_enabled()) {
        profile_event("cache", "cache_memory", "used_bytes", (int64_t)used_bytes);
    }
}

// ---------------------------------------------------------------------------
// MuPDF Fitz Allocator Implementation
// ---------------------------------------------------------------------------

static void *fitz_malloc(void *user, size_t size) {
    (void)user;
    if (size == 0) return nullptr;
    fitz_mem_header_t *hdr = (fitz_mem_header_t *)malloc(sizeof(fitz_mem_header_t) + size);
    if (!hdr) return nullptr;
    hdr->size = size;
    hdr->tag = 0;
    hdr->magic = FITZ_MEM_MAGIC;

    atomic_fetch_add_explicit(&g_fitz_total_allocs, 1, memory_order_relaxed);
    size_t cur = atomic_fetch_add_explicit(&g_fitz_current_bytes, size, memory_order_relaxed) + size;
    size_t peak = atomic_load_explicit(&g_fitz_peak_bytes, memory_order_relaxed);
    while (cur > peak && !atomic_compare_exchange_weak_explicit(&g_fitz_peak_bytes, &peak, cur, memory_order_relaxed, memory_order_relaxed)) {}

    return (void *)(hdr + 1);
}

static void fitz_free(void *user, void *old) {
    (void)user;
    if (!old) return;
    fitz_mem_header_t *hdr = ((fitz_mem_header_t *)old) - 1;
    if (hdr->magic != FITZ_MEM_MAGIC) {
        free(old);
        return;
    }
    size_t size = hdr->size;
    hdr->magic = 0;
    atomic_fetch_add_explicit(&g_fitz_total_frees, 1, memory_order_relaxed);
    atomic_fetch_sub_explicit(&g_fitz_current_bytes, size, memory_order_relaxed);
    free(hdr);
}

static void *fitz_realloc(void *user, void *old, size_t size) {
    (void)user;
    if (!old) return fitz_malloc(user, size);
    if (size == 0) {
        fitz_free(user, old);
        return nullptr;
    }
    fitz_mem_header_t *hdr = ((fitz_mem_header_t *)old) - 1;
    if (hdr->magic != FITZ_MEM_MAGIC) {
        return realloc(old, size);
    }
    size_t old_size = hdr->size;
    fitz_mem_header_t *new_hdr = (fitz_mem_header_t *)realloc(hdr, sizeof(fitz_mem_header_t) + size);
    if (!new_hdr) return nullptr;
    new_hdr->size = size;
    new_hdr->magic = FITZ_MEM_MAGIC;

    if (size > old_size) {
        size_t diff = size - old_size;
        size_t cur = atomic_fetch_add_explicit(&g_fitz_current_bytes, diff, memory_order_relaxed) + diff;
        size_t peak = atomic_load_explicit(&g_fitz_peak_bytes, memory_order_relaxed);
        while (cur > peak && !atomic_compare_exchange_weak_explicit(&g_fitz_peak_bytes, &peak, cur, memory_order_relaxed, memory_order_relaxed)) {}
    } else {
        size_t diff = old_size - size;
        atomic_fetch_sub_explicit(&g_fitz_current_bytes, diff, memory_order_relaxed);
    }
    return (void *)(new_hdr + 1);
}

static const fz_alloc_context g_fitz_alloc_context = {
    .user = nullptr,
    .malloc = fitz_malloc,
    .realloc = fitz_realloc,
    .free = fitz_free
};

[[nodiscard]] const fz_alloc_context *profile_get_fitz_alloc(void) {
    return &g_fitz_alloc_context;
}

// ---------------------------------------------------------------------------
// Kernel ProcFS Memory Reader Implementation
// ---------------------------------------------------------------------------

bool profile_get_proc_memory(size_t *rss_bytes, size_t *hwm_bytes, size_t *virt_bytes) {
    if (rss_bytes) *rss_bytes = 0;
    if (hwm_bytes) *hwm_bytes = 0;
    if (virt_bytes) *virt_bytes = 0;

    static long page_size = 0;
    if (page_size <= 0) {
        page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0) page_size = 4096;
    }

    FILE *f_statm = fopen("/proc/self/statm", "r");
    if (f_statm) {
        unsigned long size_pg = 0, res_pg = 0;
        if (fscanf(f_statm, "%lu %lu", &size_pg, &res_pg) == 2) {
            if (virt_bytes) *virt_bytes = (size_t)size_pg * (size_t)page_size;
            if (rss_bytes) *rss_bytes = (size_t)res_pg * (size_t)page_size;
        }
        fclose(f_statm);
    }

    FILE *f_status = fopen("/proc/self/status", "r");
    if (f_status) {
        char line[256];
        while (fgets(line, sizeof(line), f_status)) {
            if (strncmp(line, "VmHWM:", 6) == 0) {
                unsigned long val_kb = 0;
                if (sscanf(line + 6, "%lu", &val_kb) == 1) {
                    if (hwm_bytes) *hwm_bytes = (size_t)val_kb * 1024ULL;
                    break;
                }
            }
        }
        fclose(f_status);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Chrome / Perfetto Trace Event JSON Serializer
// ---------------------------------------------------------------------------

static void flush_trace_json(const char *path) {
    if (!path || !*path) path = "trace.json";
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "[profile] Failed to open trace output file: %s\n", path);
        return;
    }

    char stream_buf[65536];
    setvbuf(f, stream_buf, _IOFBF, sizeof(stream_buf));

    fprintf(f, "{\n  \"traceEvents\": [\n");

    int pid = g_main_pid ? g_main_pid : (int)getpid();
    uint32_t tid = g_main_tid ? g_main_tid : (uint32_t)gettid();

    // Process & Thread metadata
    fprintf(f, "    {\"name\": \"process_name\", \"ph\": \"M\", \"pid\": %d, \"tid\": %u, \"args\": {\"name\": \"pdf-cli\"}},\n", pid, tid);
    fprintf(f, "    {\"name\": \"thread_name\", \"ph\": \"M\", \"pid\": %d, \"tid\": %u, \"args\": {\"name\": \"Main Thread\"}}", pid, tid);

    size_t total_events = atomic_load_explicit(&g_event_index, memory_order_relaxed);
    size_t start_idx = 0;
    size_t count = total_events;
    if (total_events > RING_BUFFER_SIZE) {
        start_idx = total_events % RING_BUFFER_SIZE;
        count = RING_BUFFER_SIZE;
    }

    for (size_t i = 0; i < count; i++) {
        size_t slot = (start_idx + i) % RING_BUFFER_SIZE;
        const trace_event_t *ev = &g_ring_buffer[slot];

        fprintf(f, ",\n    {");
        fprintf(f, "\"name\": \"%s\", \"cat\": \"%s\", \"ph\": \"%c\", \"ts\": %lu, \"pid\": %d, \"tid\": %u",
                ev->name ? ev->name : "",
                ev->category ? ev->category : "",
                ev->phase,
                (unsigned long)ev->ts_us,
                ev->pid,
                ev->tid);

        if (ev->phase == 'X') {
            fprintf(f, ", \"dur\": %lu", (unsigned long)ev->dur_us);
        } else if (ev->phase == 'i') {
            fprintf(f, ", \"s\": \"t\"");
        }

        if (ev->arg_name) {
            fprintf(f, ", \"args\": {\"%s\": %ld}", ev->arg_name, (long)ev->arg_val);
        }
        fprintf(f, "}");
    }

    fprintf(f, "\n  ],\n  \"displayTimeUnit\": \"ms\"\n}\n");
    fclose(f);
}

// ---------------------------------------------------------------------------
// Pretty-Printing Terminal Summary Helpers
// ---------------------------------------------------------------------------

static void format_time_unit(uint64_t ns, char *out, size_t sz) {
    if (ns >= 1000000000ULL) {
        snprintf(out, sz, "%7.2f s ", (double)ns / 1e9);
    } else if (ns >= 1000000ULL) {
        snprintf(out, sz, "%7.2f ms", (double)ns / 1e6);
    } else if (ns >= 1000ULL) {
        snprintf(out, sz, "%7.2f us", (double)ns / 1e3);
    } else {
        snprintf(out, sz, "%7lu ns", (unsigned long)ns);
    }
}

static void format_bytes_unit(size_t bytes, char *out, size_t sz) {
    if (bytes >= (size_t)1024 * 1024 * 1024) {
        snprintf(out, sz, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= (size_t)1024 * 1024) {
        snprintf(out, sz, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(out, sz, "%.2f KB", (double)bytes / 1024.0);
    } else {
        snprintf(out, sz, "%zu B", bytes);
    }
}

static int compare_stat_entries_desc(const void *a, const void *b) {
    const stat_entry_t *sa = (const stat_entry_t *)a;
    const stat_entry_t *sb = (const stat_entry_t *)b;
    uint64_t t_a = atomic_load_explicit(&sa->total_ns, memory_order_relaxed);
    uint64_t t_b = atomic_load_explicit(&sb->total_ns, memory_order_relaxed);
    if (t_b > t_a) return 1;
    if (t_b < t_a) return -1;
    return 0;
}

// ---------------------------------------------------------------------------
// Terminal Summary Report
// ---------------------------------------------------------------------------

void profile_print_summary(FILE *out) {
    if (!out) out = stderr;

    uint64_t session_ns = (g_session_end_ns >= g_session_start_ns && g_session_start_ns > 0)
                          ? (g_session_end_ns - g_session_start_ns)
                          : (profile_get_time_ns() - g_session_start_ns);
    double session_sec = (double)session_ns / 1e9;
    size_t frames = atomic_load_explicit(&g_frame_count, memory_order_relaxed);
    double fps = (session_sec > 0.0) ? ((double)frames / session_sec) : 0.0;
    size_t total_events = atomic_load_explicit(&g_event_index, memory_order_relaxed);

    size_t rss_bytes = 0, hwm_bytes = 0, virt_bytes = 0;
    profile_get_proc_memory(&rss_bytes, &hwm_bytes, &virt_bytes);

    char str_rss[32], str_hwm[32], str_virt[32];
    format_bytes_unit(rss_bytes, str_rss, sizeof(str_rss));
    format_bytes_unit(hwm_bytes, str_hwm, sizeof(str_hwm));
    format_bytes_unit(virt_bytes, str_virt, sizeof(str_virt));

    size_t fitz_cur = atomic_load_explicit(&g_fitz_current_bytes, memory_order_relaxed);
    size_t fitz_peak = atomic_load_explicit(&g_fitz_peak_bytes, memory_order_relaxed);
    size_t fitz_allocs = atomic_load_explicit(&g_fitz_total_allocs, memory_order_relaxed);
    size_t fitz_frees = atomic_load_explicit(&g_fitz_total_frees, memory_order_relaxed);
    char str_fitz_cur[32], str_fitz_peak[32];
    format_bytes_unit(fitz_cur, str_fitz_cur, sizeof(str_fitz_cur));
    format_bytes_unit(fitz_peak, str_fitz_peak, sizeof(str_fitz_peak));

    size_t cache_cur = atomic_load_explicit(&g_cache_used_bytes, memory_order_relaxed);
    size_t cache_max = atomic_load_explicit(&g_cache_max_bytes, memory_order_relaxed);
    size_t cache_peak = atomic_load_explicit(&g_cache_peak_used_bytes, memory_order_relaxed);
    size_t cache_items = atomic_load_explicit(&g_cache_item_count, memory_order_relaxed);
    size_t cache_peak_items = atomic_load_explicit(&g_cache_peak_items, memory_order_relaxed);
    char str_cache_cur[32], str_cache_max[32], str_cache_peak[32];
    format_bytes_unit(cache_cur, str_cache_cur, sizeof(str_cache_cur));
    format_bytes_unit(cache_max, str_cache_max, sizeof(str_cache_max));
    format_bytes_unit(cache_peak, str_cache_peak, sizeof(str_cache_peak));

    fprintf(out, "\n========================================================================================================\n");
    fprintf(out, "                                PDF-CLI PROFILING & PERFORMANCE SUMMARY                                 \n");
    fprintf(out, "========================================================================================================\n");
    fprintf(out, "  Session Duration : %.3f s  |  Frames Rendered: %zu  |  Average Frame Rate: %.2f FPS\n",
            session_sec, frames, fps);
    fprintf(out, "  Process PID      : %d       |  Events Logged  : %zu (Ring Buffer Capacity: %d)\n",
            g_main_pid ? g_main_pid : (int)getpid(), total_events, RING_BUFFER_SIZE);
    fprintf(out, "--------------------------------------------------------------------------------------------------------\n");
    fprintf(out, "MEMORY BREAKDOWN:\n");
    fprintf(out, "  • Kernel Resident Memory (VmRSS) : %s  (Peak HWM: %s, Virtual Size: %s)\n", str_rss, str_hwm, str_virt);
    fprintf(out, "  • MuPDF Fitz Heap Memory (Peak)  : %s  (Current: %s, Allocs: %zu, Frees: %zu)\n",
            str_fitz_peak, str_fitz_cur, fitz_allocs, fitz_frees);
    fprintf(out, "  • Pixmap LRU Cache Memory (Peak) : %s  (Current: %s / %s, Peak Items: %zu, Current: %zu)\n",
            str_cache_peak, str_cache_cur, str_cache_max, cache_peak_items, cache_items);
    fprintf(out, "--------------------------------------------------------------------------------------------------------\n");
    fprintf(out, "OPERATIONS BY LATENCY (Sorted from SLOWEST to Fastest by Total Duration):\n");
    fprintf(out, "--------------------------------------------------------------------------------------------------------\n");
    fprintf(out, " %-12s | %-32s | %8s | %11s | %11s | %11s\n",
            "CATEGORY", "OPERATION", "CALLS", "TOTAL TIME", "AVG TIME", "MAX TIME");
    fprintf(out, "--------------+----------------------------------+----------+-------------+-------------+-------------\n");

    size_t count = atomic_load_explicit(&g_stat_entry_count, memory_order_relaxed);
    if (count > 0) {
        stat_entry_t sorted_stats[MAX_STAT_ENTRIES];
        memcpy(sorted_stats, g_stat_entries, count * sizeof(stat_entry_t));
        qsort(sorted_stats, count, sizeof(stat_entry_t), compare_stat_entries_desc);

        for (size_t i = 0; i < count; i++) {
            uint64_t c = atomic_load_explicit(&sorted_stats[i].call_count, memory_order_relaxed);
            uint64_t t = atomic_load_explicit(&sorted_stats[i].total_ns, memory_order_relaxed);
            uint64_t mx = atomic_load_explicit(&sorted_stats[i].max_ns, memory_order_relaxed);
            uint64_t avg = (c > 0) ? (t / c) : 0;

            char s_tot[32], s_avg[32], s_max[32];
            format_time_unit(t, s_tot, sizeof(s_tot));
            format_time_unit(avg, s_avg, sizeof(s_avg));
            format_time_unit(mx, s_max, sizeof(s_max));

            fprintf(out, " %-12.12s | %-32.32s | %8lu | %11s | %11s | %11s\n",
                    sorted_stats[i].category ? sorted_stats[i].category : "general",
                    sorted_stats[i].name ? sorted_stats[i].name : "unknown",
                    (unsigned long)c, s_tot, s_avg, s_max);
        }
    } else {
        fprintf(out, "  (No scoped operations recorded)\n");
    }

    fprintf(out, "========================================================================================================\n");
    fprintf(out, "  Trace JSON written to : %s\n", g_trace_json_path);
    fprintf(out, "  Interactive UI        : Drop file into https://ui.perfetto.dev/\n");
    fprintf(out, "========================================================================================================\n\n");
}

// ---------------------------------------------------------------------------
// Profiler Shutdown
// ---------------------------------------------------------------------------

void profile_shutdown(void) {
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(&g_profile_shutdown_done, &expected, true, memory_order_acq_rel, memory_order_relaxed)) {
        return; // Idempotent: already shut down
    }

    if (!profile_is_enabled()) return;

    g_session_end_ns = profile_get_time_ns();
    atomic_store_explicit(&g_profile_enabled, false, memory_order_release);

    flush_trace_json(g_trace_json_path);
    profile_print_summary(stderr);
}
