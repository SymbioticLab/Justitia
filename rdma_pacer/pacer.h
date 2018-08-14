#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <inttypes.h>
#include <getopt.h>
#include <malloc.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include "pingpong.h"

#define SHARED_MEM_NAME "/rdma-fairness"
#define MAX_FLOWS 512
//#define LINE_RATE_MB 12000 /* MBps */     // 100Gbps
//#define LINE_RATE_MB 1100 /* MBps */      // 10Gbps
//#define LINE_RATE_MB 4400 /* MBps */      // 40Gbps
#define LINE_RATE_MB 6000 /* MBps */        // 56Gbps
#define MSG_LEN 8
#define SOCK_PATH "/users/yiwenzhg/rdma_socket"
#define ELEPHANT_HAS_LOWER_BOUND 1  /* whether elephant has a minimum virtual link cap set by AIMD */
#define TABLE_SIZE 7
//#define FAVOR_BIG_FLOW

struct flow_info {
    uint8_t pending;
    uint8_t active;
    uint8_t read;
};

struct shared_block {
    struct flow_info flows[MAX_FLOWS];
    uint32_t active_chunk_size;
    uint32_t active_chunk_size_read;
    uint32_t active_batch_ops;
    uint16_t num_active_big_flows;         /* incremented when an elephant first sends a message */
    uint16_t num_active_small_flows;       /* incremented when a mouse first sends a message */
};

struct control_block {
    struct shared_block *sb;

    struct pingpong_context *ctx;
    uint64_t tokens;                       /* number of available tokens */
    uint64_t tokens_read;
    uint32_t virtual_link_cap;             /* capacity of the virtual link that elephants go through */
    uint32_t remote_read_rate;             /* remote read rate */
    uint32_t local_read_rate;    
    uint16_t next_slot;
    uint16_t num_big_read_flows;
};

extern struct control_block cb;            /* declaration */
extern uint32_t chunk_size_table[TABLE_SIZE];