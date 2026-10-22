#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "pdfcli/viewer.h"
#include "pdfcli/doc_engine.h"

// Generate a valid minimal PDF file for testing
static void create_test_pdf(const char *path) {
    const char *pdf =
        "%PDF-1.4\n"
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>\nendobj\n"
        "xref\n"
        "0 4\n"
        "0000000000 65535 f \n"
        "0000000009 00000 n \n"
        "0000000058 00000 n \n"
        "0000000115 00000 n \n"
        "trailer\n"
        "<< /Size 4 /Root 1 0 R >>\n"
        "startxref\n"
        "186\n"
        "%%EOF\n";
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    fputs(pdf, f);
    fclose(f);
}

int main(void) {
    printf("===> Running HUD unit tests...\n");

    const char *doc1_path = "/tmp/test_hud_doc1.pdf";
    const char *doc2_path = "/tmp/test_hud_doc2.pdf";
    create_test_pdf(doc1_path);
    create_test_pdf(doc2_path);

    // Test 0: NULL safety
    printf("  [TEST] NULL safety checks...");
    fflush(stdout);
    assert(!viewer_is_tab_bar_visible(NULL));
    viewer_toggle_hud(NULL);
    viewer_dismiss_hud(NULL);
    printf(" PASSED\n");

    // Test 1: Tab bar is hidden by default upon startup
    printf("  [TEST] Tab bar hidden by default on single-tab startup...");
    fflush(stdout);
    doc_viewer *v = viewer_create(doc1_path);
    assert(v != NULL);
    assert(viewer_open(v));
    assert(viewer_get_tab_count(v) == 1);
    assert(viewer_get_active_tab(v) == 0);
    assert(!viewer_is_tab_bar_visible(v));
    printf(" PASSED\n");

    // Test 2: Pressing 'i' (viewer_toggle_hud) reveals tab bar (viewer_is_tab_bar_visible evaluates true)
    printf("  [TEST] viewer_toggle_hud reveals HUD tab bar...");
    fflush(stdout);
    viewer_toggle_hud(v);
    assert(viewer_is_tab_bar_visible(v));
    printf(" PASSED\n");

    // Test 3: Pressing 'i' again (viewer_toggle_hud) dismisses HUD
    printf("  [TEST] viewer_toggle_hud toggles off HUD...");
    fflush(stdout);
    viewer_toggle_hud(v);
    assert(!viewer_is_tab_bar_visible(v));
    printf(" PASSED\n");

    // Test 4: viewer_dismiss_hud dismisses active HUD immediately
    printf("  [TEST] viewer_dismiss_hud dismisses HUD...");
    fflush(stdout);
    viewer_toggle_hud(v);
    assert(viewer_is_tab_bar_visible(v));
    viewer_dismiss_hud(v);
    assert(!viewer_is_tab_bar_visible(v));
    printf(" PASSED\n");

    // Test 5: Multi-tab HUD support and tab navigation
    printf("  [TEST] Multi-tab HUD behavior and tab switching...");
    fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    int devnull = open("/dev/null", 0666);
    if (devnull >= 0) {
        dup2(devnull, STDOUT_FILENO);
        close(devnull);
    }

    assert(viewer_add_tab(v, doc2_path));
    assert(viewer_get_tab_count(v) == 2);
    assert(viewer_get_active_tab(v) == 1);
    assert(!viewer_is_tab_bar_visible(v)); // Hidden by default even with multiple tabs

    viewer_toggle_hud(v);
    assert(viewer_is_tab_bar_visible(v));

    viewer_switch_tab(v, 0);
    assert(viewer_get_active_tab(v) == 0);

    viewer_switch_tab(v, 1);
    assert(viewer_get_active_tab(v) == 1);

    viewer_dismiss_hud(v);
    assert(!viewer_is_tab_bar_visible(v));

    viewer_close_tab(v, 1);
    assert(viewer_get_tab_count(v) == 1);
    assert(viewer_get_active_tab(v) == 0);

    if (saved_stdout >= 0) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
    }
    printf(" PASSED\n");

    // Test 6: 2500ms duration expiration
    printf("  [TEST] HUD timer expiration after 2500ms window...");
    fflush(stdout);
    viewer_toggle_hud(v);
    assert(viewer_is_tab_bar_visible(v));
    usleep(2550000); // 2550ms > 2500ms duration
    assert(!viewer_is_tab_bar_visible(v));
    printf(" PASSED\n");

    // Test 7: Single-tab close behavior
    printf("  [TEST] Single-tab close reduces count to zero...");
    fflush(stdout);
    viewer_close_tab(v, 0);
    assert(viewer_get_tab_count(v) == 0);
    assert(!viewer_is_tab_bar_visible(v));
    printf(" PASSED\n");

    viewer_destroy(v);
    unlink(doc1_path);
    unlink(doc2_path);

    printf("===> All HUD unit tests passed successfully!\n");
    return 0;
}
