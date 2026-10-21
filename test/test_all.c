#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <math.h>

#include "pdfcli/types.h"
#include "pdfcli/utf8_util.h"
#include "pdfcli/fuzzy_match.h"
#include "pdfcli/file_search.h"
#include "pdfcli/arena.h"
#include "pdfcli/cache.h"
#include "pdfcli/doc_engine.h"
#include "pdfcli/img_util.h"
#include "pdfcli/term_gfx.h"
#include "pdfcli/config.h"

static void test_utf8(void) {
    printf("[1/7] Testing UTF-8 and Display Width Subsystem...\n");
    uint32_t cp = 0;
    size_t len = 0;

    assert(utf8_decode("A", &cp, &len) == 0 && cp == 'A' && len == 1);
    assert(utf8_decode("€", &cp, &len) == 0 && cp == 0x20AC && len == 3);
    assert(utf8_decode("中", &cp, &len) == 0 && cp == 0x4E2D && len == 3);

    assert(utf8_width('A') == 1);
    assert(utf8_width(0x4E2D) == 2); // Chinese character is double-width
    assert(utf8_width(0x1F600) == 2); // Grinning face emoji is double-width

    const char *ansi_str = "\033[1;32mHello\033[0m \033[34mWorld\033[0m";
    assert(plain_length(ansi_str) == 11);
    assert(str_display_width(ansi_str) == 11);

    char *centered = center_text("Test", 10);
    assert(centered != nullptr);
    assert((int)strlen(centered) >= 4);
    free(centered);
    printf("  UTF-8 tests passed!\n");
}

static void test_fuzzy(void) {
    printf("[2/7] Testing Fuzzy Matching Subsystem...\n");
    assert(fuzzy_has_match("pdf", "my_document.pdf"));
    assert(fuzzy_has_match("doc", "my_document.pdf"));
    assert(!fuzzy_has_match("xyz", "my_document.pdf"));

    double s1 = fuzzy_score("pdf", "document.pdf");
    double s2 = fuzzy_score("pdf", "page_data_file.txt");
    assert(s1 > s2); // "document.pdf" has consecutive match bonus and extension bonus

    size_t pos[16];
    double score = fuzzy_match_positions("pdf", "document.pdf", pos, 16);
    assert(score > 0);
    assert(pos[0] == 9 && pos[1] == 10 && pos[2] == 11);
    printf("  Fuzzy match tests passed!\n");
}

static void test_arena(void) {
    printf("[3/7] Testing Arena Memory Allocator...\n");
    arena *a = arena_create(4096);
    assert(a != nullptr);

    void *p1 = arena_alloc(a, 64);
    assert(p1 != nullptr);
    assert(((uintptr_t)p1 % 8) == 0); // 8-byte aligned

    char *str = arena_strdup(a, "Hello C23 Arena");
    assert(str != nullptr);
    assert(strcmp(str, "Hello C23 Arena") == 0);

    assert(arena_used(a) > 64);
    arena_reset(a);
    assert(arena_used(a) == 0);

    arena_destroy(a);
    printf("  Arena tests passed!\n");
}

static void test_file_search(void) {
    printf("[4/7] Testing File Discovery...\n");
    file_list *list = file_search_dir(".");
    assert(list != nullptr);
    assert(list->count > 0);

    bool found_pdf = false;
    for (size_t i = 0; i < list->count; i++) {
        if (strstr(list->entries[i].path, "test.pdf") != nullptr) {
            found_pdf = true;
            break;
        }
    }
    file_list_free(list);
    printf("  File discovery tests passed! (Found documents in directory tree: %s)\n", found_pdf ? "yes" : "none in root");
}

