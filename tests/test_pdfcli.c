#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <limits.h>
#include "pdfcli.h"

#define TEST_CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "\nFAILED: %s (at %s:%d)\n", msg, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

// Helper to write a syntactically valid PDF with exact xref table
static void write_test_pdf_with_toc(const char *path, const char *title1, const char *text1,
                                    const char *title2, const char *text2,
                                    double w0, double h0, double w1, double h1) {
    FILE *f = fopen(path, "wb");
    TEST_CHECK(f != NULL, "Failed to open output test PDF");

    long offsets[16] = {0};
    int obj_idx = 1;

    fprintf(f, "%%PDF-1.4\n");

    // 1 0 obj: Catalog with Outlines
    offsets[obj_idx++] = ftell(f);
    fprintf(f, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R /Outlines 3 0 R >>\nendobj\n");

    // 2 0 obj: Pages
    offsets[obj_idx++] = ftell(f);
    fprintf(f, "2 0 obj\n<< /Type /Pages /Kids [5 0 R 6 0 R] /Count 2 >>\nendobj\n");

    // 3 0 obj: Outlines root
    offsets[obj_idx++] = ftell(f);
    fprintf(f, "3 0 obj\n<< /Type /Outlines /First 4 0 R /Last 7 0 R /Count 2 >>\nendobj\n");

    // 4 0 obj: Outline Item 1
    offsets[obj_idx++] = ftell(f);
    fprintf(f, "4 0 obj\n<< /Title (%s) /Parent 3 0 R /Next 7 0 R /Dest [5 0 R /Fit] >>\nendobj\n", title1);

    // 5 0 obj: Page 1
    offsets[obj_idx++] = ftell(f);
    fprintf(f, "5 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %f %f] /Contents 8 0 R /Resources << /Font << /F1 10 0 R >> >> >>\nendobj\n", w0, h0);

    // 6 0 obj: Page 2
    offsets[obj_idx++] = ftell(f);
    fprintf(f, "6 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %f %f] /Contents 9 0 R /Resources << /Font << /F1 10 0 R >> >> >>\nendobj\n", w1, h1);

    // 7 0 obj: Outline Item 2
    offsets[obj_idx++] = ftell(f);
    fprintf(f, "7 0 obj\n<< /Title (%s) /Parent 3 0 R /Prev 4 0 R /Dest [6 0 R /Fit] >>\nendobj\n", title2);

    // 8 0 obj: Contents Page 1
    offsets[obj_idx++] = ftell(f);
    size_t stream1_len = strlen(text1) + 33;
    fprintf(f, "8 0 obj\n<< /Length %zu >>\nstream\nBT /F1 24 Tf 100 700 Td (%s) Tj ET\nendstream\nendobj\n", stream1_len, text1);

    // 9 0 obj: Contents Page 2
    offsets[obj_idx++] = ftell(f);
    size_t stream2_len = strlen(text2) + 33;
    fprintf(f, "9 0 obj\n<< /Length %zu >>\nstream\nBT /F1 24 Tf 100 700 Td (%s) Tj ET\nendstream\nendobj\n", stream2_len, text2);

    // 10 0 obj: Helvetica Font
    offsets[obj_idx++] = ftell(f);
    fprintf(f, "10 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n");

    long xref_pos = ftell(f);
    fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", obj_idx);
    for (int i = 1; i < obj_idx; i++) {
        fprintf(f, "%010ld 00000 n \n", offsets[i]);
    }
    fprintf(f, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", obj_idx, xref_pos);
    fclose(f);
}

static void write_zero_page_pdf(const char *path) {
    FILE *f = fopen(path, "wb");
    TEST_CHECK(f != NULL, "Failed to open zero page PDF");
    fprintf(f,
        "%%PDF-1.4\n"
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n"
        "xref\n0 3\n0000000000 65535 f \n"
        "0000000009 00000 n \n"
        "0000000058 00000 n \n"
        "trailer\n<< /Size 3 /Root 1 0 R >>\nstartxref\n114\n%%%%EOF\n");
    fclose(f);
}

static void write_extreme_dim_pdf(const char *path) {
    FILE *f = fopen(path, "wb");
    TEST_CHECK(f != NULL, "Failed to open extreme dim PDF");
    fprintf(f,
        "%%PDF-1.4\n"
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 1 100000000] >>\nendobj\n"
        "xref\n0 4\n0000000000 65535 f \n"
        "0000000009 00000 n \n"
        "0000000058 00000 n \n"
        "0000000115 00000 n \n"
        "trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n200\n%%%%EOF\n");
    fclose(f);
}

int main(void) {
    printf("====================================================\n");
    printf("===> Running Comprehensive pdf-cli Strictness Suite\n");
    printf("====================================================\n");

    char temp_dir[] = "/tmp/pdfcli_test_XXXXXX";
    TEST_CHECK(mkdtemp(temp_dir) != NULL, "Failed to create private temp directory");

    char sample_pdf[512];
    snprintf(sample_pdf, sizeof(sample_pdf), "%s/sample.pdf", temp_dir);
    write_test_pdf_with_toc(sample_pdf, "Chapter 1", "Page One - Hello Continuous",
                            "Chapter 2", "Page Two - Moving Smoothly",
                            612.0f, 792.0f, 612.0f, 792.0f);

    // Test 1: NULL Safety & Invalid File Handling
    printf("  [TEST 1] NULL safety & invalid file handling...");
    fflush(stdout);
    TEST_CHECK(pdf_open(NULL) == NULL, "pdf_open(NULL) must return NULL");
    TEST_CHECK(pdf_open("/nonexistent_path/should_fail.pdf") == NULL, "Invalid path must return NULL");
    pdf_close(NULL);
    fz_rect empty_b = pdf_page_bounds(NULL, 0);
    TEST_CHECK(empty_b.x0 == 0 && empty_b.x1 == 0, "Bounds on NULL doc must return 0");
    TEST_CHECK(pdf_render_viewport(NULL, 0, 1.0, 0, 100, 100, false) == NULL, "Viewport on NULL must return NULL");
    size_t toc_cnt = 99;
    TEST_CHECK(pdf_get_toc(NULL, &toc_cnt) == NULL && toc_cnt == 0, "TOC on NULL must return NULL");
    pdf_free_toc(NULL, 0);
    size_t srch_cnt = 99;
    TEST_CHECK(pdf_search_text(NULL, "test", &srch_cnt) == NULL && srch_cnt == 0, "Search on NULL must return NULL");
    pdf_free_search(NULL);
    printf(" PASSED\n");

    // Test 2: Valid Multi-Page Document Lifecycle & Bounds
    printf("  [TEST 2] Multi-page document lifecycle & page bounds...");
    fflush(stdout);
    pdf_doc_t *doc = pdf_open(sample_pdf);
    TEST_CHECK(doc != NULL, "Failed to open sample PDF");
    TEST_CHECK(doc->page_count == 2, "Page count must be 2");
    fz_rect b0 = pdf_page_bounds(doc, 0);
    fz_rect b1 = pdf_page_bounds(doc, 1);
    TEST_CHECK(b0.x1 - b0.x0 > 600.0f, "Page 0 width > 600");
    TEST_CHECK(b1.y1 - b1.y0 > 700.0f, "Page 1 height > 700");
    printf(" PASSED\n");

    // Test 3: Viewport Rendering with scroll_y
    printf("  [TEST 3] Viewport rendering with scroll_y...");
    fflush(stdout);
    int vw = 800, vh = 600;
    fz_pixmap *pix = pdf_render_viewport(doc, 0, 1.0, 100, vw, vh, false);
    TEST_CHECK(pix != NULL, "Rendered pixmap must not be NULL");
    TEST_CHECK(pix->w == vw && pix->h == vh, "Rendered dimensions match viewport");
    TEST_CHECK(pix->samples != NULL, "Pixmap samples buffer must exist");
    printf(" PASSED\n");

    // Test 4: Dark Mode Pixel Inversion Correctness
    printf("  [TEST 4] Dark mode pixel inversion correctness...");
    fflush(stdout);
    fz_pixmap *pix_dark = pdf_render_viewport(doc, 0, 1.0, 0, 100, 100, true);
    TEST_CHECK(pix_dark != NULL, "Dark pixmap must not be NULL");
    TEST_CHECK(pix_dark->samples[0] == 0, "Inverted white background must be 0");
    fz_drop_pixmap(doc->ctx, pix_dark);
    printf(" PASSED\n");

    // Test 5: In-Document Text Search Across Pages
    printf("  [TEST 5] In-document text search across multiple pages...");
    fflush(stdout);
    size_t hits_found = 0;
    search_hit_t *hits = pdf_search_text(doc, "Continuous", &hits_found);
    TEST_CHECK(hits != NULL && hits_found == 1 && hits[0].page == 0, "Found 'Continuous' on page 0");
    pdf_free_search(hits);

    search_hit_t *hits2 = pdf_search_text(doc, "Smoothly", &hits_found);
    TEST_CHECK(hits2 != NULL && hits_found == 1 && hits2[0].page == 1, "Found 'Smoothly' on page 1");
    pdf_free_search(hits2);
    printf(" PASSED\n");

    // Test 6: Sixel Graphics Pipeline Output
    printf("  [TEST 6] Sixel encoding pipeline on rendered viewport...");
    fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    TEST_CHECK(saved_stdout >= 0, "dup STDOUT failed");
    FILE *devnull = fopen("/dev/null", "w");
    TEST_CHECK(devnull != NULL, "open /dev/null failed");
    dup2(fileno(devnull), STDOUT_FILENO);
    bool sixel_ok = sixel_draw(pix, 1, 1);
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    fclose(devnull);
    TEST_CHECK(sixel_ok == true, "Sixel encoding succeeded");
    fz_drop_pixmap(doc->ctx, pix);
    printf(" PASSED\n");

    // Test 7: Table of Contents & Outlines
    printf("  [TEST 7] Table of Contents extraction...");
    fflush(stdout);
    size_t toc_count = 0;
    toc_item_t *toc = pdf_get_toc(doc, &toc_count);
    TEST_CHECK(toc != NULL && toc_count == 2, "TOC count must be 2");
    TEST_CHECK(strcmp(toc[0].title, "Chapter 1") == 0 && toc[0].page == 0, "Item 1 matches");
    TEST_CHECK(strcmp(toc[1].title, "Chapter 2") == 0 && toc[1].page == 1, "Item 2 matches");
    pdf_free_toc(toc, toc_count);
    pdf_close(doc);
    printf(" PASSED\n");

    // Test 8: Mixed Page Dimensions & Self-Contained Multi-Page Rendering (Audit #37)
    printf("  [TEST 8] Self-contained mixed page dimensions rendering...");
    fflush(stdout);
    char mixed_pdf[512];
    snprintf(mixed_pdf, sizeof(mixed_pdf), "%s/mixed.pdf", temp_dir);
    write_test_pdf_with_toc(mixed_pdf, "Intro", "Small Intro Page",
                            "Wide Sheet", "Landscape Spread Text",
                            252.0f, 291.0f, 600.0f, 400.0f);

    pdf_doc_t *mixed_doc = pdf_open(mixed_pdf);
    TEST_CHECK(mixed_doc != NULL && mixed_doc->page_count == 2, "Opened mixed PDF");
    fz_rect mb0 = pdf_page_bounds(mixed_doc, 0);
    fz_rect mb1 = pdf_page_bounds(mixed_doc, 1);
    TEST_CHECK(mb0.x1 - mb0.x0 < 300.0f && mb0.y1 - mb0.y0 < 350.0f, "Page 0 small");
    TEST_CHECK(mb1.x1 - mb1.x0 > 550.0f && mb1.y1 - mb1.y0 < 450.0f, "Page 1 wide");

    fz_pixmap *dest = fz_new_pixmap_with_bbox(mixed_doc->ctx, fz_device_rgb(mixed_doc->ctx), fz_make_irect(0, 0, 800, 600), nullptr, 0);
    TEST_CHECK(dest != NULL, "Created dest pixmap");
    double scale0 = 800.0 / (double)(mb0.x1 - mb0.x0);
    bool slice0 = pdf_render_page_slice(mixed_doc, 0, scale0, 0, 0, 800, 600, dest, 0, 0);
    TEST_CHECK(slice0 == true, "Slice page 0 succeeded");
    double scale1 = 800.0 / (double)(mb1.x1 - mb1.x0);
    bool slice1 = pdf_render_page_slice(mixed_doc, 1, scale1, 0, 0, 800, 600, dest, 0, 0);
    TEST_CHECK(slice1 == true, "Slice page 1 succeeded");
    fz_drop_pixmap(mixed_doc->ctx, dest);
    pdf_close(mixed_doc);
    printf(" PASSED\n");

    // Test 9: Zero-page rejection & Extreme dimensions (Audit #4, #20)
    printf("  [TEST 9] Zero-page PDF rejection & extreme dimension bounds safety...");
    fflush(stdout);
    char zero_pdf[512], extreme_pdf[512];
    snprintf(zero_pdf, sizeof(zero_pdf), "%s/zero.pdf", temp_dir);
    snprintf(extreme_pdf, sizeof(extreme_pdf), "%s/extreme.pdf", temp_dir);
    write_zero_page_pdf(zero_pdf);
    write_extreme_dim_pdf(extreme_pdf);

    pdf_doc_t *zero_doc = pdf_open(zero_pdf);
    TEST_CHECK(zero_doc == NULL, "Zero-page PDF must be rejected by pdf_open");

    pdf_doc_t *ext_doc = pdf_open(extreme_pdf);
    TEST_CHECK(ext_doc != NULL, "Extreme dimension PDF opened");
    fz_rect ext_b = pdf_page_bounds(ext_doc, 0);
    // Extreme height should be safely clamped to prevent float-cast overflow
    TEST_CHECK(ext_b.y1 - ext_b.y0 <= 50000.0f, "Extreme dimension clamped safely");
    pdf_close(ext_doc);
    printf(" PASSED\n");

    // Test 10: Bounds Check Overflow Safety (Audit #5)
    printf("  [TEST 10] Bounds check signed overflow immunity...");
    fflush(stdout);
    pdf_doc_t *doc_test = pdf_open(sample_pdf);
    fz_pixmap *small_dest = fz_new_pixmap_with_bbox(doc_test->ctx, fz_device_rgb(doc_test->ctx), fz_make_irect(0, 0, 100, 100), nullptr, 0);
    // Supplying INT_MAX must return false without signed overflow UB
    bool r_overflow = pdf_render_page_slice(doc_test, 0, 1.0, 0, 0, INT_MAX, INT_MAX, small_dest, INT_MAX - 10, INT_MAX - 10);
    TEST_CHECK(r_overflow == false, "INT_MAX parameters safely rejected");
    fz_drop_pixmap(doc_test->ctx, small_dest);
    pdf_close(doc_test);
    printf(" PASSED\n");

    // Test 11: Hostile Text Sanitization (Audit #1)
    printf("  [TEST 11] Terminal text sanitization on hostile sequences...");
    fflush(stdout);
    const char *hostile_input = "DocTitle\033]2;OWNED\007\r\n\t\033[2JInjectedText";
    char safe_output[256];
    sanitize_terminal_string(hostile_input, safe_output, sizeof(safe_output));
    TEST_CHECK(strchr(safe_output, '\033') == NULL, "ESC must be eradicated");
    TEST_CHECK(strchr(safe_output, '\007') == NULL, "BEL must be eradicated");
    TEST_CHECK(strchr(safe_output, '\r') == NULL, "CR must be eradicated");
    TEST_CHECK(strchr(safe_output, '\n') == NULL, "LF must be eradicated");
    TEST_CHECK(strstr(safe_output, "InjectedText") != NULL, "Legitimate text preserved");
    printf(" PASSED\n");

    // Test 12: UTF-8 Display Width and Truncation (Audit #25)
    printf("  [TEST 12] UTF-8 display column width and truncation...");
    fflush(stdout);
    const char *wide_str = "PDF 文書 🚀 Test";
    int w = utf8_str_width(wide_str);
    TEST_CHECK(w > (int)strlen("PDF   Test"), "Wide characters contribute multiple columns");
    char trunc_buf[64];
    utf8_truncate_by_width(wide_str, 8, trunc_buf, sizeof(trunc_buf));
    TEST_CHECK(utf8_str_width(trunc_buf) <= 8, "Truncated string fits in 8 columns");
    printf(" PASSED\n");

    // Clean up temporary test files
    unlink(sample_pdf);
    unlink(mixed_pdf);
    unlink(zero_pdf);
    unlink(extreme_pdf);
    rmdir(temp_dir);

    printf("====================================================\n");
    printf("===> ALL STRICTNESS UNIT TESTS PASSED (0 ERRORS)\n");
    printf("====================================================\n");
    return 0;
}
