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

#define RMF_DISTRIBUTE_AMONG_HOSTS 1

#define SHARED_MEM_NAME "/rdma-fairness"
#define MAX_FLOWS 512
//#define LINE_RATE_MB 12000 /* MBps */
#define LINE_RATE_MB 6000 /* MBps */
#define MSG_LEN 8
#define SOCK_PATH "/users/yiwenzhg/rdma_socket"
#define ELEPHANT_HAS_LOWER_BOUND 1  /* whether elephant has a minimum virtual link cap set by AIMD */
#define TABLE_SIZE 7

typedef struct flow flow_t;
typedef struct port port_t;

struct request_ring_buffer {
	uint16_t head;					/* where CA polls */
	struct host_request host_req[RING_BUFFER_SIZE];
};

/* represent a set of flows starting/ending from/to a particular host */
struct flow {
    uint8_t is_assigned;                    /* whether this flow has benn assigned by the rate computation algorithm */
    uint8_t in_transit;                     /* whether the slot in the flow array can be used */
    //uint8_t is_read;
    //uint16_t remote_host;                   /* receiver in egress port; sender in ingress port */
    uint16_t src;                           /* src and dest indicates the direction of the data flow */
    uint16_t dest;
    uint16_t flow_cnt;
    uint32_t rate;                          /* aggregate rate */
};

struct port {
    uint8_t is_assigned;                    /* whether this port has been assigned by the rate computation algorithm */
    uint32_t unassigned_flows;              /* number of flows that haven't been assigned */
    //flow_t *flows;                          /* a table of flows. size = # of hosts in the cluster. */
    vector_t flows;
    //uint32_t flow_map;                      /* table of flows. value in each slot is flow count. For egress port, idx =: receiver_host; for ingress port, idx =: sender_host */
    uint32_t rate;                          /* rate of the port. default to line rate */
};

struct host_info {
    uint16_t lid;                           /* lid of this host */
    struct request_ring_buffer *ring;       /* points to the ring buffer containing the *MR* for hosts to update info via RDMA; defined in pingpong.h*/
    struct pingpong_context *ctx;           /* other rdma related ctx goes here */
    port_t *ingress_port;
    port_t *egress_port;
    struct arbiter_response ca_resp;        /* pinned for response send back to the host */
};

struct cluster_info {                       /* global knowledge of CA */
    uint16_t num_hosts;
    struct host_info *hosts;                /* array of structures containning info of each host in the cluster */
    //uint32_t *host_port_table;          /* flow cnt for ingress and egress port for each host in the cluster */
    //unsigned int *switch_port_table;        /* flow cnt for egress ports at the switch */
    flow_t flows[MAX_FLOWS];
    uint16_t next_slot;
};

extern struct cluster_info cluster;