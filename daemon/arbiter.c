#include "arbiter.h"
#include "monitor.h"
#include "get_clock.h"
#include "priority_queue.h"

struct cluster_info cluster;

/* utility fuctions */

static void usage()
{
    //printf("Usage: program remote-addr isclient [gid_idx]\n");
    printf("Usage: ./arbiter cluster_info_file \n");
    printf("cluster_info_file format:\n");
    printf("host1_ip [gid_idx1]\n");
    printf("host2_ip [gid_idx2]\n");
    printf("...\n");
    printf("If not in RoCE, leave gid_idx blank or set to -1.\n\n");
}

static inline void cpu_relax() __attribute__((always_inline));
static inline void cpu_relax()
{
/*
#if (COMPILER == MVCC)
    _mm_pause();
#elif (COMPILER == GCC || COMPILER == LLVM)
    asm("pause");
#endif
*/
    asm("nop");
}

static void termination_handler(int sig)
{
    printf("signal handler called\n");
    //remove("/dev/shm/rdma-fairness");
    //CMH_Destroy(cmh);
    _exit(0);
}

static void rm_shmem_on_exit()
{
    //remove("/dev/shm/rdma-fairness");
}

static void update_flow_info(struct host_request *req, int src_idx)
{
    int i, idx;
    idx = req->flow_idx;

    if (req->type == FLOW_JOIN) {
        for (i = 0; i < cluster.num_hosts; i++) {
            if (req->dlid == cluster.hosts[i].lid) {
                cluster.hosts[i].flows[idx].flow_idx = idx;
                if (!req->is_read) {    /* WRITE/SEND */
                    cluster.hosts[i].flows[idx].src = src_idx;
                    cluster.hosts[i].flows[idx].dest = i;
                    cluster.hosts[i].flows[idx].is_read = 0;
                    list_push(&cluster.hosts[src_idx].egress_port->flows, &cluster.hosts[i].flows[idx]);
                    cluster.hosts[src_idx].flows[idx].list_pos_egress = cluster.hosts[src_idx].egress_port->flows.head;
                    cluster.hosts[src_idx].egress_port->unassigned_flows++;
                    list_push(&cluster.hosts[i].ingress_port->flows, &cluster.hosts[i].flows[idx]);
                    cluster.hosts[i].flows[idx].list_pos_ingress = cluster.hosts[i].ingress_port->flows.head;
                    cluster.hosts[i].ingress_port->unassigned_flows++;
                    printf("DEBUG update flow info: new flow[%d] added to host%d's egress port and host%d's ingress port\n", idx, src_idx, i);
                } else {                /* READ: src and dest are consistent with data flowing direction */
                    cluster.hosts[i].flows[idx].src = i;
                    cluster.hosts[i].flows[idx].dest = src_idx;
                    cluster.hosts[i].flows[idx].is_read = 1;
                    list_push(&cluster.hosts[src_idx].ingress_port->flows, &cluster.hosts[i].flows[idx]);
                    cluster.hosts[src_idx].flows[idx].list_pos_ingress = cluster.hosts[src_idx].ingress_port->flows.head;
                    cluster.hosts[src_idx].ingress_port->unassigned_flows++;
                    list_push(&cluster.hosts[i].egress_port->flows, &cluster.hosts[i].flows[idx]);
                    cluster.hosts[i].flows[idx].list_pos_egress = cluster.hosts[i].egress_port->flows.head;
                    cluster.hosts[i].egress_port->unassigned_flows++;
                    printf("DEBUG update flow info: new flow[%d] added to host%d's ingress port and host%d's egress port\n", idx, src_idx, i);
                }
                break;
            }
        }

    } else if (req->type == FLOW_EXIT) {
        for (i = 0; i < cluster.num_hosts; i++) {
            if (req->dlid == cluster.hosts[i].lid) {
                if (!req->is_read) {    /* WRITE/SEND */
                    list_remove(&cluster.hosts[src_idx].egress_port->flows, cluster.hosts[src_idx].flows[idx].list_pos_egress);
                    list_remove(&cluster.hosts[i].ingress_port->flows, cluster.hosts[i].flows[idx].list_pos_ingress);
                    printf("DEBUG update flow info: flow[%d] removed from host%d's egress port and host%d's ingress port\n", idx, src_idx, i);
                    printf("DEBUG update flow info: host%d's egress port length = %zu; host%d's ingress port length = %zu\n", 
                        src_idx, cluster.hosts[src_idx].egress_port->flows.length, i, cluster.hosts[i].ingress_port->flows.length);
                } else {                /* READ: src and dest are consistent with data flowing direction */
                    list_remove(&cluster.hosts[src_idx].ingress_port->flows, cluster.hosts[src_idx].flows[idx].list_pos_ingress);
                    list_remove(&cluster.hosts[i].egress_port->flows, cluster.hosts[i].flows[idx].list_pos_egress);
                    printf("DEBUG update flow info: flow[%d] removed from host%d's ingress port and host%d's egress port\n", idx, src_idx, i);
                    printf("DEBUG update flow info: host%d's ingress port length = %zu; host%d's egress port length = %zu\n", 
                        src_idx, cluster.hosts[src_idx].ingress_port->flows.length, i, cluster.hosts[i].egress_port->flows.length);
                }
                break;
            }
        }

    }

}

