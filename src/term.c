#include "pdfcli.h"
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

static struct termios s_orig_termios;
static volatile sig_atomic_t s_raw_active = 0;
static volatile sig_atomic_t s_resized = 0;
static volatile sig_atomic_t s_suspended = 0;

#define IN_BUF_CAP 1024
static unsigned char s_in_buf[IN_BUF_CAP];
static size_t s_in_len = 0;

static void in_buf_consume(size_t n) {
    if (n >= s_in_len) {
        s_in_len = 0;
    } else {
        memmove(s_in_buf, s_in_buf + n, s_in_len - n);
        s_in_len -= n;
    }
}

static void on_sigwinch(int sig) {
    (void)sig;
    s_resized = 1;
}

static void on_term_signal(int sig) {
    if (s_raw_active) {
        const char cleanup[] = "\033]111\007\033[?2026l\033[?7h\033[?1006l\033[?1000l\033[?25h\033[?1049l";
        (void)write(STDOUT_FILENO, cleanup, sizeof(cleanup) - 1);
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &s_orig_termios);
        s_raw_active = 0;
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
    raise(sig);
}

static void on_sigtstp(int sig) {
    (void)sig;
    if (s_raw_active) {
        const char cleanup[] = "\033]111\007\033[?2026l\033[?7h\033[?1006l\033[?1000l\033[?25h\033[?1049l";
        (void)write(STDOUT_FILENO, cleanup, sizeof(cleanup) - 1);
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &s_orig_termios);
        s_raw_active = 0;
        s_suspended = 1;
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTSTP, &sa, nullptr);
    raise(SIGTSTP);
}

static void on_sigcont(int sig) {
    (void)sig;
    if (s_suspended) {
        term_enter_raw();
        s_suspended = 0;
        s_resized = 1;
    }
}

bool term_enter_raw(void) {
    if (s_raw_active) return true;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return false;

    sigset_t set, oldset;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGWINCH);
    sigaddset(&set, SIGTSTP);
    sigprocmask(SIG_BLOCK, &set, &oldset);

    if (tcgetattr(STDIN_FILENO, &s_orig_termios) < 0) {
        sigprocmask(SIG_SETMASK, &oldset, nullptr);
        return false;
    }

    struct termios raw = s_orig_termios;
    raw.c_iflag &= (tcflag_t)~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= (tcflag_t)~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
        sigprocmask(SIG_SETMASK, &oldset, nullptr);
        return false;
    }

    const char init_seq[] = "\033[?1049h\033[?7l\033[?25l\033[?1000h\033[?1006h\033]11;#181818\007";
    (void)write(STDOUT_FILENO, init_seq, sizeof(init_seq) - 1);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term_signal;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGTERM);
    sigaddset(&sa.sa_mask, SIGHUP);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    sa.sa_handler = on_sigwinch;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, nullptr);

    sa.sa_handler = on_sigtstp;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTSTP, &sa, nullptr);

    sa.sa_handler = on_sigcont;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCONT, &sa, nullptr);

    s_raw_active = 1;
    sigprocmask(SIG_SETMASK, &oldset, nullptr);
    return true;
}

void term_restore(void) {
    if (!s_raw_active) return;

    sigset_t set, oldset;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGWINCH);
    sigaddset(&set, SIGTSTP);
    sigprocmask(SIG_BLOCK, &set, &oldset);

    const char cleanup[] = "\033]111\007\033[?2026l\033[?7h\033[?1006l\033[?1000l\033[?25h\033[?1049l";
    (void)write(STDOUT_FILENO, cleanup, sizeof(cleanup) - 1);
    (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_orig_termios);
    s_raw_active = 0;

    sigprocmask(SIG_SETMASK, &oldset, nullptr);
}

void term_sync_begin(void) {
    fputs("\033[?2026h", stdout);
}

void term_sync_end(void) {
    fputs("\033[?2026l", stdout);
    fflush(stdout);
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

static int fill_in_buf(int timeout_ms) {
    if (s_in_len >= sizeof(s_in_buf)) return 1;

    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }
    if (ret == 0) return 0;

    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        return -1;
    }

    if (pfd.revents & POLLIN) {
        ssize_t n = read(STDIN_FILENO, s_in_buf + s_in_len, sizeof(s_in_buf) - s_in_len);
        if (n == 0) return -1;
        if (n < 0) {
            if (errno == EINTR) return 0;
            return -1;
        }
        s_in_len += (size_t)n;
        return 1;
    }
    return 0;
}

