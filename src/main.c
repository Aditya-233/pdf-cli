#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <signal.h>
#if defined(__linux__)
#include <malloc.h>
#include <sys/prctl.h>
#endif

#include "pdfcli/types.h"
#include "pdfcli/ctl_client.h"
#include "pdfcli/viewer.h"
#include "pdfcli/file_search.h"
#include "pdfcli/picker.h"
#include "pdfcli/menu.h"
#include "pdfcli/term_raw.h"

static void print_help(void) {
    puts("pdf-cli (C23) - Terminal-based document viewer\n\n"
         "USAGE: pdf-cli [OPTIONS] [PATH...]  |  dvctl [OPTIONS] [COMMAND] ...\n"
         "ARGUMENTS: [PATH...] One or more files or directory to open in tabs\n"
         "OPTIONS:\n"
         "  -h, --help     Show help\n"
         "  -v, --version  Show version\n"
         "  ctl ...        Control running viewers (see: pdf-cli ctl help)\n\n"
         "TAB NAVIGATION:\n"
         "  Ctrl+1 .. Ctrl+9 (Alt+1..9) Switch directly to tab 1 through 9\n"
         "  Ctrl+0           (Alt+0)     Switch to tab 10 / last tab\n"
         "  Ctrl+Tab / gt                Cycle to next tab\n"
         "  Ctrl+Shift+Tab / gT          Cycle to previous tab\n"
         "  Ctrl+T                       Open document in new tab\n"
         "  Ctrl+W                       Close current tab\n"
         "  Mouse Left on Tab Bar        Click tab to switch / click × to close\n"
         "  Mouse Wheel on Tab Bar       Cycle through tabs\n\n"
         "KEYBOARD SHORTCUTS:\n"
         "  j, Down        Scroll down (line) / Next page (fit-height)\n"
         "  k, Up          Scroll up (line) / Prev page (fit-height)\n"
         "  PgDn, Space    Continuous smooth scroll down / Page down\n"
         "  PgUp           Continuous smooth scroll up / Page up\n"
         "  g              Jump to specific page number (or 'gt'/'gT' for tabs)\n"
         "  f              Cycle fit modes (fit-width continuous / fit-height)\n"
         "  D              Cycle tint (white/dim/dark/invert)\n"
         "  T              Table of contents modal\n"
         "  +, -           Zoom in / out\n"
         "  b              Back to file picker\n"
         "  q              Quit\n");
}

static char *expand_path(const char *path) {
    if (!path) return nullptr;
    if (path[0] == '~' && path[1] == '/') {
        const char *h = getenv("HOME");
        if (h) {
            char buf[PATH_MAX];
            snprintf(buf, sizeof(buf), "%s/%s", h, path + 2);
            return strdup(buf);
        }
    }
    struct stat st;
    if (stat(path, &st) == 0) return strdup(path);

    // Fallback: check if relative to $HOME
    const char *h = getenv("HOME");
    if (h && path[0] != '/') {
        char buf[PATH_MAX];
        snprintf(buf, sizeof(buf), "%s/%s", h, path);
        if (stat(buf, &st) == 0) return strdup(buf);
    }
    return strdup(path);
}

