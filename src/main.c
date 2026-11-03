#include "pdfcli.h"
#include <string.h>
#include <libgen.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>

typedef struct {
    char *path;
    char *name;
    pdf_doc_t *doc;
    int page;
    int scroll_y;
    int max_scroll;
    double zoom;
    fit_mode_t fit;
    bool dark;
    char search_query[64];
    int *search_hits;
    size_t search_hit_count;
    size_t search_hit_idx;
} tab_t;

static volatile sig_atomic_t s_interrupted = 0;
static void on_signal(int sig) { (void)sig; s_interrupted = 1; }

static const char *get_base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void render_top_hud(const tab_t *tabs, int tab_count, int active_tab, int cols) {
    printf("\033[1;1H\033[2m\033[K");
    int cur_col = 1;
    for (int i = 0; i < tab_count && cur_col < cols - 32; i++) {
        bool is_act = (i == active_tab);
        char label[64];
        snprintf(label, sizeof(label), " %d: %.16s%s ", i + 1, tabs[i].name, is_act ? "*" : "");
        if (is_act) printf("\033[1;7m%s\033[0m\033[2m", label);
        else printf("\033[0;2m%s\033[0m\033[2m", label);
        cur_col += (int)strlen(label);
    }
    const tab_t *act = &tabs[active_tab];
    char info[256];
    snprintf(info, sizeof(info), " | Page %d/%d | %.0f%% | %s%s",
             act->page + 1, act->doc->page_count, act->zoom * 100.0,
             act->fit == FIT_WIDTH ? "Width" : "Page",
             act->dark ? " | Dark" : "");
    if (act->search_hit_count > 0) {
        char s_info[128];
        snprintf(s_info, sizeof(s_info), " | [%zu/%zu] \"%.32s\"", act->search_hit_idx + 1, act->search_hit_count, act->search_query);
        strncat(info, s_info, sizeof(info) - strlen(info) - 1);
    }
    printf("%s\033[0m", info);
    fflush(stdout);
}

static int calc_page_max_scroll(const tab_t *t, int page_idx, int px_w, int content_h_px) {
    fz_rect b = pdf_page_bounds(t->doc, page_idx);
    float pw = (b.x1 > b.x0) ? (b.x1 - b.x0) : 612.0f;
    float ph = (b.y1 > b.y0) ? (b.y1 - b.y0) : 792.0f;
    double base_scale = (t->fit == FIT_WIDTH) ? ((double)px_w / (double)pw) : ((double)content_h_px / (double)ph);
    double scale = base_scale * t->zoom;
    int page_h = (int)(ph * scale + 0.5);
    return (page_h > content_h_px) ? (page_h - content_h_px) : 0;
}

static void render_frame(tab_t *tabs, int tab_count, int active_tab, bool show_hud) {
    if (tab_count <= 0 || active_tab < 0 || active_tab >= tab_count) return;
    tab_t *t = &tabs[active_tab];

    int cols = 0, rows = 0, px_w = 0, px_h = 0;
    term_get_size(&cols, &rows, &px_w, &px_h);

    int content_start_row = show_hud ? 2 : 1;
    int content_rows = rows - (show_hud ? 1 : 0);
    if (content_rows < 1) content_rows = 1;

    double cell_h = (double)px_h / (double)rows;
    int content_h_px = (int)((double)content_rows * cell_h + 0.5);

    fz_rect b = pdf_page_bounds(t->doc, t->page);
    float pw = (b.x1 > b.x0) ? (b.x1 - b.x0) : 612.0f;
    float ph = (b.y1 > b.y0) ? (b.y1 - b.y0) : 792.0f;

    double base_scale = (t->fit == FIT_WIDTH) ? ((double)px_w / (double)pw) : ((double)content_h_px / (double)ph);
    double scale = base_scale * t->zoom;
    int total_page_h = (int)(ph * scale + 0.5);

    t->max_scroll = (total_page_h > content_h_px) ? (total_page_h - content_h_px) : 0;
    if (t->scroll_y > t->max_scroll) t->scroll_y = t->max_scroll;
    if (t->scroll_y < 0) t->scroll_y = 0;

    fz_pixmap *pix = pdf_render_viewport(t->doc, t->page, scale, t->scroll_y, px_w, content_h_px, t->dark);
    if (pix) {
        if (!show_hud) printf("\033[1;1H\033[K");
        sixel_draw(pix, 1, content_start_row);
        fz_drop_pixmap(t->doc->ctx, pix);
    }

    if (show_hud) render_top_hud(tabs, tab_count, active_tab, cols);
}