/* send out responses (rate updates, sender's copy of head) to the hosts that gets affected */ 
static void send_out_responses()
{
    struct ibv_sge sge;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr;
    int num_comp = 0;
    struct ibv_wc wc;

    memset(&sge, 0, sizeof(sge));

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 0;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    //wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;
    //TODO: do inline based on the message size
    wr.send_flags = IBV_SEND_SIGNALED;

    /* distribute rates back to the sender (both WRITE/SEND and READ)
     * For WRITE/SEND flows, rates are distributed back to the egress port
     * For READ flows, rates is distributed to the ingress port */
    int i, count;        /* assume count < MAX_RATE_UPDATES */
    flow_t *flow = NULL;
    node_t *node = NULL;

    for (i = 0; i < cluster.num_hosts; i++) {
        count = 0;
        /* find rates to distribute. then send them in a batch */
        ////TODO: add mutex to enxure multi-thread safe later
        printf("host[%d] ingress port flow cnt = %zu; egress port flow cnt = %zu\n", i, cluster.hosts[i].ingress_port->flows.length, cluster.hosts[i].egress_port->flows.length);

        node = cluster.hosts[i].egress_port->flows.head;
        while (node != NULL) {
            flow = node->data;
            if (!flow->is_read) {
                cluster.hosts[i].ca_resp.rate_updates[count].rate = flow->rate;
                cluster.hosts[i].ca_resp.rate_updates[count].flow_idx = flow->flow_idx;
                ++count;
            }
            node = node->next;
        }

        node = cluster.hosts[i].ingress_port->flows.head;
        while (node != NULL) {
            flow = node->data;
            if (flow->is_read) {
                cluster.hosts[i].ca_resp.rate_updates[count].rate = flow->rate;
                cluster.hosts[i].ca_resp.rate_updates[count].flow_idx = flow->flow_idx;
                ++count;
            }
            node = node->next;
        }

        /* send out response to the host */
        cluster.hosts[i].ca_resp.header.num_rate_updates = count;
        cluster.hosts[i].ca_resp.header.sender_head = cluster.hosts[i].ring->head;
        //++cluster.hosts[i].header.ca_resp.id;
        cluster.hosts[i].ca_resp.header.id += 100;

        sge.lkey = cluster.hosts[i].ctx->resp_mr->lkey;
        sge.addr = (uintptr_t)&cluster.hosts[i].ca_resp;
        sge.length = sizeof(struct arbiter_response_header) + count * sizeof(struct arbiter_rate_update);

        wr.wr.rdma.rkey = cluster.hosts[i].ctx->rem_dest->rkey_resp;
        wr.wr.rdma.remote_addr = cluster.hosts[i].ctx->rem_dest->vaddr_resp;
        //printf("DEDEDE rkey_resp = %08x; i = %d\n\n", wr.wr.rdma.rkey, i);
        //printf("DEDEDE rkey_resp host 0 = %08x\n\n", cluster.hosts[0].ctx->rem_dest->rkey_resp);
        //printf("DEDEDE rkey_resp host 1 = %08x\n\n", cluster.hosts[1].ctx->rem_dest->rkey_resp);

        ibv_post_send(cluster.hosts[i].ctx->qp_req, &wr, &bad_wr);
        printf("sending out new response (Host<%d>-[%d], num_rate_updates = %d)\n", i, cluster.hosts[i].ca_resp.header.id, count);

        do {
            num_comp = ibv_poll_cq(cluster.hosts[i].ctx->cq_req, 1, &wc);
        } while (num_comp == 0);

        if (num_comp < 0 || wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "error polling wc from response wr\n");
            //printf("num_comp = %d\n", num_comp);
            //printf("wc.status = %d\n", wc.status);
            perror("ibv_poll_cq");
            exit(EXIT_FAILURE);
        }
        printf("done polling the wr\n");
    }


}

