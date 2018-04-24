#include "pacer.h"
#include "monitor.h"
#include "get_clock.h"
//#include <immintrin.h> /* For _mm_pause */
#include "countmin.h"

//#define DEFAULT_CHUNK_SIZE 10000000
#define DEFAULT_CHUNK_SIZE 1000000
//#define DEFAULT_BATCH_OPS 667
#define DEFAULT_BATCH_OPS 1500
#define MAX_TOKEN 5
#define HOSTNAME_PATH "/proc/sys/kernel/hostname"

//cycles_t start = 0, end = 0;

extern CMH_type *cmh;
struct control_block cb;
//uint32_t chunk_size_table[] = {4096, 8192, 16384, 32768, 65536, 1048576, 1048576};
//uint32_t chunk_size_table[] = {8192, 8192, 100000, 100000, 500000, 1000000, 1000000};
uint32_t chunk_size_table[] = {1000000, 1000000, 1000000, 1000000, 1000000, 1000000, 1000000};	// Use 1048576 in Conflux
/* utility fuctions */
static void error(char *msg)
{
    perror(msg);
    exit(1);
}

static void usage()
{
    printf("Usage: ./pacer [gid_idx]\n");
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
    remove("/dev/shm/rdma-fairness");
    CMH_Destroy(cmh);
    _exit(0);
}

static void rm_shmem_on_exit()
{
    remove("/dev/shm/rdma-fairness");
}

char *get_sock_path() {
    FILE *fp;
    fp = fopen(HOSTNAME_PATH, "r");
    if (fp == NULL) {
        printf("Error opening %s, use default SOCK_PATH", HOSTNAME_PATH);
        fclose(fp);
        return SOCK_PATH;
    }

    char hostname[60];
    if (fgets(hostname, 60, fp) != NULL) {
        char *sock_path = malloc(108 * sizeof(char));
        printf("DE hostname:%s\n", hostname);
        int len = strlen(hostname);
        if (len > 0 && hostname[len-1] == '\n') hostname[len-1] = '\0';
        strcat(hostname, "_rdma_socket");
        strcpy(sock_path, getenv("HOME"));
        len = strlen(sock_path);
        sock_path[len] = '/';
        strcat(sock_path, hostname);
        fclose(fp);
        return sock_path;
    }

    fclose(fp);
    return SOCK_PATH;
}
/* end */

/* circular buffer for future logging */
struct circular_buffer {
    void *buffer;     // data buffer
    void *buffer_end; // end of data buffer
    size_t capacity;  // maximum number of items in the buffer
    size_t count;     // number of items in the buffer
    size_t sz;        // size of each item in the buffer
    void *head;       // pointer to head
    void *tail;       // pointer to tail
};

//struct circular_buffer token_cbuf;

int cbuf_init(struct circular_buffer *buf, size_t capacity, size_t sz)
{
    buf->buffer = malloc(capacity * sz);
    if (buf->buffer == NULL)
        return -1;
    buf->buffer_end = (char *)buf->buffer + capacity * sz;
    buf->capacity = capacity;
    buf->count = 0;
    buf->sz = sz;
    buf->head = buf->buffer;
    buf->tail = buf->buffer;
    return 0;
}

void cbuf_free(struct circular_buffer *buf)
{
    free(buf->buffer);
}

int cbuf_push_back(struct circular_buffer *buf, const void *item)
{
    if (buf->count == buf->capacity)
        return -1;
    memcpy(buf->head, item, buf->sz);
    buf->head = (char*)buf->head + buf->sz;
    if (buf->head == buf->buffer_end)
        buf->head = buf->buffer;
    buf->count++;
    return 0;
}

int cbuf_pop_front(struct circular_buffer *buf, void *item)
{
    if (buf->count == 0)
        return -1;
    memcpy(item, buf->tail, buf->sz);
    buf->tail = (char*)buf->tail + buf->sz;
    if (buf->tail == buf->buffer_end)
        buf->tail = buf->buffer;
    buf->count--;
    return 0;
}

