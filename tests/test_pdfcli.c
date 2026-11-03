#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "pdfcli.h"

// Generate a valid minimal PDF file for testing
static void create_test_pdf(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    fprintf(f,
        "%%PDF-1.4\n"
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>\nendobj\n"
        "4 0 obj\n<< /Length %zu >>\nstream\nBT /F1 24 Tf 100 700 Td (%s) Tj ET\nendstream\nendobj\n"
        "5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n"
        "xref\n0 6\n"
        "0000000000 65535 f \n"
        "0000000009 00000 n \n"
        "0000000058 00000 n \n"
        "0000000115 00000 n \n"
        "0000000250 00000 n \n"
        "0000000350 00000 n \n"
        "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n450\n%%%%EOF\n",
        strlen(text) + 33, text);
    fclose(f);
}

int main(void) {
    printf("====================================================\n");
    printf("===> Running Comprehensive pdf-cli Strictness Suite\n");
    printf("====================================================\n");

    const char *test_pdf_path = "/tmp/test_pdfcli_sample.pdf";
    create_test_pdf(test_pdf_path, "Hello pdf-cli Ultra Fast");

    // Test 1: NULL Safety & Invalid File
    printf("  [TEST 1] NULL safety & invalid file handling...");
    fflush(stdout);
    assert(pdf_open(NULL) == NULL);
    assert(pdf_open("/nonexistent_path/should_fail.pdf") == NULL);
    pdf_close(NULL);
    fz_rect empty_b = pdf_page_bounds(NULL, 0);
    assert(empty_b.x0 == 0 && empty_b.x1 == 0);
    assert(pdf_render_viewport(NULL, 0, 1.0, 0, 100, 100, false) == NULL);
    size_t toc_cnt = 99;
    assert(pdf_get_toc(NULL, &toc_cnt) == NULL && toc_cnt == 0);
    pdf_free_toc(NULL, 0);
    size_t srch_cnt = 99;
    assert(pdf_search_text(NULL, "test", &srch_cnt) == NULL && srch_cnt == 0);
    printf(" PASSED\n");

    // Test 2: Valid Document Lifecycle
    printf("  [TEST 2] Document lifecycle & page bounds...");
    fflush(stdout);
    pdf_doc_t *doc = pdf_open(test_pdf_path);
    assert(doc != NULL);
    assert(doc->page_count == 1);
    fz_rect bounds = pdf_page_bounds(doc, 0);
    assert(bounds.x1 - bounds.x0 > 600.0f);
    assert(bounds.y1 - bounds.y0 > 700.0f);
    printf(" PASSED\n");

    // Test 3: Viewport Rasterization
    printf("  [TEST 3] Viewport rasterization at multiple scales...");
    fflush(stdout);
    fz_pixmap *pix1 = pdf_render_viewport(doc, 0, 1.0, 0, 800, 600, false);
    assert(pix1 != NULL);
    assert(pix1->w == 800);
    assert(pix1->h == 600);
    assert(pix1->n == 3);
    assert(pix1->samples != NULL);
    fz_drop_pixmap(doc->ctx, pix1);

    fz_pixmap *pix2 = pdf_render_viewport(doc, 0, 2.5, 100, 1920, 1080, false);
    assert(pix2 != NULL);
    assert(pix2->w == 1920);
    assert(pix2->h == 1080);
    fz_drop_pixmap(doc->ctx, pix2);
    printf(" PASSED\n");

    // Test 4: Dark Mode Pixel Inversion
    printf("  [TEST 4] Dark mode pixel inversion correctness...");
    fflush(stdout);
    fz_pixmap *pix_norm = pdf_render_viewport(doc, 0, 1.0, 0, 100, 100, false);
    fz_pixmap *pix_dark = pdf_render_viewport(doc, 0, 1.0, 0, 100, 100, true);
    assert(pix_norm != NULL && pix_dark != NULL);
    // Background pixel (top-left 0,0) in normal should be 255 (white); in dark should be 0 (black)
    assert(pix_norm->samples[0] == 255);
    assert(pix_dark->samples[0] == 0);
    assert((int)pix_norm->samples[0] + (int)pix_dark->samples[0] == 255);
    fz_drop_pixmap(doc->ctx, pix_norm);
    fz_drop_pixmap(doc->ctx, pix_dark);
    printf(" PASSED\n");

    // Test 5: In-Document Text Search
    printf("  [TEST 5] In-document text search engine...");
    fflush(stdout);
    size_t hits_found = 0;
    int *hits = pdf_search_text(doc, "Ultra", &hits_found);
    assert(hits != NULL);
    assert(hits_found == 1);
    assert(hits[0] == 0);
    free(hits);

    size_t no_hits = 99;
    int *absent = pdf_search_text(doc, "NonExistentWordXYZ", &no_hits);
    assert(absent == NULL || no_hits == 0);
    free(absent);
    printf(" PASSED\n");

    // Test 6: Table of Contents Outline
    printf("  [TEST 6] Table of contents extraction safety...");
    fflush(stdout);
    size_t out_cnt = 0;
    toc_item_t *toc = pdf_get_toc(doc, &out_cnt);
    // Minimal PDF has no outline
    assert(out_cnt == 0 || toc != NULL);
    pdf_free_toc(toc, out_cnt);
    printf(" PASSED\n");

    // Test 7: Sixel Encoding Pipeline
    printf("  [TEST 7] Sixel graphics encoding validation...");
    fflush(stdout);
    fz_pixmap *test_pix = pdf_render_viewport(doc, 0, 1.0, 0, 200, 150, false);
    assert(test_pix != NULL);
    // Mute stdout to avoid dumping Sixel escape stream in test runner
    int saved_stdout = dup(STDOUT_FILENO);
    FILE *devnull = fopen("/dev/null", "w");
    dup2(fileno(devnull), STDOUT_FILENO);
    bool sixel_ok = sixel_draw(test_pix, 1, 1);
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    fclose(devnull);
    assert(sixel_ok == true);
    fz_drop_pixmap(doc->ctx, test_pix);
    printf(" PASSED\n");

    pdf_close(doc);
    unlink(test_pdf_path);

    printf("====================================================\n");
    printf("===> ALL TESTS PASSED SUCCESSFULLY (0 ERRORS)\n");
    printf("====================================================\n");
    return 0;
}
