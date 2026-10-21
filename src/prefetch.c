#include "pdfcli/prefetch.h"
#include "pdfcli/img_util.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

struct prefetch_job {
    int page_num, cols, rows;
    fit_mode fit;
    tint_mode tint;
    double zoom, dpi;
    crop_box crop;
    struct prefetch_job *next;
};

struct prefetch_queue {
    doc_handle *doc;
    page_cache *cache;
    int notify_fd;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool running;
    struct prefetch_job *job_head, *job_tail;
};

static void *prefetch_worker(void *arg) {
    auto pq = (prefetch_queue *)arg;
    while (true) {
        pthread_mutex_lock(&pq->lock);
        while (pq->running && !pq->job_head) {
            pthread_cond_wait(&pq->cond, &pq->lock);
        }
        if (!pq->running) {
            pthread_mutex_unlock(&pq->lock);
            break;
        }

        // Pop job and immediately release mutex to minimize lock hold duration
        struct prefetch_job *job_node = pq->job_head;
        pq->job_head = job_node->next;
        if (!pq->job_head) pq->job_tail = nullptr;
        pthread_mutex_unlock(&pq->lock);

        struct prefetch_job job = *job_node;
        free(job_node);

        char key[256];
        cache_make_key(key, sizeof(key), job.page_num, job.fit, job.tint, job.zoom, &job.crop, job.cols, job.rows, 0);

        // Fast O(1) check: if already cached, skip rendering
        if (pq->cache && cache_contains(pq->cache, key)) {
            continue;
        }

        fz_context *ctx = doc_get_context(pq->doc);
        if (!ctx) continue;

        fz_pixmap *pix = doc_render_page_pixmap(pq->doc, job.page_num, 1.0, job.dpi);
        if (pix) {
            if (job.crop.top > 0.0 || job.crop.bottom > 0.0 || job.crop.left > 0.0 || job.crop.right > 0.0) {
                fz_pixmap *cropped = img_crop(ctx, pix, &job.crop);
                if (cropped) {
                    fz_drop_pixmap(ctx, pix);
                    pix = cropped;
                }
            }
            if (job.tint == TINT_INVERT) img_simple_invert(ctx, pix);
            else if (job.tint == TINT_DARK) img_smart_invert(ctx, pix);
            else if (job.tint == TINT_DIM) img_dim_tint(ctx, pix);

            cache_put(pq->cache, ctx, key, pix, nullptr);
            fz_drop_pixmap(ctx, pix);

            if (pq->notify_fd >= 0) {
                uint64_t val = 1;
                (void)write(pq->notify_fd, &val, sizeof(val));
            }
        }
    }
    return nullptr;
}

[[nodiscard]] prefetch_queue *prefetch_create(doc_handle *doc, page_cache *cache, int notify_fd) {
    auto pq = (prefetch_queue *)calloc(1, sizeof(prefetch_queue));
    if (!pq) return nullptr;
    pq->doc = doc;
    pq->cache = cache;
    pq->notify_fd = notify_fd;
    pq->running = true;
    pthread_mutex_init(&pq->lock, nullptr);
    pthread_cond_init(&pq->cond, nullptr);
    if (pthread_create(&pq->thread, nullptr, prefetch_worker, pq) != 0) {
        pthread_mutex_destroy(&pq->lock);
        pthread_cond_destroy(&pq->cond);
        free(pq);
        return nullptr;
    }
    return pq;
}

void prefetch_cancel_pending(prefetch_queue *pq) {
    if (!pq) return;
    pthread_mutex_lock(&pq->lock);
    struct prefetch_job *jobs = pq->job_head;
    pq->job_head = pq->job_tail = nullptr;
    pthread_mutex_unlock(&pq->lock);

    while (jobs) {
        struct prefetch_job *nxt = jobs->next;
        free(jobs);
        jobs = nxt;
    }
}

void prefetch_destroy(prefetch_queue *pq) {
    if (!pq) return;
    pthread_mutex_lock(&pq->lock);
    pq->running = false;
    pthread_cond_signal(&pq->cond);
    pthread_mutex_unlock(&pq->lock);

    pthread_join(pq->thread, nullptr);
    prefetch_cancel_pending(pq);
    pthread_mutex_destroy(&pq->lock);
    pthread_cond_destroy(&pq->cond);
    free(pq);
}

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
) {
    if (!pq) return;

    // Fast O(1) check: avoid redundant enqueue if page is already present in cache
    char key[256];
    cache_make_key(key, sizeof(key), page_num, fit, tint, zoom, crop, cols, rows, 0);
    if (pq->cache && cache_contains(pq->cache, key)) {
        return;
    }

    pthread_mutex_lock(&pq->lock);
    if (!pq->running) {
        pthread_mutex_unlock(&pq->lock);
        return;
    }

    // Avoid duplicate pending jobs in the queue
    for (const struct prefetch_job *cur = pq->job_head; cur; cur = cur->next) {
        if (cur->page_num == page_num &&
            cur->fit == fit &&
            cur->tint == tint &&
            cur->cols == cols &&
            cur->rows == rows &&
            cur->zoom == zoom &&
            cur->dpi == dpi) {
            pthread_mutex_unlock(&pq->lock);
            return;
        }
    }

    auto job = (struct prefetch_job *)malloc(sizeof(struct prefetch_job));
    if (!job) {
        pthread_mutex_unlock(&pq->lock);
        return;
    }
    *job = (struct prefetch_job){
        .page_num = page_num,
        .cols = cols,
        .rows = rows,
        .fit = fit,
        .tint = tint,
        .zoom = zoom,
        .crop = crop ? *crop : (crop_box){},
        .dpi = dpi,
        .next = nullptr
    };

    if (pq->job_tail) pq->job_tail->next = job;
    else pq->job_head = job;
    pq->job_tail = job;

    pthread_cond_signal(&pq->cond);
    pthread_mutex_unlock(&pq->lock);
}
