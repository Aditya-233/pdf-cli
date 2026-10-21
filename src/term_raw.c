#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "pdfcli/term_raw.h"
#include "pdfcli/utf8_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>

static volatile sig_atomic_t g_winch_received = 0;
static term_state *g_active_ts = nullptr;

static void term_signal_handler(int sig) {
    if (sig == SIGWINCH) {
        g_winch_received = 1;
        return;
    }
    if (g_active_ts && g_active_ts->is_raw) {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &g_active_ts->orig_termios);
    }
    const char cleanup[] = "\033[?7h\033[?1006l\033[?1000l\033[<u\033[?25h\033]2;\007\033[?1049l";
    (void)write(STDOUT_FILENO, cleanup, sizeof(cleanup) - 1);
    struct sigaction sa = { .sa_handler = SIG_DFL };
    sigemptyset(&sa.sa_mask);
    (void)sigaction(sig, &sa, nullptr);
    (void)raise(sig);
}

void term_install_signal_cleanup(void) {
    struct sigaction sa = { .sa_handler = term_signal_handler, .sa_flags = SA_RESTART };
    sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGWINCH, &sa, nullptr);
    sa.sa_flags = 0;
    (void)sigaction(SIGINT, &sa, nullptr);
    (void)sigaction(SIGTERM, &sa, nullptr);
    (void)sigaction(SIGHUP, &sa, nullptr);
}

[[nodiscard]] bool term_enter_raw(term_state *ts) {
    if (!ts || tcgetattr(STDIN_FILENO, &ts->orig_termios) != 0) return false;
    struct termios raw = ts->orig_termios;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return false;
    ts->is_raw = true;
    g_active_ts = ts;
    return true;
}

void term_restore(const term_state *ts) {
    if (ts && ts->is_raw) {
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &ts->orig_termios);
        if (g_active_ts == ts) g_active_ts = nullptr;
    }
}

void term_get_size(int *cols, int *rows) {
    struct winsize ws = {};
    int c = 80, r = 24;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        c = ws.ws_col;
        r = ws.ws_row;
    } else if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        c = ws.ws_col;
        r = ws.ws_row;
    } else {
        int fd = open("/dev/tty", O_RDONLY | O_NOCTTY);
        if (fd >= 0) {
            if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
                c = ws.ws_col;
                r = ws.ws_row;
            }
            close(fd);
        }
    }
    if (cols) *cols = c;
    if (rows) *rows = r;
}

void term_enter_alt_screen(void) {
    fputs("\033[?1049h\033[?7l", stdout);
    fflush(stdout);
}

void term_leave_alt_screen(void) {
    fputs("\033[?7h\033[?1049l", stdout);
    fflush(stdout);
}

void term_begin_sync(void) {
    fputs("\033[?2026h", stdout);
}

void term_end_sync(void) {
    fputs("\033[?2026l", stdout);
    fflush(stdout);
}

[[nodiscard]] bool term_poll_resize(void) {
    if (g_winch_received) {
        g_winch_received = 0;
        return true;
    }
    return false;
}

static bool probe_ansi_cell_size(double *cw, double *ch) {
    int tty_fd = open("/dev/tty", O_RDWR | O_NOCTTY);
    if (tty_fd < 0) {
        if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) tty_fd = STDIN_FILENO;
        else return false;
    }
    struct termios orig_tio = {};
    bool need_restore = false;
    if (tcgetattr(tty_fd, &orig_tio) == 0) {
        struct termios raw = orig_tio;
        cfmakeraw(&raw);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;
        if (tcsetattr(tty_fd, TCSANOW, &raw) == 0) need_restore = true;
    }
    bool success = false;
    const char *probes[] = { "\033[16t", "\033[14t" };
    for (int p = 0; p < 2 && !success; p++) {
        if (write(tty_fd, probes[p], 5) == 5) {
            char resp[64] = {};
            size_t tot = 0;
            struct pollfd pfd = { .fd = tty_fd, .events = POLLIN, .revents = 0 };
            while (tot < sizeof(resp) - 1) {
                if (poll(&pfd, 1, 100) <= 0) break;
                if (read(tty_fd, resp + tot, 1) <= 0) break;
                if (resp[tot++] == 't') break;
            }
            resp[tot] = '\0';
            int h = 0, w = 0;
            if (p == 0 && sscanf(resp, "\033[6;%d;%dt", &h, &w) == 2 && w > 0 && h > 0) {
                *cw = w;
                *ch = h;
                success = true;
            } else if (p == 1 && sscanf(resp, "\033[4;%d;%dt", &h, &w) == 2 && w > 0 && h > 0) {
                int cols = 0, rows = 0;
                term_get_size(&cols, &rows);
                if (cols > 0 && rows > 0) {
                    double calc_w = (double)w / cols, calc_h = (double)h / rows;
                    if (calc_w > 4.0 && calc_h > 8.0) {
                        *cw = calc_w;
                        *ch = calc_h;
                        success = true;
                    }
                }
            }
        }
    }
    if (need_restore) (void)tcsetattr(tty_fd, TCSANOW, &orig_tio);
    if (tty_fd != STDIN_FILENO) close(tty_fd);
    return success;
}

