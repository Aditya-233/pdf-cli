#include "pdfcli/fuzzy_match.h"
#include "pdfcli/profile.h"
#include <string.h>
#include <stdlib.h>

constexpr double SCORE_GAP_LEADING = -0.005;
constexpr double SCORE_GAP_TRAILING = -0.005;
constexpr double SCORE_GAP_INNER = -0.01;
constexpr double SCORE_MATCH_CONSECUTIVE = 1.0;
constexpr double SCORE_MATCH_SLASH = 0.9;
constexpr double SCORE_MATCH_WORD = 0.8;
constexpr double SCORE_MATCH_CAPITAL = 0.7;
constexpr double SCORE_MATCH_DOT = 0.6;

// Branchless character classification tables (standard ISO C designated initializers)
static const unsigned char ch_class_table[256] = {
    ['0'] = 1, ['1'] = 1, ['2'] = 1, ['3'] = 1, ['4'] = 1,
    ['5'] = 1, ['6'] = 1, ['7'] = 1, ['8'] = 1, ['9'] = 1,
    ['a'] = 1, ['b'] = 1, ['c'] = 1, ['d'] = 1, ['e'] = 1, ['f'] = 1, ['g'] = 1,
    ['h'] = 1, ['i'] = 1, ['j'] = 1, ['k'] = 1, ['l'] = 1, ['m'] = 1, ['n'] = 1,
    ['o'] = 1, ['p'] = 1, ['q'] = 1, ['r'] = 1, ['s'] = 1, ['t'] = 1, ['u'] = 1,
    ['v'] = 1, ['w'] = 1, ['x'] = 1, ['y'] = 1, ['z'] = 1,
    ['A'] = 2, ['B'] = 2, ['C'] = 2, ['D'] = 2, ['E'] = 2, ['F'] = 2, ['G'] = 2,
    ['H'] = 2, ['I'] = 2, ['J'] = 2, ['K'] = 2, ['L'] = 2, ['M'] = 2, ['N'] = 2,
    ['O'] = 2, ['P'] = 2, ['Q'] = 2, ['R'] = 2, ['S'] = 2, ['T'] = 2, ['U'] = 2,
    ['V'] = 2, ['W'] = 2, ['X'] = 2, ['Y'] = 2, ['Z'] = 2,
};

static const unsigned char last_class_table[256] = {
    ['/'] = 1,
    ['-'] = 2, ['_'] = 2, [' '] = 2,
    ['.'] = 3,
    ['a'] = 4, ['b'] = 4, ['c'] = 4, ['d'] = 4, ['e'] = 4, ['f'] = 4, ['g'] = 4,
    ['h'] = 4, ['i'] = 4, ['j'] = 4, ['k'] = 4, ['l'] = 4, ['m'] = 4, ['n'] = 4,
    ['o'] = 4, ['p'] = 4, ['q'] = 4, ['r'] = 4, ['s'] = 4, ['t'] = 4, ['u'] = 4,
    ['v'] = 4, ['w'] = 4, ['x'] = 4, ['y'] = 4, ['z'] = 4,
};

static const double bonus_matrix[3][5] = {
    [0] = { 0.0, 0.0, 0.0, 0.0, 0.0 },
    [1] = { 0.0, SCORE_MATCH_SLASH, SCORE_MATCH_WORD, SCORE_MATCH_DOT, 0.0 },
    [2] = { 0.0, SCORE_MATCH_SLASH, SCORE_MATCH_WORD, SCORE_MATCH_DOT, SCORE_MATCH_CAPITAL },
};

static inline double compute_bonus(char last, char ch) {
    return bonus_matrix[ch_class_table[(unsigned char)ch]][last_class_table[(unsigned char)last]];
}

static inline unsigned char to_lower_fast(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32) : c;
}

// Thread-local scratch buffers: O(1) auxiliary space, eliminates per-call malloc/free
#define SCRATCH_DP_CAP 131072
static thread_local double dp_scratch[SCRATCH_DP_CAP];
static thread_local double score_D[2][FUZZY_MAX_LEN];
static thread_local double score_M[2][FUZZY_MAX_LEN];

[[nodiscard]] bool fuzzy_has_match(const char *needle, const char *haystack) {
    if (!needle || !haystack) return false;
    if (!*needle) return true;
    for (; *needle; needle++) {
        unsigned char nch = to_lower_fast((unsigned char)*needle);
        while (*haystack && to_lower_fast((unsigned char)*haystack) != nch) haystack++;
        if (!*haystack) return false;
        haystack++;
    }
    return true;
}

