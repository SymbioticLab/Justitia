#ifndef MONITOR_H
#define MONITOR_H

struct monitor_param {
    int is_client;
    const char *server_addr;
    int num_clients;
    int num_servers;
    int gid_idx;
};

void monitor_latency(void *);
void server_loop(void *);

#endif