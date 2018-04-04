#include "pingpong.h"
#include "arbiter.h"

static const int port = 18515;
static const int port2 = 18516;
static const int ib_port = 1;
static const int mtu = IBV_MTU_2048;

static struct pingpong_context * alloc_qps(struct host_request *, struct arbiter_response *, int);
static struct pingpong_dest * pp_client_exch_dest(const char *, const char *, struct pingpong_dest *, char);
static struct pingpong_dest * pp_server_exch_dest(struct pingpong_context *, const struct pingpong_dest *, int);
static int pp_connect_ctx(struct pingpong_context *, int, struct pingpong_dest *, struct pingpong_dest *, int, char);
struct pingpong_context *init_ctx_and_build_conn(const char *, const char *, int, int, struct host_request *, struct arbiter_response *, char);

struct pingpong_context *init_ctx_and_build_conn(const char *addr, const char *addr2, int is_arbiter, int gidx, 
                                                        struct host_request *host_req, struct arbiter_response *ca_resp, char rmf_choice) {
    struct pingpong_context *ctx;
    struct pingpong_dest my_dest;

    ctx = alloc_qps(host_req, ca_resp, RING_BUFFER_SIZE);

    if (!ctx)
        return NULL;
    
    if (pp_get_port_info(ctx->context, ib_port, &ctx->portinfo)) {
        fprintf(stderr, "Coundln't get port info\n");
        return NULL;
    }

    my_dest.lid = ctx->portinfo.lid;
    if (gidx >= 0) {
		if (ibv_query_gid(ctx->context, ib_port, gidx, &my_dest.gid)) {
			fprintf(stderr, "Could not get local gid for gid index %d\n", gidx);
			return NULL;
		}
	} else {
		memset(&my_dest.gid, 0, sizeof my_dest.gid);
    }
    my_dest.qpn_rmf = ctx->qp_rmf->qp_num;
    my_dest.qpn_req = ctx->qp_req->qp_num;
    //my_dest.psn = lrand48() & 0xffffff;
    my_dest.psn = 999;
    my_dest.rkey_rmf = ctx->rmf_mr->rkey;
    my_dest.rkey_req = ctx->req_mr->rkey;
    my_dest.rkey_resp = ctx->resp_mr->rkey;
    my_dest.vaddr_rmf = (uintptr_t)ctx->rmf_buf;
    my_dest.vaddr_req = (uintptr_t)host_req;
    my_dest.vaddr_resp = (uintptr_t)ca_resp;
    printf("newly fresh my dest psn is %d\n", my_dest.psn);

    if (is_arbiter) {
        ctx->rem_dest = pp_client_exch_dest(addr, addr2, &my_dest, rmf_choice);
        ctx->rmf_choice = 2;
    } else {
        ctx->rem_dest = pp_server_exch_dest(ctx, &my_dest, gidx);
        printf("done calling pp_server_exch_dest\n");
    }

    if (!ctx->rem_dest)
        return NULL;

    if (is_arbiter)
        if (pp_connect_ctx(ctx, my_dest.psn, ctx->rem_dest, NULL, gidx, '2'))
            return NULL;

    printf("my req qp qp_num=%d\n", my_dest.qpn_req);
    printf("remote req qp qp_num=%d\n", ctx->rem_dest->qpn_req);
    if (rmf_choice == '2') {
        printf("my rmf qp qp_num=%d\n", my_dest.qpn_rmf);
        printf("remote rmf qp qp_num=%d\n", ctx->rem_dest->qpn_rmf);
    }
    return ctx;
}

static struct pingpong_context *alloc_qps(struct host_request *host_req, struct arbiter_response *ca_resp, int req_buf_size) {
    struct ibv_device **dev_list;
    struct ibv_device *ib_dev;
    struct pingpong_context *ctx;

    dev_list = ibv_get_device_list(NULL);
    if (!dev_list) {
        perror("Failed to get IB devices list");
        return NULL;
    }

    ib_dev = *dev_list; // pick the first device
    if (!ib_dev) {
        fprintf(stderr, "No IB devices found\n");
        return NULL;
    }

