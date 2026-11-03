#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "pdfcli.h"

// Generate a valid multi-page PDF file for testing
static void create_test_pdf(const char *path, const char *text1, const char *text2) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    fprintf(f,
        "%%PDF-1.4\n"
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>\nendobj\n"
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 5 0 R /Resources << /Font << /F1 7 0 R >> >> >>\nendobj\n"
        "4 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 6 0 R /Resources << /Font << /F1 7 0 R >> >> >>\nendobj\n"
        "5 0 obj\n<< /Length %zu >>\nstream\nBT /F1 24 Tf 100 700 Td (%s) Tj ET\nendstream\nendobj\n"
        "6 0 obj\n<< /Length %zu >>\nstream\nBT /F1 24 Tf 100 700 Td (%s) Tj ET\nendstream\nendobj\n"
        "7 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n"
        "xref\n0 8\n"
        "0000000000 65535 f \n"
        "0000000009 00000 n \n"
        "0000000058 00000 n \n"
        "0000000125 00000 n \n"
        "0000000250 00000 n \n"
        "0000000375 00000 n \n"
        "0000000500 00000 n \n"
        "0000000625 00000 n \n"
        "trailer\n<< /Size 8 /Root 1 0 R >>\nstartxref\n725\n%%%%EOF\n",
        strlen(text1) + 33, text1,
        strlen(text2) + 33, text2);
    fclose(f);
}

int main(void) {
    printf("====================================================\n");
    printf("===> Running Comprehensive pdf-cli Strictness Suite\n");
    printf("====================================================\n");

    const char *test_pdf_path = "/tmp/test_pdfcli_sample.pdf";
    create_test_pdf(test_pdf_path, "Page One - Hello Continuous", "Page Two - Moving Smoothly");

    // Test 1: NULL Safety & Invalid File
    printf("  [TEST 1] NULL safety & invalid file handling...");
    fflush(stdout);
    assert(pdf_open(NULL) == NULL);
    assert(pdf_open("/nonexistent_path/should_fail.pdf") == NULL);
    pdf_close(NULL);
    fz_rect empty_b = pdf_page_bounds(NULL, 0);
    assert(empty_b.x0 == 0 && empty_b.x1 == 0);
    assert(!pdf_render_page_slice(NULL, 0, 1.0, 0, 0, 100, 100, NULL, 0, 0, false));
    size_t toc_cnt = 99;
    assert(pdf_get_toc(NULL, &toc_cnt) == NULL && toc_cnt == 0);
    pdf_free_toc(NULL, 0);
    size_t srch_cnt = 99;
    assert(pdf_search_text(NULL, "test", &srch_cnt) == NULL && srch_cnt == 0);
    printf(" PASSED\n");

    // Test 2: Valid Multi-Page Document Lifecycle
    printf("  [TEST 2] Multi-page document lifecycle & page bounds...");
    fflush(stdout);
    pdf_doc_t *doc = pdf_open(test_pdf_path);
    assert(doc != NULL);
    assert(doc->page_count == 2);
    fz_rect b0 = pdf_page_bounds(doc, 0);
    fz_rect b1 = pdf_page_bounds(doc, 1);
    assert(b0.x1 - b0.x0 > 600.0f);
    assert(b1.y1 - b1.y0 > 700.0f);
    printf(" PASSED\n");

    // Test 3: Continuous Multi-Page Composite Slicing
    printf("  [TEST 3] Continuous multi-page composite slice rendering...");
    fflush(stdout);
    int vw = 800, vh = 600;
    fz_pixmap *comp = fz_new_pixmap_with_bbox(doc->ctx, fz_device_rgb(doc->ctx), fz_make_irect(0, 0, vw, vh), NULL, 0);
    assert(comp != NULL);
    fz_clear_pixmap_with_value(doc->ctx, comp, 0x18);

    // Page 0 bottom slice (height 300) into top of comp (dst_y = 0)
    bool ok0 = pdf_render_page_slice(doc, 0, 1.0, 0, 400, vw, 300, comp, 0, 0, false);
    assert(ok0 == true);

    // Page 1 top slice (height 280) into bottom of comp (dst_y = 320, 20px gap)
    bool ok1 = pdf_render_page_slice(doc, 1, 1.0, 0, 0, vw, 280, comp, 0, 320, false);
    assert(ok1 == true);

    // Verify samples exist and were rendered
    assert(comp->samples != NULL);
    // Page 0 white background in comp should be 255
    assert(comp->samples[0] == 255);
    // Gap area between dst_y 300 and 320 should have background value 0x18
    unsigned char *gap_sample = comp->samples + (ptrdiff_t)310 * comp->stride;
    assert(gap_sample[0] == 0x18);
    // Page 1 white background at dst_y 320 should be 255
    unsigned char *p1_sample = comp->samples + (ptrdiff_t)325 * comp->stride;
    assert(p1_sample[0] == 255);

    printf(" PASSED\n");

    // Test 4: Dark Mode Pixel Inversion on Slices
    printf("  [TEST 4] Dark mode slice pixel inversion correctness...");
    fflush(stdout);
    fz_pixmap *comp_dark = fz_new_pixmap_with_bbox(doc->ctx, fz_device_rgb(doc->ctx), fz_make_irect(0, 0, 100, 100), NULL, 0);
    assert(comp_dark != NULL);
    fz_clear_pixmap_with_value(doc->ctx, comp_dark, 0x12);
    bool ok_dark = pdf_render_page_slice(doc, 0, 1.0, 0, 0, 100, 100, comp_dark, 0, 0, true);
    assert(ok_dark == true);
    // Inverted white background should be 0 (black)
    assert(comp_dark->samples[0] == 0);
    fz_drop_pixmap(doc->ctx, comp_dark);
    printf(" PASSED\n");

    // Test 5: In-Document Text Search
    printf("  [TEST 5] In-document text search across multiple pages...");
    fflush(stdout);
    size_t hits_found = 0;
    int *hits = pdf_search_text(doc, "Continuous", &hits_found);
    assert(hits != NULL && hits_found == 1 && hits[0] == 0);
    free(hits);

    int *hits2 = pdf_search_text(doc, "Smoothly", &hits_found);
    assert(hits2 != NULL && hits_found == 1 && hits2[0] == 1);
    free(hits2);
    printf(" PASSED\n");

    // Test 6: Sixel Graphics Pipeline Output
    printf("  [TEST 6] Sixel encoding pipeline on composite pixmap...");
    fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    FILE *devnull = fopen("/dev/null", "w");
    dup2(fileno(devnull), STDOUT_FILENO);
    bool sixel_ok = sixel_draw(comp, 1, 1);
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    fclose(devnull);
    assert(sixel_ok == true);
    fz_drop_pixmap(doc->ctx, comp);
    printf(" PASSED\n");

    pdf_close(doc);
    unlink(test_pdf_path);

    printf("====================================================\n");
    printf("===> ALL TESTS PASSED SUCCESSFULLY (0 ERRORS)\n");
    printf("====================================================\n");
    return 0;
}
