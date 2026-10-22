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

static int s_saved_stdout = -1;
static void mute_stdout(void) {
    fflush(stdout);
    s_saved_stdout = dup(STDOUT_FILENO);
    int null_fd = open("/dev/null", O_WRONLY);
    if (null_fd >= 0) {
        dup2(null_fd, STDOUT_FILENO);
        close(null_fd);
    }
}
static void unmute_stdout(void) {
    fflush(stdout);
    if (s_saved_stdout >= 0) {
        dup2(s_saved_stdout, STDOUT_FILENO);
        close(s_saved_stdout);
        s_saved_stdout = -1;
    }
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

    // Test 6: 2500ms duration expiration & hit-box invalidation
    printf("  [TEST] HUD timer expiration after 2500ms window...");
    fflush(stdout);
    viewer_toggle_hud(v);
    assert(viewer_is_tab_bar_visible(v));
    mute_stdout();
    viewer_render_tab_bar(v);
    unmute_stdout();
    bool test6_close = false;
    assert(viewer_find_tab_hit(v, 1, 1, &test6_close) == 0);
    usleep(2550000); // 2550ms > 2500ms duration
    assert(!viewer_is_tab_bar_visible(v));
    assert(viewer_find_tab_hit(v, 1, 1, &test6_close) == -1);
    printf(" PASSED\n");

    // Test 7: Single-tab close behavior
    printf("  [TEST] Single-tab close reduces count to zero...");
    fflush(stdout);
    viewer_close_tab(v, 0);
    assert(viewer_get_tab_count(v) == 0);
    assert(!viewer_is_tab_bar_visible(v));
    printf(" PASSED\n");

    viewer_destroy(v);


    // Test 8: Single-tab hit box calculation and exact close button boundary testing
    printf("  [TEST] Single-tab hit box calculation & close button accuracy...");
    fflush(stdout);
    v = viewer_create(doc1_path);
    assert(v != NULL);
    assert(viewer_open(v));

    bool is_close = false;
    // When HUD is hidden, all row 1 clicks must return -1 (pass through to links)
    for (int c = 1; c <= 80; c++) {
        assert(viewer_find_tab_hit(v, c, 1, &is_close) == -1);
    }

    viewer_toggle_hud(v);
    assert(viewer_is_tab_bar_visible(v));

    mute_stdout();
    viewer_render_tab_bar(v);
    unmute_stdout();

    // " 1: test_hud_do... × "
    // Cols 1..18: Tab label (title ends at col 18)
    // Cols 19..21: Close button (" × ")
    // Col 22+: Outside tab
    assert(viewer_find_tab_hit(v, 1, 1, &is_close) == 0 && !is_close);
    assert(viewer_find_tab_hit(v, 5, 1, &is_close) == 0 && !is_close);
    assert(viewer_find_tab_hit(v, 18, 1, &is_close) == 0 && !is_close); // Last char of title MUST NOT close tab!

    assert(viewer_find_tab_hit(v, 19, 1, &is_close) == 0 && is_close); // Space before × MUST close tab!
    assert(viewer_find_tab_hit(v, 20, 1, &is_close) == 0 && is_close); // × MUST close tab!
    assert(viewer_find_tab_hit(v, 21, 1, &is_close) == 0 && is_close); // Space after × MUST close tab!

    assert(viewer_find_tab_hit(v, 22, 1, &is_close) == -1); // Outside tab
    assert(viewer_find_tab_hit(v, 10, 2, &is_close) == -1); // Row 2 is not row 1

    viewer_dismiss_hud(v);
    assert(!viewer_is_tab_bar_visible(v));
    printf(" PASSED\n");

    // Test 9: Multi-tab hit testing (inactive tab has no close button, active tab has close button)
    printf("  [TEST] Multi-tab hit boxes & active close button distinction...");
    fflush(stdout);
    assert(viewer_add_tab(v, doc2_path));
    assert(viewer_get_tab_count(v) == 2);
    assert(viewer_get_active_tab(v) == 1);
    assert(!viewer_is_tab_bar_visible(v));

    viewer_toggle_hud(v);
    assert(viewer_is_tab_bar_visible(v));

    mute_stdout();
    viewer_render_tab_bar(v);
    unmute_stdout();

    // Tab 0 is inactive: clicking anywhere on Tab 0 must return 0 and is_close == false
    for (int c = 1; c <= 19; c++) {
        int hit = viewer_find_tab_hit(v, c, 1, &is_close);
        if (hit == 0) {
            assert(!is_close);
        }
    }

    // Tab 1 is active: title area is_close == false, close area is_close == true
    bool found_active_close = false;
    for (int c = 21; c <= 45; c++) {
        int hit = viewer_find_tab_hit(v, c, 1, &is_close);
        if (hit == 1 && is_close) {
            found_active_close = true;
        }
    }
    assert(found_active_close);

    viewer_dismiss_hud(v);
    assert(!viewer_is_tab_bar_visible(v));
    printf(" PASSED\n");

    // Test 10: UTF-8 tab title rendering and width measurement
    printf("  [TEST] UTF-8 tab title hit testing without character corruption...");
    fflush(stdout);
    const char *doc_utf8_path = "/tmp/test_hud_тест_123456789.pdf";
    create_test_pdf(doc_utf8_path);
    assert(viewer_add_tab(v, doc_utf8_path));
    assert(viewer_get_tab_count(v) == 3);
    assert(!viewer_is_tab_bar_visible(v));

    viewer_toggle_hud(v);
    assert(viewer_is_tab_bar_visible(v));

    mute_stdout();
    viewer_render_tab_bar(v);
    unmute_stdout();

    // Verify Tab 2 (the active UTF-8 tab) has valid hit boxes and close button
    found_active_close = false;
    for (int c = 1; c <= 80; c++) {
        int hit = viewer_find_tab_hit(v, c, 1, &is_close);
        if (hit == 2 && is_close) {
            found_active_close = true;
        }
    }
    assert(found_active_close);
    mute_stdout();
    viewer_close_tab(v, 2);
    unmute_stdout();
    viewer_dismiss_hud(v);
    unlink(doc_utf8_path);
    printf(" PASSED\n");

    // Test 11: Stale hit box invalidation when terminal is resized narrower
    printf("  [TEST] Stale hit box invalidation on terminal width shrinking...");
    fflush(stdout);
    const char *doc3_path = "/tmp/test_hud_doc3.pdf";
    create_test_pdf(doc3_path);
    mute_stdout();
    assert(viewer_add_tab(v, doc3_path));
    unmute_stdout();
    assert(viewer_get_tab_count(v) == 3);

    viewer_set_term_size(v, 80, 24);
    viewer_toggle_hud(v);
    assert(viewer_is_tab_bar_visible(v));

    mute_stdout();
    viewer_render_tab_bar(v);
    unmute_stdout();

    // At 80 cols, Tab 0, Tab 1, and Tab 2 all fit and have valid hit boxes
    bool has_tab0 = false, has_tab1 = false, has_tab2 = false;
    for (int c = 1; c <= 80; c++) {
        int h = viewer_find_tab_hit(v, c, 1, &is_close);
        if (h == 0) has_tab0 = true;
        if (h == 1) has_tab1 = true;
        if (h == 2) has_tab2 = true;
    }
    assert(has_tab0 && has_tab1 && has_tab2);

    // Now shrink terminal width to 25 cols (only Tab 0 fits, Tab 1 & Tab 2 overflow)
    viewer_set_term_size(v, 25, 24);
    mute_stdout();
    viewer_render_tab_bar(v);
    unmute_stdout();

    // Must NOT retain stale hit boxes for Tab 1 or Tab 2!
    for (int c = 1; c <= 80; c++) {
        int h = viewer_find_tab_hit(v, c, 1, &is_close);
        assert(h != 1); // Tab 1 must NOT be hit anywhere!
        assert(h != 2); // Tab 2 must NOT be hit anywhere!
    }
    viewer_dismiss_hud(v);
    unlink(doc3_path);
    printf(" PASSED\n");

    // Test 12: Column boundary safety & zeroed hit box protection
    printf("  [TEST] Column boundary safety & zeroed hit box protection...");
    fflush(stdout);
    viewer_toggle_hud(v);
    assert(viewer_is_tab_bar_visible(v));
    viewer_set_term_size(v, 80, 24);
    mute_stdout();
    viewer_render_tab_bar(v);
    unmute_stdout();

    // Out-of-bounds columns and zeroed hit boxes must return -1
    assert(viewer_find_tab_hit(v, 0, 1, &is_close) == -1);  // Col 0
    assert(viewer_find_tab_hit(v, -1, 1, &is_close) == -1); // Negative col
    assert(viewer_find_tab_hit(v, -99, 1, &is_close) == -1);
    assert(viewer_find_tab_hit(v, 81, 1, &is_close) == -1); // Past term_cols
    assert(viewer_find_tab_hit(v, 200, 1, &is_close) == -1);
    assert(viewer_find_tab_hit(v, 10, 0, &is_close) == -1); // Row 0
    assert(viewer_find_tab_hit(v, 10, 2, &is_close) == -1); // Row 2
    assert(viewer_find_tab_hit(v, 10, 24, &is_close) == -1); // Bottom row

    viewer_dismiss_hud(v);
    printf(" PASSED\n");

    // Test 13: Tab switching and Option A navigation
    printf("  [TEST] Tab switching and active tab state...");
    fflush(stdout);
    assert(viewer_get_tab_count(v) == 3);
    mute_stdout();
    viewer_switch_tab(v, 0);
    assert(viewer_get_active_tab(v) == 0);
    viewer_switch_tab(v, 1);
    assert(viewer_get_active_tab(v) == 1);
    viewer_switch_tab(v, 2);
    assert(viewer_get_active_tab(v) == 2);
    unmute_stdout();
    assert(!viewer_is_tab_bar_visible(v)); // Switching tabs does not force HUD open
    printf(" PASSED\n");

    // Test 14: Single-tab close via hit box
    printf("  [TEST] Single-tab close via hit-box click...");
    fflush(stdout);
    mute_stdout();
    viewer_close_tab(v, 2);
    viewer_close_tab(v, 1);
    unmute_stdout();
    assert(viewer_get_tab_count(v) == 1);
    viewer_toggle_hud(v);
    assert(viewer_is_tab_bar_visible(v));

    mute_stdout();
    viewer_render_tab_bar(v);
    unmute_stdout();

    // Find the close button for Tab 0 and click it
    int close_col = -1;
    for (int c = 1; c <= 80; c++) {
        if (viewer_find_tab_hit(v, c, 1, &is_close) == 0 && is_close) {
            close_col = c;
            break;
        }
    }
    assert(close_col > 0);
    int clicked_tab = viewer_find_tab_hit(v, close_col, 1, &is_close);
    assert(clicked_tab == 0 && is_close);
    mute_stdout();
    viewer_close_tab(v, (size_t)clicked_tab);
    unmute_stdout();
    assert(viewer_get_tab_count(v) == 0);
    assert(!viewer_is_tab_bar_visible(v));
    printf(" PASSED\n");

    viewer_destroy(v);

    // Test 15: Control character sanitization and buffer safety in viewer_truncate_to_display_width
    printf("  [TEST] Control character sanitization and buffer safety in truncate...");
    fflush(stdout);
    char buf15[64];
    // Newline / carriage return / tab / escape sanitization
    viewer_truncate_to_display_width(buf15, sizeof(buf15), "doc\nname\rtest\tfile\033[31m.pdf", 30, false);
    assert(strchr(buf15, '\n') == NULL);
    assert(strchr(buf15, '\r') == NULL);
    assert(strchr(buf15, '\t') == NULL);
    assert(strchr(buf15, '\033') == NULL);
    // Small buffer truncation without buffer overrun
    char small_buf[8];
    viewer_truncate_to_display_width(small_buf, sizeof(small_buf), "VeryLongFileNameHere.pdf", 20, true);
    assert(strlen(small_buf) < sizeof(small_buf));
    assert(strstr(small_buf, "...") != NULL);
    // Zero width / NULL handling
    viewer_truncate_to_display_width(buf15, sizeof(buf15), NULL, 10, true);
    assert(buf15[0] == '\0');
    viewer_truncate_to_display_width(buf15, 0, "test.pdf", 10, true);
    printf(" PASSED\n");

    // Test 16: Zero and minimal terminal dimension resiliency
    printf("  [TEST] Zero and minimal terminal dimension resiliency...");
    fflush(stdout);
    v = viewer_create(doc1_path);
    assert(v != NULL);
    assert(viewer_open(v));
    viewer_toggle_hud(v);
    assert(viewer_is_tab_bar_visible(v));
    viewer_set_term_size(v, 5, 1); // 5 cols, 1 row (tabs do not fit)
    mute_stdout();
    viewer_render_tab_bar(v);
    unmute_stdout();
    for (int c = 1; c <= 80; c++) {
        assert(viewer_find_tab_hit(v, c, 1, &is_close) == -1);
    }
    printf(" PASSED\n");

    // Test 17: Inactive tab close button click immunity
    printf("  [TEST] Inactive tab close button click immunity...");
    fflush(stdout);
    create_test_pdf(doc2_path);
    mute_stdout();
    assert(viewer_add_tab(v, doc2_path));
    unmute_stdout();
    assert(viewer_get_tab_count(v) == 2);
    // Tab 1 is active, Tab 0 is inactive
    assert(viewer_get_active_tab(v) == 1);
    viewer_set_term_size(v, 80, 24);
    mute_stdout();
    viewer_render_tab_bar(v);
    unmute_stdout();
    // Test EVERY column of Tab 0: is_close must NEVER be true
    for (int c = 1; c <= 80; c++) {
        int hit = viewer_find_tab_hit(v, c, 1, &is_close);
        if (hit == 0) {
            assert(!is_close);
        }
    }
    printf(" PASSED\n");

    // Test 18: Rapid toggle hammering and timer consistency
    printf("  [TEST] Rapid toggle hammering and timer consistency...");
    fflush(stdout);
    viewer_dismiss_hud(v);
    assert(!viewer_is_tab_bar_visible(v));
    for (int i = 0; i < 50; i++) {
        viewer_toggle_hud(v);
        assert(viewer_is_tab_bar_visible(v));
        viewer_toggle_hud(v);
        assert(!viewer_is_tab_bar_visible(v));
    }
    printf(" PASSED\n");

    viewer_destroy(v);
    unlink(doc1_path);
    unlink(doc2_path);

    printf("===> All HUD unit tests passed successfully!\n");
    return 0;
}
