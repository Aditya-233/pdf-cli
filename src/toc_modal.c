#include "pdfcli/toc_modal.h"
#include "pdfcli/fuzzy_match.h"
#include "pdfcli/term_raw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

[[nodiscard]] int toc_modal_run(const doc_toc_entry *entries, size_t count, int current_page) {
    if (!entries || count == 0) return -1;
    term_state ts;
    if (!term_enter_raw(&ts)) return -1;
    term_hide_cursor();
    char query[128] = {};
    size_t qlen = 0;
    int sel = 0, scroll = 0, chosen_page = -1;
    for (size_t i = 0; i < count; i++) {
        if (entries[i].page <= current_page) sel = (int)i;
    }

    while (true) {
        int cols = 80, rows = 24;
        term_get_size(&cols, &rows);
        size_t matches[count], mcount = 0;
        for (size_t i = 0; i < count; i++) {
            if (qlen == 0 || fuzzy_has_match(query, entries[i].title ? entries[i].title : "")) matches[mcount++] = i;
        }
        if (sel >= (int)mcount) sel = mcount ? (int)mcount - 1 : 0;
        if (sel < 0) sel = 0;
        int win_h = rows - 6; if (win_h < 4) win_h = 4;
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + win_h) scroll = sel - win_h + 1;

        printf("\033[H\033[2J");
        printf("\033[1;1H\033[1;36m=== Table of Contents (Type to filter, Up/Down, Enter to jump, Esc to cancel) ===\033[0m\r\n");
        printf("\033[2;1H> %s\033[K\r\n", query);
        printf("\033[3;1H\033[2m─────────────────────────────────────────────────────────────────────────────\033[0m\r\n");

        for (int i = 0; i < win_h; i++) {
            int idx = scroll + i;
            printf("\033[%d;1H\033[K", 4 + i);
            if (idx < (int)mcount) {
                const doc_toc_entry *e = &entries[matches[idx]];
                int indent = (e->level > 1) ? (e->level - 1) * 2 : 0;
                char line[256];
                snprintf(line, sizeof(line), "%*s%s  (p. %d)", indent, "", e->title ? e->title : "", e->page + 1);
                if (idx == sel) printf("\033[7m %s \033[0m", line);
                else printf("  %s", line);
            }
        }
        printf("\033[%d;1H\033[2mShowing %zu/%zu sections\033[0m\033[K", rows, mcount, count);
        fflush(stdout);

        char buf[32] = {};
        int k = term_read_key(STDIN_FILENO, buf, sizeof(buf), nullptr, nullptr);
        if (k == 27 || k == KEY_CTRL_C) break;
        if (k == '\r' || k == '\n') {
            if (mcount > 0 && sel < (int)mcount) chosen_page = entries[matches[sel]].page;
            break;
        }
        if (k == KEY_ARROW_UP || k == 'k') { if (sel > 0) sel--; }
        else if (k == KEY_ARROW_DOWN || k == 'j') { if (sel + 1 < (int)mcount) sel++; }
        else if (k == KEY_PAGE_UP) { sel -= win_h; if (sel < 0) sel = 0; }
        else if (k == KEY_PAGE_DOWN) { sel += win_h; if (sel >= (int)mcount) sel = mcount ? (int)mcount - 1 : 0; }
        else if (k == 127 || k == 8) { if (qlen > 0) { query[--qlen] = '\0'; sel = 0; scroll = 0; } }
        else if (k >= 32 && k < 127 && qlen + 1 < sizeof(query)) {
            query[qlen++] = (char)k; query[qlen] = '\0'; sel = 0; scroll = 0;
        }
    }
    term_restore(&ts);
    term_show_cursor();
    return chosen_page;
}
