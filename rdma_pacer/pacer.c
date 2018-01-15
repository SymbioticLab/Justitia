#include "pacer.h"
#include "monitor.h"
#include "get_clock.h"
#include <immintrin.h> /* For _mm_pause */
#define DEBUG 0

struct control_block cb;
uint32_t chunk_size_table[] = {4096, 8192, 16384, 32768, 65536, 1048576, 1048576};

/* utility fuctions */
static void error(char *msg)
{
    perror(msg);
    exit(1);
}

static void usage()
{
    printf("Usage: program remote-addr isclient\n");
}

static inline void cpu_relax() __attribute__((always_inline));
static inline void cpu_relax()
{
#if (COMPILER == MVCC)
    _mm_pause();
#elif (COMPILER == GCC || COMPILER == LLVM)
    asm("pause");
#endif
}

static void termination_handler(int sig)
{
    remove("/dev/shm/rdma-fairness");
    _exit(0);
}

static void rm_shmem_on_exit()
{
    remove("/dev/shm/rdma-fairness");
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

    /* get a socket descriptor */
    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
        error("socket");

    /* bind to an address */
    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, SOCK_PATH);
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
            cb.next_slot++;
            while (__atomic_load_n(&cb.sb->flows[cb.next_slot].active, __ATOMIC_RELAXED))
            {
                cb.next_slot++;
            }
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

/* generate tokens at some rate
 */
static void generate_tokens()
{
    cycles_t start_cycle;
    cycles_t end_cycle;
    double cpu_mhz = get_cpu_mhz(1);
    struct timespec wait_time;
    wait_time.tv_sec = 0;
    wait_time.tv_nsec = 45000; /* nanosleep overhead > 55 */
    start_cycle = get_cycles();
    nanosleep(&wait_time, NULL);
    end_cycle = get_cycles();
    printf("nanosleep call takes %.2f us\n", (end_cycle - start_cycle) / cpu_mhz);
    start_cycle = get_cycles();
    end_cycle = get_cycles();
    printf("time elapsed between two get_cycles call is %.2f us\n", (end_cycle - start_cycle) / cpu_mhz);
    /* infinite loop: generate tokens at a rate calculated 
     * from virtual_link_cap and active chunk size 
     */
    uint32_t temp, chunk_size;
    start_cycle = 0;
    while (1)
    {
        if (__atomic_load_n(&cb.sb->num_active_big_flows, __ATOMIC_RELAXED))
        {
            temp = __atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED);
            chunk_size = chunk_size_table[temp / 1000];
            __atomic_store_n(&cb.sb->active_chunk_size, chunk_size, __ATOMIC_RELAXED);
            __atomic_fetch_add(&cb.tokens, 10, __ATOMIC_RELAXED);
            wait_time.tv_nsec = 10 * chunk_size / temp * 1000;
        }
        nanosleep(&wait_time, NULL);
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
    pthread_t th1, th2, th3;
    struct monitor_param param;
    char *endPtr;
    param.addr = argv[1];
    if (argc == 3)
    {
        param.isclient = strtol(argv[2], &endPtr, 10);
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
    cb.virtual_link_cap = LINE_RATE_MB;
    cb.next_slot = 0;
    cb.sb->active_chunk_size = DEFAULT_CHUNK_SIZE;
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

    /* main loop: fetch token */
    while (1)
    {
        for (i = 0; i < MAX_FLOWS; i++)
        {
            if (__atomic_load_n(&cb.sb->flows[i].pending, __ATOMIC_RELAXED))
            {
                fetch_token();
                __atomic_fetch_sub(&cb.sb->flows[i].pending, 1, __ATOMIC_RELAXED);
            }
        }
    }
}