[[nodiscard]] double fuzzy_score(const char *needle, const char *haystack) {
    PROFILE_SCOPE("fuzzy", "fuzzy_score");
    if (!needle || !haystack) return FUZZY_SCORE_MIN;
    if (!*needle) return 0.0;

    size_t n = 0; while (needle[n]) n++;
    size_t m = 0; while (haystack[m]) m++;
    if (m > FUZZY_MAX_LEN || n > m) return FUZZY_SCORE_MIN;
    if (n == m) {
        size_t eq = 0;
        for (; eq < n; eq++) {
            if (to_lower_fast((unsigned char)needle[eq]) != to_lower_fast((unsigned char)haystack[eq])) break;
        }
        if (eq == n) return FUZZY_SCORE_MAX;
        return FUZZY_SCORE_MIN;
    }

    // Ukkonen diagonal banding cutoff: verify subsequence and bound leftmost valid indices
    size_t min_j[FUZZY_MAX_LEN];
    size_t h = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char nch = to_lower_fast((unsigned char)needle[i]);
        while (h < m && to_lower_fast((unsigned char)haystack[h]) != nch) h++;
        if (h == m || h > m - n + i) return FUZZY_SCORE_MIN;
        min_j[i] = h++;
    }

    char ln[FUZZY_MAX_LEN], lh[FUZZY_MAX_LEN];
    double bonus[FUZZY_MAX_LEN];
    char last_ch = '/';
    for (size_t i = 0; i < n; i++) ln[i] = (char)to_lower_fast((unsigned char)needle[i]);
    for (size_t j = 0; j < m; j++) {
        lh[j] = (char)to_lower_fast((unsigned char)haystack[j]);
        bonus[j] = compute_bonus(last_ch, haystack[j]);
        last_ch = haystack[j];
    }

    // Rolling 2-row DP in L1 cache (zero heap allocation)
    double prev_score = FUZZY_SCORE_MIN;
    double gap0 = (n == 1) ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;
    size_t j_start0 = min_j[0];
    for (size_t j = 0; j < j_start0; j++) {
        score_D[0][j] = FUZZY_SCORE_MIN;
        score_M[0][j] = FUZZY_SCORE_MIN;
    }
    for (size_t j = j_start0; j < m; j++) {
        if (ln[0] == lh[j]) {
            double score = (double)j * SCORE_GAP_LEADING + bonus[j];
            score_D[0][j] = score;
            score_M[0][j] = prev_score = (score > prev_score + gap0) ? score : prev_score + gap0;
        } else {
            score_D[0][j] = FUZZY_SCORE_MIN;
            score_M[0][j] = prev_score = prev_score + gap0;
        }
    }

    for (size_t i = 1; i < n; i++) {
        int curr = (int)(i & 1);
        int prev = 1 - curr;
        double gap = (i == n - 1) ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;
        prev_score = FUZZY_SCORE_MIN;
        size_t j_start = min_j[i];
        for (size_t j = 0; j < j_start; j++) {
            score_D[curr][j] = FUZZY_SCORE_MIN;
            score_M[curr][j] = FUZZY_SCORE_MIN;
        }
        double prev_M = score_M[prev][j_start - 1];
        double prev_D = score_D[prev][j_start - 1];
        for (size_t j = j_start; j < m; j++) {
            if (ln[i] == lh[j]) {
                double s1 = prev_M + bonus[j];
                double s2 = prev_D + SCORE_MATCH_CONSECUTIVE;
                double score = s1 > s2 ? s1 : s2;
                prev_D = score_D[prev][j];
                prev_M = score_M[prev][j];
                score_D[curr][j] = score;
                score_M[curr][j] = prev_score = (score > prev_score + gap) ? score : prev_score + gap;
            } else {
                prev_D = score_D[prev][j];
                prev_M = score_M[prev][j];
                score_D[curr][j] = FUZZY_SCORE_MIN;
                score_M[curr][j] = prev_score = prev_score + gap;
            }
        }
    }

    return score_M[(n - 1) & 1][m - 1];
}