void logging_tokens()
{
    //cbuf_init(&token_cbuf, 1000000, sizeof(uint64_t));
    //cbuf_push_back(&token_cbuf, &cb.tokens)
    FILE *f = fopen("token_log.txt", "w");
    fprintf(f, "Time(us)\tnum_tokens\n");
    cycles_t start_cycle, curr_cycle = 0;
    start_cycle = get_cycles();
    double cpu_mhz = get_cpu_mhz(1);
    while (1) {
        while (get_cycles() - curr_cycle < cpu_mhz * DEFAULT_CHUNK_SIZE / LINE_RATE_MB)
            cpu_relax();
        curr_cycle = get_cycles();
        fprintf(f, "%.2f\t\t%lld\n", ((double) (curr_cycle - start_cycle) / cpu_mhz), cb.tokens);
        //fprintf(f, "%.2f\t\t%" PRIu64 "\n", (double) ((curr_cycle - start_cycle) / cpu_mhz), __atomic_load_n(&cb.tokens, __ATOMIC_RELAXED));
    }

}
/* end */

/* submit a host request to the ring buffer; also used in monitor.c */
void submit_request(enum host_request_type type, uint8_t is_read, uint16_t dlid, uint16_t flow_idx, unsigned int worker_id)
{
    ssize_t offset = -1;
    struct host_request request;
    //request.num_req = 0;
    request.type = type;
    request.dlid = dlid;
    request.flow_idx = flow_idx;
    request.is_read = is_read;
    request.check_byte = 1;

    while (offset == -1) {
        if (worker_id == 0) {
            offset = ringbuf_acquire(cb.ring, cb.flow_handler_worker, 1);
        } else if (worker_id == 1) {
            offset = ringbuf_acquire(cb.ring, cb.latency_monitor_worker, 1);
        }
    }

    memcpy(&cb.host_req[offset], &request, sizeof(struct host_request));

    if (worker_id == 0) {
        ringbuf_produce(cb.ring, cb.flow_handler_worker);
    } else if (worker_id == 1) {
        ringbuf_produce(cb.ring, cb.latency_monitor_worker);
    }
    
    printf("done submiting a request\n");
}

