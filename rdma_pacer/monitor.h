#ifndef MONITOR_H
#define MONITOR_H

struct monitor_param {
    char *addr;
    int  isclient;
};

void monitor_latency(void *);

extern struct control_block cb;

#endif