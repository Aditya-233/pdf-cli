#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "pdfcli/types.h"
#include "pdfcli/doc_engine.h"

typedef struct ctl_viewer_state {
    int pid;
    char path[1024];
    char name[256];
    char file_type[32];
    int page;
    int pages;
    char fit[32];
    char tint[32];
    char view[32];
    char dual[32];
    char half[32];
    double zoom;
    int html_width;
    crop_box crop;
    char search[256];
    int search_hits;
    int search_index;
    bool reflowable;
    bool is_image;
    char agterm_session[64];
    char socket[1024];
} ctl_viewer_state;

void ctl_get_socket_dir(char *out_dir, size_t dir_size);
void ctl_get_socket_path(int pid, char *out_path, size_t path_size);

[[nodiscard]] char *ctl_serialize_state_response(bool ok, const char *err_msg, const ctl_viewer_state *st);
[[nodiscard]] char *ctl_serialize_toc_response(bool ok, const char *err_msg, const doc_toc_entry *toc, size_t count);
[[nodiscard]] char *ctl_serialize_simple_response(bool ok, const char *err_msg);
[[nodiscard]] bool ctl_parse_request(const char *json_str, char *out_cmd, size_t cmd_size, char *out_args_json, size_t args_size);

[[nodiscard]] bool ctl_parse_state_response(const char *json_str, ctl_viewer_state *out_state);
[[nodiscard]] bool ctl_parse_toc_response(const char *json_str, doc_toc_entry **out_entries, size_t *out_count);
[[nodiscard]] const char *ctl_args_get(const char *args_json, const char *key, char *out_val, size_t val_size);