void term_detect_cell_size(double *width_px, double *height_px) {
    const char *cell_env = getenv("DOCVIEWER_CELL_SIZE");
    if (cell_env) {
        double ew = 0.0, eh = 0.0;
        if (sscanf(cell_env, "%lfx%lf", &ew, &eh) == 2 && ew > 0.0 && eh > 0.0) {
            if (width_px) *width_px = ew;
            if (height_px) *height_px = eh;
            return;
        }
    }
    struct winsize ws = {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 || ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_xpixel > 0 && ws.ws_ypixel > 0 && ws.ws_col > 0 && ws.ws_row > 0) {
            double cw = (double)ws.ws_xpixel / ws.ws_col, ch = (double)ws.ws_ypixel / ws.ws_row;
            if (cw >= 4.0 && ch >= 8.0) {
                if (width_px) *width_px = cw;
                if (height_px) *height_px = ch;
                return;
            }
        }
    }
    double pw = 0.0, ph = 0.0;
    if (probe_ansi_cell_size(&pw, &ph)) {
        if (width_px) *width_px = pw;
        if (height_px) *height_px = ph;
        return;
    }
    const char *tp = getenv("TERM_PROGRAM"), *te = getenv("TERM");
    bool k = (tp && (strstr(tp, "kitty") || strstr(tp, "ghostty") || strstr(tp, "WezTerm") || strstr(tp, "iTerm"))) ||
             (te && (strstr(te, "kitty") || strstr(te, "ghostty") || strstr(te, "wezterm"))) || getenv("KITTY_WINDOW_ID");
    if (width_px) *width_px = k ? 12.0 : 11.0;
    if (height_px) *height_px = k ? 24.0 : 22.0;
}

void term_get_pixel_size(int *width_px, int *height_px) {
    struct winsize ws = {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 || ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
            if (width_px) *width_px = ws.ws_xpixel;
            if (height_px) *height_px = ws.ws_ypixel;
            return;
        }
    }
    int cols = 80, rows = 24;
    term_get_size(&cols, &rows);
    double cw = 11.0, ch = 22.0;
    term_detect_cell_size(&cw, &ch);
    if (width_px) *width_px = (int)(cols * cw + 0.5);
    if (height_px) *height_px = (int)(rows * ch + 0.5);
}

void term_enable_kitty_keyboard(void) {
    // Enable Kitty progressive keyboard protocol (flag 1: disambiguate escape codes)
    // and xterm modifyOtherKeys (mode 2)
    fputs("\033[>1u\033[>4;2m", stdout);
    fflush(stdout);
}

void term_disable_kitty_keyboard(void) {
    // Disable Kitty keyboard protocol and xterm modifyOtherKeys
    fputs("\033[<u\033[>4;0m", stdout);
    fflush(stdout);
}

void term_enable_mouse(void) {
    fputs("\033[?1000h\033[?1006h", stdout);
    fflush(stdout);
}

void term_disable_mouse(void) {
    fputs("\033[?1006l\033[?1000l", stdout);
    fflush(stdout);
}

void term_hide_cursor(void) {
    fputs("\033[?25l", stdout);
    fflush(stdout);
}

void term_show_cursor(void) {
    fputs("\033[?25h", stdout);
    fflush(stdout);
}

void term_clear_screen(void) {
    int cols = 80, rows = 24;
    term_get_size(&cols, &rows);
    if (cols <= 0) cols = 80;
    if (rows <= 0) rows = 24;
    fputs("\033[2J\033[3J\033[H\033[0m", stdout);
    char bl[1024];
    int fc = (cols < (int)sizeof(bl)) ? cols : (int)sizeof(bl) - 1;
    memset(bl, ' ', fc);
    bl[fc] = '\0';
    for (int r = 1; r <= rows; r++) printf("\033[%d;1H%s\033[%d;1H\033[2K", r, bl, r);
    fputs("\033[1;1H", stdout);
    fflush(stdout);
}

void term_set_title(const char *title) {
    if (title) {
        printf("\033]2;%s\007", title);
        fflush(stdout);
    }
}

void term_clear_title(void) {
    fputs("\033]2;\007", stdout);
    fflush(stdout);
}

