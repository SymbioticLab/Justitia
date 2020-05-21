#include "pingpong.h"

static const int port = 18515;
static const int ib_port = 1;
static const int mtu = IBV_MTU_2048;
static const int ib_dev_idx = 0;
//static const int ib_dev_idx = 1;
//static const int ib_dev_idx = 2;  // used in xl170

static struct pingpong_context * alloc_monitor_qp();
static struct pingpong_dest * pp_client_exch_dest(const char *, struct pingpong_dest *);
static struct pingpong_dest * pp_server_exch_dest(struct pingpong_context *, const struct pingpong_dest *, int);
static int pp_connect_ctx(struct pingpong_context *, int, struct pingpong_dest *, int);

struct pingpong_context *init_monitor_chan(const char *addr, int isclient, int gidx){
    struct pingpong_context *ctx;
    struct pingpong_dest my_dest;

    ctx = alloc_monitor_qp();
    if (!ctx)
        return NULL;
    
    if (pp_get_port_info(ctx->context, ib_port, &ctx->portinfo)) {
        fprintf(stderr, "Coundln't get port info\n");
        return NULL;
    }

    printf("%d", isclient);
    my_dest.lid = ctx->portinfo.lid;
    if (gidx >= 0) {
		if (ibv_query_gid(ctx->context, ib_port, gidx, &my_dest.gid)) {
			fprintf(stderr, "Could not get local gid for gid index %d\n", gidx);
			return NULL;
		}
	} else {
		memset(&my_dest.gid, 0, sizeof my_dest.gid);
    }
    my_dest.qpn = ctx->qp->qp_num;
    my_dest.qpn_read = ctx->qp_read->qp_num;
    my_dest.psn = lrand48() & 0xffffff;
    my_dest.rkey = ctx->recv_mr->rkey;
    my_dest.vaddr = (uintptr_t)ctx->recv_buf;

    if (isclient)
        ctx->rem_dest = pp_client_exch_dest(addr, &my_dest);
    else
        ctx->rem_dest = pp_server_exch_dest(ctx, &my_dest, gidx);
    if (!ctx->rem_dest)
        return NULL;

    if (isclient)
        if (pp_connect_ctx(ctx, my_dest.psn, ctx->rem_dest, gidx))
            return NULL;

    printf("my monitor qp qp_num=%d\n", my_dest.qpn);
    printf("my send/recv qp qp_num=%d\n", my_dest.qpn_read);
    printf("remote monitor qp qp_num=%d\n", ctx->rem_dest->qpn);
    printf("remote send/recv qp qp_num=%d\n", ctx->rem_dest->qpn_read);
    return ctx;
}

static struct pingpong_context *alloc_monitor_qp() {
    struct ibv_device **dev_list;
    struct ibv_device *ib_dev;
    struct pingpong_context *ctx;

    dev_list = ibv_get_device_list(NULL);
    if (!dev_list) {
        perror("Failed to get IB devices list");
        return NULL;
    }

