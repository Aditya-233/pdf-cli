#include "pdfcli/file_watcher.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/inotify.h>

struct file_watcher {
    char *file_path;
    char *file_name;
    int inotify_fd;
    int watch_wd;
    time_t last_mtime;
    long last_mtime_nsec;
};

[[nodiscard]] file_watcher *file_watcher_create(const char *file_path) {
    if (!file_path) return nullptr;
    auto fw = (file_watcher *)calloc(1, sizeof(file_watcher));
    if (!fw) return nullptr;
    fw->file_path = strdup(file_path);
    char *c1 = strdup(file_path);
    char *c2 = strdup(file_path);
    fw->file_name = strdup(basename(c2));
    struct stat st;
    if (stat(file_path, &st) == 0) {
        fw->last_mtime = st.st_mtime;
        fw->last_mtime_nsec = st.st_mtim.tv_nsec;
    }
    fw->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    fw->watch_wd = (fw->inotify_fd >= 0) ? inotify_add_watch(fw->inotify_fd, dirname(c1), IN_CLOSE_WRITE | IN_MOVED_TO | IN_MODIFY | IN_ATTRIB) : -1;
    free(c1);
    free(c2);
    return fw;
}

[[nodiscard]] int file_watcher_get_fd(const file_watcher *fw) {
    return fw ? fw->inotify_fd : -1;
}

[[nodiscard]] bool file_watcher_check_modified(file_watcher *fw) {
    if (!fw) return false;
    bool modified = false;
    if (fw->inotify_fd >= 0) {
        alignas(struct inotify_event) char buf[4096];
        ssize_t len;
        while ((len = read(fw->inotify_fd, buf, sizeof(buf))) > 0) {
            for (char *p = buf; p < buf + len;) {
                auto ev = (const struct inotify_event *)p;
                if (ev->len > 0 && strcmp(ev->name, fw->file_name) == 0) modified = true;
                p += sizeof(struct inotify_event) + ev->len;
            }
        }
    }
    struct stat st;
    if (stat(fw->file_path, &st) == 0) {
        if (st.st_mtime > fw->last_mtime || (st.st_mtime == fw->last_mtime && st.st_mtim.tv_nsec > fw->last_mtime_nsec)) {
            fw->last_mtime = st.st_mtime;
            fw->last_mtime_nsec = st.st_mtim.tv_nsec;
            return true;
        }
    }
    return modified;
}

void file_watcher_destroy(file_watcher *fw) {
    if (!fw) return;
    if (fw->inotify_fd >= 0) {
        if (fw->watch_wd >= 0) inotify_rm_watch(fw->inotify_fd, fw->watch_wd);
        close(fw->inotify_fd);
    }
    free(fw->file_path);
    free(fw->file_name);
    free(fw);
}