static void send_out_request()
{
    struct ibv_sge sge;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr;
    int num_comp = 0;
    struct ibv_wc wc;

    memset(&sge, 0, sizeof(sge));
    //sge.length = sizeof(struct host_request);
    sge.lkey = cb.ctx->req_mr->lkey;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 0;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.send_flags = IBV_SEND_SIGNALED;
    //wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;
    //TODO: check if SQ overflow is a problem in unsignalling
    //wr.send_flags = IBV_SEND_INLINE;
    wr.wr.rdma.rkey = cb.ctx->rem_dest->rkey_req;

    size_t offset = 0, len = 0, rem = 0, num_req = 0;
    uint16_t sender_head = 0;
    while (1) {
        len = ringbuf_consume(cb.ring, &offset);
        if (len) {
            printf("LEN = %d\n", len);
            rem = len;
            /* check sender's head updates from arbiter */
            /* send update */
            sender_head = __atomic_load_n(&cb.sender_head, __ATOMIC_RELAXED);
            
            /* the logic is fine without considering wrap-around because ringbuf at pacer side guarantees contiguous memory, and tail of both rings are always synchronized */
            while (rem && (cb.sender_tail != sender_head)) {
                if ((cb.sender_tail > sender_head && (cb.sender_tail - sender_head) <= (RING_BUFFER_SIZE - rem)) ||
                    (cb.sender_tail < sender_head && (sender_head - cb.sender_tail) >= rem)) {
                    num_req = rem;
                } else {
                    num_req = (cb.sender_tail > sender_head) ? (cb.sender_tail - sender_head) : (sender_head - cb.sender_tail);
                }
                sge.length = num_req * sizeof(struct host_request);
                //cb.host_req[offset].num_req = num_req;       /* update number of requests send in a batch */
                sge.addr = (uintptr_t)&cb.host_req[offset];

                printf("cb.sender_tail = %d\n", cb.sender_tail);
                printf("sender_head = %d\n", sender_head);
                wr.wr.rdma.remote_addr = cb.ctx->rem_dest->vaddr_req + cb.sender_tail * sizeof(struct host_request);
                if (ibv_post_send(cb.ctx->qp_req, &wr, &bad_wr)) {
                    fprintf(stderr, "DEBUG POST SEND: REALLY BAD!!, errno = %d\n", errno);
                    exit(EXIT_FAILURE);
                }

                rem -= num_req;
                offset += num_req;
                cb.sender_tail += num_req;
                if (cb.sender_tail >= RING_BUFFER_SIZE) {
                    cb.sender_tail -= RING_BUFFER_SIZE;
                }

                do {
                    if (cb.host_req[offset-1].type == FLOW_JOIN || cb.host_req[offset-1].type == FLOW_EXIT) 
                        num_comp = ibv_poll_cq(cb.ctx->cq_req, 1, &wc);
                    else
                        num_comp = ibv_poll_cq(cb.ctx->cq_rmf, 1, &wc);
                } while (num_comp == 0);
                if (num_comp < 0 || wc.status != IBV_WC_SUCCESS) {
                    fprintf(stderr, "error polling wc from request wr\n");
                    perror("ibv_poll_cq");
                    exit(EXIT_FAILURE);
                }
                printf("done polling the wr\n");
                
            }


            /* send request one at a time (old)
            while (rem && (cb.sender_tail != sender_head))  {
                //printf("OFFSET = %d\n", offset);
                sge.addr = (uintptr_t)&cb.host_req[offset++];
                --rem;

                printf("cb.sender_tail = %d\n", cb.sender_tail);
                printf("sender_head = %d\n", sender_head);
                //printf("cb.host_req[0].check_byte = %d\n", cb.host_req[0].check_byte);
                wr.wr.rdma.remote_addr = cb.ctx->rem_dest->vaddr_req + cb.sender_tail * sizeof(struct host_request);
                if (ibv_post_send(cb.ctx->qp_req, &wr, &bad_wr)) {
                    fprintf(stderr, "DEBUG POST SEND: REALLY BAD!!, errno = %d\n", errno);
                    exit(EXIT_FAILURE);
                }
                printf("done sending out one request\n");

                int num_comp = 0;
                struct ibv_wc wc;
                do {
                    if (cb.host_req[offset-1].type == FLOW_JOIN || cb.host_req[offset-1].type == FLOW_EXIT) 
                        num_comp = ibv_poll_cq(cb.ctx->cq_req, 1, &wc);
                    else
                        num_comp = ibv_poll_cq(cb.ctx->cq_rmf, 1, &wc);
                } while (num_comp == 0);
                if (num_comp < 0 || wc.status != IBV_WC_SUCCESS) {
                    printf("num_comp = %d\n", num_comp);
                    printf("wc.status = %d\n", wc.status);
                    perror("ibv_poll_cq");
                    break;
                }
                printf("done polling the request\n");

                ++cb.sender_tail;
                if (cb.sender_tail == RING_BUFFER_SIZE)
                    cb.sender_tail = 0;
            }
            */
            ringbuf_release(cb.ring, len);
        }
    }
}