static void show_toc_modal(tab_t *tabs, int tab_count, int active_tab, bool show_hud) {
    tab_t *t = &tabs[active_tab];
    size_t toc_cnt = 0;
    toc_item_t *toc = pdf_get_toc(t->doc, &toc_cnt);
    if (!toc || toc_cnt == 0) { pdf_free_toc(toc, toc_cnt); return; }

    int cols = 0, rows = 0;
    term_get_size(&cols, &rows, nullptr, nullptr);
    int top = 3, bottom = rows - 2, max_visible = bottom - top + 1;
    if (max_visible < 3) { pdf_free_toc(toc, toc_cnt); return; }

    int sel = 0, offset = 0;
    for (size_t i = 0; i < toc_cnt; i++) {
        if (toc[i].page >= t->page) { sel = (int)i; break; }
    }

    while (!s_interrupted) {
        if (sel < offset) offset = sel;
        if (sel >= offset + max_visible) offset = sel - max_visible + 1;

        printf("\033[%d;1H\033[1;7m === Table of Contents (Enter: jump, q/Esc: cancel) ===\033[0m\033[K", top - 1);
        for (int i = 0; i < max_visible; i++) {
            int idx = offset + i;
            printf("\033[%d;1H", top + i);
            if (idx < (int)toc_cnt) {
                bool is_sel = (idx == sel);
                int indent = (toc[idx].level - 1) * 2;
                if (indent > 12) indent = 12;
                printf("%s %*s%-*.*s (p. %d)\033[0m\033[K", is_sel ? "\033[7m >" : "   ",
                       indent, "", cols - indent - 16, cols - indent - 16,
                       toc[idx].title, toc[idx].page + 1);
            } else {
                printf("\033[K");
            }
        }
        fflush(stdout);

        int k = term_read_key(500);
        if (k == KEY_NONE) continue;
        if (k == 'q' || k == KEY_ESC || k == KEY_CTRL_C) break;
        if (k == 'j' || k == KEY_DOWN || k == KEY_WHEEL_DOWN) {
            if (sel < (int)toc_cnt - 1) sel++;
        } else if (k == 'k' || k == KEY_UP || k == KEY_WHEEL_UP) {
            if (sel > 0) sel--;
        } else if (k == KEY_PGDN) {
            sel = (sel + max_visible < (int)toc_cnt) ? (sel + max_visible) : (int)toc_cnt - 1;
        } else if (k == KEY_PGUP) {
            sel = (sel >= max_visible) ? (sel - max_visible) : 0;
        } else if (k == '\r' || k == '\n') {
            int target_p = toc[sel].page;
            if (target_p >= 0 && target_p < t->doc->page_count) {
                t->page = target_p;
                t->scroll_y = 0;
            }
            break;
        }
    }
    pdf_free_toc(toc, toc_cnt);
    render_frame(tabs, tab_count, active_tab, show_hud);
}

static bool prompt_input(const char *prompt, char *buf, size_t maxlen) {
    int rows = 0;
    term_get_size(nullptr, &rows, nullptr, nullptr);
    size_t len = 0;
    buf[0] = '\0';
    (void)write(STDOUT_FILENO, "\033[?25h", 6);
    while (!s_interrupted) {
        printf("\033[%d;1H\033[7m %s%s \033[0m\033[K", rows, prompt, buf);
        fflush(stdout);
        int ch = term_read_key(500);
        if (ch == KEY_NONE) continue;
        if (ch == KEY_ESC || ch == KEY_CTRL_C) {
            (void)write(STDOUT_FILENO, "\033[?25l", 6);
            return false;
        }
        if (ch == '\r' || ch == '\n') break;
        if (ch == 127 || ch == 8) {
            if (len > 0) buf[--len] = '\0';
        } else if (isprint(ch) && len < maxlen - 1) {
            buf[len++] = (char)ch;
            buf[len] = '\0';
        }
    }
    (void)write(STDOUT_FILENO, "\033[?25l", 6);
    return len > 0;
}

