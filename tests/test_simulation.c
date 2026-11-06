#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <pty.h>
#include <utmp.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <termios.h>

#define SIM_CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "\nSIMULATION FAILED: %s (at %s:%d)\n", msg, __FILE__, __LINE__); \
        if (child_pid > 0) { kill(child_pid, SIGKILL); waitpid(child_pid, nullptr, 0); } \
        exit(1); \
    } \
} while (0)

static pid_t child_pid = -1;

static void write_sim_pdf(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open sim PDF %s\n", path);
        exit(1);
    }

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
    fprintf(f, "4 0 obj\n<< /Title (Section 1 - Introduction) /Parent 3 0 R /Next 7 0 R /Dest [5 0 R /Fit] >>\nendobj\n");

    // 5 0 obj: Page 1
    offsets[obj_idx++] = ftell(f);
    fprintf(f, "5 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 8 0 R /Resources << /Font << /F1 10 0 R >> >> >>\nendobj\n");

    // 6 0 obj: Page 2
    offsets[obj_idx++] = ftell(f);
    fprintf(f, "6 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 9 0 R /Resources << /Font << /F1 10 0 R >> >> >>\nendobj\n");

    // 7 0 obj: Outline Item 2
    offsets[obj_idx++] = ftell(f);
    fprintf(f, "7 0 obj\n<< /Title (Section 2 - Advanced Topics) /Parent 3 0 R /Prev 4 0 R /Dest [6 0 R /Fit] >>\nendobj\n");

    // 8 0 obj: Contents Page 1
    offsets[obj_idx++] = ftell(f);
    const char *text1 = "Simulation Page 1 - Continuous Stream";
    size_t s1_len = strlen(text1) + 33;
    fprintf(f, "8 0 obj\n<< /Length %zu >>\nstream\nBT /F1 24 Tf 100 700 Td (%s) Tj ET\nendstream\nendobj\n", s1_len, text1);

    // 9 0 obj: Contents Page 2
    offsets[obj_idx++] = ftell(f);
    const char *text2 = "Simulation Page 2 - Navigation Content";
    size_t s2_len = strlen(text2) + 33;
    fprintf(f, "9 0 obj\n<< /Length %zu >>\nstream\nBT /F1 24 Tf 100 700 Td (%s) Tj ET\nendstream\nendobj\n", s2_len, text2);

    // 10 0 obj: Standard Helvetica Font
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

static size_t drain_pty(int master_fd, char *buf, size_t max_len, int timeout_ms) {
    size_t total = 0;
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (total < max_len - 1) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms >= timeout_ms) break;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(master_fd, &fds);
        struct timeval tv = {
            .tv_sec = 0,
            .tv_usec = 30000
        };

        int ret = select(master_fd + 1, &fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(master_fd, &fds)) {
            ssize_t n = read(master_fd, buf + total, max_len - 1 - total);
            if (n <= 0) break;
            total += (size_t)n;
        } else if (ret == 0 && total > 0) {
            break;
        }
    }
    buf[total] = '\0';
    return total;
}

static void check_child_alive(void) {
    int status = 0;
    pid_t p = waitpid(child_pid, &status, WNOHANG);
    SIM_CHECK(p == 0, "pdf-cli process terminated prematurely");
}