static void test_doc_engine(void) {
    printf("[5/7] Testing MuPDF Fitz Engine & Document Operations...\n");
    const char *test_pdf = "depends/go-fitz/testdata/test.pdf";
    if (access(test_pdf, R_OK) != 0) {
        printf("  Skipping test.pdf (file not found)\n");
        return;
    }

    doc_handle *doc = doc_open(test_pdf);
    assert(doc != nullptr);

    int pages = doc_get_page_count(doc);
    assert(pages > 0);
    printf("  Document loaded: %d pages\n", pages);

    fz_rect bounds = doc_get_page_bounds(doc, 0);
    assert(bounds.x1 > bounds.x0 && bounds.y1 > bounds.y0);

    fz_pixmap *pix = doc_render_page_pixmap(doc, 0, 1.0, 72.0);
    assert(pix != nullptr);
    assert(pix->w > 0 && pix->h > 0);

    fz_context *ctx = doc_get_context(doc);

    // Test Image manipulation
    crop_box crop = {.top = 0.05, .bottom = 0.05, .left = 0.05, .right = 0.05};
    fz_pixmap *cropped = img_crop(ctx, pix, &crop);
    assert(cropped != nullptr);
    assert(cropped->w < pix->w && cropped->h < pix->h);

    img_smart_invert(ctx, cropped);
    img_dim_tint(ctx, cropped);

    size_t png_size = 0;
    unsigned char *png_bytes = img_save_png_to_memory(ctx, cropped, &png_size);
    assert(png_bytes != nullptr && png_size > 0);
    free(png_bytes);

    fz_drop_pixmap(ctx, cropped);
    fz_drop_pixmap(ctx, pix);

    // Test TOC
    size_t toc_count = 0;
    doc_toc_entry *toc = doc_get_toc(doc, &toc_count);
    if (toc) {
        printf("  Extracted %zu TOC entries from document\n", toc_count);
        doc_free_toc(toc, toc_count);
    }

    doc_close(doc);
    printf("  Doc engine & image processing tests passed!\n");
}

static void test_cache(void) {
    printf("[6/7] Testing Thread-Safe LRU Render Cache...\n");
    page_cache *c = cache_create(2); // Max 2 entries
    assert(c != nullptr);

    fz_context *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    assert(ctx != nullptr);

    fz_pixmap *p1 = fz_new_pixmap(ctx, fz_device_rgb(ctx), 10, 10, nullptr, 0);
    fz_pixmap *p2 = fz_new_pixmap(ctx, fz_device_rgb(ctx), 10, 10, nullptr, 0);
    fz_pixmap *p3 = fz_new_pixmap(ctx, fz_device_rgb(ctx), 10, 10, nullptr, 0);

    cache_put(c, ctx, "key1", p1, "/tmp/p1.png");
    cache_put(c, ctx, "key2", p2, "/tmp/p2.png");

    fz_pixmap *g1 = cache_get(c, ctx, "key1");
    assert(g1 != nullptr);
    fz_drop_pixmap(ctx, g1);

    // Adding key3 should evict key2 (since key1 was accessed and became MRU)
    cache_put(c, ctx, "key3", p3, "/tmp/p3.png");

    fz_pixmap *g2 = cache_get(c, ctx, "key2");
    assert(g2 == nullptr); // Evicted!

    fz_pixmap *g3 = cache_get(c, ctx, "key3");
    assert(g3 != nullptr);
    fz_drop_pixmap(ctx, g3);

    fz_drop_pixmap(ctx, p1);
    fz_drop_pixmap(ctx, p2);
    fz_drop_pixmap(ctx, p3);

    cache_destroy(c, ctx);
    fz_drop_context(ctx);
    printf("  LRU Cache tests passed!\n");
}

static void test_term_gfx(void) {
    printf("[7/7] Testing Terminal Graphics Context...\n");
    term_gfx_context *gfx = term_gfx_init();
    assert(gfx != nullptr);

    uint32_t id1 = term_gfx_next_image_id();
    uint32_t id2 = term_gfx_next_image_id();
    assert(id2 == id1 + 1);

    term_gfx_destroy(gfx);
    printf("  Terminal graphics tests passed!\n");
}

static void test_key_parse(const char *seq, int expected_key) {
    int pfd[2];
    assert(pipe(pfd) == 0);
    size_t len = strlen(seq);
    assert(write(pfd[1], seq, len) == (ssize_t)len);
    char buf[32] = {};
    int mouse_c = 0, mouse_r = 0;
    int k = term_read_key(pfd[0], buf, sizeof(buf), &mouse_c, &mouse_r);
    close(pfd[0]);
    close(pfd[1]);
    if (k != expected_key) {
        fprintf(stderr, "test_key_parse failed: seq='%s', got=%d (0x%X), expected=%d (0x%X)\n",
                seq, k, k, expected_key, expected_key);
    }
    assert(k == expected_key);
}