/* handle incoming flows one by one; assign a slot to an incoming flow */
static void flow_handler()
{
    /* prepare unix domain socket communication */
    printf("starting flow_handler...\n");
    unsigned int s, s2, len;
    uint16_t slot;
    struct sockaddr_un local, remote;
    char buf[MSG_LEN];
    char *sock_path = get_sock_path();

    /* no longer need to handle read rate update from pacer */
    /*
    struct ibv_send_wr send_wr, *bad_wr = NULL;
    struct ibv_sge send_sge;

    memset(&send_wr, 0, sizeof send_wr);
    send_wr.opcode = IBV_WR_SEND;
    send_wr.sg_list = &send_sge;
    send_wr.num_sge = 1;
    send_wr.send_flags = IBV_SEND_INLINE;

    memset(&send_sge, 0, sizeof send_sge);
    */

    /* get a socket descriptor */
    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
        error("socket");

    /* bind to an address */
    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, sock_path);
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);
    if (bind(s, (struct sockaddr *)&local, len))
        error("bind");

    /* listen for clients */
    if (listen(s, 10))
        error("listen");

    /* handling loop */
    while (1)
    {
        len = sizeof(struct sockaddr_un);
        if ((s2 = accept(s, (struct sockaddr *)&remote, &len)) == -1)
            error("accept");

        /* check join */
        len = recv(s2, (void *)buf, (size_t)MSG_LEN, 0);
        printf("receive message of length %d.\n", len);
        ////buf[len] = '\0';     /* now msg len is always fixed at MSG_LEN; msg itself will contain null char */
        printf("message is %s.\n", buf);
        if (strcmp(buf, "join") == 0)
        {
            /* send back slot number */
            printf("sending back slot number %d...\n", cb.next_slot);
            len = snprintf(buf, MSG_LEN, "%d", cb.next_slot);
            cb.sb->flows[cb.next_slot].active = 1;
            send(s2, &buf, len, 0);

            /* find next empty slot */
            cb.next_slot = (cb.next_slot + 1) % MAX_FLOWS;
            while (__atomic_load_n(&cb.sb->flows[cb.next_slot].active, __ATOMIC_RELAXED))
            {
                cb.next_slot = (cb.next_slot + 1) % MAX_FLOWS;
            }
        }
        else if (strcmp(buf, "WRITEjoin") == 0)     /* when receiving WRITEjoin or READjoin, flow has been joined before and is trying to send its first message */
        {
            /* submit update to CA */
            if ((len = recv(s2, (void *)buf, (size_t)MSG_LEN, 0)) > 0) {
                printf("receive slot message of length %d.\n", len);
                ////buf[len] = '\0';    /* now msg len is always fixed at MSG_LEN; msg itself will contain null char */
                slot = strtol(buf, NULL, 10);
                printf("flow slot is %d.\n", slot);
            } else {
                if (len < 0) perror("recv");
                else printf("Server closed connection\n");
                exit(1);
            }

            //start = get_cycles();
            submit_request(FLOW_JOIN, 0, __atomic_load_n(&cb.sb->flows[slot].dlid, __ATOMIC_RELAXED), slot, 0);
            //int j;
            //for (j = 0; j < 200; j++) {
            //    submit_request(FLOW_JOIN, 0, cb.sb->flows[cb.next_slot].dest_qp_num, 0);
            //}
            printf("sending WRITE/SEND FLOW JOIN message\n");
        }
        else if (strcmp(buf, "READjoin") == 0)
        {
            printf("READ detected\n");
            if ((len = recv(s2, (void *)buf, (size_t)MSG_LEN, 0)) > 0) {
                printf("receive slot message of length %d.\n", len);
                ////buf[len] = '\0';    /* now msg len is always fixed at MSG_LEN; msg itself will contain null char */
                slot = strtol(buf, NULL, 10);
                printf("flow slot is %d.\n", slot);
            } else {
                if (len < 0) perror("recv");
                else printf("Server closed connection\n");
                exit(1);
            }

            /*
            send_sge.addr = (uintptr_t)cb.ctx->local_read_buf;
            send_sge.length = BUF_READ_SIZE;
            send_sge.lkey = cb.ctx->local_read_mr->lkey;

            strcpy(cb.ctx->local_read_buf, buf);
            ibv_post_send(cb.ctx->qp_read, &send_wr, &bad_wr);
            __atomic_fetch_add(&cb.num_big_read_flows, 1, __ATOMIC_RELAXED);
            */

            /* submit update to CA */
            submit_request(FLOW_JOIN, 1, cb.sb->flows[slot].dlid, slot, 0);
            printf("sending READ FLOW JOIN message\n");
        }
        else if (strcmp(buf, "exit") == 0)
        {
            //TODO: include read verb as the FLOW_EXIT request sent to the centralized arbiter
            /*
            strcpy(cb.ctx->local_read_buf, buf);
            ibv_post_send(cb.ctx->qp_read, &send_wr, &bad_wr);
            __atomic_fetch_sub(&cb.num_big_read_flows, 1, __ATOMIC_RELAXED);
            */
            if ((len = recv(s2, (void *)buf, (size_t)MSG_LEN, 0)) > 0) {
                printf("receive slot message of length %d.\n", len);
                ////buf[len] = '\0';    /* now msg len is always fixed at MSG_LEN; msg itself will contain null char */
                slot = strtol(buf, NULL, 10);
                printf("flow slot is %d.\n", slot);
            } else {
                if (len < 0) perror("recv");
                else printf("Server closed connection\n");
                exit(1);
            }

            /* make flow at this slot inactive */ 
            cb.sb->flows[slot].active = 0;

            /* submit update to CA */
            submit_request(FLOW_EXIT, 0, cb.sb->flows[slot].dlid, slot, 0);
            printf("sending FLOW EXIT message\n");
        }
    }
}

