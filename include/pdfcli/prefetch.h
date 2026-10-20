#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "types.h"
#include "cache.h"
#include "doc_engine.h"

typedef struct prefetch_queue prefetch_queue;

[[nodiscard]] prefetch_queue *prefetch_create(doc_handle *doc, page_cache *cache, int notify_fd);
void prefetch_destroy(prefetch_queue *pq);

void prefetch_request(
    prefetch_queue *pq,
    int page_num,
    fit_mode fit,
    tint_mode tint,
    double zoom,
    const crop_box *crop,
    int cols,
    int rows,
    double dpi
);

void prefetch_cancel_pending(prefetch_queue *pq);