static void show_search_prompt(tab_t *tabs, int tab_count, int active_tab, bool show_hud) {
    tab_t *t = &tabs[active_tab];
    char query[64] = {0};
    if (prompt_input("Search:", query, sizeof(query))) {
        snprintf(t->search_query, sizeof(t->search_query), "%s", query);
        free(t->search_hits);
        t->search_hits = pdf_search_text(t->doc, query, &t->search_hit_count);
        t->search_hit_idx = 0;
        if (t->search_hit_count > 0) {
            for (size_t i = 0; i < t->search_hit_count; i++) {
                if (t->search_hits[i] >= t->page) { t->search_hit_idx = i; break; }
            }
            t->page = t->search_hits[t->search_hit_idx];
            t->scroll_y = 0;
        }
    }
    render_frame(tabs, tab_count, active_tab, show_hud);
}

static void show_page_jump_prompt(tab_t *tabs, int tab_count, int active_tab, bool show_hud) {
    tab_t *t = &tabs[active_tab];
    char num[16] = {0}, prompt[32];
    snprintf(prompt, sizeof(prompt), "Go to page (1-%d):", t->doc->page_count);
    if (prompt_input(prompt, num, sizeof(num))) {
        int p = atoi(num);
        if (p >= 1 && p <= t->doc->page_count) {
            t->page = p - 1;
            t->scroll_y = 0;
        }
    }
    render_frame(tabs, tab_count, active_tab, show_hud);
}