    //ib_dev = *dev_list; // pick the first device
    ib_dev = dev_list[ib_dev_idx];
    printf("IB DEV NAME: %s\n", ib_dev->name);
    //printf("start printing all dev name from idx 0:\n");
    //printf("dev_list[0]: %s\n", dev_list[0]->name);
    //printf("dev_list[1]: %s\n", dev_list[1]->name);
    //printf("dev_list[2]: %s\n", dev_list[2]->name);
    //printf("dev_list[3]: %s\n", dev_list[3]->name);
    if (!ib_dev) {
        fprintf(stderr, "No IB devices found\n");
        return NULL;
    }

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        fprintf(stderr, "Couldn't allocate pingpong_context.\n");
        return NULL;
    }
    
    /* buffers */
    ctx->send_buf = memalign(sysconf(_SC_PAGE_SIZE), BUF_SIZE);
    if (!ctx->send_buf) {
        fprintf(stderr, "Couldn't allocate send buf.\n");
        goto clean_ctx;
    }
    ctx->recv_buf = memalign(sysconf(_SC_PAGE_SIZE), BUF_SIZE);
    if (!ctx->recv_buf) {
        fprintf(stderr, "Couldn't allocate recv buf.\n");
        goto clean_send_buf;
    }
    ctx->local_read_buf = memalign(sysconf(_SC_PAGE_SIZE), BUF_READ_SIZE * 2);
    if (!ctx->send_buf) {
        fprintf(stderr, "Couldn't allocate send buf.\n");
        goto clean_ctx;
    }
    ctx->remote_read_buf = memalign(sysconf(_SC_PAGE_SIZE), BUF_READ_SIZE);
    if (!ctx->recv_buf) {
        fprintf(stderr, "Couldn't allocate recv buf.\n");
        goto clean_send_buf;
    }
    
    /* device context */
    ctx->context = ibv_open_device(ib_dev);
    if (!ctx->context) {
        fprintf(stderr, "Couldn't get context for %s\n",
            ibv_get_device_name(ib_dev));
        goto clean_recv_buf;
    }
    
    /* montior qp's pd & mr */
    ctx->pd = ibv_alloc_pd(ctx->context);
    if (!ctx->pd) {
        fprintf(stderr, "Couldn't allocate PD\n");
        goto clean_device;
    }

    ctx->send_mr = ibv_reg_mr(ctx->pd, ctx->send_buf, BUF_SIZE, IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);
    if (!ctx->send_mr) {
        fprintf(stderr, "Couldn't register SEND_MR\n");
        goto clean_pd;
    }

    // if remote write is allowed then local write must also be allowed
    ctx->recv_mr = ibv_reg_mr(ctx->pd, ctx->recv_buf, BUF_SIZE, IBV_ACCESS_REMOTE_WRITE|IBV_ACCESS_LOCAL_WRITE);
    if (!ctx->recv_mr) {
        fprintf(stderr, "Couldn't register RECV_MR\n");
        goto clean_send_mr;
    }

    /* send/recv qp's qd & mr */
    ctx->pd_read = ibv_alloc_pd(ctx->context);
    if (!ctx->pd) {
        fprintf(stderr, "Couldn't allocate PD_READ\n");
        goto clean_recv_mr;
    }

    ctx->local_read_mr = ibv_reg_mr(ctx->pd_read, ctx->local_read_buf, BUF_READ_SIZE, IBV_ACCESS_LOCAL_WRITE);
    if (!ctx->send_mr) {
        fprintf(stderr, "Couldn't register LOCAL_READ_MR\n");
        goto clean_pd_read;
    }

    // if remote write is allowed then local write must also be allowed
    ctx->remote_read_mr = ibv_reg_mr(ctx->pd_read, ctx->remote_read_buf, BUF_READ_SIZE, IBV_ACCESS_REMOTE_WRITE|IBV_ACCESS_LOCAL_WRITE);
    if (!ctx->recv_mr) {
        fprintf(stderr, "Couldn't register REMOTE_READ_MR\n");
        goto clean_local_read_mr;
    }
    
    /* monitor comp event channel */
    ctx->channel = ibv_create_comp_channel(ctx->context);
    if (!ctx->channel) {
        fprintf(stderr, "Couldn't create completion channel\n");
        exit(1);
    }

    /* monitor qp's cq */
    //ctx->cq = ibv_create_cq(ctx->context, 2, NULL, NULL, 0);
    ctx->cq = ibv_create_cq(ctx->context, 2, NULL, ctx->channel, 0);
    if (!ctx->cq) {
        fprintf(stderr, "Couldn't create CQ\n");
        goto clean_remote_read_mr;
    }

    if (ibv_req_notify_cq(ctx->cq, 0)) {
        fprintf(stderr, "Couldn't request CQ notification\n");
        exit(1);
    }

    /* send/recv qp's cq */
    ctx->cq_send = ibv_create_cq(ctx->context, 2, NULL, NULL, 0);       //TODO: add event channel
    if (!ctx->cq_send) {
        fprintf(stderr, "Couldn't create CQ_SEND\n");
        goto clean_cq;
    }

    ctx->cq_recv = ibv_create_cq(ctx->context, 2, NULL, NULL, 0);       //TODO: add event channel
    if (!ctx->cq_recv) {
        fprintf(stderr, "Coundln't create CQ_RECV\n");
        goto clean_cq_send;
    }

    {
        struct ibv_qp_init_attr init_attr;
	    memset(&init_attr, 0, sizeof(struct ibv_qp_init_attr));
	    init_attr.send_cq = ctx->cq;
	    init_attr.recv_cq = ctx->cq;
	    init_attr.cap.max_send_wr  = 2;
	    init_attr.cap.max_recv_wr  = 2;
	    init_attr.cap.max_send_sge = 1;
	    init_attr.cap.max_recv_sge = 1;
	    init_attr.cap.max_inline_data = 100;	// probably not going to use inline there
	    init_attr.qp_type = IBV_QPT_RC;
        init_attr.qp_context = (void *)1;
        
        ctx->qp = ibv_create_qp(ctx->pd, &init_attr);
        if (!ctx->qp)  {
            fprintf(stderr, "Couldn't create QP\n");
            goto clean_cq_recv;
        }

        init_attr.send_cq = ctx->cq_send;
        init_attr.recv_cq = ctx->cq_recv;
        ctx->qp_read = ibv_create_qp(ctx->pd_read, &init_attr);
        if (!ctx->qp_read)  {
            fprintf(stderr, "Couldn't create QP_READ\n");
            goto clean_qp;
        }
    }

    {
        struct ibv_qp_attr attr = {
            .qp_state = IBV_QPS_INIT,
            .pkey_index = 0,
            .port_num = ib_port,
            .qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE
        };
        if (ibv_modify_qp(ctx->qp, &attr,
                IBV_QP_STATE            |
                IBV_QP_PKEY_INDEX       |
                IBV_QP_PORT             |
                IBV_QP_ACCESS_FLAGS)) {
            fprintf(stderr, "Failed to modify QP to INIT\n");
            goto clean_qp_read;
        }
        if (ibv_modify_qp(ctx->qp_read, &attr,
                IBV_QP_STATE            |
                IBV_QP_PKEY_INDEX       |
                IBV_QP_PORT             |
                IBV_QP_ACCESS_FLAGS)) {
            fprintf(stderr, "Failed to modify QP to INIT\n");
            goto clean_qp_read;
        }
    }
    ibv_free_device_list(dev_list);
    return ctx;

