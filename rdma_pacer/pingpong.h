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

struct pingpong_context {
	struct ibv_context		*context;
	struct ibv_pd			*pd;
	struct ibv_mr			*send_mr;
	struct ibv_mr			*recv_mr;
	struct ibv_cq			*cq;
	struct ibv_qp			*qp;
	struct pingpong_dest 	*rem_dest;
	void			    	*send_buf;
	void					*recv_buf;
	struct ibv_port_attr	portinfo;
};

struct pingpong_dest {
    int lid;
	int qpn;
	int psn;
	unsigned rkey;
	unsigned long long vaddr;
	union ibv_gid gid;
};

struct pingpong_context * init_monitor_chan(const char *, int, int);

#endif