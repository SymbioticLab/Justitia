#include "arbiter.h"
#include "monitor.h"
#include "get_clock.h"

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
    printf("sig//nal handler called\n");
    //remove("/dev/shm/rdma-fairness");
    //CMH_Destroy(cmh);
    _exit(0);
}

static void rm_shmem_on_exit()
{
    //remove("/dev/shm/rdma-fairness");
}

//TODO: compute rate 
// don't assign 0 to rate -- 0 value assumed somewhere else
uint32_t compute_rate(struct host_request *host_req)
{
    uint32_t rate = LINE_RATE_MB;
    return rate;
}

/* read host updates and send out response (rate, ringbuf_info, etc) */
static void handle_host_updates()
{
    struct ibv_sge sge;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr;

    memset(&sge, 0, sizeof(sge));
    sge.length = sizeof(struct arbiter_response);

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 0;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    //wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;
    wr.send_flags = IBV_SEND_INLINE;

    /* checking ring buffer for each host */
    unsigned int i, head;
    while (1) {
        for (i = 0; i < cluster.num_hosts; ++i) {
            head = cluster.hosts[i].ring->head + 1;
            if (head == RING_BUFFER_SIZE)
                head = 0;
            uint32_t rate = 0;
            
            while (cluster.hosts[i].ring->host_req[head].check_byte == 1) {
                printf("Getting new request from Host%d; ", i);
                if (cluster.hosts[i].ring->host_req[head].type == RMF_ABOVE_TARGET) {
                    printf("received RMF_EXCEED_TARGET message\n");
                } else if (cluster.hosts[i].ring->host_req[head].type == RMF_BELOW_TARGET) {
                    printf("received RMF_BELOW_TARGET message\n");
                } else if (cluster.hosts[i].ring->host_req[head].type == FLOW_JOIN) {
                    printf("received FLOW_JOIN message\n");
                } else if (cluster.hosts[i].ring->host_req[head].type == FLOW_EXIT) {
                    printf("received FLOW_EXIT message\n");
                } else {
                    printf("unrecognized message\n");
                    exit(EXIT_FAILURE);
                }
                rate = compute_rate(&cluster.hosts[i].ring->host_req[head]);
                cluster.hosts[i].ring->host_req[head].check_byte = 0;
                ++head;
                if (head == RING_BUFFER_SIZE)
                    head = 0;
            }
            /* update head pointer */
            if (head == 0) {
                cluster.hosts[i].ring->head = RING_BUFFER_SIZE - 1;
            } else {
                cluster.hosts[i].ring->head = head - 1;
            }

            /* send out responses (rate updates, sender's copy of head) */ 
            if (rate) {    /* if ever computed a rate */
                cluster.hosts[i].ca_resp.rate = rate;
                cluster.hosts[i].ca_resp.sender_head = cluster.hosts[i].ring->head;
                ++cluster.hosts[i].ca_resp.id;

                sge.lkey = cluster.hosts[i].ctx->resp_mr->lkey;
                sge.addr = (uintptr_t)&cluster.hosts[i].ca_resp;

                wr.wr.rdma.rkey = cluster.hosts[i].ctx->rem_dest->rkey_resp;
                wr.wr.rdma.remote_addr = cluster.hosts[i].ctx->rem_dest->vaddr_resp;

                ibv_post_send(cluster.hosts[i].ctx->qp_req, &wr, &bad_wr);
                printf("sending out new response [%d]\n", cluster.hosts[i].ca_resp.id);
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
