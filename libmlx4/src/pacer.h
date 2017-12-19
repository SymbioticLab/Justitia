#ifndef PACER_H
#define PACER_H

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <semaphore.h>
#include <inttypes.h>
#include <malloc.h>
#include <pthread.h>

#define SHARED_MEM_NAME "/rdma-fairness"
#define SOCK_PATH "/users/yuetan/rdma_socket"
#define MSG_LEN 8
#define MAX_FLOWS 512

struct flow_info {
    uint64_t bytes;
    uint32_t measured;
    uint32_t target;
    uint32_t chunk_size;
    uint8_t active;
    uint8_t small;
} *flow;

unsigned int slot;

#endif