int term_read_key(int timeout_ms, int *mouse_col, int *mouse_row) {
    if (s_resized) {
        s_resized = 0;
        return 'r';
    }

    if (s_in_len == 0) {
        int r = fill_in_buf(timeout_ms);
        if (r < 0) return KEY_EOF;
        if (s_in_len == 0) return KEY_NONE;
    }

    if (s_in_buf[0] != '\033') {
        int ch = (int)s_in_buf[0];
        in_buf_consume(1);
        return ch;
    }

    // Byte is ESC (\033)
    if (s_in_len == 1) {
        int r = fill_in_buf(25);
        if (r < 0) return KEY_EOF;
        if (s_in_len == 1) {
            in_buf_consume(1);
            return KEY_ESC;
        }
    }

    // Alt+1..9 or Alt+0
    if (s_in_buf[1] >= '1' && s_in_buf[1] <= '9') {
        int key = KEY_CTRL_1 + (s_in_buf[1] - '1');
        in_buf_consume(2);
        return key;
    }
    if (s_in_buf[1] == '0') {
        in_buf_consume(2);
        return KEY_CTRL_0;
    }

    // SS3 sequences (ESC O ...)
    if (s_in_buf[1] == 'O') {
        if (s_in_len < 3) fill_in_buf(25);
        if (s_in_len >= 3) {
            char c = (char)s_in_buf[2];
            in_buf_consume(3);
            if (c == 'H') return KEY_HOME;
            if (c == 'F') return KEY_END;
        }
        return KEY_NONE;
    }

    // CSI sequences (ESC [ ...)
    if (s_in_buf[1] == '[') {
        size_t term_pos = 0;
        for (int tries = 0; tries < 4; tries++) {
            for (size_t i = 2; i < s_in_len; i++) {
                if (s_in_buf[i] >= 0x40 && s_in_buf[i] <= 0x7E) {
                    term_pos = i;
                    break;
                }
            }
            if (term_pos > 0 || s_in_len >= sizeof(s_in_buf)) break;
            if (fill_in_buf(25) <= 0) break;
        }

        if (term_pos == 0) {
            in_buf_consume(1);
            return KEY_ESC;
        }

        char seq[128];
        size_t seq_len = term_pos + 1;
        if (seq_len >= sizeof(seq)) seq_len = sizeof(seq) - 1;
        memcpy(seq, s_in_buf, seq_len);
        seq[seq_len] = '\0';
        in_buf_consume(term_pos + 1);

        // SGR Mouse: ESC [ < btn ; col ; row (M|m)
        if (seq[2] == '<') {
            int btn = 0, col = 0, row = 0;
            char type = 0;
            if (sscanf(seq + 3, "%d;%d;%d%c", &btn, &col, &row, &type) >= 4) {
                if (mouse_col && col >= 1 && col <= 4096) *mouse_col = col;
                if (mouse_row && row >= 1 && row <= 4096) *mouse_row = row;
                if (btn == 64 && type == 'M') return KEY_WHEEL_UP;
                if (btn == 65 && type == 'M') return KEY_WHEEL_DOWN;
                if (btn == 0 && type == 'M') return KEY_MOUSE_LEFT;
            }
            return KEY_NONE;
        }

        // Legacy Xterm Mouse: ESC [ M cb cx cy
        if (seq[2] == 'M' && seq_len >= 6) {
            int btn = (unsigned char)seq[3] - 32;
            int col = (unsigned char)seq[4] - 32;
            int row = (unsigned char)seq[5] - 32;
            if (mouse_col && col >= 1) *mouse_col = col;
            if (mouse_row && row >= 1) *mouse_row = row;
            if (btn == 64) return KEY_WHEEL_UP;
            if (btn == 65) return KEY_WHEEL_DOWN;
            if (btn == 0) return KEY_MOUSE_LEFT;
            return KEY_NONE;
        }

        // Arrow and navigation keys
        char final_ch = seq[seq_len - 1];
        if (seq_len == 3) {
            switch (final_ch) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
                case 'Z': return KEY_CTRL_SHIFT_TAB;
                default: break;
            }
        }

        // Tilde keys: ESC [ 1 ~ .. 6 ~
        if (final_ch == '~' && seq_len == 4) {
            switch (seq[2]) {
                case '1': return KEY_HOME;
                case '4': return KEY_END;
                case '5': return KEY_PGUP;
                case '6': return KEY_PGDN;
                default: break;
            }
        }

        // Modified keys (CSI-u and xterm modifyOtherKeys)
        int p1 = 0, p2 = 0, p3 = 0;
        int matched = sscanf(seq + 2, "%d;%d;%d", &p1, &p2, &p3);

        if (final_ch == 'u' && matched >= 2) {
            // CSI-u format: ESC [ <key> ; <mod> u
            int mod = p2;
            if (mod == 5) { // Ctrl
                if (p1 >= '1' && p1 <= '9') return KEY_CTRL_1 + (p1 - '1');
                if (p1 == '0' || p1 == 48) return KEY_CTRL_0;
                if (p1 == 9 || p1 == '\t') return KEY_CTRL_TAB;
                if (p1 == 'w' || p1 == 'W') return KEY_CTRL_W;
                if (p1 == 5 || p1 == 53) return KEY_PGUP;
                if (p1 == 6 || p1 == 54) return KEY_PGDN;
            } else if (mod == 6) { // Ctrl + Shift
                if (p1 == 9 || p1 == '\t') return KEY_CTRL_SHIFT_TAB;
            }
        } else if (final_ch == '~' && matched == 3 && p1 == 27 && p2 == 5) {
            // modifyOtherKeys: ESC [ 27 ; 5 ; <key> ~
            if (p3 >= '1' && p3 <= '9') return KEY_CTRL_1 + (p3 - '1');
            if (p3 == '0' || p3 == 48) return KEY_CTRL_0;
            if (p3 == 9 || p3 == '\t') return KEY_CTRL_TAB;
            if (p3 == 'w' || p3 == 'W') return KEY_CTRL_W;
        } else if (final_ch == '~' && matched >= 2 && p2 == 5) {
            if (p1 == 5) return KEY_PGUP;
            if (p1 == 6) return KEY_PGDN;
            if (p1 >= '1' && p1 <= '9') return KEY_CTRL_1 + (p1 - '1');
            if (p1 == 9) return KEY_CTRL_TAB;
            if (p1 == 'w' || p1 == 'W') return KEY_CTRL_W;
        } else if (final_ch == 'I' && matched >= 2) {
            if (p1 == 1 && p2 == 5) return KEY_CTRL_TAB;
            if (p1 == 1 && p2 == 6) return KEY_CTRL_SHIFT_TAB;
        }

        return KEY_NONE;
    }

    in_buf_consume(1);
    return KEY_NONE;
}