/* fetch one token; block if no token is available 
 */
static inline void fetch_token() __attribute__((always_inline));
static inline void fetch_token()
{
    while (!__atomic_load_n(&cb.tokens, __ATOMIC_RELAXED))
        cpu_relax();
    __atomic_fetch_sub(&cb.tokens, 1, __ATOMIC_RELAXED);
}

static inline void fetch_token_read() __attribute__((always_inline));
static inline void fetch_token_read()
{
    while (!__atomic_load_n(&cb.tokens_read, __ATOMIC_RELAXED))
        cpu_relax();
    __atomic_fetch_sub(&cb.tokens_read, 1, __ATOMIC_RELAXED);
}

static void handle_response()
{
    uint32_t curr_id = 1, prev_id = 0;
    int i = 0, num_rate_updates = 0, flow_idx, rate = 0;
    while (1) {
        /* update sender's copy of head at arbiter's ring buffer, and get new rate*/
        //TODO: handle wrap-around
        curr_id = __atomic_load_n(&cb.ca_resp.header.id, __ATOMIC_RELAXED);
        if (curr_id > prev_id) {
            prev_id = curr_id;
            __atomic_store_n(&cb.sender_head, cb.ca_resp.header.sender_head, __ATOMIC_RELAXED);
            //__atomic_store_n(&cb.virtual_link_cap, cb.ca_resp.rate, __ATOMIC_RELAXED);
            /* temporary hack */
            __atomic_store_n(&cb.virtual_link_cap, LINE_RATE_MB, __ATOMIC_RELAXED);
            
            num_rate_updates = __atomic_load_n(&cb.ca_resp.header.num_rate_updates, __ATOMIC_RELAXED);
            printf("received a new response from central arbiter [id:%d, num_updates = %d]\n", curr_id, cb.ca_resp.header);
            printf("new sender_head: %d\n", cb.sender_head);
            for (i = 0; i < num_rate_updates; i++) {
                rate = __atomic_load_n(&cb.ca_resp.rate_updates[i].rate, __ATOMIC_RELAXED);
                flow_idx = __atomic_load_n(&cb.ca_resp.rate_updates[i].flow_idx, __ATOMIC_RELAXED);
                printf("rate update #%d: flow[%d] = %d", i, flow_idx, rate);
            }
        } 
        //TODO: find another way to detect error. The following way doesn't work for obvious reasons
        //else {
        //    fprintf(stderr, "Error receiving responses, prev_id: %d, new_id: %d\n", prev_id, cb.ca_resp.header.id);
        //}
        
    }
}


/* generate tokens at some rate
 */
static void generate_tokens()
{
    cycles_t start_cycle = 0;
    int cpu_mhz = get_cpu_mhz(1);
    int start_flag = 1;
    // struct timespec wait_time;

    /* infinite loop: generate tokens at a rate calculated 
     * from virtual_link_cap and active chunk size 
     */
    uint32_t temp, chunk_size;
    uint16_t num_big;
    while (1)
    {
        //TODO: change locally calculated link cap to the one updated from arbiter
        if ((temp = __atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED)))
        {
            if ((num_big = __atomic_load_n(&cb.sb->num_active_big_flows, __ATOMIC_RELAXED)))
                chunk_size = chunk_size_table[temp / num_big / (LINE_RATE_MB/6)];
            else
                chunk_size = DEFAULT_CHUNK_SIZE;
            __atomic_store_n(&cb.sb->active_chunk_size, chunk_size, __ATOMIC_RELAXED);
            //__atomic_store_n(&cb.sb->active_batch_ops, chunk_size/1000000.0*DEFAULT_BATCH_OPS, __ATOMIC_RELAXED);
            __atomic_store_n(&cb.sb->active_batch_ops, chunk_size/DEFAULT_CHUNK_SIZE*DEFAULT_BATCH_OPS, __ATOMIC_RELAXED);
            // __atomic_fetch_add(&cb.tokens, 10, __ATOMIC_RELAXED);
            // wait_time.tv_nsec = 10 * chunk_size / temp * 1000;
            if (__atomic_load_n(&cb.tokens, __ATOMIC_RELAXED) < MAX_TOKEN)
            {
                if (start_flag)
                {
                    start_flag = 0;
                    start_cycle = get_cycles();
                    __atomic_fetch_add(&cb.tokens, 1, __ATOMIC_RELAXED);
                }
                else
                {
                    while (get_cycles() - start_cycle < cpu_mhz * chunk_size / temp)
                        cpu_relax();
                    start_cycle = get_cycles();
                    __atomic_fetch_add(&cb.tokens, 1, __ATOMIC_RELAXED);
                }
            }
        }
        // nanosleep(&wait_time, NULL);
    }
}

