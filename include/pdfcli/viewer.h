#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "types.h"

typedef struct doc_viewer doc_viewer;

[[nodiscard]] doc_viewer *viewer_create(const char *path);
void viewer_destroy(doc_viewer *v);

[[nodiscard]] bool viewer_open(doc_viewer *v);

// Runs the main interactive viewer loop.
// Returns true if the user requested going back to the file picker ('b'), false on quit ('q' or Ctrl+C).
[[nodiscard]] bool viewer_run(doc_viewer *v);
