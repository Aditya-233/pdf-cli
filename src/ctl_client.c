#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "pdfcli/ctl_client.h"
#include "pdfcli/ctl_proto.h"
#include <yyjson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <ctype.h>

static constexpr char DEFAULT_PRESETS_JSON[] = {
#embed "../assets/default_presets.json"
, '\0'
};

typedef struct { char socket[1024]; ctl_viewer_state state; } ctl_inst;
typedef struct { char target[256]; bool all, as_json, quiet; } ctl_opts;

static bool ctl_dial(const char *path, const char *req, char **resp, bool *dead) {
    if (dead) *dead = false;
    if (resp) *resp = nullptr;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    struct timeval tv = { .tv_sec = 2 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        if (errno == ECONNREFUSED || errno == ENOENT) { if (dead) *dead = true; }
        close(fd); return false;
    }
    size_t req_len = strlen(req);
    if (write(fd, req, req_len) <= 0 || write(fd, "\n", 1) <= 0) { close(fd); return false; }
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { close(fd); return false; }
    while (len + 1024 < cap) {
        ssize_t n = read(fd, buf + len, cap - 1 - len);
        if (n > 0) { len += (size_t)n; buf[len] = '\0'; if (strchr(buf, '\n')) break; }
        else if (n < 0 && errno == EINTR) continue;
        else break;
    }
    close(fd);
    buf[len] = '\0';
    char *nl = strpbrk(buf, "\r\n");
    if (nl) *nl = '\0';
    if (resp) *resp = buf; else free(buf);
    return true;
}

static ctl_inst *ctl_find_instances(size_t *cnt) {
    *cnt = 0;
    char dir[512] = {};
    ctl_get_socket_dir(dir, sizeof(dir));
    DIR *d = opendir(dir);
    if (!d) return nullptr;
    size_t cap = 8;
    auto list = (ctl_inst *)calloc(cap, sizeof(ctl_inst));
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        size_t nlen = strlen(ent->d_name);
        if (nlen < 6 || strcmp(ent->d_name + nlen - 5, ".sock") != 0) continue;
        char spath[1024];
        snprintf(spath, sizeof(spath), "%s/%s", dir, ent->d_name);
        bool dead = false; char *resp = nullptr;
        if (!ctl_dial(spath, "{\"cmd\":\"status\"}", &resp, &dead)) {
            if (dead) unlink(spath);
            free(resp); continue;
        }
        ctl_viewer_state st = {};
        if (resp && ctl_parse_state_response(resp, &st)) {
            if (*cnt >= cap) { cap *= 2; list = (ctl_inst *)realloc(list, cap * sizeof(ctl_inst)); }
            snprintf(list[*cnt].socket, sizeof(list[*cnt].socket), "%s", spath);
            list[*cnt].state = st;
            (*cnt)++;
        }
        free(resp);
    }
    closedir(d);
    return list;
}

static ctl_inst *ctl_resolve_targets(ctl_inst *all, size_t all_cnt, const ctl_opts *opts, size_t *out_cnt, char *err, size_t err_sz) {
    *out_cnt = 0;
    if (err && err_sz) err[0] = '\0';
    if (!all_cnt) { if (err && err_sz) snprintf(err, err_sz, "no pdf-cli viewer is running"); return nullptr; }
    if (opts->all) {
        auto res = (ctl_inst *)calloc(all_cnt, sizeof(ctl_inst));
        memcpy(res, all, all_cnt * sizeof(ctl_inst));
        *out_cnt = all_cnt;
        return res;
    }
    if (opts->target[0]) {
        const char *t = opts->target;
        bool is_num = true;
        for (const char *p = t; *p; p++) if (!isdigit((unsigned char)*p)) is_num = false;
        auto matches = (ctl_inst *)calloc(all_cnt, sizeof(ctl_inst));
        size_t mc = 0;
        int tpid = is_num ? atoi(t) : -1;
        for (size_t i = 0; i < all_cnt; i++) {
            if (is_num && all[i].state.pid == tpid) { matches[mc++] = all[i]; break; }
            if (!is_num && (strcasecmp(all[i].state.agterm_session, t) == 0 ||
                            strcasestr(all[i].state.name, t) || strcasestr(all[i].state.path, t))) {
                matches[mc++] = all[i];
            }
        }
        if (!mc) { free(matches); if (err && err_sz) snprintf(err, err_sz, "no pdf-cli viewer matches \"%s\"", t); return nullptr; }
        *out_cnt = mc; return matches;
    }
    if (all_cnt == 1) {
        auto res = (ctl_inst *)calloc(1, sizeof(ctl_inst));
        res[0] = all[0]; *out_cnt = 1; return res;
    }
    if (err && err_sz) {
        size_t off = (size_t)snprintf(err, err_sz, "%zu viewers are running; name one with --target or use --all:", all_cnt);
        for (size_t i = 0; i < all_cnt && off + 64 < err_sz; i++)
            off += (size_t)snprintf(err + off, err_sz - off, "\n  %d %s", all[i].state.pid, all[i].state.name);
    }
    return nullptr;
}

