#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "pdfcli/config.h"
#include "pdfcli/ctl_proto.h"
#include "pdfcli/ctl_server.h"
#include "pdfcli/ctl_client.h"

static void test_config(void) {
    printf("[1/4] Testing config_save and config_load...\n");
    const char *test_doc = "/tmp/pdfcli_test_document.pdf";

    doc_config orig = {
        .fit_mode = "width",
        .scale_factor = 1.35,
        .dark_mode = "dim",
        .dual_page_mode = "half",
        .force_mode = "image",
        .html_page_width = 1250,
        .crop = {
            .top = 0.04,
            .bottom = 0.05,
            .left = 0.06,
            .right = 0.07
        }
    };

    config_save(test_doc, &orig);

    doc_config loaded = config_load(test_doc);
    assert(strcmp(loaded.fit_mode, "width") == 0);
    assert(loaded.scale_factor > 1.34 && loaded.scale_factor < 1.36);
    assert(strcmp(loaded.dark_mode, "dim") == 0);
    assert(strcmp(loaded.dual_page_mode, "half") == 0);
    assert(strcmp(loaded.force_mode, "image") == 0);
    assert(loaded.html_page_width == 1250);
    assert(loaded.crop.top > 0.039 && loaded.crop.top < 0.041);
    assert(loaded.crop.bottom > 0.049 && loaded.crop.bottom < 0.051);
    assert(loaded.crop.left > 0.059 && loaded.crop.left < 0.061);
    assert(loaded.crop.right > 0.069 && loaded.crop.right < 0.071);

    // Test non-existent doc fallback
    doc_config def = config_load("/tmp/non_existent_random_doc_xyz.pdf");
    assert(strcmp(def.fit_mode, "height") == 0);
    assert(def.scale_factor == 1.0);
    assert(def.html_page_width == 1000);

    printf("  Config tests passed!\n");
}

static void test_ctl_proto(void) {
    printf("[2/4] Testing ctl_proto serialization & parsing...\n");

    // 1. State serialization & parsing
    ctl_viewer_state st = {
        .pid = 4242,
        .path = "/home/user/doc.pdf",
        .name = "doc.pdf",
        .file_type = "pdf",
        .page = 5,
        .pages = 100,
        .fit = "height",
        .tint = "dark",
        .view = "auto",
        .dual = "half",
        .half = "bottom",
        .zoom = 1.25,
        .html_width = 1000,
        .crop = { .top = 0.02, .bottom = 0.02, .left = 0.01, .right = 0.01 },
        .search = "needle",
        .search_hits = 12,
        .search_index = 3,
        .reflowable = false,
        .is_image = false,
        .tab_count = 3,
        .active_tab = 2,
        .agterm_session = "sess-99",
        .socket = "/tmp/4242.sock"
    };

    char *state_json = ctl_serialize_state_response(true, nullptr, &st);
    assert(state_json != nullptr);

    ctl_viewer_state parsed_st = {};
    bool ok = ctl_parse_state_response(state_json, &parsed_st);
    assert(ok);
    assert(parsed_st.pid == 4242);
    assert(strcmp(parsed_st.name, "doc.pdf") == 0);
    assert(parsed_st.page == 5);
    assert(parsed_st.pages == 100);
    assert(strcmp(parsed_st.tint, "dark") == 0);
    assert(strcmp(parsed_st.half, "bottom") == 0);
    assert(parsed_st.zoom > 1.24 && parsed_st.zoom < 1.26);
    assert(parsed_st.search_hits == 12);
    assert(parsed_st.tab_count == 3);
    assert(parsed_st.active_tab == 2);
    free(state_json);

    // 2. TOC serialization & parsing
    doc_toc_entry toc[2] = {
        { .level = 1, .title = "Chapter 1", .page = 1, .x = 0, .y = 0 },
        { .level = 2, .title = "Section 1.1", .page = 3, .x = 0, .y = 0 }
    };
    char *toc_json = ctl_serialize_toc_response(true, nullptr, toc, 2);
    assert(toc_json != nullptr);

    doc_toc_entry *parsed_toc = nullptr;
    size_t toc_count = 0;
    ok = ctl_parse_toc_response(toc_json, &parsed_toc, &toc_count);
    assert(ok);
    assert(toc_count == 2);
    assert(parsed_toc[0].level == 1);
    assert(strcmp(parsed_toc[0].title, "Chapter 1") == 0);
    assert(parsed_toc[0].page == 1);
    assert(parsed_toc[1].level == 2);
    assert(strcmp(parsed_toc[1].title, "Section 1.1") == 0);
    assert(parsed_toc[1].page == 3);
    for (size_t i = 0; i < toc_count; ++i) {
        free(parsed_toc[i].title);
    }
    free(parsed_toc);
    free(toc_json);

    // 3. Request parsing
    const char *req_str = "{\"cmd\":\"set\",\"args\":{\"zoom\":\"1.5\",\"tint\":\"dim\"}}";
    char cmd[64] = {};
    char args[512] = {};
    ok = ctl_parse_request(req_str, cmd, sizeof(cmd), args, sizeof(args));
    assert(ok);
    assert(strcmp(cmd, "set") == 0);

    char val[64] = {};
    assert(ctl_args_get(args, "zoom", val, sizeof(val)) != nullptr);
    assert(strcmp(val, "1.5") == 0);
    assert(ctl_args_get(args, "tint", val, sizeof(val)) != nullptr);
    assert(strcmp(val, "dim") == 0);

    printf("  ctl_proto tests passed!\n");
}

