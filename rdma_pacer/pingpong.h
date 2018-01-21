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

struct pingpong_context {
	struct ibv_context		*context;
	struct ibv_pd			*pd;
	struct ibv_pd           *pd_read;
	struct ibv_mr			*send_mr;
	struct ibv_mr			*recv_mr;
	struct ibv_mr 			*local_read_mr;
	struct ibv_mr           *remote_read_mr;
	struct ibv_cq			*cq;
	struct ibv_qp			*qp;
	struct ibv_cq           *cq_read;
	struct ibv_qp			*qp_read;
	struct pingpong_dest 	*rem_dest;
	void			    	*send_buf;
	void					*recv_buf;
	void                    *local_read_buf;
	void                    *remote_read_buf;
	struct ibv_port_attr	portinfo;
};

struct pingpong_dest {
    int lid;
	int qpn;
	int qpn_read;
	int psn;
	unsigned rkey;
	unsigned long long vaddr;
	union ibv_gid gid;
};

struct pingpong_context * init_monitor_chan(const char *, int);

#endif