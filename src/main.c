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
    float def_pw, def_ph;
    int page_count;
    int64_t *page_y;
    int *page_h;
    int64_t total_h, virtual_y, max_scroll;
    double zoom;
    fit_mode_t fit;
    bool dark;
    int last_w, last_h;
    double last_zoom;
    fit_mode_t last_fit;
    char search_query[64];
    int *search_hits;
    size_t search_hit_count, search_hit_idx;
} tab_t;

static const char *get_base_name(const char *path) {
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static int get_active_page(const tab_t *t) {
    if (!t->page_y || t->page_count <= 0) return 0;
    for (int i = 0; i < t->page_count; i++) {
        if (t->page_y[i] + t->page_h[i] / 2 > t->virtual_y) return i;
    }
    return t->page_count - 1;
}

static void next_page(tab_t *t) {
    if (!t->page_y || t->page_count <= 0) return;
    for (int i = 0; i < t->page_count; i++)
        if (t->page_y[i] > t->virtual_y + 10) { t->virtual_y = t->page_y[i]; return; }
    t->virtual_y = t->max_scroll;
}

static void prev_page(tab_t *t) {
    if (!t->page_y || t->page_count <= 0) return;
    for (int i = t->page_count - 1; i >= 0; i--)
        if (t->page_y[i] < t->virtual_y - 10) { t->virtual_y = t->page_y[i]; return; }
    t->virtual_y = 0;
}

static void render_top_hud(const tab_t *tabs, int tab_count, int active_tab, int cols) {
    printf("\033[1;1H\033[2m\033[K");
    int cur_col = 1;
    for (int i = 0; i < tab_count && cur_col < cols - 32; i++) {
        bool is_act = (i == active_tab);
        char lbl[64];
        snprintf(lbl, sizeof(lbl), " %d: %.16s%s ", i + 1, tabs[i].name, is_act ? "*" : "");
        printf(is_act ? "\033[1;7m%s\033[0m\033[2m" : "\033[0;2m%s\033[0m\033[2m", lbl);
        cur_col += (int)strlen(lbl);
    }
    const tab_t *act = &tabs[active_tab];
    char info[256];
    snprintf(info, sizeof(info), " | Page %d/%d | %.0f%% | %s%s",
             get_active_page(act) + 1, act->page_count, act->zoom * 100.0,
             act->fit == FIT_WIDTH ? "Width" : "Page", act->dark ? " | Dark" : "");
    if (act->search_hit_count > 0) {
        char s_info[128];
        snprintf(s_info, sizeof(s_info), " | [%zu/%zu] \"%.32s\"", act->search_hit_idx + 1, act->search_hit_count, act->search_query);
        strncat(info, s_info, sizeof(info) - strlen(info) - 1);
    }
    printf("%s\033[0m", info);
}

static int find_tab_by_col(const tab_t *tabs, int tab_count, int active_tab, int cols, int col) {
    int cur_col = 1;
    for (int i = 0; i < tab_count && cur_col < cols - 32; i++) {
        char lbl[64];
        snprintf(lbl, sizeof(lbl), " %d: %.16s%s ", i + 1, tabs[i].name, (i == active_tab) ? "*" : "");
        int len = (int)strlen(lbl);
        if (col >= cur_col && col < cur_col + len) return i;
        cur_col += len;
    }
    return -1;
}

static void update_tab_layout(tab_t *t, int total_w, int content_h) {
    if (t->page_count <= 0) return;
    if (t->page_y && t->last_w == total_w && t->last_h == content_h &&
        t->last_zoom == t->zoom && t->last_fit == t->fit) return;

    if (!t->page_y) {
        t->page_y = (int64_t *)malloc(sizeof(int64_t) * t->page_count);
        t->page_h = (int *)malloc(sizeof(int) * t->page_count);
    }
    if (!t->page_y || !t->page_h) return;

    int64_t cur_y = 0;
    const int GAP = 12;
    float pw = t->def_pw > 0 ? t->def_pw : 612.0f;
    float ph = t->def_ph > 0 ? t->def_ph : 792.0f;
    double scale = ((t->fit == FIT_WIDTH) ? ((double)total_w / pw) : ((double)content_h / ph)) * t->zoom;
    int ph_px = (int)(ph * scale + 0.5);
    if (ph_px < 1) ph_px = 1;

    for (int i = 0; i < t->page_count; i++) {
        t->page_y[i] = cur_y;
        int slot = (t->fit == FIT_WIDTH) ? ph_px : ((ph_px < content_h) ? content_h : (ph_px + GAP));
        t->page_h[i] = (t->fit == FIT_WIDTH) ? ph_px : slot;
        cur_y += slot + (t->fit == FIT_WIDTH ? GAP : 0);
    }
    if (cur_y >= GAP) cur_y -= GAP;
    t->total_h = cur_y;
    t->max_scroll = (t->total_h > content_h) ? (t->total_h - content_h) : 0;
    if (t->virtual_y > t->max_scroll) t->virtual_y = t->max_scroll;
    if (t->virtual_y < 0) t->virtual_y = 0;

    t->last_w = total_w; t->last_h = content_h;
    t->last_zoom = t->zoom; t->last_fit = t->fit;
}

static void render_frame(tab_t *tabs, int tab_count, int active_tab, bool show_hud) {
    if (tab_count <= 0 || active_tab < 0 || active_tab >= tab_count) return;
    tab_t *t = &tabs[active_tab];

    int cols = 0, rows = 0, px_w = 0, px_h = 0;
    term_get_size(&cols, &rows, &px_w, &px_h);
    int content_rows = rows - (show_hud ? 1 : 0);
    if (content_rows < 1) content_rows = 1;
    int content_h_px = (int)((double)content_rows * ((double)px_h / rows) + 0.5);

    update_tab_layout(t, px_w, content_h_px);
    term_sync_begin();
    printf("\033]11;%s\007", t->dark ? "#000000" : "#181818");

    fz_context *ctx = t->doc->ctx;
    fz_pixmap *vpix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), fz_make_irect(0, 0, px_w, content_h_px), nullptr, 0);
    if (vpix) {
        fz_clear_pixmap_with_value(ctx, vpix, t->dark ? 0xFF : 0x18);
        float pw = t->def_pw > 0 ? t->def_pw : 612.0f;
        float ph = t->def_ph > 0 ? t->def_ph : 792.0f;
        double scale = ((t->fit == FIT_WIDTH) ? ((double)px_w / pw) : ((double)content_h_px / ph)) * t->zoom;
        int page_pixel_w = (int)(pw * scale + 0.5), page_pixel_h = (int)(ph * scale + 0.5);
        if (page_pixel_w < 1) page_pixel_w = 1;
        if (page_pixel_h < 1) page_pixel_h = 1;

        int slice_w = (page_pixel_w <= px_w) ? page_pixel_w : px_w;
        int sx = (page_pixel_w <= px_w) ? 0 : (page_pixel_w - px_w) / 2;
        int dst_x = (page_pixel_w <= px_w) ? (px_w - page_pixel_w) / 2 : 0;

        for (int i = 0; i < t->page_count; i++) {
            int64_t y0 = t->page_y[i], y1 = y0 + t->page_h[i];
            if (y1 <= t->virtual_y) continue;
            if (y0 >= t->virtual_y + content_h_px) break;

            int64_t page_y0 = (t->fit != FIT_WIDTH && page_pixel_h < content_h_px) ? y0 + (content_h_px - page_pixel_h) / 2 : y0;
            int64_t page_y1 = page_y0 + page_pixel_h;
            int64_t v0 = (t->virtual_y > page_y0) ? t->virtual_y : page_y0;
            int64_t v1 = (t->virtual_y + content_h_px < page_y1) ? (t->virtual_y + content_h_px) : page_y1;
            if (v1 <= v0) continue;

            pdf_render_page_slice(t->doc, i, scale, sx, (int)(v0 - page_y0), slice_w, (int)(v1 - v0), vpix, dst_x, (int)(v0 - t->virtual_y));
        }

        if (t->dark) {
            for (int y = 0; y < vpix->h; y++) {
                unsigned char *row = vpix->samples + (size_t)y * (size_t)vpix->stride;
                int rb = vpix->w * 3;
                #pragma GCC ivdep
                for (int x = 0; x < rb; x++) row[x] = 255 - row[x];
            }
        }

        sixel_draw(vpix, 1, show_hud ? 2 : 1);
        fz_drop_pixmap(ctx, vpix);
    }

    if (show_hud) render_top_hud(tabs, tab_count, active_tab, cols);
    term_sync_end();
}

