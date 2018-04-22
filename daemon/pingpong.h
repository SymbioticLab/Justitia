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
#define MAX_RATE_UPDATES 128		// maximum number of rate updates in one response

static const int BUF_SIZE = 10;
static const int BUF_READ_SIZE = 5;

/* host request message definition shared between the arbiter and the pacer */
enum host_request_type {
    FLOW_JOIN = 0,
    FLOW_EXIT = 1,
	RMF_ABOVE_TARGET = 2,	/* ABOVE means worse (larger) tail */
	RMF_BELOW_TARGET = 3
};

struct host_request {                       /* request sent from host pacer */
	//uint8_t num_req;						/* number of requests to come */
    enum host_request_type type;
    uint16_t dlid;
	uint16_t flow_idx;						/* idx in the flow array at the host pacer, needed when hearing back from arbiter */
    uint8_t is_read;
	uint8_t check_byte;						/* indicates completion */
};

struct arbiter_rate_update {
	uint32_t rate;
	uint16_t flow_idx;						/* idx in the flow array at the host pacer */
};

/* response message header that arbiter WRITEs to host pacer, followed by rate updates */
struct arbiter_response_header {
	uint16_t sender_head;					/* where host pacer stops writing */
	uint16_t num_rate_updates;				/* number of rate updates (per flow) coming in the response */
	uint32_t id;							/* response id */
};

/* memory region for arbiter responses */
struct arbiter_response_region {
	struct arbiter_response_header header;
	struct arbiter_rate_update rate_updates[MAX_RATE_UPDATES];
};


struct pingpong_context {
	struct ibv_context		*context;
	struct ibv_pd			*pd;
	struct ibv_mr			*rmf_mr;
	struct ibv_mr			*req_mr;
	struct ibv_mr			*resp_mr;
	struct ibv_cq			*cq_rmf;
	struct ibv_cq			*cq_req;
	struct ibv_qp			*qp_rmf;
	struct ibv_qp			*qp_req;
	struct pingpong_dest 	*rem_dest;
	struct pingpong_dest 	*rem_host_dest;		/* used by host to for conn with another host (for rmf) */
	int						rmf_choice;
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
	unsigned rkey_resp;
	unsigned long long vaddr_rmf;
	unsigned long long vaddr_req;
	unsigned long long vaddr_resp;
	union ibv_gid gid;
};

struct pingpong_context *init_ctx_and_build_conn(const char *, const char *, int, int, struct host_request *, struct arbiter_response_region *, char);

#endif