//TODO: add mutex for multi-threading in flows list later
void compute_rate()
{
    heap_t *ports = calloc(1, sizeof(heap_t));
    int i, sort_key;
    port_t *port = NULL;
    flow_t *flow = NULL;
    node_t *node = NULL;
    for (i = 0; i < cluster.num_hosts; ++i) {
        port = cluster.hosts[i].egress_port;
        if (port->flows.length) {
            //sort_key = port->max_rate / port->unassigned_flows;
            sort_key = port->max_rate / port->flows.length;
            pq_push(ports, sort_key, port);
        }
        port = cluster.hosts[i].ingress_port;
        if (port->flows.length) {
            //sort_key = port->max_rate / port->unassigned_flows;
            sort_key = port->max_rate / port->flows.length;
            pq_push(ports, sort_key, port);
        }
    }

    /* both src and dest ports needs to be updated:
        if a given flow is assigned a rate at an egress port,
            find the flow's dest port (which is an ingress port) and decrement the unassigned_flows there.
        if the flow is assigned at an ingress port instead,
            find the flow's src port (which is an egress port) and decrement the counter there.
    */
    while((port = pq_pop(ports)) != NULL) {
        port->unassigned_flows = port->flows.length;
        port->used_rate = 0;
        printf("COMPUTE_RATE: @Port[%d], num_flow = %zu, num_unassigned_flow = %d\n", port->host_id, port->flows.length, port->unassigned_flows);
        node = port->flows.head;
        while (node != NULL) {
            flow = node->data;
            if (!port->unassigned_flows) {  /* can happen for ports with low priority after a few pops */
                printf("port->unassigned = 0! skip this port.\n");
                node = node->next;
                continue;
            }

            flow->rate = (port->max_rate - port->used_rate) / port->unassigned_flows;
            printf("rate = %d\n", flow->rate);
            flow->is_assigned = 1;
            port->unassigned_flows--;
            port->used_rate += flow->rate;

            /* update the port on the other side; the logic also works for READ flows due to the way we set src/dest */
            if (port->is_egress) {
                cluster.hosts[flow->dest].ingress_port->unassigned_flows--;
                cluster.hosts[flow->dest].ingress_port->used_rate += flow->rate;
            } else {
                cluster.hosts[flow->src].egress_port->unassigned_flows--;
                cluster.hosts[flow->src].egress_port->used_rate += flow->rate;
            }

            if (port->unassigned_flows < 0) {
                fprintf(stderr, "Error computing rates: unassigned_flows can't be negative\n");
                free(ports);
                exit(EXIT_FAILURE);
            }
            
            node = node->next;
        }

    }

    free(ports);
}