static inline int set_key(char *buf, size_t max_len, int k) {
    if (buf && max_len > 0) {
        buf[0] = (char)k;
        if (max_len > 1) buf[1] = '\0';
    }
    return k;
}

int term_read_key(int fd, char *buf, size_t max_len, int *out_mouse_col, int *out_mouse_row) {
    if (buf && max_len > 0) buf[0] = '\0';
    for (;;) {
        unsigned char b0 = 0;
        ssize_t n = read(fd, &b0, 1);
        if (n == 0) return 0;
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
        if (b0 != 27) {
            if (b0 >= 0x80) {
                size_t needed = ((b0 & 0xE0) == 0xC0) ? 1 : (((b0 & 0xF0) == 0xE0) ? 2 : (((b0 & 0xF8) == 0xF0) ? 3 : 0));
                if (needed > 0 && buf && max_len > needed + 1) {
                    buf[0] = (char)b0;
                    size_t got = 1;
                    for (size_t i = 0; i < needed; i++) {
                        unsigned char cb = 0;
                        if (read(fd, &cb, 1) <= 0) break;
                        buf[got++] = (char)cb;
                    }
                    buf[got] = '\0';
                    uint32_t cp = 0;
                    size_t ulen = 0;
                    if (utf8_decode(buf, &cp, &ulen) == 0 && ulen == got) return (int)cp;
                }
                continue;
            }
            if (b0 == 0x09) return set_key(buf, max_len, KEY_HISTORY_BACK);
            if (b0 == 0x0F) return set_key(buf, max_len, KEY_HISTORY_FORWARD);
            return set_key(buf, max_len, b0);
        }

        struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
        if (poll(&pfd, 1, 50) <= 0) return set_key(buf, max_len, 27);
        unsigned char b2 = 0;
        if (read(fd, &b2, 1) <= 0) return set_key(buf, max_len, 27);
        if (b2 >= '1' && b2 <= '9') {
            // Alt+1 .. Alt+9 fallback
            return set_key(buf, max_len, KEY_CTRL_1 + (b2 - '1'));
        }
        if (b2 == '0') {
            return set_key(buf, max_len, KEY_CTRL_0);
        }
        if (b2 != '[' && b2 != 'O') return set_key(buf, max_len, 27);
        if (b2 == 'O') {
            unsigned char fn = 0;
            if (read(fd, &fn, 1) <= 0) return 27;
            if (fn == 'A') return set_key(buf, max_len, KEY_ARROW_UP);
            if (fn == 'B') return set_key(buf, max_len, KEY_ARROW_DOWN);
            if (fn == 'C') return set_key(buf, max_len, KEY_ARROW_RIGHT);
            if (fn == 'D') return set_key(buf, max_len, KEY_ARROW_LEFT);
            return 0;
        }

        char params[64] = {};
        size_t plen = 0;
        unsigned char final_byte = 0;
        for (size_t i = 0; i < 32; i++) {
            unsigned char c = 0;
            if (read(fd, &c, 1) <= 0) break;
            if (c >= 0x40 && c <= 0x7E) {
                final_byte = c;
                break;
            }
            if (plen < sizeof(params) - 1) params[plen++] = (char)c;
        }
        params[plen] = '\0';

        bool shift = false, super = false;
        const char *semi = strchr(params, ';');
        if (semi) {
            int mods = atoi(semi + 1) - 1;
            if (mods > 0) {
                shift = (mods & 1);
                super = (mods & 8);
            }
        }

        switch (final_byte) {
        case 'A': return set_key(buf, max_len, shift ? 'K' : KEY_ARROW_UP);
        case 'B': return set_key(buf, max_len, shift ? 'J' : KEY_ARROW_DOWN);
        case 'C': return set_key(buf, max_len, super ? KEY_HISTORY_FORWARD : KEY_ARROW_RIGHT);
        case 'D': return set_key(buf, max_len, super ? KEY_HISTORY_BACK : KEY_ARROW_LEFT);
        case 'V': return set_key(buf, max_len, KEY_PAGE_UP);
        case 'U': return set_key(buf, max_len, KEY_PAGE_DOWN);
        case 'Z': return set_key(buf, max_len, KEY_CTRL_SHIFT_TAB); // Shift+Tab (backtab)
        case '~': {
            int p1 = 0, p2 = 0, p3 = 0;
            int n = sscanf(params, "%d;%d;%d", &p1, &p2, &p3);
            if (n == 3 && p1 == 27) {
                // XTerm modifyOtherKeys: \033[27;<mods>;<kcode>~
                int mods = p2 - 1;
                int kcode = p3;
                bool ctrl = (mods & 4) != 0;
                bool s_held = (mods & 1) != 0;
                if (ctrl) {
                    if (kcode >= '1' && kcode <= '9') return set_key(buf, max_len, KEY_CTRL_1 + (kcode - '1'));
                    if (kcode == '0') return set_key(buf, max_len, KEY_CTRL_0);
                    if (kcode == '\t' || kcode == 9) return set_key(buf, max_len, s_held ? KEY_CTRL_SHIFT_TAB : KEY_CTRL_TAB);
                    if (kcode == 'w' || kcode == 'W') return set_key(buf, max_len, KEY_CTRL_W);
                    if (kcode == 't' || kcode == 'T') return set_key(buf, max_len, KEY_CTRL_T);
                }
            } else if (n >= 1) {
                if (p1 == 5) return set_key(buf, max_len, KEY_PAGE_UP);
                if (p1 == 6) return set_key(buf, max_len, KEY_PAGE_DOWN);
            }
            return 0;
        }
        case 'u': {
            int key = 0, raw_mods = 0;
            sscanf(params, "%d;%d", &key, &raw_mods);
            int mod_bits = (raw_mods > 0) ? (raw_mods - 1) : 0;
            bool s_held = (mod_bits & 1), sup_held = (mod_bits & 8);
            if (key == 27) return set_key(buf, max_len, 27);
            if (key == 57354 || key == 57375 || key == 57356) return set_key(buf, max_len, KEY_PAGE_UP);
            if (key == 57355 || key == 57376 || key == 57357) return set_key(buf, max_len, KEY_PAGE_DOWN);
            if (key == 57352) return set_key(buf, max_len, s_held ? 'K' : KEY_ARROW_UP);
            if (key == 57353) return set_key(buf, max_len, s_held ? 'J' : KEY_ARROW_DOWN);
            if (key == 57350) return set_key(buf, max_len, sup_held ? KEY_HISTORY_BACK : KEY_ARROW_LEFT);
            if (key == 57351) return set_key(buf, max_len, sup_held ? KEY_HISTORY_FORWARD : KEY_ARROW_RIGHT);
            if (raw_mods > 0) {
                if ((mod_bits & 4) != 0) {
                    if (key >= '1' && key <= '9') return set_key(buf, max_len, KEY_CTRL_1 + (key - '1'));
                    if (key == '0') return set_key(buf, max_len, KEY_CTRL_0);
                    if (key == '\t' || key == 9) return set_key(buf, max_len, s_held ? KEY_CTRL_SHIFT_TAB : KEY_CTRL_TAB);
                    if (key == 'w' || key == 'W') return set_key(buf, max_len, KEY_CTRL_W);
                    if (key == 't' || key == 'T') return set_key(buf, max_len, KEY_CTRL_T);
                    if (key == 'i') return set_key(buf, max_len, KEY_HISTORY_BACK);
                    if (key == 'o') return set_key(buf, max_len, KEY_HISTORY_FORWARD);
                    if (key == 'c') return set_key(buf, max_len, 3);
                    if (key == 'd') return set_key(buf, max_len, 4);
                    if (key == 'u') return set_key(buf, max_len, 21);
                }
                if ((mod_bits & 10) == 10) {
                    if (key == '[') return set_key(buf, max_len, s_held ? KEY_UNCROP_TOP : KEY_UNCROP_LEFT);
                    if (key == ']') return set_key(buf, max_len, s_held ? KEY_UNCROP_BOTTOM : KEY_UNCROP_RIGHT);
                }
            }
            if (key > 0 && key < 128) {
                int ch = (s_held && key >= 'a' && key <= 'z') ? (key - 'a' + 'A') : key;
                return set_key(buf, max_len, ch);
            }
            return 0;
        }
        case 'M':
        case 'm': {
            if (params[0] == '<') {
                int btn = 0, col = 0, row = 0;
                sscanf(params + 1, "%d;%d;%d", &btn, &col, &row);
                if (col > 0 && row > 0) {
                    if (out_mouse_col) *out_mouse_col = col;
                    if (out_mouse_row) *out_mouse_row = row;
                    if (final_byte == 'M') {
                        if (btn == 0) return set_key(buf, max_len, KEY_MOUSE_LEFT);
                        if (btn == 8) return set_key(buf, max_len, KEY_MOUSE_ALT_CLICK);
                        if (btn == 16 || btn == 18) return set_key(buf, max_len, KEY_MOUSE_CTRL_CLICK);
                        if (btn == 64) return set_key(buf, max_len, KEY_MOUSE_WHEEL_UP);
                        if (btn == 65) return set_key(buf, max_len, KEY_MOUSE_WHEEL_DOWN);
                    }
                }
                continue;
            }
            if (final_byte == 'M' && plen == 0) {
                unsigned char dummy[3];
                (void)read(fd, dummy, 3);
                continue;
            }
            break;
        }
        default: return 0;
        }
    }
}