static void show_toc_modal(tab_t *tabs, int tab_count, int active_tab, bool show_hud) {
    tab_t *t = &tabs[active_tab];
    size_t toc_cnt = 0;
    toc_item_t *toc = pdf_get_toc(t->doc, &toc_cnt);
    if (!toc || toc_cnt == 0) { pdf_free_toc(toc, toc_cnt); return; }

    int cols = 0, rows = 0;
    term_get_size(&cols, &rows, nullptr, nullptr);
    int top = 3, bottom = rows - 2, max_v = bottom - top + 1;
    if (max_v < 3) { pdf_free_toc(toc, toc_cnt); return; }

    int sel = 0, offset = 0, cur_p = get_active_page(t);
    for (size_t i = 0; i < toc_cnt; i++) {
        if (toc[i].page >= cur_p) { sel = (int)i; break; }
    }

    while (true) {
        if (sel < offset) offset = sel;
        if (sel >= offset + max_v) offset = sel - max_v + 1;

        term_sync_begin();
        printf("\033[%d;1H\033[1;7m === Table of Contents (Enter: jump, q/Esc: cancel) ===\033[0m\033[K", top - 1);
        for (int i = 0; i < max_v; i++) {
            int idx = offset + i;
            printf("\033[%d;1H", top + i);
            if (idx < (int)toc_cnt) {
                int indent = (toc[idx].level - 1) * 2;
                if (indent > 12) indent = 12;
                printf("%s %*s%-*.*s (p. %d)\033[0m\033[K", (idx == sel) ? "\033[7m >" : "   ",
                       indent, "", cols - indent - 16, cols - indent - 16,
                       toc[idx].title, toc[idx].page + 1);
            } else { printf("\033[K"); }
        }
        term_sync_end();

        int k = term_read_key(500, nullptr, nullptr);
        if (k == KEY_NONE) continue;
        if (k == 'q' || k == KEY_ESC || k == KEY_CTRL_C || k == 3) break;
        if (k == 'j' || k == KEY_DOWN || k == KEY_WHEEL_DOWN) { if (sel < (int)toc_cnt - 1) sel++; }
        else if (k == 'k' || k == KEY_UP || k == KEY_WHEEL_UP) { if (sel > 0) sel--; }
        else if (k == KEY_PGDN) sel = (sel + max_v < (int)toc_cnt) ? (sel + max_v) : (int)toc_cnt - 1;
        else if (k == KEY_PGUP) sel = (sel >= max_v) ? (sel - max_v) : 0;
        else if (k == '\r' || k == '\n') {
            int p = toc[sel].page;
            if (p >= 0 && p < t->page_count && t->page_y) t->virtual_y = t->page_y[p];
            break;
        }
    }
    pdf_free_toc(toc, toc_cnt);
    render_frame(tabs, tab_count, active_tab, show_hud);
}

