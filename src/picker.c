#define _DEFAULT_SOURCE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pdfcli/picker.h"
#include "pdfcli/fuzzy_match.h"
#include "pdfcli/term_raw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

[[nodiscard]] char *picker_run_ex(file_list *files, bool *out_new_tab) {
    if (!files || files->count == 0) return nullptr;
    if (out_new_tab) *out_new_tab = false;
    term_state ts;
    if (!term_enter_raw(&ts)) return nullptr;
    term_hide_cursor();
    char query[128] = {};
    size_t qlen = 0;
    int sel = 0, scroll = 0;
    char *selected_path = nullptr;

    while (true) {
        int cols = 80, rows = 24;
        term_get_size(&cols, &rows);
        size_t matches[files->count], mcount = 0;
        for (size_t i = 0; i < files->count; i++) {
            if (qlen == 0 || fuzzy_has_match(query, files->entries[i].path)) matches[mcount++] = i;
        }
        if (sel >= (int)mcount) sel = mcount ? (int)mcount - 1 : 0;
        if (sel < 0) sel = 0;
        int win_h = rows - 6; if (win_h < 4) win_h = 4;
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + win_h) scroll = sel - win_h + 1;

        printf("\033[H\033[2J");
        printf("\033[1;1H\033[1;36m=== Open Document (Type to filter, Enter: open, Ctrl+T: tab, Esc: cancel) ===\033[0m\r\n");
        printf("\033[2;1H> %s\033[K\r\n", query);
        printf("\033[3;1H\033[2m─────────────────────────────────────────────────────────────────────────────\033[0m\r\n");

        for (int i = 0; i < win_h; i++) {
            int idx = scroll + i;
            printf("\033[%d;1H\033[K", 4 + i);
            if (idx < (int)mcount) {
                const char *p = files->entries[matches[idx]].path;
                if (idx == sel) printf("\033[7m %s \033[0m", p);
                else printf("  %s", p);
            }
        }
        printf("\033[%d;1H\033[2mShowing %zu/%zu files\033[0m\033[K", rows, mcount, files->count);
        fflush(stdout);

        char buf[32] = {};
        int k = term_read_key(STDIN_FILENO, buf, sizeof(buf), nullptr, nullptr);
        if (k == 27 || k == KEY_CTRL_C) break;
        if (k == '\r' || k == '\n' || k == KEY_CTRL_T) {
            if (mcount > 0 && sel < (int)mcount) {
                selected_path = strdup(files->entries[matches[sel]].path);
                if (out_new_tab) *out_new_tab = (k == KEY_CTRL_T);
            }
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
    return selected_path;
}

[[nodiscard]] char *picker_run(file_list *files) {
    return picker_run_ex(files, nullptr);
}