static int ctl_cmd_presets(const ctl_opts *opts) {
    yyjson_doc *doc = yyjson_read(DEFAULT_PRESETS_JSON, strlen(DEFAULT_PRESETS_JSON), 0);
    if (!doc) { fprintf(stderr, "dvctl: failed to load presets\n"); return 1; }
    if (opts->as_json) {
        char *json = yyjson_write(doc, YYJSON_WRITE_PRETTY, nullptr);
        if (json) { puts(json); free(json); }
    } else {
        yyjson_val *root = yyjson_doc_get_root(doc);
        size_t idx, max_idx; yyjson_val *k, *v;
        yyjson_obj_foreach(root, idx, max_idx, k, v) {
            printf("%-10s", yyjson_get_str(k));
            if (yyjson_is_obj(v)) {
                size_t si, sm; yyjson_val *sk, *sv;
                yyjson_obj_foreach(v, si, sm, sk, sv) printf(" %s=%s", yyjson_get_str(sk), yyjson_get_str(sv));
            }
            printf("\n");
        }
    }
    yyjson_doc_free(doc);
    return 0;
}

static void ctl_print_states(const ctl_viewer_state *st, size_t cnt) {
    if (!cnt) return;
    printf("%-7s %-9s %-7s %-7s %-11s %-6s %-6s %s\n", "PID", "PAGE", "TINT", "FIT", "DUAL", "ZOOM", "CROP", "FILE");
    for (size_t i = 0; i < cnt; i++) {
        const ctl_viewer_state *s = &st[i];
        char crop[64] = "-", dual[64] = "off", page[32], zoom[32];
        if (s->crop.top > 0 || s->crop.bottom > 0 || s->crop.left > 0 || s->crop.right > 0)
            snprintf(crop, sizeof(crop), "%.0f/%.0f/%.0f/%.0f", s->crop.top*100, s->crop.bottom*100, s->crop.left*100, s->crop.right*100);
        if (strcmp(s->dual, "half") == 0) snprintf(dual, sizeof(dual), "half:%s", s->half[0] ? s->half : "top");
        else snprintf(dual, sizeof(dual), "%s", s->dual[0] ? s->dual : "off");
        snprintf(page, sizeof(page), "%d/%d", s->page, s->pages);
        snprintf(zoom, sizeof(zoom), "%.0f%%", s->zoom * 100.0);
        printf("%-7d %-9s %-7s %-7s %-11s %-6s %-6s %s\n", s->pid, page, s->tint, s->fit, dual, zoom, crop, s->name);
    }
}

