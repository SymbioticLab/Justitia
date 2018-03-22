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

static const int BUF_SIZE = 10;
static const int BUF_READ_SIZE = 5;

struct host_request;
struct host_info;

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

#endif