/* read host updates and send out response (rate, ringbuf_info, etc) */
static void handle_host_updates()
{
    /* checking ring buffer for each host */
    unsigned int i, head;
    unsigned int msg_flag = 0;
    while (1) {
        msg_flag = 0;   /* "receving msg from any host will cause response sent to all hosts" is the current implementation */
        for (i = 0; i < cluster.num_hosts; ++i) {
            head = cluster.hosts[i].ring->head + 1;
            if (head == RING_BUFFER_SIZE)
                head = 0;

            while (__atomic_load_n(&cluster.hosts[i].ring->host_req[head].check_byte, __ATOMIC_RELAXED) == 1) {
            //while ((num_req = __atomic_load_n(&cluster.hosts[i].ring->host_req[head].num_req, __ATOMIC_RELAXED)) != 0) {
                printf("Getting new request from Host%d; ", i);
                msg_flag = 1;
                if (cluster.hosts[i].ring->host_req[head].type == RMF_ABOVE_TARGET) {
                    printf("received RMF_ABOVE_TARGET message\n");
                } else if (cluster.hosts[i].ring->host_req[head].type == RMF_BELOW_TARGET) {
                    printf("received RMF_BELOW_TARGET message\n");
                } else if (cluster.hosts[i].ring->host_req[head].type == FLOW_JOIN) {
                    printf("received FLOW_JOIN message\n");
                    update_flow_info(&cluster.hosts[i].ring->host_req[head], i);
                } else if (cluster.hosts[i].ring->host_req[head].type == FLOW_EXIT) {
                    printf("received FLOW_EXIT message\n");
                    update_flow_info(&cluster.hosts[i].ring->host_req[head], i);
                } else {
                    printf("unrecognized message\n");
                    exit(EXIT_FAILURE);
                }
                //compute_rate(&cluster.hosts[i].ring->host_req[head]);
                cluster.hosts[i].ring->host_req[head].check_byte = 0;
                ++head;
                if (head == RING_BUFFER_SIZE)
                    head = 0;
            }


            if (msg_flag) {
                msg_flag = 0;   /* in send_out_responses() responses will be sent to the other hosts too. so clear the flag here to avoid resendind */
                /* compute rate */
                compute_rate();

                /* update head pointer */
                if (head == 0) {
                    cluster.hosts[i].ring->head = RING_BUFFER_SIZE - 1;
                } else {
                    cluster.hosts[i].ring->head = head - 1;
                }

                /* send out responses (rate updates, sender's copy of head) to the hosts that gets affected */ 
                send_out_responses();
            }
        }
    }

}