clean_qp_read:
    ibv_destroy_qp(ctx->qp_read);
clean_qp:
    ibv_destroy_qp(ctx->qp);
clean_cq_recv:
    ibv_destroy_cq(ctx->cq_recv);
clean_cq_send:
    ibv_destroy_cq(ctx->cq_send);
clean_cq:
    ibv_destroy_cq(ctx->cq);
clean_remote_read_mr:
    ibv_dereg_mr(ctx->remote_read_mr);
clean_local_read_mr:
    ibv_dereg_mr(ctx->local_read_mr);
clean_pd_read:
    ibv_dealloc_pd(ctx->pd_read);
clean_recv_mr:
    ibv_dereg_mr(ctx->recv_mr);
clean_send_mr:
    ibv_dereg_mr(ctx->send_mr);
clean_pd:
    ibv_dealloc_pd(ctx->pd);
clean_device:
    ibv_close_device(ctx->context);
clean_recv_buf:
    free(ctx->recv_buf);
clean_send_buf:
    free(ctx->send_buf);
clean_ctx:
    free(ctx);

    ibv_free_device_list(dev_list);
    return NULL;
}

static struct pingpong_dest * pp_client_exch_dest(const char *servername, 
                                            struct pingpong_dest *my_dest) {
    struct addrinfo *res, *t;
    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM
    };
    char *service;
    char msg[sizeof "0000:000000:000000:000000:00000000:0000000000000000:00000000000000000000000000000000"];
    int n;
    int sockfd = -1;
    struct pingpong_dest *rem_dest = NULL;
    char gid[33];

    printf("CLIENT\n");
    if (asprintf(&service, "%d", port) < 0)
        return NULL;

    n = getaddrinfo(servername, service, &hints, &res);

    if (n < 0) {
        fprintf(stderr, "%s for %s:%d\n", gai_strerror(n), servername, port);
        free(service);
        return NULL;
    }

    for (t = res; t; t = t->ai_next) {
        sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
        if (sockfd >= 0) {
            if (!connect(sockfd, t->ai_addr, t->ai_addrlen))
                break;
            close(sockfd);
            sockfd = -1;
        }
    }

    freeaddrinfo(res);
    free(service);

    if (sockfd < 0) {
        fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
        return NULL;
    }

    gid_to_wire_gid(&my_dest->gid, gid);
    sprintf(msg, "%04x:%06x:%06x:%06x:%08x:%016Lx:%s", my_dest->lid, my_dest->qpn, my_dest->qpn_read,
                            my_dest->psn, my_dest->rkey, my_dest->vaddr, gid);
    if (write(sockfd, msg, sizeof msg) != sizeof msg) {
        fprintf(stderr, "Couldn't send local address\n");
        goto out;
    }

    if (recv(sockfd, msg, sizeof(msg), MSG_WAITALL) != sizeof(msg)) {
        perror("client read");
        fprintf(stderr, "Couldn't read remote address\n");
        goto out;
    }

    if (write(sockfd, "done", sizeof("done")) != sizeof("done")) {
        fprintf(stderr, "Couldn't send \"done\" msg\n");
        goto out;
    }

    rem_dest = malloc(sizeof *rem_dest);
    if (!rem_dest)
        goto out;

    sscanf(msg, "%x:%x:%x:%x:%x:%Lx:%s", &rem_dest->lid, &rem_dest->qpn, &rem_dest->qpn_read,
                            &rem_dest->psn, &rem_dest->rkey, &rem_dest->vaddr, gid);
    wire_gid_to_gid(gid, &rem_dest->gid);