static bool run_viewer_loop(const char *initial_path, const char *search_dir) {
    char *cur = strdup(initial_path);
    while (cur) {
        struct stat st;
        if (stat(cur, &st) != 0 || S_ISDIR(st.st_mode)) {
            file_list *files = file_search_dir(cur);
            if (!files || files->count == 0) {
                printf("No supported documents found in %s\n", cur);
                if (files) file_list_free(files);
                free(cur);
                return true;
            }
            char *sel = picker_run(files);
            file_list_free(files);
            if (!sel) { free(cur); return true; }
            free(cur);
            cur = sel;
        }

        doc_viewer *v = viewer_create(cur);
        if (!v || !viewer_open(v)) {
            printf("Error opening file: %s\n", cur);
            if (v) viewer_destroy(v);
            free(cur);
            return false;
        }

        bool want_back = viewer_run(v);
        viewer_destroy(v);
        if (!want_back) { free(cur); return false; }

        file_list *files = file_search_dir(search_dir);
        if (!files || files->count == 0) {
            if (files) file_list_free(files);
            free(cur);
            return true;
        }
        char *sel = picker_run(files);
        file_list_free(files);
        if (!sel) { free(cur); return true; }
        free(cur);
        cur = sel;
    }
    return true;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
#if defined(__linux__)
#if defined(PR_SET_THP_DISABLE)
    (void)prctl(PR_SET_THP_DISABLE, 1, 0, 0, 0);
#endif
    mallopt(M_ARENA_MAX, 1);
    mallopt(M_MMAP_THRESHOLD, 64 * 1024);
    mallopt(M_TRIM_THRESHOLD, 64 * 1024);
#endif

    if (strcmp(basename(argv[0]), "dvctl") == 0) return ctl_client_main(argc, argv);
    if (argc > 1 && strcmp(argv[1], "ctl") == 0) return ctl_client_main(argc - 1, argv + 1);
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) { print_help(); return 0; }
        if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
            puts("pdf-cli v.3.0 (C23 ISO/IEC 9899:2024)");
            return 0;
        }
    }

    if (argc <= 1) {
        while (true) {
            menu_result res = menu_run();
            term_clear_screen();
            if (res.choice == MENU_QUIT) return 0;
            if (res.choice == MENU_BROWSE) {
                file_list *files = file_search_broad();
                if (!files || files->count == 0) {
                    printf("No supported documents found. Press Enter...");
                    getchar();
                    if (files) file_list_free(files);
                    continue;
                }
                char *sel = picker_run(files);
                file_list_free(files);
                if (!sel) continue;
                char *pc = strdup(sel), *dir = strdup(dirname(pc));
                free(pc);
                bool cont = run_viewer_loop(sel, dir);
                free(sel); free(dir);
                if (!cont) return 0;
            } else if (res.choice == MENU_ENTER_DIR && res.dir_path[0]) {
                char *exp = expand_path(res.dir_path);
                struct stat st;
                if (stat(exp, &st) != 0) {
                    printf("\n  Not found: %s\n  Press Enter...\n", exp);
                    getchar();
                    free(exp);
                    continue;
                }
                char *dir = S_ISDIR(st.st_mode) ? strdup(exp) : strdup(dirname(strdup(exp)));
                bool cont = run_viewer_loop(exp, dir);
                free(exp); free(dir);
                if (!cont) return 0;
            }
        }
    }

    if (argc > 2) {
        char *first_exp = expand_path(argv[1]);
        struct stat st;
        if (stat(first_exp, &st) != 0 || S_ISDIR(st.st_mode)) {
            printf("Path not found: %s\n", first_exp);
            free(first_exp);
            return 1;
        }
        doc_viewer *v = viewer_create(first_exp);
        free(first_exp);
        if (!v || !viewer_open(v)) {
            printf("Error opening file: %s\n", argv[1]);
            if (v) viewer_destroy(v);
            return 1;
        }
        for (int i = 2; i < argc; i++) {
            char *next_exp = expand_path(argv[i]);
            if (next_exp) {
                (void)viewer_add_tab(v, next_exp);
                free(next_exp);
            }
        }
        viewer_set_active_tab(v, 0);
        (void)viewer_run(v);
        viewer_destroy(v);
        return 0;
    }

    char *exp = expand_path(argv[1]);
    struct stat st;
    if (stat(exp, &st) != 0) {
        printf("Path not found: %s\n", exp);
        free(exp);
        return 1;
    }
    char *pc = strdup(exp);
    char *dir = S_ISDIR(st.st_mode) ? strdup(exp) : strdup(dirname(pc));
    free(pc);
    run_viewer_loop(exp, dir);
    free(exp); free(dir);
    return 0;
}
