#pragma once

#include "pdfcli/types.h"

typedef struct doc_config {
    char fit_mode[32];
    double scale_factor;
    char dark_mode[32];
    char dual_page_mode[32];
    char force_mode[32];
    int html_page_width;
    crop_box crop;
} doc_config;

[[nodiscard]] doc_config config_load(const char *abs_doc_path);
void config_save(const char *abs_doc_path, const doc_config *cfg);
