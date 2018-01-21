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
#define MAX_FLOWS 100

struct flow_info {
    uint8_t pending;
    uint8_t active;
};

struct shared_block {
    struct flow_info flows[MAX_FLOWS];
    uint32_t active_chunk_size;
    uint16_t num_active_big_flows;
    uint16_t num_active_small_flows;
};

extern struct flow_info *flow;     /* declaration; initialization in verbs.c */
extern struct shared_block *sb;    /* declaration; initialization in verbs.c */
extern int start_flag;             /* Initialized in verbs.c */
extern int start_recv;             /* initialized in qp.c */
extern int isSmall;                /* initialized in qp.c */
extern int isRead;                 /* initialized in qp.c */
extern int num_active_small_flows; /* initialized in verbs.c */
extern int num_active_big_flows;   /* initialized in verbs.c */

#endif
