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
//#define LINE_RATE_MB 12000 /* MBps */
#define LINE_RATE_MB 6000 /* MBps */
#define MSG_LEN 8
#define SOCK_PATH "/users/yiwenzhg/rdma_socket"
#define ELEPHANT_HAS_LOWER_BOUND 1  /* whether elephant has a minimum virtual link cap set by AIMD */
#define TABLE_SIZE 7

struct request_ring_buffer {
	uint16_t head;					/* where CA polls */
	struct host_request host_req[RING_BUFFER_SIZE];
};

struct host_info {
    struct request_ring_buffer *ring;       /* points to the ring buffer containing the *MR* for hosts to update info via RDMA; defined in pingpong.h*/ 
    struct arbiter_response ca_resp;       /* pinned for response send back to the host */
    uint16_t *flow_map;                     /* an array keeping track of flows sending to other host in the cluster */
    struct pingpong_context *ctx;           /* other rdma related ctx goes here */
};

struct cluster_info {                       /* global knowledge of CA */
    uint16_t num_hosts;
    struct host_info *hosts;                /* array of structures containning info of each host in the cluster */
};

extern struct cluster_info cluster;