out:
    close(sockfd);
    return rem_dest;
}

static struct pingpong_dest * pp_server_exch_dest(struct pingpong_context *ctx,
                                            const struct pingpong_dest *my_dest,
                                            int sgid_idx) {
    struct addrinfo *res, *t;
    struct addrinfo hints = {
        .ai_flags    = AI_PASSIVE,
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM
    };
    char *service;
    char msg[sizeof "0000:000000:000000:000000:00000000:0000000000000000:00000000000000000000000000000000"];
    int n;
    int sockfd = -1, connfd;
    struct pingpong_dest *rem_dest = NULL;
    char gid[33];

    printf("SERVER\n");
    if (asprintf(&service, "%d", port) < 0)
        return NULL;

    n = getaddrinfo(NULL, service, &hints, &res);

    if (n < 0) {
        fprintf(stderr, "%s for port %d\n", gai_strerror(n), port);
        free(service);
        return NULL;
    }

    for (t = res; t; t = t->ai_next) {
        sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
        if (sockfd >= 0) {
            n = 1;

            setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof n);

            if (!bind(sockfd, t->ai_addr, t->ai_addrlen))
                break;
            close(sockfd);
            sockfd = -1;
        }
    }

    freeaddrinfo(res);
    free(service);

    if (sockfd < 0) {
        fprintf(stderr, "Couldn't listen to port %d\n", port);
        return NULL;
    }

    listen(sockfd, 1);
    connfd = accept(sockfd, NULL, 0);
    close(sockfd);
    if (connfd < 0) {
        fprintf(stderr, "accept() failed\n");
        return NULL;
    }
    
    n = recv(connfd, msg, sizeof(msg), MSG_WAITALL);
    if (n != sizeof msg) {
        perror("server read");
        fprintf(stderr, "%d/%d: Couldn't read remote address\n", n, (int) sizeof msg);
        goto out;
    }

    rem_dest = malloc(sizeof *rem_dest);
    if (!rem_dest)
        goto out;

    sscanf(msg, "%x:%x:%x:%x:%x:%Lx:%s", &rem_dest->lid, &rem_dest->qpn, &rem_dest->qpn_read,
                            &rem_dest->psn, &rem_dest->rkey, &rem_dest->vaddr, gid);
    wire_gid_to_gid(gid, &rem_dest->gid);

    ////
    /* 
    if (pp_connect_ctx(ctx, my_dest->psn, rem_dest)) {
        fprintf(stderr, "Couldn't connect to remote QP\n");
        free(rem_dest);
        rem_dest = NULL;
        goto out;
    }
    */
    ////


    gid_to_wire_gid(&my_dest->gid, gid);
    sprintf(msg, "%04x:%06x:%06x:%06x:%08x:%016Lx:%s", my_dest->lid, my_dest->qpn, my_dest->qpn_read,
                            my_dest->psn, my_dest->rkey, my_dest->vaddr, gid);
    if (write(connfd, msg, sizeof msg) != sizeof msg) {
        fprintf(stderr, "Couldn't send local address\n");
        free(rem_dest);
        rem_dest = NULL;
        goto out;
    }

    /* expecting "done" msg */
    if (read(connfd, msg, sizeof(msg)) <= 0) {
        fprintf(stderr, "Couldn't read \"done\" msg\n");
        free(rem_dest);
        rem_dest = NULL;
        goto out;
    }

    if (pp_connect_ctx(ctx, my_dest->psn, rem_dest, sgid_idx)) {
        fprintf(stderr, "Couldn't connect to remote QP\n");
        free(rem_dest);
        rem_dest = NULL;
        goto out;
    }