int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        puts("pdf-cli (C23) - Fast Sixel Terminal PDF Viewer\n\n"
             "Usage: pdf-cli [options] <file1.pdf> [file2.pdf ...]\n\n"
             "Controls:\n"
             "  j, Down, WheelDown     Scroll down (line / screen step)\n"
             "  k, Up, WheelUp         Scroll up (line / screen step)\n"
             "  Space, PgDn / PgUp     Page down / Page up\n"
             "  Home, < / End, >, G    Jump to first page / Jump to last page\n"
             "  n, p, N                Next / Previous page (or search match)\n"
             "  i                      Toggle top Tab bar & document info\n"
             "  g                      Jump to page number\n"
             "  f                      Toggle fit mode (Fit Width vs Fit Page)\n"
             "  d                      Toggle dark mode / color invert\n"
             "  +, - / 0               Zoom in / Zoom out / Reset zoom\n"
             "  T                      Table of Contents overlay\n"
             "  /                      In-document text search\n"
             "  1..9 / Ctrl+1..9       Switch tab directly\n"
             "  Ctrl+Tab               Cycle tabs\n"
             "  Ctrl+W                 Close tab (exits on last tab)\n"
             "  r                      Force re-render / window resize\n"
             "  q                      Quit viewer");
        return 0;
    }

    tab_t tabs[MAX_TABS] = {0};
    int tab_count = 0;

    for (int i = 1; i < argc && tab_count < MAX_TABS; i++) {
        if (argv[i][0] == '-') continue;
        pdf_doc_t *doc = pdf_open(argv[i]);
        if (!doc) {
            fprintf(stderr, "Warning: failed to open PDF '%s'\n", argv[i]);
            continue;
        }

        tab_t *t = &tabs[tab_count++];
        t->path = strdup(argv[i]);
        t->name = strdup(get_base_name(argv[i]));
        t->doc = doc;
        t->page = 0;
        t->scroll_y = 0;
        t->zoom = 1.0;
        t->fit = FIT_WIDTH;
        t->dark = false;
    }

    if (tab_count == 0) {
        fprintf(stderr, "Error: no valid PDF documents opened.\n");
        return 1;
    }

    struct sigaction sa = { .sa_handler = on_signal };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    if (!term_enter_raw()) {
        fprintf(stderr, "Error: failed to initialize raw terminal mode.\n");
        return 1;
    }
    atexit(term_restore);

    int active_tab = 0;
    bool show_hud = false;
    render_frame(tabs, tab_count, active_tab, show_hud);

    while (tab_count > 0 && !s_interrupted) {
        int key = term_read_key(500);
        if (key == KEY_NONE) continue;
        if (key == 'q' || key == KEY_CTRL_C) break;

        tab_t *t = &tabs[active_tab];
        int cols = 0, rows = 0, px_w = 0, px_h = 0;
        term_get_size(&cols, &rows, &px_w, &px_h);
        int content_rows = rows - (show_hud ? 1 : 0);
        if (content_rows < 1) content_rows = 1;
        int content_h_px = (int)((double)content_rows * ((double)px_h / rows) + 0.5);

        int step_y = 60;
        int screen_step = (int)((double)content_h_px * 0.75);
        if (screen_step < 60) screen_step = 60;

        if (key == KEY_ESC) {
            if (show_hud) {
                show_hud = false;
                render_frame(tabs, tab_count, active_tab, show_hud);
            }
            continue;
        }

        if (key == 'i') {
            show_hud = !show_hud;
            render_frame(tabs, tab_count, active_tab, show_hud);
            continue;
        }

        if (key >= KEY_CTRL_1 && key <= KEY_CTRL_9) {
            int target = key - KEY_CTRL_1;
            if (target < tab_count) { active_tab = target; render_frame(tabs, tab_count, active_tab, show_hud); }
        } else if (tab_count > 1 && key >= '1' && key <= '9') {
            int target = key - '1';
            if (target < tab_count) { active_tab = target; render_frame(tabs, tab_count, active_tab, show_hud); }
        } else if (key == KEY_CTRL_TAB) {
            active_tab = (active_tab + 1) % tab_count;
            render_frame(tabs, tab_count, active_tab, show_hud);
        } else if (key == KEY_CTRL_W) {
            pdf_close(tabs[active_tab].doc);
            free(tabs[active_tab].path);
            free(tabs[active_tab].name);
            free(tabs[active_tab].search_hits);
            for (int i = active_tab; i < tab_count - 1; i++) tabs[i] = tabs[i + 1];
            tab_count--;
            if (tab_count == 0) break;
            if (active_tab >= tab_count) active_tab = tab_count - 1;
            render_frame(tabs, tab_count, active_tab, show_hud);
        } else if (key == 'j' || key == KEY_DOWN || key == KEY_WHEEL_DOWN) {
            int step = (key == KEY_WHEEL_DOWN) ? screen_step : step_y;
            if (t->fit == FIT_HEIGHT) {
                if (t->page < t->doc->page_count - 1) { t->page++; t->scroll_y = 0; render_frame(tabs, tab_count, active_tab, show_hud); }
            } else {
                if (t->scroll_y + step <= t->max_scroll) {
                    t->scroll_y += step;
                    render_frame(tabs, tab_count, active_tab, show_hud);
                } else if (t->page < t->doc->page_count - 1) {
                    t->page++;
                    t->scroll_y = 0;
                    render_frame(tabs, tab_count, active_tab, show_hud);
                }
            }
        } else if (key == 'k' || key == KEY_UP || key == KEY_WHEEL_UP) {
            int step = (key == KEY_WHEEL_UP) ? screen_step : step_y;
            if (t->fit == FIT_HEIGHT) {
                if (t->page > 0) { t->page--; t->scroll_y = 0; render_frame(tabs, tab_count, active_tab, show_hud); }
            } else {
                if (t->scroll_y >= step) {
                    t->scroll_y -= step;
                    render_frame(tabs, tab_count, active_tab, show_hud);
                } else if (t->page > 0) {
                    t->page--;
                    t->scroll_y = calc_page_max_scroll(t, t->page, px_w, content_h_px);
                    render_frame(tabs, tab_count, active_tab, show_hud);
                }
            }
        } else if (key == ' ' || key == KEY_PGDN) {
            if (t->scroll_y + screen_step <= t->max_scroll) {
                t->scroll_y += screen_step;
                render_frame(tabs, tab_count, active_tab, show_hud);
            } else if (t->page < t->doc->page_count - 1) {
                t->page++;
                t->scroll_y = 0;
                render_frame(tabs, tab_count, active_tab, show_hud);
            }
        } else if (key == KEY_PGUP) {
            if (t->scroll_y >= screen_step) {
                t->scroll_y -= screen_step;
                render_frame(tabs, tab_count, active_tab, show_hud);
            } else if (t->page > 0) {
                t->page--;
                t->scroll_y = 0;
                render_frame(tabs, tab_count, active_tab, show_hud);
            }
        } else if (key == KEY_HOME || key == '<') {
            t->page = 0;
            t->scroll_y = 0;
            render_frame(tabs, tab_count, active_tab, show_hud);
        } else if (key == KEY_END || key == 'G' || key == '>') {
            t->page = t->doc->page_count - 1;
            t->scroll_y = calc_page_max_scroll(t, t->page, px_w, content_h_px);
            render_frame(tabs, tab_count, active_tab, show_hud);
        } else if (key == 'n') {
            if (t->search_hit_count > 0) {
                t->search_hit_idx = (t->search_hit_idx + 1) % t->search_hit_count;
                t->page = t->search_hits[t->search_hit_idx];
                t->scroll_y = 0;
                render_frame(tabs, tab_count, active_tab, show_hud);
            } else if (t->page < t->doc->page_count - 1) {
                t->page++;
                t->scroll_y = 0;
                render_frame(tabs, tab_count, active_tab, show_hud);
            }
        } else if (key == 'p' || key == 'N') {
            if (t->search_hit_count > 0 && key == 'N') {
                t->search_hit_idx = (t->search_hit_idx + t->search_hit_count - 1) % t->search_hit_count;
                t->page = t->search_hits[t->search_hit_idx];
                t->scroll_y = 0;
                render_frame(tabs, tab_count, active_tab, show_hud);
            } else if (t->page > 0) {
                t->page--;
                t->scroll_y = 0;
                render_frame(tabs, tab_count, active_tab, show_hud);
            }
        } else if (key == 'f') {
            t->fit = (t->fit == FIT_WIDTH) ? FIT_HEIGHT : FIT_WIDTH;
            t->scroll_y = 0;
            render_frame(tabs, tab_count, active_tab, show_hud);
        } else if (key == 'd') {
            t->dark = !t->dark;
            render_frame(tabs, tab_count, active_tab, show_hud);
        } else if (key == '+' || key == '=') {
            if (t->zoom < 5.0) { t->zoom += 0.1; render_frame(tabs, tab_count, active_tab, show_hud); }
        } else if (key == '-' || key == '_') {
            if (t->zoom > 0.2) { t->zoom -= 0.1; render_frame(tabs, tab_count, active_tab, show_hud); }
        } else if (key == '0') {
            t->zoom = 1.0;
            t->scroll_y = 0;
            render_frame(tabs, tab_count, active_tab, show_hud);
        } else if (key == 'g') {
            show_page_jump_prompt(tabs, tab_count, active_tab, show_hud);
        } else if (key == 'T') {
            show_toc_modal(tabs, tab_count, active_tab, show_hud);
        } else if (key == '/') {
            show_search_prompt(tabs, tab_count, active_tab, show_hud);
        } else if (key == 'r') {
            render_frame(tabs, tab_count, active_tab, show_hud);
        }
    }

    for (int i = 0; i < tab_count; i++) {
        pdf_close(tabs[i].doc);
        free(tabs[i].path);
        free(tabs[i].name);
        free(tabs[i].search_hits);
    }
    return 0;
}
