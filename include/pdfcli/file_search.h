#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct file_entry {
    char *path;
    char *filename;
    double score;
} file_entry;

typedef struct file_list {
    file_entry *entries;
    size_t count;
    size_t capacity;
    uint32_t *ht;
    size_t ht_capacity;
} file_list;

// Checks whether the file extension is supported:
// .pdf, .epub, .docx, .html, .htm, .png, .jpg, .jpeg
[[nodiscard]] bool file_search_is_supported_ext(const char *path);

// Recursively searches 'dir' for supported document files, skipping hidden
// directories (starting with '.') and common non-document dirs (node_modules, vendor).
[[nodiscard]] file_list *file_search_dir(const char *dir);

// Broad document discovery across standard user directories:
// ~/Documents, ~/Downloads, ~/Desktop, ~/Books, ~/Projects, ., /usr/share/doc, ~/.local/share/books.
// Automatically de-duplicates discovered files.
[[nodiscard]] file_list *file_search_broad(void);

// Frees all memory associated with a file_list.
void file_list_free(file_list *list);
