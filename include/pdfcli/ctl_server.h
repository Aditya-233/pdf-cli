#pragma once

#include <stddef.h>
#include <stdbool.h>

typedef struct ctl_server ctl_server;

[[nodiscard]] ctl_server *ctl_server_create(int pid);
[[nodiscard]] int ctl_server_get_fd(const ctl_server *srv);
void ctl_server_destroy(ctl_server *srv);

// Non-blocking accept & read command: returns true if command received
[[nodiscard]] bool ctl_server_recv(ctl_server *srv, int *out_client_fd, char *out_cmd, size_t cmd_size, char *out_args_json, size_t args_size);
void ctl_server_send_response(int client_fd, const char *json_response);