static bool prompt_input(const char *prompt, char *buf, size_t maxlen) {
    int rows = 0; term_get_size(nullptr, &rows, nullptr, nullptr);
    size_t len = 0; buf[0] = '\0';
    fputs("\033[?25h", stdout);
    while (true) {
        printf("\033[%d;1H\033[7m %s%s \033[0m\033[K", rows, prompt, buf);
        fflush(stdout);
        int ch = term_read_key(500, nullptr, nullptr);
        if (ch == KEY_NONE) continue;
        if (ch == KEY_ESC || ch == KEY_CTRL_C || ch == 3) { len = 0; break; }
        if (ch == '\r' || ch == '\n') break;
        if ((ch == 127 || ch == 8) && len > 0) buf[--len] = '\0';
        else if (isprint(ch) && len < maxlen - 1) { buf[len++] = (char)ch; buf[len] = '\0'; }
    }
    fputs("\033[?25l", stdout);
    fflush(stdout);
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
            int cur_p = get_active_page(t);
            for (size_t i = 0; i < t->search_hit_count; i++) {
                if (t->search_hits[i] >= cur_p) { t->search_hit_idx = i; break; }
            }
            int p = t->search_hits[t->search_hit_idx];
            if (p >= 0 && p < t->page_count && t->page_y) t->virtual_y = t->page_y[p];
        }
    }
    render_frame(tabs, tab_count, active_tab, show_hud);
}

