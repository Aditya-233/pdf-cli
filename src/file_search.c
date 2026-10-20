#define _DEFAULT_SOURCE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pdfcli/file_search.h"
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
    return (strcasecmp(dot, ".pdf") == 0 || strcasecmp(dot, ".epub") == 0 ||
            strcasecmp(dot, ".docx") == 0 || strcasecmp(dot, ".html") == 0 ||
            strcasecmp(dot, ".htm") == 0 || strcasecmp(dot, ".png") == 0 ||
            strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0);
}

static void file_list_append(file_list *l, const char *path) {
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

static void walk_dir(const char *base, file_list *l, int depth) {
    if (depth > 6 || l->count > 5000) return;
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        if (strcmp(ent->d_name, "node_modules") == 0 || strcmp(ent->d_name, "vendor") == 0) continue;
        char sub[1024];
        snprintf(sub, sizeof(sub), "%s/%s", base, ent->d_name);
        struct stat st;
        if (stat(sub, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            walk_dir(sub, l, depth + 1);
        } else if (S_ISREG(st.st_mode) && file_search_is_supported_ext(ent->d_name)) {
            file_list_append(l, sub);
        }
    }
    closedir(d);
}

[[nodiscard]] file_list *file_search_dir(const char *dir) {
    auto l = (file_list *)calloc(1, sizeof(file_list));
    if (!l) return nullptr;
    walk_dir(dir, l, 0);
    return l;
}

[[nodiscard]] file_list *file_search_broad(void) {
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
    free(list);
}
