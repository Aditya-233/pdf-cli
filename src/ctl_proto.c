#include "pdfcli/ctl_proto.h"
#include <yyjson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ctl_get_socket_dir(char *out, size_t sz) {
    if (!out || !sz) return;
    const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    if (xdg && *xdg) snprintf(out, sz, "%s/docviewer/ctl", xdg);
    else if (home && *home) snprintf(out, sz, "%s/.cache/docviewer/ctl", home);
    else snprintf(out, sz, "/tmp/docviewer-ctl");
}

void ctl_get_socket_path(int pid, char *out, size_t sz) {
    char dir[512] = {};
    ctl_get_socket_dir(dir, sizeof(dir));
    snprintf(out, sz, "%s/%d.sock", dir, pid);
}

[[nodiscard]] char *ctl_serialize_state_response(bool ok, const char *err, const ctl_viewer_state *st) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "ok", ok);
    if (err && *err) yyjson_mut_obj_add_str(doc, root, "error", err);
    if (st) {
        yyjson_mut_val *s = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_int(doc, s, "pid", st->pid);
        yyjson_mut_obj_add_str(doc, s, "path", st->path);
        yyjson_mut_obj_add_str(doc, s, "name", st->name);
        yyjson_mut_obj_add_str(doc, s, "file_type", st->file_type);
        yyjson_mut_obj_add_int(doc, s, "page", st->page);
        yyjson_mut_obj_add_int(doc, s, "pages", st->pages);
        yyjson_mut_obj_add_str(doc, s, "fit", st->fit);
        yyjson_mut_obj_add_str(doc, s, "tint", st->tint);
        yyjson_mut_obj_add_str(doc, s, "view", st->view);
        yyjson_mut_obj_add_str(doc, s, "dual", st->dual);
        yyjson_mut_obj_add_str(doc, s, "half", st->half);
        yyjson_mut_obj_add_real(doc, s, "zoom", st->zoom);
        yyjson_mut_obj_add_int(doc, s, "html_width", st->html_width);
        yyjson_mut_obj_add_real(doc, s, "crop_top", st->crop.top);
        yyjson_mut_obj_add_real(doc, s, "crop_bottom", st->crop.bottom);
        yyjson_mut_obj_add_real(doc, s, "crop_left", st->crop.left);
        yyjson_mut_obj_add_real(doc, s, "crop_right", st->crop.right);
        if (st->search[0]) yyjson_mut_obj_add_str(doc, s, "search", st->search);
        yyjson_mut_obj_add_int(doc, s, "search_hits", st->search_hits);
        yyjson_mut_obj_add_int(doc, s, "search_index", st->search_index);
        yyjson_mut_obj_add_bool(doc, s, "reflowable", st->reflowable);
        yyjson_mut_obj_add_bool(doc, s, "is_image", st->is_image);
        if (st->agterm_session[0]) yyjson_mut_obj_add_str(doc, s, "agterm_session", st->agterm_session);
        yyjson_mut_obj_add_str(doc, s, "socket", st->socket);
        yyjson_mut_obj_add_val(doc, root, "state", s);
    }
    char *out = yyjson_mut_write(doc, 0, nullptr);
    yyjson_mut_doc_free(doc);
    return out;
}

[[nodiscard]] char *ctl_serialize_toc_response(bool ok, const char *err, const doc_toc_entry *toc, size_t count) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "ok", ok);
    if (err && *err) yyjson_mut_obj_add_str(doc, root, "error", err);
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    for (size_t i = 0; i < count; i++) {
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_int(doc, item, "level", toc[i].level);
        yyjson_mut_obj_add_str(doc, item, "title", toc[i].title ? toc[i].title : "");
        yyjson_mut_obj_add_int(doc, item, "page", toc[i].page);
        yyjson_mut_arr_append(arr, item);
    }
    yyjson_mut_obj_add_val(doc, root, "toc", arr);
    char *out = yyjson_mut_write(doc, 0, nullptr);
    yyjson_mut_doc_free(doc);
    return out;
}

[[nodiscard]] char *ctl_serialize_simple_response(bool ok, const char *err) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "ok", ok);
    if (err && *err) yyjson_mut_obj_add_str(doc, root, "error", err);
    char *out = yyjson_mut_write(doc, 0, nullptr);
    yyjson_mut_doc_free(doc);
    return out;
}

[[nodiscard]] bool ctl_parse_request(const char *json, char *cmd, size_t cmd_sz, char *args, size_t args_sz) {
    if (!json || !cmd || !cmd_sz) return false;
    cmd[0] = '\0';
    if (args && args_sz) args[0] = '\0';
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    if (!doc) return false;
    yyjson_val *root = yyjson_doc_get_root(doc);
    const char *c = yyjson_get_str(yyjson_obj_get(root, "cmd"));
    if (c) snprintf(cmd, cmd_sz, "%s", c);
    yyjson_val *a = yyjson_obj_get(root, "args");
    if (a && args && args_sz > 0) {
        char *w = yyjson_val_write(a, 0, nullptr);
        if (w) { snprintf(args, args_sz, "%s", w); free(w); }
    }
    yyjson_doc_free(doc);
    return cmd[0] != '\0';
}