static void show_page_jump_prompt(tab_t *tabs, int tab_count, int active_tab, bool show_hud) {
    tab_t *t = &tabs[active_tab];
    char num[16] = {0}, prompt[32];
    snprintf(prompt, sizeof(prompt), "Go to page (1-%d):", t->page_count);
    if (prompt_input(prompt, num, sizeof(num))) {
        int p = atoi(num);
        if (p >= 1 && p <= t->page_count && t->page_y) t->virtual_y = t->page_y[p - 1];
    }
    render_frame(tabs, tab_count, active_tab, show_hud);
}

int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        puts("pdf-cli (C23) - Fast Sixel Terminal PDF Viewer\n\n"
             "Usage: pdf-cli [options] <file1.pdf> [file2.pdf ...]\n\n"
             "Controls:\n"
             "  j, Down, WheelDown     Continuous scroll down (line / wheel step)\n"
             "  k, Up, WheelUp         Continuous scroll up (line / wheel step)\n"
             "  d, Space, PgDn         Scroll down screen step (~35%)\n"
             "  u, PgUp                Scroll up screen step (~35%)\n"
             "  Home, < / End, >, G    Jump to start / Jump to end of document\n"
             "  n, Right, J            Jump to next page\n"
             "  p, Left, K, N          Jump to previous page\n"
             "  i                      Toggle top Tab bar HUD\n"
             "  g                      Jump to page number\n"
             "  f                      Toggle fit mode (Fit Width vs Fit Page)\n"
             "  D                      Toggle dark mode / color invert\n"
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
        if (!doc) { fprintf(stderr, "Warning: failed to open PDF '%s'\n", argv[i]); continue; }
        tab_t *t = &tabs[tab_count++];
        t->path = strdup(argv[i]);
        t->name = strdup(get_base_name(argv[i]));
        t->doc = doc;
        t->page_count = doc->page_count;
        fz_rect b = pdf_page_bounds(doc, 0);
        t->def_pw = (b.x1 > b.x0) ? (b.x1 - b.x0) : 612.0f;
        t->def_ph = (b.y1 > b.y0) ? (b.y1 - b.y0) : 792.0f;
        t->zoom = 1.0; t->fit = FIT_WIDTH; t->dark = false;
    }
    if (tab_count == 0) { fprintf(stderr, "Error: no valid PDF documents opened.\n"); return 1; }

    if (!term_enter_raw()) { fprintf(stderr, "Error: raw terminal failed.\n"); return 1; }
    atexit(term_restore);

    int active_tab = 0;
    bool show_hud = false;
    render_frame(tabs, tab_count, active_tab, show_hud);

    while (tab_count > 0) {
        int mouse_col = 0, mouse_row = 0;
        int key = term_read_key(500, &mouse_col, &mouse_row);
        if (key == KEY_NONE) continue;
        if (key == 'q' || key == KEY_CTRL_C || key == 3) break;

        tab_t *t = &tabs[active_tab];
        int cols = 0, rows = 0, px_w = 0, px_h = 0;
        term_get_size(&cols, &rows, &px_w, &px_h);
        int content_h_px = (int)((double)(rows - (show_hud ? 1 : 0)) * ((double)px_h / rows) + 0.5);
        int step_line = (int)(((double)px_h / rows) * 3.0);
        if (step_line < 24) step_line = 24;
        int wstep = (int)(content_h_px * 0.15);
        if (wstep < 40) wstep = 40;
        int step_pg = (int)(content_h_px * 0.35);
        if (step_pg < 50) step_pg = 50;

        bool redraw = true;
        if (key == KEY_ESC) {
            if (show_hud) show_hud = false; else redraw = false;
        } else if (key == 'i') {
            show_hud = !show_hud;
        } else if (key == KEY_MOUSE_LEFT) {
            if (show_hud && mouse_row == 1) {
                int hit = find_tab_by_col(tabs, tab_count, active_tab, cols, mouse_col);
                if (hit >= 0 && hit < tab_count && hit != active_tab) active_tab = hit;
                else redraw = false;
            } else if (show_hud) {
                show_hud = false;
            } else { redraw = false; }
        } else if (key >= KEY_CTRL_1 && key <= KEY_CTRL_9) {
            int target = key - KEY_CTRL_1;
            if (target < tab_count) active_tab = target; else redraw = false;
        } else if (tab_count > 1 && key >= '1' && key <= '9') {
            int target = key - '1';
            if (target < tab_count) active_tab = target; else redraw = false;
        } else if (key == KEY_CTRL_TAB) {
            active_tab = (active_tab + 1) % tab_count;
        } else if (key == KEY_CTRL_W) {
            pdf_close(tabs[active_tab].doc);
            free(tabs[active_tab].path); free(tabs[active_tab].name);
            free(tabs[active_tab].page_y); free(tabs[active_tab].page_h);
            free(tabs[active_tab].search_hits);
            for (int i = active_tab; i < tab_count - 1; i++) tabs[i] = tabs[i + 1];
            if (--tab_count == 0) break;
            if (active_tab >= tab_count) active_tab = tab_count - 1;
        } else if (key == KEY_WHEEL_DOWN) {
            if (show_hud && mouse_row == 1 && tab_count > 1) active_tab = (active_tab + 1) % tab_count;
            else t->virtual_y += wstep;
        } else if (key == KEY_WHEEL_UP) {
            if (show_hud && mouse_row == 1 && tab_count > 1) active_tab = (active_tab + tab_count - 1) % tab_count;
            else t->virtual_y -= wstep;
        } else if (key == 'j' || key == KEY_DOWN) {
            t->virtual_y += step_line;
        } else if (key == 'k' || key == KEY_UP) {
            t->virtual_y -= step_line;
        } else if (key == ' ' || key == KEY_PGDN || key == 'd') {
            t->virtual_y += step_pg;
        } else if (key == KEY_PGUP || key == 'u') {
            t->virtual_y -= step_pg;
        } else if (key == KEY_HOME || key == '<') {
            t->virtual_y = 0;
        } else if (key == KEY_END || key == 'G' || key == '>') {
            t->virtual_y = t->max_scroll;
        } else if (key == KEY_RIGHT || key == 'n' || key == 'J') {
            if (key == 'n' && t->search_hit_count > 0) {
                t->search_hit_idx = (t->search_hit_idx + 1) % t->search_hit_count;
                int p = t->search_hits[t->search_hit_idx];
                if (p >= 0 && p < t->page_count && t->page_y) t->virtual_y = t->page_y[p];
            } else {
                next_page(t);
            }
        } else if (key == KEY_LEFT || key == 'p' || key == 'N' || key == 'K') {
            if (key == 'N' && t->search_hit_count > 0) {
                t->search_hit_idx = (t->search_hit_idx + t->search_hit_count - 1) % t->search_hit_count;
                int p = t->search_hits[t->search_hit_idx];
                if (p >= 0 && p < t->page_count && t->page_y) t->virtual_y = t->page_y[p];
            } else {
                prev_page(t);
            }
        } else if (key == 'f') {
            t->fit = (t->fit == FIT_WIDTH) ? FIT_HEIGHT : FIT_WIDTH;
            t->virtual_y = 0;
        } else if (key == 'D') {
            t->dark = !t->dark;
        } else if (key == '+' || key == '=') {
            if (t->zoom < 5.0) t->zoom += 0.1;
        } else if (key == '-' || key == '_') {
            if (t->zoom > 0.2) t->zoom -= 0.1;
        } else if (key == '0') {
            t->zoom = 1.0; t->virtual_y = 0;
        } else if (key == 'g') {
            show_page_jump_prompt(tabs, tab_count, active_tab, show_hud);
            redraw = false;
        } else if (key == 'T') {
            show_toc_modal(tabs, tab_count, active_tab, show_hud);
            redraw = false;
        } else if (key == '/') {
            show_search_prompt(tabs, tab_count, active_tab, show_hud);
            redraw = false;
        } else if (key != 'r') {
            redraw = false;
        }

        if (t->virtual_y > t->max_scroll) t->virtual_y = t->max_scroll;
        if (t->virtual_y < 0) t->virtual_y = 0;
        if (redraw) render_frame(tabs, tab_count, active_tab, show_hud);
    }

    term_restore();

    for (int i = 0; i < tab_count; i++) {
        pdf_close(tabs[i].doc);
        free(tabs[i].path); free(tabs[i].name);
        free(tabs[i].page_y); free(tabs[i].page_h);
        free(tabs[i].search_hits);
    }
    return 0;
}