static void generate_tokens_read()
{
    cycles_t start_cycle = 0;
    int cpu_mhz = get_cpu_mhz(1);
    int start_flag = 1;
    // struct timespec wait_time;

    /* infinite loop: generate tokens at a rate calculated 
     * from virtual_link_cap and active chunk size 
     */
    uint32_t temp, chunk_size;
    uint16_t num_read;
    while (1)
    {
        if ((num_read = __atomic_load_n(&cb.num_big_read_flows, __ATOMIC_RELAXED)))
        {
            // temp = 4999; // for testing
            if ((temp = __atomic_load_n(&cb.local_read_rate, __ATOMIC_RELAXED)))
            {
                chunk_size = chunk_size_table[temp / num_read / 1000];
                __atomic_store_n(&cb.sb->active_chunk_size_read, chunk_size, __ATOMIC_RELAXED);
                // __atomic_fetch_add(&cb.tokens, 10, __ATOMIC_RELAXED);
                // wait_time.tv_nsec = 10 * chunk_size / temp * 1000;
                if (__atomic_load_n(&cb.tokens_read, __ATOMIC_RELAXED) < MAX_TOKEN)
                {
                    if (start_flag)
                    {
                        start_flag = 0;
                        start_cycle = get_cycles();
                        __atomic_fetch_add(&cb.tokens_read, 1, __ATOMIC_RELAXED);
                    }
                    else
                    {
                        while (get_cycles() - start_cycle < cpu_mhz * chunk_size / temp)
                            cpu_relax();
                        start_cycle = get_cycles();
                        __atomic_fetch_add(&cb.tokens_read, 1, __ATOMIC_RELAXED);
                    }
                }
            }
        }
        // nanosleep(&wait_time, NULL);
    }
}

