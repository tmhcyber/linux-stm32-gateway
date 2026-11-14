#ifndef GATEWAY_IPC_H
#define GATEWAY_IPC_H
#include "output.h"
#include <poll.h>
typedef struct Ipc Ipc;
#define IPC_POLL_MAX 10
Ipc *ipc_open(const char *path);
size_t ipc_pollfds(Ipc *ipc, Storage *storage, struct pollfd *fds);
void ipc_dispatch(Ipc *ipc, Output *output, struct pollfd *fds);
void ipc_close(Ipc *ipc);
#endif
