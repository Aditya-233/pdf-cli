#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>

#include "pdfcli/viewer.h"
#include "pdfcli/types.h"

int main(void) {
    printf("============================================================\n");
    printf("  pdf-cli (C23 ISO/IEC 9899:2024) Tab Support Unit Tests\n");
    printf("============================================================\n");

    const char *test_pdf = "depends/go-fitz/testdata/test.pdf";
    if (access(test_pdf, R_OK) != 0) {
        printf("Skipping tab tests (test.pdf not found)\n");
        return 0;
    }

    int devnull = open("/dev/null", O_WRONLY);
    int real_stdout = dup(STDOUT_FILENO);

    printf("[1/5] Creating viewer with initial document...\n");
    doc_viewer *v = viewer_create(test_pdf);
    assert(v != nullptr);
    assert(viewer_get_tab_count(v) == 1);
    assert(viewer_get_active_tab(v) == 0);

    bool opened = viewer_open(v);
    assert(opened);
    printf("  Viewer initialized with 1 tab successfully!\n");

    printf("[2/5] Adding secondary and tertiary tabs...\n");
    bool tab2 = viewer_add_tab(v, test_pdf);
    assert(tab2);
    assert(viewer_get_tab_count(v) == 2);
    assert(viewer_get_active_tab(v) == 1);

    bool tab3 = viewer_add_tab(v, test_pdf);
    assert(tab3);
    assert(viewer_get_tab_count(v) == 3);
    assert(viewer_get_active_tab(v) == 2);
    printf("  Added 3 tabs successfully!\n");

    printf("[3/5] Testing tab switching & cyclic navigation...\n");
    // Suppress TUI escape sequences during headless test
    if (devnull >= 0) dup2(devnull, STDOUT_FILENO);

    viewer_switch_tab(v, 0);
    assert(viewer_get_active_tab(v) == 0);

    viewer_switch_tab(v, 1);
    assert(viewer_get_active_tab(v) == 1);

    viewer_next_tab(v);
    assert(viewer_get_active_tab(v) == 2);

    viewer_next_tab(v); // wraps around to tab 0
    assert(viewer_get_active_tab(v) == 0);

    viewer_prev_tab(v); // wraps backwards to tab 2
    assert(viewer_get_active_tab(v) == 2);

    viewer_prev_tab(v);
    assert(viewer_get_active_tab(v) == 1);

    if (devnull >= 0) dup2(real_stdout, STDOUT_FILENO);
    printf("  Tab switching and cyclic navigation passed!\n");

    printf("[4/5] Testing tab closing & active tab adjustment...\n");
    if (devnull >= 0) dup2(devnull, STDOUT_FILENO);

    // Currently on tab 1 of 3 (indices 0, 1, 2)
    // Close active tab (1) -> should fall back to tab 1 (which was tab 2)
    viewer_close_tab(v, 1);
    assert(viewer_get_tab_count(v) == 2);
    assert(viewer_get_active_tab(v) == 1);

    // Close tab 1 -> should fall back to tab 0
    viewer_close_tab(v, 1);
    assert(viewer_get_tab_count(v) == 1);
    assert(viewer_get_active_tab(v) == 0);

    if (devnull >= 0) dup2(real_stdout, STDOUT_FILENO);
    printf("  Tab close and active tab fallback passed!\n");

    printf("[5/5] Testing tab limit (max 10 tabs)...\n");
    for (size_t i = 1; i < MAX_TABS; i++) {
        bool ok = viewer_add_tab(v, test_pdf);
        assert(ok);
    }
    assert(viewer_get_tab_count(v) == MAX_TABS);

    // Attempting to add 11th tab must fail gracefully
    bool overflow = viewer_add_tab(v, test_pdf);
    assert(!overflow);
    assert(viewer_get_tab_count(v) == MAX_TABS);
    printf("  Tab limit of 10 enforced correctly!\n");

    if (devnull >= 0) dup2(devnull, STDOUT_FILENO);
    viewer_destroy(v);
    if (devnull >= 0) {
        dup2(real_stdout, STDOUT_FILENO);
        close(devnull);
        close(real_stdout);
    }

    printf("\n>>> ALL TAB SUPPORT TESTS PASSED WITH 100%% SUCCESS! <<<\n");
    return 0;
}