void rate_limit_read()
{
    int i;
    while (1)
    {
        for (i = 0; i < MAX_FLOWS; i++)
        {
            if (__atomic_load_n(&cb.sb->flows[i].read, __ATOMIC_RELAXED) && __atomic_load_n(&cb.sb->flows[i].pending, __ATOMIC_RELAXED))
            {
                fetch_token_read();
                __atomic_store_n(&cb.sb->flows[i].pending, 0, __ATOMIC_RELAXED);
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

    int fd_shm, i;
    pthread_t th1, th2, th3, th4, th5;
    int gid_idx = -1;
    if (argc == 2)
    {
        gid_idx = strtol(argv[3], NULL, 10);
    }
    else if (argc != 1)
    {
        usage();
        exit(1);
    }

    /* allocate shared memory */
    if ((fd_shm = shm_open(SHARED_MEM_NAME, O_RDWR | O_CREAT, 0666)) < 0)
        error("shm_open");

    if (ftruncate(fd_shm, sizeof(struct shared_block)) < 0)
        error("ftruncate");

    if ((cb.sb = mmap(NULL, sizeof(struct shared_block),
                      PROT_WRITE | PROT_READ, MAP_SHARED, fd_shm, 0)) == MAP_FAILED)
        error("mmap");

    /* initialize control block */
    cb.tokens = 0;
    cb.tokens_read = 0;
    cb.num_big_read_flows = 0;
    cb.virtual_link_cap = LINE_RATE_MB;
    cb.local_read_rate = LINE_RATE_MB;
    cb.next_slot = 0;
    cb.sb->active_chunk_size = DEFAULT_CHUNK_SIZE;
    cb.sb->active_chunk_size_read = DEFAULT_CHUNK_SIZE;
    cb.sb->active_batch_ops = DEFAULT_BATCH_OPS;
    cb.sb->num_active_big_flows = 0;
    cb.sb->num_active_small_flows = 0;
    //cb.sender_head = 0;
    cb.sender_head = RING_BUFFER_SIZE - 1;
    cb.sender_tail = 0;
    memset(&cb.ca_resp, 0, sizeof(struct arbiter_response_region));
    for (i = 0; i < MAX_FLOWS; i++)
    {
        cb.sb->flows[i].pending = 0;
        cb.sb->flows[i].active = 0;
    }

    /* initialize data buffer for the ring buffer for requests issued from the host pacer */
    cb.host_req = calloc(RING_BUFFER_SIZE, sizeof(struct host_request));

    /* setup host side ring buffer manager */
    size_t ring_buf_size = 0;
    ringbuf_get_sizes(2, &ring_buf_size, NULL);
    cb.ring = malloc(ring_buf_size);
    ringbuf_setup(cb.ring, 2, RING_BUFFER_SIZE);

    /* register worker for host request ring buffer */
    cb.flow_handler_worker = ringbuf_register(cb.ring, 0);
    cb.latency_monitor_worker = ringbuf_register(cb.ring, 1);

    /* initialize RDMA context and build connection with CA (2 QPs) */
    //TODO: fix input arg later
    cb.ctx = init_ctx_and_build_conn(NULL, NULL, 0, gid_idx, cb.host_req, &cb.ca_resp, '0');    /* last arg is don't-care for pacer */

    printf("starting thread for response handling...\n");
    if (pthread_create(&th4, NULL, (void *(*)(void *)) & handle_response, NULL))
    {
        error("pthread_create: handle_response");
    }

    printf("starting thread for sending out requests...\n");
    if (pthread_create(&th5, NULL, (void *(*)(void *)) & send_out_request, NULL))
    {
        error("pthread_create: send_out_request");
    }

    printf("starting thread for flow handling...\n");
    if (pthread_create(&th1, NULL, (void *(*)(void *)) & flow_handler, NULL))
    {
        error("pthread_create: flow_handler");
    }

    printf("starting thread for latency monitoring...\n");
    if (pthread_create(&th2, NULL, (void *(*)(void *)) & monitor_latency, (void *)cb.ctx))
    {
        error("pthread_create: monitor_latency");
    }

    printf("starting thread for token generating...\n");
    if (pthread_create(&th3, NULL, (void *(*)(void *)) & generate_tokens, NULL))
    {
        error("pthread_create: generate_tokens");
    }


    /*
    printf("starting thread for token generating for read...\n");
    if (pthread_create(&th4, NULL, (void *(*)(void *)) & generate_tokens_read, NULL))
    {
        error("pthread_create: generate_tokens_read");
    }

    printf("starting thread for rate limiting big read flows...\n");
    if (pthread_create(&th4, NULL, (void *(*)(void *)) & rate_limit_read, NULL))
    {
        error("pthread_create: generate_tokens_read");
    }
    */

    /* logging thread */
    /*
    printf("starting thread for logging...\n");
    if (pthread_create(&th5, NULL, (void *(*)(void *)) & logging_tokens, NULL))
    {
        error("pthread_create: logging_tokens");
    }
    */

    /* main loop: fetch token */
    while (1)
    {
        for (i = 0; i < MAX_FLOWS; i++)
        {
            if (!__atomic_load_n(&cb.sb->flows[i].read, __ATOMIC_RELAXED) && __atomic_load_n(&cb.sb->flows[i].pending, __ATOMIC_RELAXED))
            {
                fetch_token();
                __atomic_store_n(&cb.sb->flows[i].pending, 0, __ATOMIC_RELAXED);
            }
        }
    }
}