int main(int argc, char **argv) {
    const char *bin_path = (argc > 1) ? argv[1] : "./pdf-cli";

    printf("====================================================\n");
    printf("===> Running Interactive PTY Terminal Simulation (C23)\n");
    printf("====================================================\n");

    char temp_dir[] = "/tmp/pdfcli_sim_XXXXXX";
    if (!mkdtemp(temp_dir)) {
        perror("mkdtemp");
        return 1;
    }

    char pdf_path[512];
    snprintf(pdf_path, sizeof(pdf_path), "%s/test_doc.pdf", temp_dir);
    write_sim_pdf(pdf_path);

    int master = -1, slave = -1;
    struct winsize ws = {
        .ws_row = 24,
        .ws_col = 80,
        .ws_xpixel = 720,
        .ws_ypixel = 432
    };

    if (openpty(&master, &slave, nullptr, nullptr, &ws) < 0) {
        perror("openpty");
        return 1;
    }

    child_pid = fork();
    if (child_pid < 0) {
        perror("fork");
        return 1;
    }

    if (child_pid == 0) {
        close(master);
        setsid();
        if (ioctl(slave, TIOCSCTTY, 0) < 0) {
            // Non-fatal if setsid took care
        }
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        close(slave);

        char *const args[] = { (char *)bin_path, pdf_path, nullptr };
        execv(bin_path, args);
        _exit(127);
    }

    close(slave);

    char out_buf[262144];

    // SIM 1: Wait for initial render
    printf("  [SIM 1] Waiting for initial viewer frame render...");
    fflush(stdout);
    size_t n = drain_pty(master, out_buf, sizeof(out_buf), 1000);
    check_child_alive();
    SIM_CHECK(n > 0, "No initial output received from viewer");
    SIM_CHECK(strstr(out_buf, "\033[24;1H[1/") == nullptr, "Bottom status bar unexpectedly rendered");
    printf(" PASSED\n");

    // SIM 2: Barrage of random mouse clicks and wheel events
    printf("  [SIM 2] Injecting barrage of random mouse clicks and wheel events...");
    fflush(stdout);
    const char *click_seqs[] = {
        "\033[<0;10;5M",   // Left press
        "\033[<0;10;5m",   // Left release
        "\033[<2;40;12M",  // Right press
        "\033[<2;40;12m",  // Right release
        "\033[<64;40;12M", // Wheel up
        "\033[<65;40;12M", // Wheel down
        "\033[<0;1;1M",    // Top-left corner
        "\033[<0;80;24M"   // Bottom-right corner
    };
    for (int rep = 0; rep < 5; rep++) {
        for (size_t s = 0; s < sizeof(click_seqs)/sizeof(click_seqs[0]); s++) {
            if (write(master, click_seqs[s], strlen(click_seqs[s])) < 0) {}
            usleep(10000);
            check_child_alive();
        }
    }
    drain_pty(master, out_buf, sizeof(out_buf), 300);
    check_child_alive();
    printf(" PASSED\n");

    // SIM 3: HUD toggle with 'i' and ESC
    printf("  [SIM 3] Testing top Tab Bar / HUD display toggling with 'i' and ESC...");
    fflush(stdout);
    if (write(master, "i", 1) < 0) {}
    drain_pty(master, out_buf, sizeof(out_buf), 400);
    check_child_alive();
    SIM_CHECK(strstr(out_buf, "\033[1;1H") != nullptr, "Top HUD not rendered at Row 1 on 'i'");
    SIM_CHECK(strstr(out_buf, "test_doc.pdf") != nullptr, "Tab name not found in top HUD");

    if (write(master, "\033", 1) < 0) {}
    usleep(50000);
    check_child_alive();
    drain_pty(master, out_buf, sizeof(out_buf), 200);
    printf(" PASSED\n");

    // SIM 4: Navigation and feature toggles
    printf("  [SIM 4] Testing next/prev page navigation and feature toggles...");
    fflush(stdout);
    const char *toggles = "npDf+-0";
    for (size_t i = 0; i < strlen(toggles); i++) {
        if (write(master, &toggles[i], 1) < 0) {}
        usleep(40000);
        check_child_alive();
    }
    drain_pty(master, out_buf, sizeof(out_buf), 400);
    printf(" PASSED\n");

    // SIM 5: Table of Contents modal and dismiss
    printf("  [SIM 5] Testing Table of Contents modal and cancellation...");
    fflush(stdout);
    if (write(master, "T", 1) < 0) {}
    drain_pty(master, out_buf, sizeof(out_buf), 500);
    check_child_alive();
    SIM_CHECK(strstr(out_buf, "Table of Contents") != nullptr, "TOC header not found in modal output");

    if (write(master, "q", 1) < 0) {}
    usleep(100000);
    check_child_alive();
    drain_pty(master, out_buf, sizeof(out_buf), 400);
    printf(" PASSED\n");

    // SIM 6: Input batching
    printf("  [SIM 6] Testing input batching and queue retention...");
    fflush(stdout);
    if (write(master, "jji", 3) < 0) {}
    usleep(100000);
    check_child_alive();
    drain_pty(master, out_buf, sizeof(out_buf), 500);
    SIM_CHECK(strstr(out_buf, "test_doc.pdf") != nullptr, "Batched 'i' command was dropped");
    printf(" PASSED\n");

    // SIM 7: Clean shutdown on 'q'
    printf("  [SIM 7] Verifying clean shutdown on 'q' and terminal restoration...");
    fflush(stdout);
    if (write(master, "q", 1) < 0) {}
    drain_pty(master, out_buf, sizeof(out_buf), 1000);

    int exit_status = 0;
    waitpid(child_pid, &exit_status, 0);
    child_pid = -1;
    SIM_CHECK(WIFEXITED(exit_status) && WEXITSTATUS(exit_status) == 0, "Expected clean exit code 0");
    SIM_CHECK(strstr(out_buf, "\033[?1049l") != nullptr, "Alternate screen buffer exit sequence not emitted");
    SIM_CHECK(strstr(out_buf, "\033]111\007") != nullptr, "Terminal background reset sequence not emitted");
    printf(" PASSED\n");

    close(master);
    unlink(pdf_path);
    rmdir(temp_dir);

    printf("====================================================\n");
    printf("===> ALL PTY TERMINAL SIMULATION TESTS PASSED!\n");
    printf("====================================================\n");
    return 0;
}