int ctl_client_main(int argc, char **argv) {
    ctl_opts opts = {};
    int cmd_idx = 1;
    while (cmd_idx < argc && argv[cmd_idx][0] == '-') {
        if (strcmp(argv[cmd_idx], "--json") == 0) opts.as_json = true;
        else if (strcmp(argv[cmd_idx], "--all") == 0) opts.all = true;
        else if (strcmp(argv[cmd_idx], "--quiet") == 0) opts.quiet = true;
        else if (strcmp(argv[cmd_idx], "--target") == 0 && cmd_idx + 1 < argc) {
            snprintf(opts.target, sizeof(opts.target), "%s", argv[++cmd_idx]);
        } else if (strcmp(argv[cmd_idx], "--") == 0) { cmd_idx++; break; }
        cmd_idx++;
    }
    const char *cmd = (cmd_idx < argc) ? argv[cmd_idx] : "list";
    if (strcmp(cmd, "presets") == 0) return ctl_cmd_presets(&opts);
    size_t count = 0;
    ctl_inst *all = ctl_find_instances(&count);
    char err[512] = {};
    size_t tcnt = 0;
    ctl_inst *targets = ctl_resolve_targets(all, count, &opts, &tcnt, err, sizeof(err));
    if (!tcnt) {
        if (!opts.quiet) fprintf(stderr, "dvctl: %s\n", err[0] ? err : "no pdf-cli viewer is running");
        free(all); return 1;
    }
    if (strcmp(cmd, "list") == 0 || strcmp(cmd, "status") == 0) {
        if (opts.as_json) {
            yyjson_mut_doc *d = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val *arr = yyjson_mut_arr(d);
            for (size_t i = 0; i < tcnt; i++) {
                char *s = ctl_serialize_state_response(true, nullptr, &targets[i].state);
                if (s) {
                    yyjson_doc *tmp = yyjson_read(s, strlen(s), 0);
                    if (tmp) {
                        yyjson_val *st = yyjson_obj_get(yyjson_doc_get_root(tmp), "state");
                        if (st) yyjson_mut_arr_append(arr, yyjson_val_mut_copy(d, st));
                        yyjson_doc_free(tmp);
                    }
                    free(s);
                }
            }
            yyjson_mut_doc_set_root(d, arr);
            char *out = yyjson_mut_write(d, YYJSON_WRITE_PRETTY, nullptr);
            if (out) { puts(out); free(out); }
            yyjson_mut_doc_free(d);
        } else {
            auto states = (ctl_viewer_state *)calloc(tcnt, sizeof(ctl_viewer_state));
            for (size_t i = 0; i < tcnt; i++) states[i] = targets[i].state;
            ctl_print_states(states, tcnt);
            free(states);
        }
        free(targets); free(all); return 0;
    }
    // Build JSON request for command
    yyjson_mut_doc *rdoc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val *rroot = yyjson_mut_obj(rdoc);
    yyjson_mut_doc_set_root(rdoc, rroot);
    yyjson_mut_obj_add_str(rdoc, rroot, "cmd", cmd);
    if (cmd_idx + 1 < argc) {
        yyjson_mut_val *args = yyjson_mut_obj(rdoc);
        for (int i = cmd_idx + 1; i < argc; i++) {
            if (argv[i][0] == '-' && argv[i][1] == '-') {
                const char *k = argv[i] + 2, *eq = strchr(k, '=');
                char key[64] = {}, val[256] = {};
                if (eq) { strncpy(key, k, (size_t)(eq - k)); snprintf(val, sizeof(val), "%s", eq + 1); }
                else if (i + 1 < argc) { snprintf(key, sizeof(key), "%s", k); snprintf(val, sizeof(val), "%s", argv[++i]); }
                for (char *p = key; *p; p++) if (*p == '-') *p = '_';
                yyjson_mut_obj_add_str(rdoc, args, key, val);
            } else if (strcmp(cmd, "page") == 0) {
                yyjson_mut_obj_add_str(rdoc, args, "to", argv[i]);
            } else if (strcmp(cmd, "tab") == 0) {
                if (i == cmd_idx + 1) {
                    yyjson_mut_obj_add_str(rdoc, args, "action", argv[i]);
                } else if (i == cmd_idx + 2) {
                    yyjson_mut_obj_add_str(rdoc, args, "param", argv[i]);
                }
            }
        }
        yyjson_mut_obj_add_val(rdoc, rroot, "args", args);
    }
    char *req_json = yyjson_mut_write(rdoc, 0, nullptr);
    yyjson_mut_doc_free(rdoc);
    int rc = 0;
    for (size_t i = 0; i < tcnt; i++) {
        char *resp = nullptr;
        bool ok = ctl_dial(targets[i].socket, req_json, &resp, nullptr);
        if (ok && resp) {
            if (opts.as_json) puts(resp);
            free(resp);
        } else { rc = 1; }
    }
    free(req_json); free(targets); free(all);
    return rc;
}
