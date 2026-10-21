#define _DEFAULT_SOURCE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pdfcli/file_search.h"
#include "pdfcli/profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

[[nodiscard]] bool file_search_is_supported_ext(const char *path) {
    if (!path) return false;
    const char *dot = strrchr(path, '.');
    if (!dot) return false;

    char ext[8];
    size_t i = 0;
    for (; dot[i] && i < 6; i++) {
        char c = dot[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
        ext[i] = c;
    }
    if (dot[i] != '\0') return false;
    ext[i] = '\0';

    if (i == 4) {
        return (memcmp(ext, ".pdf", 4) == 0 ||
                memcmp(ext, ".htm", 4) == 0 ||
                memcmp(ext, ".png", 4) == 0 ||
                memcmp(ext, ".jpg", 4) == 0);
    } else if (i == 5) {
        return (memcmp(ext, ".epub", 5) == 0 ||
                memcmp(ext, ".docx", 5) == 0 ||
                memcmp(ext, ".html", 5) == 0 ||
                memcmp(ext, ".jpeg", 5) == 0);
    }
    return false;
}

static inline uint32_t hash_path(const char *str) {
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        hash ^= (uint32_t)*p;
        hash *= 16777619u;
    }
    return hash;
}

static void file_list_ensure_ht(file_list *l) {
    if (l->ht && l->count * 2 < l->ht_capacity) return;
    size_t new_cap = l->ht_capacity ? l->ht_capacity * 2 : 1024;
    uint32_t *new_ht = (uint32_t *)calloc(new_cap, sizeof(uint32_t));
    if (!new_ht) return;
    size_t mask = new_cap - 1;
    for (size_t i = 0; i < l->count; i++) {
        uint32_t h = hash_path(l->entries[i].path);
        size_t idx = (size_t)h & mask;
        while (new_ht[idx] != 0) {
            idx = (idx + 1) & mask;
        }
        new_ht[idx] = (uint32_t)(i + 1);
    }
    free(l->ht);
    l->ht = new_ht;
    l->ht_capacity = new_cap;
}

static void file_list_append(file_list *l, const char *path) {
    if (!l || !path) return;
    file_list_ensure_ht(l);
    if (l->ht && l->ht_capacity > 0) {
        uint32_t h = hash_path(path);
        size_t mask = l->ht_capacity - 1;
        size_t idx = (size_t)h & mask;
        while (l->ht[idx] != 0) {
            uint32_t entry_idx = l->ht[idx] - 1;
            if (strcmp(l->entries[entry_idx].path, path) == 0) return;
            idx = (idx + 1) & mask;
        }
        if (l->count >= l->capacity) {
            l->capacity = l->capacity ? l->capacity * 2 : 16;
            l->entries = (file_entry *)realloc(l->entries, l->capacity * sizeof(file_entry));
        }
        const char *slash = strrchr(path, '/');
        l->entries[l->count].path = strdup(path);
        l->entries[l->count].filename = strdup(slash ? slash + 1 : path);
        l->entries[l->count].score = 0.0;
        l->ht[idx] = (uint32_t)(l->count + 1);
        l->count++;
    } else {
        for (size_t i = 0; i < l->count; i++) {
            if (strcmp(l->entries[i].path, path) == 0) return;
        }
        if (l->count >= l->capacity) {
            l->capacity = l->capacity ? l->capacity * 2 : 16;
            l->entries = (file_entry *)realloc(l->entries, l->capacity * sizeof(file_entry));
        }
        const char *slash = strrchr(path, '/');
        l->entries[l->count].path = strdup(path);
        l->entries[l->count].filename = strdup(slash ? slash + 1 : path);
        l->entries[l->count].score = 0.0;
        l->count++;
    }
}

static void walk_dir(const char *base, file_list *l, int depth) {
    PROFILE_SCOPE("fs", "file_search_walk_dir");
    if (depth > 6 || l->count > 5000) return;
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        if (strcmp(ent->d_name, "node_modules") == 0 || strcmp(ent->d_name, "vendor") == 0) continue;
        if (l->count > 5000) break;

        bool is_dir = (ent->d_type == DT_DIR);
        bool is_reg = (ent->d_type == DT_REG);

        if (ent->d_type == DT_UNKNOWN || ent->d_type == DT_LNK) {
            char sub[1024];
            snprintf(sub, sizeof(sub), "%s/%s", base, ent->d_name);
            struct stat st;
            if (stat(sub, &st) != 0) continue;
            is_dir = S_ISDIR(st.st_mode);
            is_reg = S_ISREG(st.st_mode);
            if (is_dir) {
                walk_dir(sub, l, depth + 1);
            } else if (is_reg && file_search_is_supported_ext(ent->d_name)) {
                file_list_append(l, sub);
            }
            continue;
        }

        if (is_dir) {
            char sub[1024];
            snprintf(sub, sizeof(sub), "%s/%s", base, ent->d_name);
            walk_dir(sub, l, depth + 1);
        } else if (is_reg) {
            if (file_search_is_supported_ext(ent->d_name)) {
                char sub[1024];
                snprintf(sub, sizeof(sub), "%s/%s", base, ent->d_name);
                file_list_append(l, sub);
            }
        }
    }
    closedir(d);
}

[[nodiscard]] file_list *file_search_dir(const char *dir) {
    PROFILE_SCOPE("fs", "file_search_dir");
    auto l = (file_list *)calloc(1, sizeof(file_list));
    if (!l) return nullptr;
    walk_dir(dir, l, 0);
    return l;
}

[[nodiscard]] file_list *file_search_broad(void) {
    PROFILE_SCOPE("fs", "file_search_broad");
    auto l = (file_list *)calloc(1, sizeof(file_list));
    if (!l) return nullptr;
    const char *home = getenv("HOME");
    char p[1024];
    if (home) {
        snprintf(p, sizeof(p), "%s/Documents", home); walk_dir(p, l, 0);
        snprintf(p, sizeof(p), "%s/Downloads", home); walk_dir(p, l, 0);
        snprintf(p, sizeof(p), "%s/Desktop", home); walk_dir(p, l, 0);
        snprintf(p, sizeof(p), "%s/Books", home); walk_dir(p, l, 0);
    }
    walk_dir(".", l, 0);
    return l;
}

void file_list_free(file_list *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->entries[i].path);
        free(list->entries[i].filename);
    }
    free(list->entries);
    free(list->ht);
    free(list);
}