    ctx = calloc(1, sizeof(struct pingpong_context));
    if (!ctx) {
        fprintf(stderr, "Couldn't allocate pingpong_context.\n");
        return NULL;
    }

    ctx->context = ibv_open_device(ib_dev);
    if (!ctx->context) {
        fprintf(stderr, "Couldn't get context for %s\n",
            ibv_get_device_name(ib_dev));
        goto clean_device;
    }

    ctx->pd = ibv_alloc_pd(ctx->context);
    if (!ctx->pd) {
        fprintf(stderr, "Couldn't allocate PD\n");
        goto clean_pd;
    }


    ctx->rmf_buf = memalign(sysconf(_SC_PAGE_SIZE), BUF_SIZE);
    if (!ctx->rmf_buf) {
        fprintf(stderr, "Couldn't allocate send buf.\n");
        goto clean_rmf_buf;
    }

    ctx->rmf_mr = ibv_reg_mr(ctx->pd, ctx->rmf_buf, BUF_SIZE, IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);
    if (!ctx->rmf_mr) {
        fprintf(stderr, "Couldn't register RMF_MR\n");
        goto clean_rmf_mr;
    }

    ctx->req_mr = ibv_reg_mr(ctx->pd, host_req, req_buf_size * sizeof(struct host_request), IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);
    if (!ctx->req_mr) {
        fprintf(stderr, "Couldn't register REQ_MR\n");
        goto clean_req_mr;
    }

    ctx->resp_mr = ibv_reg_mr(ctx->pd, ca_resp, sizeof(struct arbiter_response), IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE);
    if (!ctx->resp_mr) {
        fprintf(stderr, "Couldn't register RESP_MR\n");
        goto clean_resp_mr;
    }

    ctx->cq_rmf = ibv_create_cq(ctx->context, 2, NULL, NULL, 0);
    if (!ctx->cq_rmf) {
        fprintf(stderr, "Couldn't create CQ RMF\n");
        goto clean_cq_rmf;
    }

    ctx->cq_req = ibv_create_cq(ctx->context, 1024, NULL, NULL, 0);
    if (!ctx->cq_req) {
        fprintf(stderr, "Couldn't create CQ REQ\n");
        goto clean_cq_req;
    }

    {
        struct ibv_qp_init_attr init_attr;
	    memset(&init_attr, 0, sizeof(struct ibv_qp_init_attr));
	    init_attr.send_cq = ctx->cq_rmf;
	    init_attr.recv_cq = ctx->cq_rmf;
	    init_attr.cap.max_send_wr  = 2;
	    init_attr.cap.max_recv_wr  = 2;
	    init_attr.cap.max_send_sge = 1;
	    init_attr.cap.max_recv_sge = 1;
	    init_attr.cap.max_inline_data = 100;	// probably not going to use inline there
	    init_attr.qp_type = IBV_QPT_RC;
        init_attr.qp_context = (void *)1;
        
        ctx->qp_rmf = ibv_create_qp(ctx->pd, &init_attr);
        if (!ctx->qp_rmf)  {
            fprintf(stderr, "Couldn't create QP\n");
            goto clean_qp_rmf;
        }

	    init_attr.cap.max_send_wr  = 1024;
	    init_attr.cap.max_recv_wr  = 1024;
        init_attr.send_cq = ctx->cq_req;
        init_attr.recv_cq = ctx->cq_req;
        ctx->qp_req = ibv_create_qp(ctx->pd, &init_attr);
        if (!ctx->qp_req)  {
            fprintf(stderr, "Couldn't create QP_READ\n");
            goto clean_qp_req;
        }
    }


