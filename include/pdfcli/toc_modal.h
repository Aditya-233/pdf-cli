#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "pdfcli/doc_engine.h"

// Runs the Table of Contents modal dialog.
// Displays the document outline tree with indentation levels and page numbers.
// Provides real-time fuzzy filtering over section/chapter titles.
// 'current_page' is 0-indexed; the TOC entry covering 'current_page' is initially selected.
// Returns the selected 0-indexed destination page, or -1 if cancelled (Esc / Ctrl+C).
[[nodiscard]] int toc_modal_run(const doc_toc_entry *entries, size_t count, int current_page);
