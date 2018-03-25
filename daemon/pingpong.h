#ifndef PINGPONG_H
#define PINGPONG_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <malloc.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <time.h>

#include "pingpong_utils.h"

#define RING_BUFFER_SIZE 128		// number of requests the ring can hold

static const int BUF_SIZE = 10;
static const int BUF_READ_SIZE = 5;

/* host request message definition shared between the arbiter and the pacer */
enum host_request_type {
    FLOW_JOIN = 0,
    FLOW_EXIT = 1,
	RMF_EXCEED_TARGET = 2
};

struct host_request {                       /* request sent from host pacer */
    uint32_t request_id;                    /* TODO: handle overflow later */
    enum host_request_type req_type;
    uint8_t is_read;
    uint8_t dest_qp_num;
};

struct request_ring_buffer {
	struct host_request host_req[RING_BUFFER_SIZE];
	uint16_t head;					/* where CA polls */
	uint16_t tail;					/* where sender writes */
	uint16_t sender_head;			/* where sender stops writing */
};
/* end of host request message definition */

struct pingpong_context {
	struct ibv_context		*context;
	struct ibv_pd			*pd;
	struct ibv_mr			*rmf_mr;
	struct ibv_mr			*req_mr;
	struct ibv_cq			*cq_rmf;
	struct ibv_cq			*cq_req;
	struct ibv_qp			*qp_rmf;
	struct ibv_qp			*qp_req;
	struct pingpong_dest 	*rem_dest;
	void			    	*rmf_buf;
	struct ibv_port_attr	portinfo;
};

struct pingpong_dest {
    int lid;
	int qpn_rmf;
	int qpn_req;
	int psn;
	unsigned rkey_rmf;
	unsigned rkey_req;
	unsigned long long vaddr_rmf;
	unsigned long long vaddr_req;
	union ibv_gid gid;
};

struct pingpong_context *init_ctx_and_build_conn(const char *, int, int, struct host_request *);

#endif