#pragma once

#include <stdbool.h>

typedef struct file_watcher file_watcher;

[[nodiscard]] file_watcher *file_watcher_create(const char *file_path);
[[nodiscard]] int file_watcher_get_fd(const file_watcher *fw);
[[nodiscard]] bool file_watcher_check_modified(file_watcher *fw);
void file_watcher_destroy(file_watcher *fw);