[[nodiscard]] double fuzzy_match_positions(const char *needle, const char *haystack, size_t *pos, size_t max_pos) {
    PROFILE_SCOPE("fuzzy", "fuzzy_match_positions");
    if (!pos || max_pos == 0) return fuzzy_score(needle, haystack);
    if (!needle || !haystack) return FUZZY_SCORE_MIN;
    if (!*needle) return 0.0;

    size_t n = 0; while (needle[n]) n++;
    size_t m = 0; while (haystack[m]) m++;
    if (m > FUZZY_MAX_LEN || n > m) return FUZZY_SCORE_MIN;
    if (n == m) {
        size_t eq = 0;
        for (; eq < n; eq++) {
            if (to_lower_fast((unsigned char)needle[eq]) != to_lower_fast((unsigned char)haystack[eq])) break;
        }
        if (eq == n) {
            for (size_t i = 0; i < n && i < max_pos; i++) pos[i] = i;
            return FUZZY_SCORE_MAX;
        }
        return FUZZY_SCORE_MIN;
    }

    size_t min_j[FUZZY_MAX_LEN];
    size_t h = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char nch = to_lower_fast((unsigned char)needle[i]);
        while (h < m && to_lower_fast((unsigned char)haystack[h]) != nch) h++;
        if (h == m || h > m - n + i) return FUZZY_SCORE_MIN;
        min_j[i] = h++;
    }

    char ln[FUZZY_MAX_LEN], lh[FUZZY_MAX_LEN];
    double bonus[FUZZY_MAX_LEN];
    char last_ch = '/';
    for (size_t i = 0; i < n; i++) ln[i] = (char)to_lower_fast((unsigned char)needle[i]);
    for (size_t j = 0; j < m; j++) {
        lh[j] = (char)to_lower_fast((unsigned char)haystack[j]);
        bonus[j] = compute_bonus(last_ch, haystack[j]);
        last_ch = haystack[j];
    }

    size_t total_cells = n * m;
    double *D = nullptr;
    double *M = nullptr;
    bool allocated = false;
    if (total_cells * 2 <= SCRATCH_DP_CAP) {
        D = dp_scratch;
        M = dp_scratch + total_cells;
    } else {
        D = (double *)malloc(total_cells * sizeof(double));
        M = (double *)malloc(total_cells * sizeof(double));
        if (!D || !M) { free(D); free(M); return FUZZY_SCORE_MIN; }
        allocated = true;
    }

    double prev_score = FUZZY_SCORE_MIN;
    double gap0 = (n == 1) ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;
    size_t j_start0 = min_j[0];
    for (size_t j = 0; j < j_start0; j++) {
        D[j] = FUZZY_SCORE_MIN;
        M[j] = FUZZY_SCORE_MIN;
    }
    for (size_t j = j_start0; j < m; j++) {
        if (ln[0] == lh[j]) {
            double score = (double)j * SCORE_GAP_LEADING + bonus[j];
            D[j] = score;
            M[j] = prev_score = (score > prev_score + gap0) ? score : prev_score + gap0;
        } else {
            D[j] = FUZZY_SCORE_MIN;
            M[j] = prev_score = prev_score + gap0;
        }
    }

    for (size_t i = 1; i < n; i++) {
        double gap = (i == n - 1) ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;
        prev_score = FUZZY_SCORE_MIN;
        size_t j_start = min_j[i];
        size_t row_offset = i * m;
        size_t prev_row_offset = (i - 1) * m;
        for (size_t j = 0; j < j_start; j++) {
            D[row_offset + j] = FUZZY_SCORE_MIN;
            M[row_offset + j] = FUZZY_SCORE_MIN;
        }
        double prev_M = M[prev_row_offset + j_start - 1];
        double prev_D = D[prev_row_offset + j_start - 1];
        for (size_t j = j_start; j < m; j++) {
            size_t idx = row_offset + j;
            if (ln[i] == lh[j]) {
                double s1 = prev_M + bonus[j];
                double s2 = prev_D + SCORE_MATCH_CONSECUTIVE;
                double score = s1 > s2 ? s1 : s2;
                prev_D = D[prev_row_offset + j];
                prev_M = M[prev_row_offset + j];
                D[idx] = score;
                M[idx] = prev_score = (score > prev_score + gap) ? score : prev_score + gap;
            } else {
                prev_D = D[prev_row_offset + j];
                prev_M = M[prev_row_offset + j];
                D[idx] = FUZZY_SCORE_MIN;
                M[idx] = prev_score = prev_score + gap;
            }
        }
    }

    bool req = false;
    for (int i = (int)n - 1, j = (int)m - 1; i >= 0; i--) {
        for (; j >= 0; j--) {
            size_t idx = (size_t)i * m + (size_t)j;
            if (D[idx] != FUZZY_SCORE_MIN && (req || D[idx] == M[idx])) {
                req = (i > 0 && j > 0 && M[idx] == D[(size_t)(i - 1) * m + (size_t)(j - 1)] + SCORE_MATCH_CONSECUTIVE);
                if ((size_t)i < max_pos) pos[i] = (size_t)j;
                j--;
                break;
            }
        }
    }

    double result = M[n * m - 1];
    if (allocated) { free(D); free(M); }
    return result;
}