    {
        struct ibv_qp_attr attr = {
            .qp_state = IBV_QPS_INIT,
            .pkey_index = 0,
            .port_num = ib_port,
            .qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE
        };
        if (ibv_modify_qp(ctx->qp_rmf, &attr,
                IBV_QP_STATE            |
                IBV_QP_PKEY_INDEX       |
                IBV_QP_PORT             |
                IBV_QP_ACCESS_FLAGS)) {
            fprintf(stderr, "Failed to modify RMF QP to INIT\n");
            goto clean_qp_rmf;
        }
        if (ibv_modify_qp(ctx->qp_req, &attr,
                IBV_QP_STATE            |
                IBV_QP_PKEY_INDEX       |
                IBV_QP_PORT             |
                IBV_QP_ACCESS_FLAGS)) {
            fprintf(stderr, "Failed to modify REQ QP to INIT\n");
            goto clean_qp_req;
        }
    }
    ibv_free_device_list(dev_list);
    return ctx;

clean_qp_rmf:
    ibv_destroy_qp(ctx->qp_rmf);
clean_qp_req:
    ibv_destroy_qp(ctx->qp_req);
clean_cq_rmf:
    ibv_destroy_cq(ctx->cq_rmf);
clean_cq_req:
    ibv_destroy_cq(ctx->cq_req);
clean_resp_mr:
    ibv_dereg_mr(ctx->resp_mr);
clean_req_mr:
    ibv_dereg_mr(ctx->req_mr);
clean_rmf_mr:
    ibv_dereg_mr(ctx->rmf_mr);
clean_rmf_buf:
    free(ctx->rmf_buf);
clean_pd:
    ibv_dealloc_pd(ctx->pd);
clean_device:
    ibv_close_device(ctx->context);
//clean_ctx:
    free(ctx);

    ibv_free_device_list(dev_list);
    return NULL;
}

/* pp_client_exch_dest is only used by the arbiter */
/* rmf_choice: 0 tells the host to maintain rmf with another host, and be the server in ip connection;
                   1 tells the host to maintain rmf with another host, and be the client in ip connection;
                   2 tells the host to maintain rmf with the arbiter
*/
static struct pingpong_dest * pp_client_exch_dest(const char *servername, const char *servername2,
                                            struct pingpong_dest *my_dest, char rmf_choice) {
    struct addrinfo *res, *t;
    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM
    };
    char *service;
    char msg[sizeof "0000:000000:000000:000000:00000000:00000000:00000000:0000000000000000:0000000000000000:0000000000000000:00000000000000000000000000000000"];
    char msg2[sizeof "0000:000000:000000:00000000:00000000:0000000000000000:0000000000000000:00000000000000000000000000000000"];
    char rmf_server_ip[16];
    if (rmf_choice == '1') {
        strcpy(rmf_server_ip, servername2);
    }
    int n;
    int sockfd = -1;
    struct pingpong_dest *rem_dest = NULL;
    char gid[33];

    printf("ARBITER\n");
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

    if (write(sockfd, &rmf_choice, 1) != 1) {
        fprintf(stderr, "(rmf_choice) Couldn't send local address\n");
        goto out;
    }

    rem_dest = malloc(sizeof(struct pingpong_dest));
    if (!rem_dest)
        goto out;

    printf("(arbiter) my psn is %d\n", my_dest->psn);
    printf("(arbiter) my qpn_req is %d\n", my_dest->qpn_req);
    if (rmf_choice == '2') {
        sprintf(msg, "%04x:%06x:%06x:%06x:%08x:%08x:%08x:%016Lx:%016Lx:%016Lx:%s", my_dest->lid, my_dest->qpn_rmf, my_dest->qpn_req,
                    my_dest->psn, my_dest->rkey_rmf, my_dest->rkey_req, my_dest->rkey_resp, my_dest->vaddr_rmf, my_dest->vaddr_req, my_dest->vaddr_resp, gid);

        if (write(sockfd, msg, sizeof msg) != sizeof msg) {
            fprintf(stderr, "(msg) Couldn't send local address\n");
            goto out;
        }

        if (recv(sockfd, msg, sizeof msg, MSG_WAITALL) != sizeof msg) {
            perror("client read");
            fprintf(stderr, "Couldn't read remote address\n");
            goto out;
        }

        if (write(sockfd, "done", sizeof("done")) != sizeof("done")) {
            fprintf(stderr, "Couldn't send \"done\" msg\n");
            goto out;
        }

        sscanf(msg, "%x:%x:%x:%x:%x:%x:%x:%Lx:%Lx:%Lx:%s", &rem_dest->lid, &rem_dest->qpn_rmf, &rem_dest->qpn_req,
                    &rem_dest->psn, &rem_dest->rkey_rmf, &rem_dest->rkey_req, &rem_dest->rkey_resp, &rem_dest->vaddr_rmf, &rem_dest->vaddr_req, &rem_dest->vaddr_resp, gid);
    } else {
        if (rmf_choice == '1') {
            if (write(sockfd, rmf_server_ip, sizeof rmf_server_ip) != sizeof rmf_server_ip) {
                fprintf(stderr, "Couldn't send pairing host ip address\n");
                goto out;
            }
        }

        sprintf(msg2, "%04x:%06x:%06x:%08x:%08x:%016Lx:%016Lx:%s", my_dest->lid, my_dest->qpn_req,
                    my_dest->psn, my_dest->rkey_req, my_dest->rkey_resp, my_dest->vaddr_req, my_dest->vaddr_resp, gid);

        if (write(sockfd, msg2, sizeof msg2) != sizeof msg2) {
            fprintf(stderr, "(msg2) Couldn't send local address\n");
            goto out;
        }

        if (recv(sockfd, msg2, sizeof msg2, MSG_WAITALL) != sizeof msg2) {
            perror("client read");
            fprintf(stderr, "Couldn't read remote address\n");
            goto out;
        }

        if (write(sockfd, "done", sizeof("done")) != sizeof("done")) {
            fprintf(stderr, "Couldn't send \"done\" msg\n");
            goto out;
        }
        sscanf(msg2, "%x:%x:%x:%x:%x:%Lx:%Lx:%s", &rem_dest->lid, &rem_dest->qpn_req,
                    &rem_dest->psn, &rem_dest->rkey_req, &rem_dest->rkey_resp, &rem_dest->vaddr_req, &rem_dest->vaddr_resp, gid);
    }

    printf("DEBUG: (arbiter) local vaddr_req:%016Lx\n", my_dest->vaddr_req);
    printf("DEBUG: (arbiter) remote vaddr_req:%016Lx\n", rem_dest->vaddr_req);
    
    wire_gid_to_gid(gid, &rem_dest->gid);

