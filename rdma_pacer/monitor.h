#ifndef MONITOR_H
#define MONITOR_H

struct monitor_param {
    char *addr;
    int isclient;      // yiwen: isclient now is also used to indicate whether the node acts as a "receiver"; TODO: make this more general later
    int gid_idx;
};

void monitor_latency(void *);

#endif