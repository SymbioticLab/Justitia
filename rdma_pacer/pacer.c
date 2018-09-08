#include "pacer.h"
#include "monitor.h"
#include "get_clock.h"
//#include <immintrin.h> /* For _mm_pause */
#include "countmin.h"

//#define DEFAULT_CHUNK_SIZE 10000000
//#define DEFAULT_CHUNK_SIZE 1000000
#define DEFAULT_CHUNK_SIZE 5000
//#define DEFAULT_BATCH_OPS 5000    // xl170 (when using 10Gbps link)
//#define DEFAULT_BATCH_OPS 667     // Conflux
#define DEFAULT_BATCH_OPS 1500    // c6220/r320
//#define DEFAULT_BATCH_OPS 2500    // Shin's RoCEv2
//#define MAX_TOKEN 5
#define MAX_TOKEN 5
#define HOSTNAME_PATH "/proc/sys/kernel/hostname"
//#define SPLIT_QP_NUM_ONE_SIDED 2
#define TIMEFRAME 2         // In microseconds

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
    printf("Usage: program remote-addr isclient [gid_idx]\n");
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

    char hostname[100];
    if (fgets(hostname, 100, fp) != NULL) {
        char *sock_path = (char *)malloc(108 * sizeof(char));
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
    cycles_t start_cycle, curr_cycle;
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

/* handle incoming flows one by one; assign a slot to an incoming flow */
static void flow_handler()
{
    /* prepare unix domain socket communication */
    printf("starting flow_handler...\n");
    unsigned int s, s2, len;
    struct sockaddr_un local, remote;
    char buf[MSG_LEN];
    char *sock_path = get_sock_path();

    struct ibv_send_wr send_wr, *bad_wr = NULL;
    struct ibv_sge send_sge;

    memset(&send_wr, 0, sizeof send_wr);
    send_wr.opcode = IBV_WR_SEND;
    send_wr.sg_list = &send_sge;
    send_wr.num_sge = 1;
    send_wr.send_flags = IBV_SEND_INLINE;

    memset(&send_sge, 0, sizeof send_sge);

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
        buf[len] = '\0';
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
        else if (strcmp(buf, "read") == 0)
        {
            send_sge.addr = (uintptr_t)cb.ctx->local_read_buf;
            send_sge.length = BUF_READ_SIZE;
            send_sge.lkey = cb.ctx->local_read_mr->lkey;

            strcpy(cb.ctx->local_read_buf, buf);
            ibv_post_send(cb.ctx->qp_read, &send_wr, &bad_wr);
            __atomic_fetch_add(&cb.num_big_read_flows, 1, __ATOMIC_RELAXED);
        }
        else if (strcmp(buf, "exit") == 0)
        {
            strcpy(cb.ctx->local_read_buf, buf);
            ibv_post_send(cb.ctx->qp_read, &send_wr, &bad_wr);
            __atomic_fetch_sub(&cb.num_big_read_flows, 1, __ATOMIC_RELAXED);
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
        // temp = 4999; // for testing
        if ((temp = __atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED)))
        {
            if ((num_big = __atomic_load_n(&cb.sb->num_active_big_flows, __ATOMIC_RELAXED)))
                //chunk_size = chunk_size_table[temp / num_big / (LINE_RATE_MB/6)];
                //chunk_size = 1000000;       // hard-coded to be 1MB
                chunk_size = DEFAULT_CHUNK_SIZE;
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
                    //while (get_cycles() - start_cycle < (cpu_mhz * chunk_size / temp) / SPLIT_QP_NUM_ONE_SIDED)
#ifndef USE_TIMEFRAME
                    while (get_cycles() - start_cycle < cpu_mhz * chunk_size / temp)      // number of cycles needed to send 1 split chunk at current virtual link rate
#else
                    while (get_cycles() - start_cycle < cpu_mhz * TIMEFRAME)      // number of cycles needed to send 1 split chunk at current virtual link rate
#endif
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
    pthread_t th1, th2, th3, th4;
    //pthread_t th1, th2, th3, th4, th5;
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
    cb.sb->num_active_small_flows = 0; /* cancel out pacer's monitor flow */
    for (i = 0; i < MAX_FLOWS; i++)
    {
        cb.sb->flows[i].pending = 0;
        cb.sb->flows[i].active = 0;
    }

    /* start thread handling incoming flows */
    printf("starting thread for flow handling...\n");
    if (pthread_create(&th1, NULL, (void *(*)(void *)) & flow_handler, NULL))
    {
        error("pthread_create: flow_handler");
    }

    /* start monitoring thread */
    printf("starting thread for latency monitoring...\n");
    if (pthread_create(&th2, NULL, (void *(*)(void *)) & monitor_latency, (void *)&param))
    {
        error("pthread_create: monitor_latency");
    }

    /* start token generating thread */
    printf("starting thread for token generating...\n");
    if (pthread_create(&th3, NULL, (void *(*)(void *)) & generate_tokens, NULL))
    {
        error("pthread_create: generate_tokens");
    }

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