out:
    close(sockfd);
    return rem_dest;
}

/* Used by all the host pacers */
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
    char rmf_choice;
    char msg[sizeof "0000:000000:000000:000000:00000000:00000000:00000000:0000000000000000:00000000000000000:000000000000000:00000000000000000000000000000000"];
    char msg2[sizeof "0000:000000:000000:00000000:00000000:0000000000000000:0000000000000000:00000000000000000000000000000000"];
    char msg3[sizeof "0000:000000:000000:00000000:0000000000000000:00000000000000000000000000000000"];
    char rmf_server_ip[16];
    int n;
    int sockfd = -1, connfd = -1, connfd2 = -1;
    struct pingpong_dest *rem_dest = NULL;
    char gid[33];
    struct pingpong_dest *rem_host_dest = malloc(sizeof(struct pingpong_dest));

    printf("PACER\n");
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

    n = recv(connfd, &rmf_choice, 1 , MSG_WAITALL);
    if (n != 1) {
        perror("server read");
        fprintf(stderr, "%d/%d: Couldn't read rmf choice\n", n, 1);
        goto out;
    }

    ctx->rmf_choice = rmf_choice - '0';      /* assume ASCII */

    rem_dest = malloc(sizeof(struct pingpong_dest));

    if (rmf_choice == '2') {
        n = recv(connfd, msg, sizeof msg, MSG_WAITALL);
        if (n != sizeof msg) {
            perror("server read");
            fprintf(stderr, "%d/%d: Couldn't read remote address\n", n, (int) sizeof msg);
            goto out;
        }

        sscanf(msg, "%x:%x:%x:%x:%x:%x:%x:%Lx:%Lx:%Lx:%s", &rem_dest->lid, &rem_dest->qpn_rmf, &rem_dest->qpn_req,
                    &rem_dest->psn, &rem_dest->rkey_rmf, &rem_dest->rkey_req, &rem_dest->rkey_resp, &rem_dest->vaddr_rmf, &rem_dest->vaddr_req, &rem_dest->vaddr_resp, gid);
    } else {
        if (rmf_choice == '1') {
            n = recv(connfd, rmf_server_ip, sizeof rmf_server_ip, MSG_WAITALL);
            if (n != sizeof rmf_server_ip) {
                perror("server read");
                fprintf(stderr, "%d/%d: Couldn't read remote address\n", n, (int) sizeof msg);
                goto out;
            }
            printf("Received host IP: %s", rmf_server_ip);
        }

        n = recv(connfd, msg2, sizeof msg2, MSG_WAITALL);
        if (n != sizeof msg2) {
            perror("server read");
            fprintf(stderr, "%d/%d: Couldn't read remote address\n", n, (int) sizeof msg2);
            goto out;
        }

        sscanf(msg2, "%x:%x:%x:%x:%x:%Lx:%Lx:%s", &rem_dest->lid, &rem_dest->qpn_req,
                    &rem_dest->psn, &rem_dest->rkey_req, &rem_dest->rkey_resp, &rem_dest->vaddr_req, &rem_dest->vaddr_resp, gid);
    }
    printf("DEBUG: (pacer) local vaddr_req:%016Lx\n", my_dest->vaddr_req);
    printf("DEBUG: (pacer) remote vaddr_req:%016Lx\n", rem_dest->vaddr_req);

    wire_gid_to_gid(gid, &rem_dest->gid);

    ////
     
    //if (pp_connect_ctx(ctx, my_dest->psn, rem_dest)) {
    //    fprintf(stderr, "Couldn't connect to remote QP\n");
    //    free(rem_dest);
    //    rem_dest = NULL;
    //    goto out;
    //}
    
    ////

    gid_to_wire_gid(&my_dest->gid, gid);

    if (rmf_choice == '2') {
        sprintf(msg, "%04x:%06x:%06x:%06x:%08x:%08x:%08x:%016Lx:%016Lx:%016Lx:%s", my_dest->lid, my_dest->qpn_rmf, my_dest->qpn_req,
                    my_dest->psn, my_dest->rkey_rmf, my_dest->rkey_req, my_dest->rkey_resp, my_dest->vaddr_rmf, my_dest->vaddr_req, my_dest->vaddr_resp, gid);

        if (write(connfd, msg, sizeof msg) != sizeof msg) {
            fprintf(stderr, "(msg) Couldn't send local address\n");
            free(rem_dest);
            rem_dest = NULL;
            goto out;
        }
    } else {
        sprintf(msg2, "%04x:%06x:%06x:%08x:%08x:%016Lx:%016Lx:%s", my_dest->lid, my_dest->qpn_req,
                    my_dest->psn, my_dest->rkey_req, my_dest->rkey_resp, my_dest->vaddr_req, my_dest->vaddr_resp, gid);
        printf("host pacer: my psn is %d\n", my_dest->psn);

        if (write(connfd, msg2, sizeof msg2) != sizeof msg2) {
            fprintf(stderr, "(msg2) Couldn't send local address\n");
            goto out;
        }
    }

    // expecting "done" msg
    if (read(connfd, msg, sizeof msg) <= 0) {
        fprintf(stderr, "Couldn't read \"done\" msg\n");
        free(rem_dest);
        rem_dest = NULL;
        goto out;
    }
    printf("Done sending msg/msg2 to arbiter; Received done message from arbiter. \n");

    /* extra info exchange for rmf_qp if not sending rmf to the arbiter */
    if (rmf_choice == '0') {
        /* server in ip conn*/
        if (asprintf(&service, "%d", port2) < 0)
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
        connfd2 = accept(sockfd, NULL, 0);
        close(sockfd);
        if (connfd2 < 0) {
            fprintf(stderr, "accept() failed\n");
            return NULL;
        }

        n = recv(connfd2, msg3, sizeof msg3, MSG_WAITALL);
        if (n != sizeof msg3) {
            perror("server read");
            fprintf(stderr, "%d/%d: Couldn't read remote address\n", n, (int) sizeof msg3);
            goto out;
        }

        sscanf(msg3, "%x:%x:%x:%x:%Lx:%s", &rem_host_dest->lid, &rem_host_dest->qpn_rmf,
                    &rem_host_dest->psn, &rem_host_dest->rkey_rmf, &rem_host_dest->vaddr_rmf, gid);
        printf("(server) the psn received from remote pacer is %d\n", rem_host_dest->psn);

        wire_gid_to_gid(gid, &rem_host_dest->gid);

        ctx->rem_host_dest = rem_host_dest;

        struct pingpong_dest my_host_dest;
        my_host_dest.lid = ctx->portinfo.lid;
        memcpy(&my_host_dest, my_dest, sizeof(struct pingpong_dest));
        /* keep psn the same is the easier hack */
        my_host_dest.qpn_rmf = ctx->qp_rmf->qp_num;
        my_host_dest.rkey_rmf = ctx->rmf_mr->rkey;
        my_host_dest.vaddr_rmf = (uintptr_t)ctx->rmf_buf;

        gid_to_wire_gid(&my_host_dest.gid, gid);

        sprintf(msg3, "%04x:%06x:%06x:%08x:%016Lx:%s", my_host_dest.lid, my_host_dest.qpn_rmf, 
                    my_host_dest.psn, my_host_dest.rkey_rmf, my_host_dest.vaddr_rmf, gid);

        if (write(connfd2, msg3, sizeof msg3) != sizeof msg3) {
            fprintf(stderr, "(msg3) Couldn't send local address\n");
            free(rem_dest);
            rem_dest = NULL;
            goto out;
        }

        if (read(connfd2, msg, sizeof msg) <= 0) {
            fprintf(stderr, "Couldn't read \"done\" msg\n");
            free(rem_dest);
            rem_dest = NULL;
            goto out;
        }

        printf("(server) Done exchanging msg3 to another host\n");
        printf("my rmf qp qp_num=%d\n", my_host_dest.qpn_rmf);
        printf("remote host (pacer) rmf qp qp_num=%d\n", rem_host_dest->qpn_rmf);
        
    } else if (rmf_choice == '1') {
        /* client in ip conn*/
        if (asprintf(&service, "%d", port2) < 0)
            return NULL;

        n = getaddrinfo(rmf_server_ip, service, &hints, &res);

        if (n < 0) {
            fprintf(stderr, "%s for %s:%d\n", gai_strerror(n), rmf_server_ip, port2);
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
            fprintf(stderr, "Couldn't connect to %s:%d\n", rmf_server_ip, port2);
            return NULL;
        }

        struct pingpong_dest my_host_dest;
        my_host_dest.lid = ctx->portinfo.lid;
        memcpy(&my_host_dest, my_dest, sizeof(struct pingpong_dest));
        /* keep psn the same is the easier hack */
        my_host_dest.qpn_rmf = ctx->qp_rmf->qp_num;
        my_host_dest.rkey_rmf = ctx->rmf_mr->rkey;
        my_host_dest.vaddr_rmf = (uintptr_t)ctx->rmf_buf;

        gid_to_wire_gid(&my_host_dest.gid, gid);

        sprintf(msg3, "%04x:%06x:%06x:%08x:%016Lx:%s", my_host_dest.lid, my_host_dest.qpn_rmf, 
                    my_host_dest.psn, my_host_dest.rkey_rmf, my_host_dest.vaddr_rmf, gid);

        if (write(sockfd, msg3, sizeof msg3) != sizeof msg3) {
            fprintf(stderr, "(msg3) Couldn't send local address\n");
            free(rem_dest);
            rem_dest = NULL;
            goto out;
        }

        if (recv(sockfd, msg3, sizeof msg3, MSG_WAITALL) != sizeof msg3) {
            perror("client read");
            fprintf(stderr, "(msg3) Couldn't read remote address\n");
            goto out;
        }

        if (write(sockfd, "done", sizeof("done")) != sizeof("done")) {
            fprintf(stderr, "Couldn't send \"done\" msg\n");
            goto out;
        }

        sscanf(msg3, "%x:%x:%x:%x:%Lx:%s", &rem_host_dest->lid, &rem_host_dest->qpn_rmf,
                    &rem_host_dest->psn, &rem_host_dest->rkey_rmf, &rem_host_dest->vaddr_rmf, gid);
        printf("(client) the psn received from remote pacer is %d\n", rem_host_dest->psn);

        ctx->rem_host_dest = rem_host_dest;

        wire_gid_to_gid(gid, &rem_host_dest->gid);

        printf("(client) Done exchanging msg3 to another host\n");
        printf("my rmf qp qp_num=%d\n", my_host_dest.qpn_rmf);
        printf("remote host (pacer) rmf qp qp_num=%d\n", ctx->rem_host_dest->qpn_rmf);
    }

    if (pp_connect_ctx(ctx, my_dest->psn, rem_dest, rem_host_dest, sgid_idx, rmf_choice)) {
        fprintf(stderr, "Couldn't connect to remote QP\n");
        free(rem_dest);
        rem_dest = NULL;
        goto out;
    }

