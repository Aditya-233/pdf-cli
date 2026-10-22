#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "types.h"

#define MAX_TABS 10

typedef struct doc_viewer doc_viewer;

[[nodiscard]] doc_viewer *viewer_create(const char *path);
void viewer_destroy(doc_viewer *v);

[[nodiscard]] bool viewer_open(doc_viewer *v);

// Multi-document tab management
[[nodiscard]] bool viewer_add_tab(doc_viewer *v, const char *path);
void viewer_switch_tab(doc_viewer *v, size_t index);
void viewer_set_active_tab(doc_viewer *v, size_t index);
void viewer_close_tab(doc_viewer *v, size_t index);
void viewer_next_tab(doc_viewer *v);
void viewer_prev_tab(doc_viewer *v);
[[nodiscard]] size_t viewer_get_tab_count(const doc_viewer *v);
[[nodiscard]] size_t viewer_get_active_tab(const doc_viewer *v);
[[nodiscard]] bool viewer_is_tab_bar_visible(const doc_viewer *v);
void viewer_toggle_hud(doc_viewer *v);
void viewer_dismiss_hud(doc_viewer *v);

// Runs the main interactive viewer loop.
// Returns true if the user requested going back to the file picker ('b'), false on quit ('q' or Ctrl+C).
[[nodiscard]] bool viewer_run(doc_viewer *v);