int main(int argc, char **argv)
{
    /* set up signal handler */
    struct sigaction new_action, old_action;
    new_action.sa_handler = termination_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;

    sigaction(SIGINT, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction(SIGINT, &new_action, NULL);

    sigaction(SIGHUP, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction(SIGHUP, &new_action, NULL);

    sigaction(SIGTERM, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction(SIGTERM, &new_action, NULL);
    /* end */
    atexit(rm_shmem_on_exit);

    //int fd_shm, i;
    //pthread_t th1, th2, th3, th4, th5;
    pthread_t th1;

    /* input args check */
    if (argc != 2) {
        usage();
        exit(EXIT_FAILURE);
    }
    /*
    struct monitor_param param;
    char *endPtr;
    param.addr = argv[1];
    param.gid_idx = -1;
    if (argc == 3)
    {
        param.isclient = strtol(argv[2], &endPtr, 10);
    }
    else if (argc == 4)
    {
        param.isclient = strtol(argv[2], &endPtr, 10);
        param.gid_idx = strtol(argv[3], NULL, 10);
    }
    else
    {
        usage();
        exit(1);
    }
    */

    /* allocate shared memory */
    /*
    if ((fd_shm = shm_open(SHARED_MEM_NAME, O_RDWR | O_CREAT, 0666)) < 0)
        error("shm_open");

    if (ftruncate(fd_shm, sizeof(struct shared_block)) < 0)
        error("ftruncate");

    if ((cb.sb = mmap(NULL, sizeof(struct shared_block),
                      PROT_WRITE | PROT_READ, MAP_SHARED, fd_shm, 0)) == MAP_FAILED)
        error("mmap");
    */

    /* parse cluster info from input file */
    FILE *fp;
    fp = fopen(argv[1], "r");
    char line[64];
    int num_hosts = 0;
    int i = 0;
    while (fgets(line, sizeof(line), fp)) {
        ++num_hosts;
    }
    rewind(fp);
    printf("NUM_HOSTS = %d\n", num_hosts);

    char **ip = malloc(num_hosts * sizeof(char *));
    for (i = 0; i < num_hosts; ++i) {
        ip[i] = calloc(64, sizeof(char));
    }
    char *gid_idx_char, *token;
    int *gid_idx = malloc(num_hosts * sizeof(int));
    for (i = 0; i < num_hosts; ++i) {
        gid_idx[i] = -1;
    }

    i = 0;
    while (fgets(line, sizeof(line), fp)) {
        token = strtok(line, " ");
        strcpy(ip[i], token);
        printf("HOST%d = %s", i + 1, ip[i]);
        gid_idx_char = strtok(NULL, " ");
        if (gid_idx_char != NULL) {
            gid_idx[i] = strtol(gid_idx_char, NULL, 10);
            printf("HOST%d gid_idx = %d", i + 1, gid_idx[i]);
        }
        ++i;
    }
    fclose(fp);

    /* initialize control structure */
    cluster.num_hosts = num_hosts;
    cluster.hosts = calloc(num_hosts, sizeof(struct host_info));
    for (i = 0; i < num_hosts; ++i) {
        printf("HOST LOOP #%d\n", i + 1);
        /* init ctx, mr, and connect to each host via RDMA RC */
        cluster.hosts[i].ring = calloc(1, sizeof(struct request_ring_buffer));
        cluster.hosts[i].ring->head = RING_BUFFER_SIZE - 1;
        cluster.hosts[i].ingress_port = calloc(1, sizeof(port_t));
        cluster.hosts[i].ingress_port->max_rate = LINE_RATE_MB;
        cluster.hosts[i].ingress_port->host_id = i;
        cluster.hosts[i].ingress_port->is_egress = 0;
        list_init(&cluster.hosts[i].ingress_port->flows);
        cluster.hosts[i].egress_port = calloc(1, sizeof(port_t));
        cluster.hosts[i].egress_port->max_rate = LINE_RATE_MB;
        cluster.hosts[i].egress_port->host_id = i;
        cluster.hosts[i].egress_port->is_egress = 1;
        list_init(&cluster.hosts[i].egress_port->flows);
        memset(&cluster.hosts[i].ca_resp, 0, sizeof(struct arbiter_response_region));
        memset(&cluster.hosts[i].flows, 0, sizeof(flow_t) * MAX_FLOWS);

        if (!RMF_DISTRIBUTE_AMONG_HOSTS) {
            cluster.hosts[i].ctx = init_ctx_and_build_conn(ip[i], NULL, 1, gid_idx[i], cluster.hosts[i].ring->host_req, &cluster.hosts[i].ca_resp, '2');
        } else {
            if (num_hosts % 2 == 0) {
                if (i % 2 == 0)
                    cluster.hosts[i].ctx = init_ctx_and_build_conn(ip[i], NULL, 1, gid_idx[i], cluster.hosts[i].ring->host_req, &cluster.hosts[i].ca_resp, '0');
                else
                    cluster.hosts[i].ctx = init_ctx_and_build_conn(ip[i], ip[i-1], 1, gid_idx[i], cluster.hosts[i].ring->host_req, &cluster.hosts[i].ca_resp, '1');
            } else {
                if (i == num_hosts - 1)
                    cluster.hosts[i].ctx = init_ctx_and_build_conn(ip[i], NULL, 1, gid_idx[i], cluster.hosts[i].ring->host_req, &cluster.hosts[i].ca_resp, '2');
                else if (i % 2 == 0)
                    cluster.hosts[i].ctx = init_ctx_and_build_conn(ip[i], NULL, 1, gid_idx[i], cluster.hosts[i].ring->host_req, &cluster.hosts[i].ca_resp, '0');
                else 
                    cluster.hosts[i].ctx = init_ctx_and_build_conn(ip[i], ip[i-1], 1, gid_idx[i], cluster.hosts[i].ring->host_req, &cluster.hosts[i].ca_resp, '1');
            }
        }
        if (cluster.hosts[i].ctx == NULL) {
            fprintf(stderr, "init_ctx_and_build_conn failed, exit\n");
            exit(EXIT_FAILURE);
        }

        cluster.hosts[i].lid = cluster.hosts[i].ctx->rem_dest->lid;
    }




    /* start thread handling host updates */
    printf("starting thread handling host updates...\n");
    if (pthread_create(&th1, NULL, (void *(*)(void *)) & handle_host_updates, NULL))
    {
        error("pthread_create: host_updates_handler");
    }

    /* start monitoring thread */
    //printf("starting thread for latency monitoring...\n");
    //if (pthread_create(&th2, NULL, (void *(*)(void *)) & monitor_latency, (void *)&param))
    //{
    //    error("pthread_create: monitor_latency");
    //}

    /* main loop: compute and distribute rate */
    while (1)
    {
        for (i = 0; i < num_hosts; i++)
        {

        }
    }

    free(gid_idx);
    for (i = 0; i < num_hosts; ++i) {
        free(ip[i]);
        free(cluster.hosts[i].ring);
        free(&cluster.hosts[i]);
    }

    return 0;
}
