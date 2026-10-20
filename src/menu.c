#define _DEFAULT_SOURCE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pdfcli/menu.h"
#include "pdfcli/term_raw.h"
#include "pdfcli/utf8_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

static void print_centered(const char *text, int width) {
    int len = str_display_width(text);
    int pad = (len < width) ? (width - len) / 2 : 0;
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s\r\n", text);
}

static bool prompt_for_dir(char *out_path, size_t out_sz) {
    int w = 80, h = 24;
    term_get_size(&w, &h);
    int row = h / 2 + 5;
    printf("\033[%d;1H\033[K", row);
    print_centered("\033[1;36mEnter directory path (ESC cancel • Enter confirm):\033[0m", w);
    char input[1024] = {};
    size_t in_len = 0;
    term_show_cursor();

    while (true) {
        printf("\033[%d;1H\033[K", row + 1);
        int pad = (w - 40) / 2; if (pad < 2) pad = 2;
        for (int i = 0; i < pad; i++) putchar(' ');
        printf("> %s", input);
        fflush(stdout);

        char buf[32] = {};
        int k = term_read_key(STDIN_FILENO, buf, sizeof(buf), nullptr, nullptr);
        if (k == 27 || k == KEY_CTRL_C) { term_hide_cursor(); return false; }
        if (k == '\r' || k == '\n') {
            term_hide_cursor();
            if (in_len == 0) return false;
            if (input[0] == '~' && (input[1] == '/' || input[1] == '\0')) {
                const char *home = getenv("HOME");
                snprintf(out_path, out_sz, "%s%s", home ? home : "", input + 1);
            } else {
                snprintf(out_path, out_sz, "%s", input);
            }
            return true;
        }
        if (k == 127 || k == 8) { if (in_len > 0) input[--in_len] = '\0'; }
        else if (k >= 32 && k < 127 && in_len + 1 < sizeof(input)) {
            input[in_len++] = (char)k; input[in_len] = '\0';
        }
    }
}

[[nodiscard]] menu_result menu_run(void) {
    term_state ts;
    if (!term_enter_raw(&ts)) return (menu_result){ .choice = MENU_QUIT };
    term_hide_cursor();
    int sel = 0;

    while (true) {
        int w = 80, h = 24;
        term_get_size(&w, &h);
        printf("\033[2J\033[H\r\n");
        int pad_top = (h - 16) / 2; if (pad_top < 0) pad_top = 0;
        for (int i = 0; i < pad_top; i++) printf("\r\n");

        print_centered("\033[1;36m██████╗ ██████╗ ███████╗      ██████╗██╗     ██╗\033[0m", w);
        print_centered("\033[1;36m██╔══██╗██╔══██╗██╔════╝     ██╔════╝██║     ██║\033[0m", w);
        print_centered("\033[1;36m██████╔╝██║  ██║█████╗ █████╗██║     ██║     ██║\033[0m", w);
        print_centered("\033[1;36m██╔═══╝ ██║  ██║██╔══╝ ╚════╝██║     ██║     ██║\033[0m", w);
        print_centered("\033[1;36m██║     ██████╔╝██║          ╚██████╗███████╗██║\033[0m", w);
        print_centered("\033[1;36m╚═╝     ╚═════╝ ╚═╝           ╚═════╝╚══════╝╚═╝\033[0m", w);
        printf("\r\n");
        print_centered("\033[2mTerminal Document Viewer (C23)\033[0m", w);
        print_centered("\033[2m────────────────────────────────────────\033[0m", w);
        printf("\r\n");

        if (sel == 0) {
            print_centered("  \033[96;1m▸ 📂  Browse Files\033[0m    \033[2mSearch across documents\033[0m", w);
            print_centered("    📁  Enter Directory \033[2mOpen specific path\033[0m     ", w);
        } else {
            print_centered("    📂  Browse Files   \033[2mSearch across documents\033[0m ", w);
            print_centered("  \033[96;1m▸ 📁  Enter Directory\033[0m \033[2mOpen specific path\033[0m     ", w);
        }
        printf("\r\n");
        print_centered("\033[2m↑/↓ Navigate  •  Enter Select  •  q Quit\033[0m", w);
        fflush(stdout);

        char buf[32] = {};
        int k = term_read_key(STDIN_FILENO, buf, sizeof(buf), nullptr, nullptr);
        if (k == 'q' || k == 27 || k == KEY_CTRL_C) {
            term_restore(&ts);
            term_show_cursor();
            return (menu_result){ .choice = MENU_QUIT };
        }
        if (k == KEY_ARROW_UP || k == 'k') sel = 0;
        else if (k == KEY_ARROW_DOWN || k == 'j') sel = 1;
        else if (k == '\r' || k == '\n') {
            if (sel == 0) {
                term_restore(&ts);
                term_show_cursor();
                return (menu_result){ .choice = MENU_BROWSE };
            } else {
                char dir[1024] = {};
                if (prompt_for_dir(dir, sizeof(dir))) {
                    term_restore(&ts);
                    term_show_cursor();
                    menu_result r = { .choice = MENU_ENTER_DIR };
                    snprintf(r.dir_path, sizeof(r.dir_path), "%s", dir);
                    return r;
                }
            }
        }
    }
}
