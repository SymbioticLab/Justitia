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
#define HOSTNAME_PATH "/proc/sys/kernel/hostname"

struct flow_info {
    uint8_t pending;
    uint8_t active;
    uint8_t read;
    uint32_t dest_qp_num;
};

struct shared_block {
    struct flow_info flows[MAX_FLOWS];
    uint32_t active_chunk_size;
    uint32_t active_chunk_size_read;
    uint32_t active_batch_ops;
    uint16_t num_active_big_flows;         /* incremented when an elephant first sends a message */
    uint16_t num_active_small_flows;       /* incremented when a mouse first sends a message */
};

extern struct flow_info *flow;     /* declaration; initialization in verbs.c */
extern struct shared_block *sb;    /* declaration; initialization in verbs.c */
extern int start_flag;             /* Initialized in verbs.c */
extern int start_recv;             /* initialized in qp.c */
extern int isSmall;                /* initialized in qp.c */
extern int num_active_small_flows; /* initialized in verbs.c */
extern int num_active_big_flows;   /* initialized in verbs.c */

char *get_sock_path();

#endif
