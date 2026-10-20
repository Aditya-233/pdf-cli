#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <math.h>

constexpr size_t FUZZY_MAX_LEN = 1024;
constexpr double FUZZY_SCORE_MIN = -INFINITY;
constexpr double FUZZY_SCORE_MAX = INFINITY;

// Returns true if all characters in needle appear in haystack in order (case-insensitive).
[[nodiscard]] bool fuzzy_has_match(const char *needle, const char *haystack);

// Computes the Gotoh dynamic programming alignment score between needle and haystack.
// Returns FUZZY_SCORE_MIN if needle cannot match haystack.
[[nodiscard]] double fuzzy_score(const char *needle, const char *haystack);

// Computes the Gotoh score and populates 'positions' with the matching indices in haystack.
// At most 'max_pos' indices are written to 'positions'.
// Returns the computed score.
[[nodiscard]] double fuzzy_match_positions(const char *needle, const char *haystack, size_t *positions, size_t max_pos);