static void test_presets(void) {
    printf("[3/4] Testing presets listing via dvctl...\n");
    char *argv[] = { "dvctl", "presets", "--json" };
    int rc = ctl_client_main(3, argv);
    assert(rc == 0);
    printf("  Presets test passed!\n");
}

static void test_ctl_server_client_roundtrip(void) {
    printf("[4/4] Testing ctl_server & ctl_client IPC roundtrip...\n");
    int test_pid = 987654;
    ctl_server *srv = ctl_server_create(test_pid);
    assert(srv != nullptr);
    assert(ctl_server_get_fd(srv) >= 0);

    // Test dvctl list in a child process or thread, while server answers status
    pid_t pid = fork();
    if (pid == 0) {
        // Child: run dvctl status --target 987654 --json
        char *argv[] = { "dvctl", "status", "--target", "987654", "--json" };
        int rc = ctl_client_main(5, argv);
        exit(rc);
    }

    // Parent: handle incoming connection
    int client_fd = -1;
    char cmd[64] = {};
    char args[512] = {};
    bool received = false;

    for (int retry = 0; retry < 50; ++retry) {
        if (ctl_server_recv(srv, &client_fd, cmd, sizeof(cmd), args, sizeof(args))) {
            received = true;
            break;
        }
        usleep(20000); // 20ms
    }
    assert(received);
    assert(strcmp(cmd, "status") == 0);

    ctl_viewer_state st = {
        .pid = test_pid,
        .path = "/test/ipc.pdf",
        .name = "ipc.pdf",
        .page = 1,
        .pages = 42,
        .fit = "width",
        .tint = "off",
        .zoom = 1.0
    };
    char *resp = ctl_serialize_state_response(true, nullptr, &st);
    ctl_server_send_response(client_fd, resp);
    free(resp);

    int status = 0;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    // Clean up server
    ctl_server_destroy(srv);

    // Test dead socket cleanup
    char dead_sock[1024] = {};
    ctl_get_socket_path(test_pid, dead_sock, sizeof(dead_sock));
    // Create a dead socket file (touch it)
    FILE *f = fopen(dead_sock, "w");
    if (f) {
        fclose(f);
        assert(access(dead_sock, F_OK) == 0);
        // dvctl list will connect to dead_sock, get ECONNREFUSED, and unlink it
        char *argv[] = { "dvctl", "list" };
        int rc_list = ctl_client_main(2, argv);
        (void)rc_list;
        // Verify dead socket was unlinked!
        assert(access(dead_sock, F_OK) != 0);
    }

    printf("  IPC roundtrip and dead socket cleanup tests passed!\n");
}

int main(void) {
    printf("=== Running Subagent 5 IPC, dvctl & Config Test Suite ===\n");
    test_config();
    test_ctl_proto();
    test_presets();
    test_ctl_server_client_roundtrip();
    printf("=== ALL TESTS PASSED SUCCESSFULLY! ===\n");
    return 0;
}
