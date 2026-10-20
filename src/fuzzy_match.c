#include "pdfcli/fuzzy_match.h"
#include <ctype.h>
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

[[nodiscard]] bool fuzzy_has_match(const char *needle, const char *haystack) {
    if (!needle || !haystack) return false;
    for (; *needle; needle++) {
        char nch = (char)tolower((unsigned char)*needle);
        while (*haystack && (char)tolower((unsigned char)*haystack) != nch) haystack++;
        if (!*haystack) return false;
        haystack++;
    }
    return true;
}

static inline double compute_bonus(char last, char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        if (last == '/') return SCORE_MATCH_SLASH;
        if (last == '-' || last == '_' || last == ' ') return SCORE_MATCH_WORD;
        if (last == '.') return SCORE_MATCH_DOT;
        if (last >= 'a' && last <= 'z') return SCORE_MATCH_CAPITAL;
    } else if (isalnum((unsigned char)ch)) {
        if (last == '/') return SCORE_MATCH_SLASH;
        if (last == '-' || last == '_' || last == ' ') return SCORE_MATCH_WORD;
        if (last == '.') return SCORE_MATCH_DOT;
    }
    return 0.0;
}

[[nodiscard]] double fuzzy_match_positions(const char *needle, const char *haystack, size_t *pos, size_t max_pos) {
    if (!needle || !haystack) return FUZZY_SCORE_MIN;
    if (!*needle) return 0.0;
    if (!fuzzy_has_match(needle, haystack)) return FUZZY_SCORE_MIN;
    size_t n = strlen(needle), m = strlen(haystack);
    if (m > FUZZY_MAX_LEN || n > m) return FUZZY_SCORE_MIN;
    if (n == m) {
        if (pos) for (size_t i = 0; i < n && i < max_pos; i++) pos[i] = i;
        return FUZZY_SCORE_MAX;
    }
    char ln[FUZZY_MAX_LEN], lh[FUZZY_MAX_LEN];
    double bonus[FUZZY_MAX_LEN], last_ch = '/';
    for (size_t i = 0; i < n; i++) ln[i] = (char)tolower((unsigned char)needle[i]);
    for (size_t j = 0; j < m; j++) {
        lh[j] = (char)tolower((unsigned char)haystack[j]);
        bonus[j] = compute_bonus((char)last_ch, haystack[j]);
        last_ch = haystack[j];
    }
    double *D = malloc(n * m * sizeof(double)), *M = malloc(n * m * sizeof(double));
    if (!D || !M) { free(D); free(M); return FUZZY_SCORE_MIN; }
    for (size_t i = 0; i < n; i++) {
        double prev_score = FUZZY_SCORE_MIN, gap = (i == n - 1) ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;
        double prev_M = FUZZY_SCORE_MIN, prev_D = FUZZY_SCORE_MIN;
        for (size_t j = 0; j < m; j++) {
            size_t idx = i * m + j;
            if (ln[i] == lh[j]) {
                double s1 = prev_M + bonus[j], s2 = prev_D + SCORE_MATCH_CONSECUTIVE;
                double score = (i == 0) ? ((double)j * SCORE_GAP_LEADING + bonus[j]) : (s1 > s2 ? s1 : s2);
                if (i > 0) { prev_D = D[(i - 1) * m + j]; prev_M = M[(i - 1) * m + j]; }
                D[idx] = score;
                M[idx] = prev_score = (score > prev_score + gap) ? score : prev_score + gap;
            } else {
                if (i > 0) { prev_D = D[(i - 1) * m + j]; prev_M = M[(i - 1) * m + j]; }
                D[idx] = FUZZY_SCORE_MIN;
                M[idx] = prev_score = prev_score + gap;
            }
        }
    }
    if (pos && max_pos > 0) {
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
    }
    double result = M[n * m - 1];
    free(D); free(M);
    return result;
}

[[nodiscard]] double fuzzy_score(const char *needle, const char *haystack) {
    return fuzzy_match_positions(needle, haystack, nullptr, 0);
}
