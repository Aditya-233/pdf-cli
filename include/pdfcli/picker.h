#pragma once

#include "pdfcli/file_search.h"

// Runs the interactive file picker TUI with real-time fuzzy filtering.
// Arrow keys (Up/Down), Page Up/Down, Enter to open, Esc / Ctrl+C to cancel.
// Matches highlighted in cyan/yellow using fuzzy_match_positions.
// Returns a newly heap-allocated string of the selected file path (caller must free),
// or nullptr if cancelled.
[[nodiscard]] char *picker_run(file_list *files);
[[nodiscard]] char *picker_run_ex(file_list *files, bool *out_new_tab);
