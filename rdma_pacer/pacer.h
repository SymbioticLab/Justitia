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

#define SHARED_MEM_NAME "/rdma-fairness"
#define MAX_FLOWS 512
#define LINE_RATE_GB 56 /* Gb/s */
#define LINE_RATE_MB 6000 /* MB/s */
#define LINE_RATE_KB 7340032 /* KB/s */
#define MSG_LEN 8
#define DEFAULT_CHUNK_SIZE 1048576
#define SOCK_PATH "/users/yuetan/rdma_socket"

struct flow_info {
    uint64_t bytes;
    uint32_t measured;
    uint32_t target;
    uint32_t chunk_size;
    uint8_t active;

};

static struct control_block {
    struct flow_info *flows;
    double unused;
    double redistributed;
    uint32_t active_chunk_size;
    uint16_t next_slot;
    uint16_t num_active_flows;
    uint16_t num_saturated;
    uint8_t test;
    uint8_t true;
} cb;