static void test_term_input(void) {
    printf("[8/8] Testing Terminal Input & Key Parsing Subsystem...\n");
    // Kitty progressive keyboard sequences
    test_key_parse("\033[57354u", KEY_PAGE_UP);
    test_key_parse("\033[57355u", KEY_PAGE_DOWN);
    test_key_parse("\033[57352u", KEY_ARROW_UP);
    test_key_parse("\033[57353u", KEY_ARROW_DOWN);
    test_key_parse("\033[57350u", KEY_ARROW_LEFT);
    test_key_parse("\033[57351u", KEY_ARROW_RIGHT);

    // Standard VT / xterm escape sequences
    test_key_parse("\033[5~", KEY_PAGE_UP);
    test_key_parse("\033[6~", KEY_PAGE_DOWN);
    test_key_parse("\033[A", KEY_ARROW_UP);
    test_key_parse("\033[B", KEY_ARROW_DOWN);
    test_key_parse("\033[C", KEY_ARROW_RIGHT);
    test_key_parse("\033[D", KEY_ARROW_LEFT);

    // SS3 sequences
    test_key_parse("\033OA", KEY_ARROW_UP);
    test_key_parse("\033OB", KEY_ARROW_DOWN);
    test_key_parse("\033OC", KEY_ARROW_RIGHT);
    test_key_parse("\033OD", KEY_ARROW_LEFT);

    // SGR Mouse wheel events
    test_key_parse("\033[<64;15;25M", KEY_MOUSE_WHEEL_UP);
    test_key_parse("\033[<65;15;25M", KEY_MOUSE_WHEEL_DOWN);

    // Kitty Keyboard Protocol (CSI u) Tab shortcuts
    test_key_parse("\033[49;5u", KEY_CTRL_1);
    test_key_parse("\033[50;5u", KEY_CTRL_2);
    test_key_parse("\033[57;5u", KEY_CTRL_9);
    test_key_parse("\033[48;5u", KEY_CTRL_0);
    test_key_parse("\033[9;5u", KEY_CTRL_TAB);
    test_key_parse("\033[9;6u", KEY_CTRL_SHIFT_TAB);
    test_key_parse("\033[119;5u", KEY_CTRL_W);
    test_key_parse("\033[116;5u", KEY_CTRL_T);

    // XTerm modifyOtherKeys (Mode 2) Tab shortcuts
    test_key_parse("\033[27;5;49~", KEY_CTRL_1);
    test_key_parse("\033[27;5;50~", KEY_CTRL_2);
    test_key_parse("\033[27;5;57~", KEY_CTRL_9);
    test_key_parse("\033[27;5;48~", KEY_CTRL_0);
    test_key_parse("\033[27;5;9~", KEY_CTRL_TAB);
    test_key_parse("\033[27;6;9~", KEY_CTRL_SHIFT_TAB);
    test_key_parse("\033[27;5;119~", KEY_CTRL_W);
    test_key_parse("\033[27;5;116~", KEY_CTRL_T);

    // Standard Shift+Tab (backtab)
    test_key_parse("\033[Z", KEY_CTRL_SHIFT_TAB);

    // Alt+1 .. Alt+9, Alt+0 universal fallbacks
    test_key_parse("\0331", KEY_CTRL_1);
    test_key_parse("\0332", KEY_CTRL_2);
    test_key_parse("\0339", KEY_CTRL_9);
    test_key_parse("\0330", KEY_CTRL_0);

    printf("  Terminal input & key parsing tests passed!\n");
}

int main(void) {
    printf("============================================================\n");
    printf("  pdf-cli (C23 ISO/IEC 9899:2024) Full Regression Test Suite\n");
    printf("============================================================\n");

    test_utf8();
    test_fuzzy();
    test_arena();
    test_file_search();
    test_doc_engine();
    test_cache();
    test_term_gfx();
    test_term_input();

    printf("\n>>> ALL 8 CORE SUBSYSTEM TESTS PASSED WITH 100%% SUCCESS! <<<\n");
    return 0;
}
