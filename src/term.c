#include "pdfcli.h"
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>

static struct termios s_orig_termios;
static bool s_raw_active = false;
static volatile sig_atomic_t s_resized = 0;

static void on_sigwinch(int sig) { (void)sig; s_resized = 1; }

bool term_enter_raw(void) {
    if (s_raw_active) return true;
    if (tcgetattr(STDIN_FILENO, &s_orig_termios) < 0) return false;

    struct termios raw = s_orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) return false;

    // Alt screen, hide cursor, enable SGR 1006 mouse tracking
    (void)write(STDOUT_FILENO, "\033[?1049h\033[?25l\033[?1000h\033[?1006h", 28);

    struct sigaction sa = { .sa_handler = on_sigwinch, .sa_flags = SA_RESTART };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, nullptr);
    s_raw_active = true;
    return true;
}

void term_restore(void) {
    if (!s_raw_active) return;
    (void)write(STDOUT_FILENO, "\033[?1006l\033[?1000l\033[?25h\033[?1049l", 28);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_orig_termios);
    s_raw_active = false;
}

void term_get_size(int *cols, int *rows, int *px_w, int *px_h) {
    struct winsize ws = {0};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        if (cols) *cols = ws.ws_col;
        if (rows) *rows = ws.ws_row;
        if (px_w) *px_w = ws.ws_xpixel > 0 ? (int)ws.ws_xpixel : ((int)ws.ws_col * 9);
        if (px_h) *px_h = ws.ws_ypixel > 0 ? (int)ws.ws_ypixel : ((int)ws.ws_row * 18);
    } else {
        if (cols) *cols = 80;
        if (rows) *rows = 24;
        if (px_w) *px_w = 720;
        if (px_h) *px_h = 432;
    }
}

int term_read_key(int timeout_ms) {
    if (s_resized) { s_resized = 0; return 'r'; }

    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };
    if (poll(&pfd, 1, timeout_ms) <= 0) return KEY_NONE;

    char buf[128] = {0};
    ssize_t n = read(STDIN_FILENO, buf, 1);
    if (n <= 0) return KEY_NONE;

    if (buf[0] == '\033') {
        if (poll(&pfd, 1, 25) <= 0) return KEY_ESC;
        ssize_t n2 = read(STDIN_FILENO, buf + 1, sizeof(buf) - 2);
        if (n2 <= 0) return KEY_ESC;
        n += n2;

        if (buf[1] >= '1' && buf[1] <= '9') return KEY_CTRL_1 + (buf[1] - '1');

        if (buf[1] == '[') {
            if (buf[2] == '<') { // SGR mouse
                while (!strpbrk(buf + 3, "Mm") && n < (ssize_t)sizeof(buf) - 1) {
                    if (poll(&pfd, 1, 20) <= 0) break;
                    ssize_t more = read(STDIN_FILENO, buf + n, 1);
                    if (more <= 0) break;
                    buf[++n] = '\0';
                }
                int btn = 0, col = 0, row = 0;
                char type = 0;
                if (sscanf(buf + 3, "%d;%d;%d%c", &btn, &col, &row, &type) >= 3) {
                    if (btn == 64) return KEY_WHEEL_UP;
                    if (btn == 65) return KEY_WHEEL_DOWN;
                }
                return KEY_NONE; // Safe ignore click/release/motion
            }
            if (buf[2] == 'M') { // Legacy Xterm mouse
                while (n < 6) {
                    if (poll(&pfd, 1, 20) <= 0) break;
                    ssize_t more = read(STDIN_FILENO, buf + n, 6 - n);
                    if (more <= 0) break;
                    n += more;
                }
                if (n >= 6) {
                    unsigned char cb = (unsigned char)buf[3];
                    if (cb == 96) return KEY_WHEEL_UP;
                    if (cb == 97) return KEY_WHEEL_DOWN;
                }
                return KEY_NONE;
            }
            if (buf[2] == 'A') return KEY_UP;
            if (buf[2] == 'B') return KEY_DOWN;
            if (buf[2] == 'C') return KEY_RIGHT;
            if (buf[2] == 'D') return KEY_LEFT;
            if (buf[2] == 'H' || (buf[2] == '1' && buf[3] == '~')) return KEY_HOME;
            if (buf[2] == 'F' || (buf[2] == '4' && buf[3] == '~')) return KEY_END;
            if (buf[2] == '5' && buf[3] == '~') return KEY_PGUP;
            if (buf[2] == '6' && buf[3] == '~') return KEY_PGDN;

            int p1 = 0, p2 = 0, p3 = 0;
            int matched = sscanf(buf + 2, "%d;%d;%d", &p1, &p2, &p3);
            if (matched == 3 && p1 == 27 && p2 == 5) {
                if (p3 >= '1' && p3 <= '9') return KEY_CTRL_1 + (p3 - '1');
                if (p3 == '\t' || p3 == 9) return KEY_CTRL_TAB;
            } else if (matched >= 2 && p2 == 5) {
                if (p1 >= '1' && p1 <= '9') return KEY_CTRL_1 + (p1 - '1');
                if (p1 == 9) return KEY_CTRL_TAB;
                if (p1 == 5) return KEY_PGUP;
                if (p1 == 6) return KEY_PGDN;
            }
        }
        return KEY_NONE;
    }
    return (unsigned char)buf[0];
}
