#pragma once

#include "pdfcli/types.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Decode a single UTF-8 codepoint from 'str'.
// Returns 0 on success, or -1 on invalid UTF-8 / null pointer.
// If successful, '*out_cp' receives the decoded Unicode codepoint,
// and '*out_len' receives the number of bytes consumed (1-4, or 0 at '\0').
[[nodiscard]] int utf8_decode(const char *str, uint32_t *out_cp, size_t *out_len);

// Return the visible terminal display width of Unicode codepoint 'cp'.
// Returns 0 for zero-width / combining characters,
// 1 for standard characters,
// 2 for fullwidth / CJK / wide emoji characters,
// -1 for non-printable control characters.
[[nodiscard]] int utf8_width(uint32_t cp);

// Calculate visible column display width of 'str', stripping ANSI escape sequences
// and properly accounting for wide CJK characters and zero-width combiners.
[[nodiscard]] int str_display_width(const char *str);

// Return plain character (rune) count of 'str', stripping ANSI escape sequences.
[[nodiscard]] int plain_length(const char *str);

// Center 'str' within a target column 'width', stripping ANSI sequences when
// calculating visible width. Returns a newly heap-allocated string that the
// caller must free(), or nullptr on allocation failure.
[[nodiscard]] char *center_text(const char *str, int width);
