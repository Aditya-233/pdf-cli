#include "pdfcli/ctl_server.h"
#include "pdfcli/ctl_proto.h"
#include "pdfcli/profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/time.h>

struct ctl_server {
    int listen_fd;
    int pid;
    char socket_path[1024];
};

[[nodiscard]] ctl_server *ctl_server_create(int pid) {
    if (pid <= 0) pid = (int)getpid();
    auto srv = (ctl_server *)calloc(1, sizeof(ctl_server));
    if (!srv) return nullptr;
    srv->listen_fd = -1;
    srv->pid = pid;
    char dir[512] = {};
    ctl_get_socket_dir(dir, sizeof(dir));
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0700); *p = '/'; }
    }
    mkdir(tmp, 0700);
    ctl_get_socket_path(pid, srv->socket_path, sizeof(srv->socket_path));
    unlink(srv->socket_path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { free(srv); return nullptr; }
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    size_t plen = strlen(srv->socket_path);
    if (plen >= sizeof(addr.sun_path)) plen = sizeof(addr.sun_path) - 1;
    memcpy(addr.sun_path, srv->socket_path, plen);
    addr.sun_path[plen] = '\0';
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 16) != 0) {
        close(fd); unlink(srv->socket_path); free(srv); return nullptr;
    }
    srv->listen_fd = fd;
    return srv;
}

[[nodiscard]] int ctl_server_get_fd(const ctl_server *srv) { return srv ? srv->listen_fd : -1; }

void ctl_server_destroy(ctl_server *srv) {
    if (!srv) return;
    if (srv->listen_fd >= 0) close(srv->listen_fd);
    if (srv->socket_path[0]) unlink(srv->socket_path);
    free(srv);
}

[[nodiscard]] bool ctl_server_recv(ctl_server *srv, int *out_client_fd, char *cmd, size_t cmd_sz, char *args, size_t args_sz) {
    PROFILE_SCOPE("ipc", "ctl_server_recv");
    if (!srv || srv->listen_fd < 0 || !out_client_fd || !cmd) return false;
    *out_client_fd = -1;
    int client = accept(srv->listen_fd, nullptr, nullptr);
    if (client < 0) return false;
    struct timeval tv = { .tv_sec = 2 };
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char buf[4096] = {};
    size_t total = 0;
    while (total + 1 < sizeof(buf)) {
        ssize_t n = read(client, buf + total, sizeof(buf) - 1 - total);
        if (n > 0) { total += (size_t)n; if (strchr(buf, '\n')) break; }
        else if (n < 0 && errno == EINTR) continue;
        else break;
    }
    char *nl = strpbrk(buf, "\r\n");
    if (nl) *nl = '\0';
    if (!total || !ctl_parse_request(buf, cmd, cmd_sz, args, args_sz)) {
        ctl_server_send_response(client, "{\"ok\":false,\"error\":\"bad request\"}");
        return false;
    }
    *out_client_fd = client;
    return true;
}

void ctl_server_send_response(int client_fd, const char *json) {
    PROFILE_SCOPE("ipc", "ctl_server_send_response");
    if (client_fd < 0) return;
    if (json) {
        size_t len = strlen(json), off = 0;
        while (off < len) {
            ssize_t n = write(client_fd, json + off, len - off);
            if (n > 0) off += (size_t)n;
            else if (n < 0 && errno == EINTR) continue;
            else break;
        }
        (void)write(client_fd, "\n", 1);
    }
    close(client_fd);
}