out:
    close(connfd);
    if (rmf_choice == '0')
        close(connfd2);
    return rem_dest;
}

static int pp_connect_ctx(struct pingpong_context *ctx, 
                        int my_psn,
                        struct pingpong_dest *dest, struct pingpong_dest *dest2, int sgid_idx, char rmf_choice)
{
    struct ibv_qp_attr attr = {
        .qp_state       = IBV_QPS_RTR,
        .path_mtu       = mtu,
        .dest_qp_num    = dest->qpn_rmf,
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

    if (rmf_choice != '2') {
        attr.dest_qp_num    = dest2->qpn_rmf;
        attr.rq_psn         = dest2->psn;
        attr.ah_attr.dlid   = dest2->lid;
    }

    if (dest->gid.global.interface_id) {
		attr.ah_attr.is_global = 1;
		attr.ah_attr.grh.hop_limit = 1;
        if (rmf_choice != '2') {
		    attr.ah_attr.grh.dgid = dest2->gid;
        } else {
		    attr.ah_attr.grh.dgid = dest->gid;
        }
		attr.ah_attr.grh.sgid_index = sgid_idx;
	}

    if (ibv_modify_qp(ctx->qp_rmf, &attr,
        IBV_QP_STATE              |
        IBV_QP_AV                 |
        IBV_QP_PATH_MTU           |
        IBV_QP_DEST_QPN           |
        IBV_QP_RQ_PSN             |
        IBV_QP_MAX_DEST_RD_ATOMIC |
        IBV_QP_MIN_RNR_TIMER)) {
        fprintf(stderr, "Failed to modify RMF QP to RTR: %s\n", strerror(errno));
        return 1;
    }

