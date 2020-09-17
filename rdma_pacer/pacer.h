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
#define MAX_CLIENTS 16      // clients per server
#define MAX_SERVERS 4       // servers (receivers) per clients
//#define LINE_RATE_MB 12000 /* MBps */     // 100Gbps
//#define LINE_RATE_MB 1100 /* MBps */      // 10Gbps
//#define LINE_RATE_MB 4400 /* MBps */      // 40Gbps
#define LINE_RATE_MB 6000 /* MBps */        // 56Gbps
#define MSG_LEN 24
#define SOCK_PATH "/users/yiwenzhg/rdma_socket"
#define ELEPHANT_HAS_LOWER_BOUND 1  /* whether elephant has a minimum virtual link cap set by AIMD */
#define TABLE_SIZE 7
//#define FAVOR_BIG_FLOW
//#define SMART_RMF
//#define USE_TIMEFRAME
//#define DYNAMIC_NUM_SPLIT_QPS
//#define DEFAULT_NUM_SPLIT_QPS 2     // default vaule when not use DYNAMIC_NUM_SPLIT_QPS
#define DEFAULT_NUM_SPLIT_QPS 1     // Now never use more than 1 SQPs
#define MAX_NUM_SPLIT_QPS 4         // qp = 3, 4 or above is not very helpful
#define CPU_FRIENDLY                //// Don't not use busy-wait checking for "pending" in shared memory. Use UDS with token enforcement.
//#define DYNAMIC_CPU_OPT           // dymanically change shaper local busy waiting interval; similar to dynamical num split qps adjustment
#define MAX_SPLIT_LEVEL 5           // maximum possible split level
#define DEFAULT_SPLIT_LEVEL 2       // default split level used when DYNAMIC_CPU_OPT is OFF
#define MIN_SPLIT_LEVEL 2           // minimun split level when small flows are present (so always use smaller chunks in such cases)
//#define INCAST_HACK
#define INCAST_ACTIVE_CHUNK_SIZE 1000
#define INCAST_SAFEUTIL 550
//#define INCAST_SAFEUTIL 1100
#define INCAST_SPLIT_LEVEL 2
#define TREAT_L_AS_ONE

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
    uint32_t virtual_link_cap;
    //uint16_t num_active_split_qps;         /* added to dynamically change number of split qps */
    uint16_t num_active_big_flows;         /* incremented when an elephant or tput flow first sends a message */
    uint16_t num_active_small_flows;       /* incremented when a mouse first sends a message */
    uint16_t num_active_bw_flows;         /* incremented when an elephant first sends a message */
    uint16_t split_level;
};

struct control_block {
    struct shared_block *sb;

    //struct pingpong_context *ctx;           // used by each client
    struct pingpong_context *ctx_per_server[MAX_SERVERS];           // used by each client
    struct pingpong_context *ctx_per_client[MAX_CLIENTS];           // used by the server
    pid_t pid_list[MAX_FLOWS];             /* used to map pid to slot; index is the slot number; treat flows from the same process as one */
    uint64_t tokens;                       /* number of available tokens */
    uint64_t tokens_read;
    uint64_t app_vaddrs[MAX_SERVERS];           // used to compare and find which flow/app sends to which direction
    //uint32_t virtual_link_cap;           /* capacity of the virtual link that elephants go through */ /* moved to sb */
    uint32_t remote_read_rate;             /* remote read rate */
    uint32_t local_read_rate;
    uint16_t next_slot;
    uint16_t num_big_read_flows;
    uint16_t num_receiver_big_flows[MAX_SERVERS];        // big: bw + tput; received from receiver; Note: this value also includes this sender's local big flow
    uint16_t num_receiver_small_flows[MAX_SERVERS];      // small: lat
};

extern struct control_block cb;            /* declaration */
extern uint32_t chunk_size_table[TABLE_SIZE];