[[nodiscard]] bool ctl_parse_state_response(const char *json, ctl_viewer_state *st) {
    if (!json || !st) return false;
    memset(st, 0, sizeof(*st));
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    if (!doc) return false;
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *s = yyjson_obj_get(root, "state");
    if (!s || !yyjson_is_obj(s)) { yyjson_doc_free(doc); return false; }
    st->pid = yyjson_get_int(yyjson_obj_get(s, "pid"));
    const char *v;
    if ((v = yyjson_get_str(yyjson_obj_get(s, "path")))) snprintf(st->path, sizeof(st->path), "%s", v);
    if ((v = yyjson_get_str(yyjson_obj_get(s, "name")))) snprintf(st->name, sizeof(st->name), "%s", v);
    if ((v = yyjson_get_str(yyjson_obj_get(s, "file_type")))) snprintf(st->file_type, sizeof(st->file_type), "%s", v);
    st->page = yyjson_get_int(yyjson_obj_get(s, "page"));
    st->pages = yyjson_get_int(yyjson_obj_get(s, "pages"));
    if ((v = yyjson_get_str(yyjson_obj_get(s, "fit")))) snprintf(st->fit, sizeof(st->fit), "%s", v);
    if ((v = yyjson_get_str(yyjson_obj_get(s, "tint")))) snprintf(st->tint, sizeof(st->tint), "%s", v);
    if ((v = yyjson_get_str(yyjson_obj_get(s, "view")))) snprintf(st->view, sizeof(st->view), "%s", v);
    if ((v = yyjson_get_str(yyjson_obj_get(s, "dual")))) snprintf(st->dual, sizeof(st->dual), "%s", v);
    if ((v = yyjson_get_str(yyjson_obj_get(s, "half")))) snprintf(st->half, sizeof(st->half), "%s", v);
    st->zoom = yyjson_get_num(yyjson_obj_get(s, "zoom"));
    st->html_width = yyjson_get_int(yyjson_obj_get(s, "html_width"));
    st->crop.top = yyjson_get_num(yyjson_obj_get(s, "crop_top"));
    st->crop.bottom = yyjson_get_num(yyjson_obj_get(s, "crop_bottom"));
    st->crop.left = yyjson_get_num(yyjson_obj_get(s, "crop_left"));
    st->crop.right = yyjson_get_num(yyjson_obj_get(s, "crop_right"));
    if ((v = yyjson_get_str(yyjson_obj_get(s, "search")))) snprintf(st->search, sizeof(st->search), "%s", v);
    st->search_hits = yyjson_get_int(yyjson_obj_get(s, "search_hits"));
    st->search_index = yyjson_get_int(yyjson_obj_get(s, "search_index"));
    st->reflowable = yyjson_get_bool(yyjson_obj_get(s, "reflowable"));
    st->is_image = yyjson_get_bool(yyjson_obj_get(s, "is_image"));
    if ((v = yyjson_get_str(yyjson_obj_get(s, "agterm_session")))) snprintf(st->agterm_session, sizeof(st->agterm_session), "%s", v);
    if ((v = yyjson_get_str(yyjson_obj_get(s, "socket")))) snprintf(st->socket, sizeof(st->socket), "%s", v);
    yyjson_doc_free(doc);
    return true;
}

[[nodiscard]] bool ctl_parse_toc_response(const char *json, doc_toc_entry **out, size_t *out_cnt) {
    if (!json || !out || !out_cnt) return false;
    *out = nullptr; *out_cnt = 0;
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    if (!doc) return false;
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *arr = yyjson_obj_get(root, "toc");
    if (!arr || !yyjson_is_arr(arr)) { yyjson_doc_free(doc); return false; }
    size_t sz = yyjson_arr_size(arr);
    auto entries = (doc_toc_entry *)calloc(sz ? sz : 1, sizeof(doc_toc_entry));
    if (!entries) { yyjson_doc_free(doc); return false; }
    size_t idx, max_idx;
    yyjson_val *it;
    yyjson_arr_foreach(arr, idx, max_idx, it) {
        entries[idx].level = yyjson_get_int(yyjson_obj_get(it, "level"));
        const char *t = yyjson_get_str(yyjson_obj_get(it, "title"));
        entries[idx].title = strdup(t ? t : "");
        entries[idx].page = yyjson_get_int(yyjson_obj_get(it, "page"));
    }
    *out = entries;
    *out_cnt = sz;
    yyjson_doc_free(doc);
    return true;
}

[[nodiscard]] const char *ctl_args_get(const char *args, const char *key, char *out, size_t sz) {
    if (!args || !key || !out || !sz) return nullptr;
    out[0] = '\0';
    yyjson_doc *doc = yyjson_read(args, strlen(args), 0);
    if (!doc) return nullptr;
    yyjson_val *v = yyjson_obj_get(yyjson_doc_get_root(doc), key);
    if (v) {
        if (yyjson_is_str(v)) snprintf(out, sz, "%s", yyjson_get_str(v));
        else if (yyjson_is_int(v)) snprintf(out, sz, "%d", yyjson_get_int(v));
        else if (yyjson_is_real(v)) snprintf(out, sz, "%g", yyjson_get_real(v));
        else if (yyjson_is_bool(v)) snprintf(out, sz, "%s", yyjson_get_bool(v) ? "true" : "false");
    }
    yyjson_doc_free(doc);
    return out[0] ? out : nullptr;
}
