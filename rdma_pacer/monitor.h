#ifndef MONITOR_H
#define MONITOR_H

#define MAX_CLIENTS 32

struct monitor_param {
    int is_client;
    const char *server_addr;
    const char *client_addrs[MAX_CLIENTS];
    int num_clients;
    int gid_idx;
};

void monitor_latency(void *);

#endif