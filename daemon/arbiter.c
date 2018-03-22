#include "arbiter.h"
#include "monitor.h"
#include "get_clock.h"

struct cluster_info cluster;

/* utility fuctions */
static void error(char *msg)
{
    perror(msg);
    exit(1);
}

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

/* end */


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
    char **ip = (char **)malloc(num_hosts * sizeof(char *));
    char *gid_idx_char;
    int *gid_idx = (int *)malloc(num_hosts * sizeof(int));
    for (i = 0; i < num_hosts; ++i) {
        gid_idx[i] = -1;
    }
    i = 0;
    while (fgets(line, sizeof(line), fp)) {
        ip[i] = strtok(line, " ");
        gid_idx_char = strtok(line, " ");
        if (gid_idx_char != NULL) {
            gid_idx[i] = strtol(strtok(line, " "), NULL, 10);
        }
        ++i;
    }
    fclose(fp);

    /* initialize control structure */ 
    cluster.num_hosts = num_hosts;
    cluster.hosts = (struct host_info *)calloc(num_hosts, sizeof(struct host_info));
    for (i = 0; i < num_hosts; ++i) {
        /* init ctx, mr, and connect to each host via RDMA RC */
        cluster.hosts[i].ctx = init_ctx_and_build_conn(ip[i], 1, gid_idx[i], &cluster.hosts[i]);
        if (cluster.hosts[i].ctx == NULL) {
            fprintf(stderr, "init_ctx_and_build_conn failed, exit\n");
            exit(EXIT_FAILURE);
        }
    }




    /* start thread handling incoming flows */
    //printf("starting thread for flow handling...\n");
    //if (pthread_create(&th1, NULL, (void *(*)(void *)) & flow_handler, NULL))
    //{
    //    error("pthread_create: flow_handler");
    //}

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

    return 0;
}
