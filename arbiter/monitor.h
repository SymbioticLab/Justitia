#ifndef MONITOR_H
#define MONITOR_H

struct monitor_param {
    char *addr;
    int  isclient;
    int gid_idx;
};

void monitor_latency(void *);

#endif