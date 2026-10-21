#pragma once

#include "pdfcli/types.h"
#include <termios.h>
#include <stdbool.h>
#include <stddef.h>

// Synthetic key codes returned by term_read_key for special sequences
enum term_key : int {
    KEY_NONE             = 0,
    KEY_CTRL_C           = 3,
    KEY_TAB              = 9,
    KEY_ESC              = 27,
    KEY_UNCROP_LEFT      = 0xE0, // Cmd+Opt+[
    KEY_UNCROP_RIGHT     = 0xE1, // Cmd+Opt+]
    KEY_UNCROP_TOP       = 0xE2, // Cmd+Opt+Shift+[
    KEY_UNCROP_BOTTOM    = 0xE3, // Cmd+Opt+Shift+]
    KEY_PAGE_UP          = 0xE5, // Page Up (\033[5~)
    KEY_PAGE_DOWN        = 0xE6, // Page Down (\033[6~)
    KEY_MOUSE_LEFT       = 0xE7, // SGR plain left click (press)
    KEY_MOUSE_ALT_CLICK  = 0xE8, // SGR Opt(Alt)+left click (press)
    KEY_MOUSE_CTRL_CLICK = 0xE9, // SGR Ctrl+left click (press)
    KEY_HISTORY_BACK     = 0xEA, // Ctrl+I / Tab / Cmd+Left
    KEY_HISTORY_FORWARD  = 0xEB, // Ctrl+O / Cmd+Right
    KEY_ARROW_UP         = 0xEC, // Up Arrow
    KEY_ARROW_DOWN       = 0xED, // Down Arrow
    KEY_MOUSE_WHEEL_UP   = 0xEE, // SGR mouse wheel up
    KEY_MOUSE_WHEEL_DOWN = 0xEF, // SGR mouse wheel down
    KEY_ARROW_LEFT       = 0xF0, // Left Arrow
    KEY_ARROW_RIGHT      = 0xF1, // Right Arrow
    KEY_ARROW_PREV       = KEY_ARROW_LEFT,
    KEY_ARROW_NEXT       = KEY_ARROW_RIGHT,
    KEY_CTRL_W           = 0x17, // Ctrl+W (standard ASCII 23)
    KEY_CTRL_T           = 0x14, // Ctrl+T (standard ASCII 20)
    KEY_CTRL_1           = 0x101, // Ctrl+1 (Tab 1)
    KEY_CTRL_2           = 0x102, // Ctrl+2 (Tab 2)
    KEY_CTRL_3           = 0x103, // Ctrl+3 (Tab 3)
    KEY_CTRL_4           = 0x104, // Ctrl+4 (Tab 4)
    KEY_CTRL_5           = 0x105, // Ctrl+5 (Tab 5)
    KEY_CTRL_6           = 0x106, // Ctrl+6 (Tab 6)
    KEY_CTRL_7           = 0x107, // Ctrl+7 (Tab 7)
    KEY_CTRL_8           = 0x108, // Ctrl+8 (Tab 8)
    KEY_CTRL_9           = 0x109, // Ctrl+9 (Tab 9)
    KEY_CTRL_0           = 0x10A, // Ctrl+0 (Tab 10 / Last)
    KEY_CTRL_TAB         = 0x10B, // Ctrl+Tab (Next tab)
    KEY_CTRL_SHIFT_TAB   = 0x10C  // Ctrl+Shift+Tab (Prev tab)
};

struct term_state {
    struct termios orig_termios;
    bool is_raw;
};
typedef struct term_state term_state;

// Puts the terminal connected to STDIN_FILENO into raw mode.
// Saves original terminal attributes in 'ts'.
[[nodiscard]] bool term_enter_raw(term_state *ts);

// Restores the terminal to the saved state in 'ts'.
void term_restore(const term_state *ts);

// Queries the terminal dimensions (columns and rows) via ioctl TIOCGWINSZ.
// Falls back to 80x24 if size cannot be determined.
void term_get_size(int *cols, int *rows);

// Queries the terminal pixel dimensions via ioctl TIOCGWINSZ or cell size estimation.
void term_get_pixel_size(int *width_px, int *height_px);

// Enters alternate screen buffer (\033[?1049h) and disables autowrap (\033[?7l).
void term_enter_alt_screen(void);

// Re-enables autowrap (\033[?7h) and leaves alternate screen buffer (\033[?1049l).
void term_leave_alt_screen(void);

// Begins synchronized update (\033[?2026h) to prevent flicker and tearing.
void term_begin_sync(void);

// Ends synchronized update (\033[?2026l) and flushes output.
void term_end_sync(void);

// Checks if a terminal resize signal (SIGWINCH) was received.
[[nodiscard]] bool term_poll_resize(void);

// Detects character cell size in pixels by checking environment overrides,
// probing terminal with ANSI escape sequences (\033[16t / \033[14t),
// querying ioctl pixel dimensions, and falling back to known terminal defaults.
void term_detect_cell_size(double *width_px, double *height_px);

// Enables Kitty progressive keyboard enhancement (disambiguate escape codes: \033[>1u).
void term_enable_kitty_keyboard(void);

// Disables Kitty progressive keyboard enhancement (\033[<u).
void term_disable_kitty_keyboard(void);

// Enables mouse tracking in SGR mode (\033[?1000h\033[?1006h).
void term_enable_mouse(void);

// Disables mouse tracking (\033[?1006l\033[?1000l).
void term_disable_mouse(void);

// Hides the terminal text cursor (\033[?25l).
void term_hide_cursor(void);

// Shows the terminal text cursor (\033[?25h).
void term_show_cursor(void);

// Clears the entire terminal screen and moves cursor home (\033[2J\033[H).
void term_clear_screen(void);

// Sets the terminal window title via OSC sequence (\033]2;...\007).
void term_set_title(const char *title);

// Clears the terminal window title (\033]2;\007).
void term_clear_title(void);

// Reads and parses a single key, escape sequence, Kitty CSI u sequence,
// or SGR mouse event from 'fd'.
// Returns the key character code, synthetic key enum (term_key), or 0 on EOF.
// If 'out_mouse_col' and 'out_mouse_row' are non-null, they receive the 1-based
// cell coordinates of mouse clicks.
int term_read_key(int fd, char *buf, size_t max_len, int *out_mouse_col, int *out_mouse_row);

// Logs raw keystrokes and events to /tmp/pdfcli_keystrokes.log with timestamps and hex dumps
void term_log_keystroke(const char *fmt, ...);

// Returns a human-readable display string for a key code
const char *term_key_name(int key);

// Installs signal handlers for SIGINT, SIGTERM, SIGHUP, and SIGWINCH
// to ensure terminal raw mode, cursor visibility, mouse tracking,
// and title are cleanly restored upon abnormal termination.
void term_install_signal_cleanup(void);