    attr.dest_qp_num    = dest->qpn_req;
    attr.rq_psn         = dest->psn;
    attr.ah_attr.dlid   = dest->lid;  /* important */
    if (ibv_modify_qp(ctx->qp_req, &attr,
        IBV_QP_STATE              |
        IBV_QP_AV                 |
        IBV_QP_PATH_MTU           |
        IBV_QP_DEST_QPN           |
        IBV_QP_RQ_PSN             |
        IBV_QP_MAX_DEST_RD_ATOMIC |
        IBV_QP_MIN_RNR_TIMER)) {
        fprintf(stderr, "Failed to modify REQ QP to RTR\n");
        return 1;
    }

    attr.qp_state       = IBV_QPS_RTS;
    attr.timeout        = 14;
    attr.retry_cnt      = 7;
    attr.rnr_retry      = 7;
    attr.sq_psn     = my_psn;
    attr.max_rd_atomic  = 1;
    if (ibv_modify_qp(ctx->qp_rmf, &attr,
        IBV_QP_STATE              |
        IBV_QP_TIMEOUT            |
        IBV_QP_RETRY_CNT          |
        IBV_QP_RNR_RETRY          |
        IBV_QP_SQ_PSN             |
        IBV_QP_MAX_QP_RD_ATOMIC)) {
        fprintf(stderr, "Failed to modify RMF QP to RTS\n");
        return 1;
    }
    if (ibv_modify_qp(ctx->qp_req, &attr,
        IBV_QP_STATE              |
        IBV_QP_TIMEOUT            |
        IBV_QP_RETRY_CNT          |
        IBV_QP_RNR_RETRY          |
        IBV_QP_SQ_PSN             |
        IBV_QP_MAX_QP_RD_ATOMIC)) {
        fprintf(stderr, "Failed to modify REQ QP to RTS\n");
        return 1;
    }
    printf("DDD: attr.rq_psn: %d\n", attr.rq_psn);
    printf("DDD: attr.sq_psn: %d\n", attr.sq_psn);


    return 0;
}