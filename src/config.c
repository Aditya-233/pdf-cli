#include "pdfcli/config.h"
#include <yyjson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

static void path_hash_hex(const char *path, char out[33]) {
    uint64_t h1 = 0xcbf29ce484222325ULL, h2 = 0x100000001b3ULL;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        h1 = (h1 ^ *p) * 0x100000001b3ULL;
        h2 = (h2 ^ (*p + 0x5a)) * 0xcbf29ce484222325ULL;
    }
    snprintf(out, 33, "%016lx%016lx", (unsigned long)h1, (unsigned long)h2);
}

static void get_cfg_path(const char *doc_path, char *out, size_t sz) {
    char hex[33];
    path_hash_hex(doc_path ? doc_path : "", hex);
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    char dir[512];
    if (xdg && *xdg) snprintf(dir, sizeof(dir), "%s/docviewer", xdg);
    else if (home && *home) snprintf(dir, sizeof(dir), "%s/.config/docviewer", home);
    else snprintf(dir, sizeof(dir), "/tmp/docviewer-cfg");
    mkdir(dir, 0700);
    snprintf(out, sz, "%s/%s.json", dir, hex);
}

[[nodiscard]] doc_config config_load(const char *doc_path) {
    doc_config cfg = { .fit_mode = "height", .scale_factor = 1.0, .dark_mode = "off",
                       .dual_page_mode = "off", .force_mode = "auto", .html_page_width = 1000 };
    if (!doc_path) return cfg;
    char path[1024];
    get_cfg_path(doc_path, path, sizeof(path));
    yyjson_doc *doc = yyjson_read_file(path, 0, nullptr, nullptr);
    if (!doc) return cfg;
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (yyjson_is_obj(root)) {
        const char *v;
        if ((v = yyjson_get_str(yyjson_obj_get(root, "fit_mode")))) strncpy(cfg.fit_mode, v, sizeof(cfg.fit_mode) - 1);
        if ((v = yyjson_get_str(yyjson_obj_get(root, "dark_mode")))) strncpy(cfg.dark_mode, v, sizeof(cfg.dark_mode) - 1);
        if ((v = yyjson_get_str(yyjson_obj_get(root, "dual_page_mode")))) strncpy(cfg.dual_page_mode, v, sizeof(cfg.dual_page_mode) - 1);
        if ((v = yyjson_get_str(yyjson_obj_get(root, "force_mode")))) strncpy(cfg.force_mode, v, sizeof(cfg.force_mode) - 1);
        yyjson_val *val;
        if ((val = yyjson_obj_get(root, "scale_factor")) && yyjson_is_num(val)) cfg.scale_factor = yyjson_get_num(val);
        if ((val = yyjson_obj_get(root, "html_page_width")) && yyjson_is_int(val)) cfg.html_page_width = yyjson_get_int(val);
        yyjson_val *crop = yyjson_obj_get(root, "crop");
        if (yyjson_is_obj(crop)) {
            val = yyjson_obj_get(crop, "top"); if (val) cfg.crop.top = yyjson_get_num(val);
            val = yyjson_obj_get(crop, "bottom"); if (val) cfg.crop.bottom = yyjson_get_num(val);
            val = yyjson_obj_get(crop, "left"); if (val) cfg.crop.left = yyjson_get_num(val);
            val = yyjson_obj_get(crop, "right"); if (val) cfg.crop.right = yyjson_get_num(val);
        }
    }
    yyjson_doc_free(doc);
    return cfg;
}

void config_save(const char *doc_path, const doc_config *cfg) {
    if (!doc_path || !cfg) return;
    char path[1024];
    get_cfg_path(doc_path, path, sizeof(path));
    yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "fit_mode", cfg->fit_mode);
    yyjson_mut_obj_add_real(doc, root, "scale_factor", cfg->scale_factor);
    yyjson_mut_obj_add_str(doc, root, "dark_mode", cfg->dark_mode);
    yyjson_mut_obj_add_str(doc, root, "dual_page_mode", cfg->dual_page_mode);
    yyjson_mut_obj_add_str(doc, root, "force_mode", cfg->force_mode);
    yyjson_mut_obj_add_int(doc, root, "html_page_width", cfg->html_page_width);
    yyjson_mut_val *crop = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_real(doc, crop, "top", cfg->crop.top);
    yyjson_mut_obj_add_real(doc, crop, "bottom", cfg->crop.bottom);
    yyjson_mut_obj_add_real(doc, crop, "left", cfg->crop.left);
    yyjson_mut_obj_add_real(doc, crop, "right", cfg->crop.right);
    yyjson_mut_obj_add_val(doc, root, "crop", crop);
    yyjson_mut_write_file(path, doc, YYJSON_WRITE_PRETTY, nullptr, nullptr);
    yyjson_mut_doc_free(doc);
}