out:
    close(connfd);
    return rem_dest;
}

static int pp_connect_ctx(struct pingpong_context *ctx, 
                        int my_psn,
                        struct pingpong_dest *dest, int sgid_idx)
{
    struct ibv_qp_attr attr = {
        .qp_state       = IBV_QPS_RTR,
        .path_mtu       = mtu,
        .dest_qp_num    = dest->qpn,
        .rq_psn         = dest->psn,
        .max_dest_rd_atomic = 1,
        .min_rnr_timer      = 12,
        .ah_attr        = {
            .is_global  = 0,
            .dlid       = dest->lid,
            .sl         = 0,
            .src_path_bits  = 0,
            .port_num   = ib_port
        }
    };

    if (dest->gid.global.interface_id) {
		attr.ah_attr.is_global = 1;
		attr.ah_attr.grh.hop_limit = 1;
		attr.ah_attr.grh.dgid = dest->gid;
		attr.ah_attr.grh.sgid_index = sgid_idx;
	}

    if (ibv_modify_qp(ctx->qp, &attr,
        IBV_QP_STATE              |
        IBV_QP_AV                 |
        IBV_QP_PATH_MTU           |
        IBV_QP_DEST_QPN           |
        IBV_QP_RQ_PSN             |
        IBV_QP_MAX_DEST_RD_ATOMIC |
        IBV_QP_MIN_RNR_TIMER)) {
        fprintf(stderr, "Failed to modify QP to RTR\n");
        return 1;
    }

    attr.dest_qp_num = dest->qpn_read;
    if (ibv_modify_qp(ctx->qp_read, &attr,
        IBV_QP_STATE              |
        IBV_QP_AV                 |
        IBV_QP_PATH_MTU           |
        IBV_QP_DEST_QPN           |
        IBV_QP_RQ_PSN             |
        IBV_QP_MAX_DEST_RD_ATOMIC |
        IBV_QP_MIN_RNR_TIMER)) {
        fprintf(stderr, "Failed to modify QP_READ to RTR\n");
        return 1;
    }

    attr.qp_state       = IBV_QPS_RTS;
    attr.timeout        = 14;
    attr.retry_cnt      = 7;
    attr.rnr_retry      = 7;
    attr.sq_psn     = my_psn;
    attr.max_rd_atomic  = 1;
    if (ibv_modify_qp(ctx->qp, &attr,
        IBV_QP_STATE              |
        IBV_QP_TIMEOUT            |
        IBV_QP_RETRY_CNT          |
        IBV_QP_RNR_RETRY          |
        IBV_QP_SQ_PSN             |
        IBV_QP_MAX_QP_RD_ATOMIC)) {
        fprintf(stderr, "Failed to modify QP to RTS\n");
        return 1;
    }
    if (ibv_modify_qp(ctx->qp_read, &attr,
        IBV_QP_STATE              |
        IBV_QP_TIMEOUT            |
        IBV_QP_RETRY_CNT          |
        IBV_QP_RNR_RETRY          |
        IBV_QP_SQ_PSN             |
        IBV_QP_MAX_QP_RD_ATOMIC)) {
        fprintf(stderr, "Failed to modify QP_READ to RTS\n");
        return 1;
    }

    return 0;
}