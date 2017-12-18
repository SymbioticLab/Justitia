#ifndef MONITOR_H
#define MONITOR_H

struct monitor_param {
    char *addr;
    int  isclient;
};

static const int THRESHOLD = 1;

void monitor_latency(void *);

#endif