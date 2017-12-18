#include "pacer.h"
#include "monitor.h"
#define DEBUG 0

void error (char * msg) {
    perror(msg);
    exit(1);
}

static void usage() {
   printf("Usage: program remote-addr isclient\n");
}


static void flow_handler() {
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
    cb.next_slot = 0;
    while (1) {
        len = sizeof(struct sockaddr_un);
        if((s2 = accept(s, (struct sockaddr *)&remote, &len)) == -1)
            error("accept");

        /* check join or quit */
        len = recv(s2, (void *)buf, (size_t)MSG_LEN, 0);
        printf("receive message of length %d.\n", len);
        buf[len] = '\0';
        printf("message is %s.\n", buf);
        if (strcmp(buf, "join") == 0) {
            /* send back slot number */
            printf("sending back slot number %d...\n", cb.next_slot);
            len = snprintf(buf, MSG_LEN, "%d", cb.next_slot);
            cb.flows[cb.next_slot].active = 1;
            cb.flows[cb.next_slot].chunk_size = cb.active_chunk_size;
            send(s2, &buf, len, 0);

            /* find next empty slot */
            cb.next_slot++;
            while (__atomic_load_n(&cb.flows[cb.next_slot].active, __ATOMIC_RELAXED)) {
                cb.next_slot++;
            }
        }
        /* A new flow comes in.
         * We need first enforce every flow to have equal allocation. */
        __atomic_store_n(&cb.test, 1, __ATOMIC_RELAXED);
    }
}

// static inline double subtract_time(struct timespec *time1, struct timespec *time2) {
//     return difftime(time1->tv_sec, time2->tv_sec) * 1000000 + (double) (time1->tv_nsec - time2->tv_nsec) / 1000;
// }
static void termination_handler(int sig) {
    remove("/dev/shm/rdma-fairness");
    _exit(0);
}

int main (int argc, char** argv) {
    /* set up signal handler */
    struct sigaction new_action, old_action;
    new_action.sa_handler = termination_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;

    sigaction (SIGINT, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction (SIGINT, &new_action, NULL);

    sigaction (SIGHUP, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction (SIGHUP, &new_action, NULL);

    sigaction (SIGTERM, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN)
        sigaction (SIGTERM, &new_action, NULL);
    /* end */

    int fd_shm, i;
    pthread_t th1, th2;
    struct monitor_param param;
    char *endPtr;
    param.addr = argv[1];
    if (argc == 3) {
        param.isclient = strtol(argv[2], &endPtr, 10);
    } else {
        usage();
        exit(1);
    }

    /* allocate shared memory */
    if ((fd_shm = shm_open(SHARED_MEM_NAME, O_RDWR | O_CREAT, 0666)) < 0)
        error("shm_open");

    if (ftruncate(fd_shm, MAX_FLOWS * sizeof(struct flow_info)) < 0)
        error("ftruncate");

    if ((cb.flows = mmap(NULL, MAX_FLOWS * sizeof(struct flow_info), 
        PROT_WRITE | PROT_READ, MAP_SHARED, fd_shm, 0)) == MAP_FAILED)
        error("mmap");

    /* initialize control block */
    cb.active_chunk_size = DEFAULT_CHUNK_SIZE;
    for (i = 0; i < MAX_FLOWS; i++) {
        cb.flows[i].target = LINE_RATE_MB;
        cb.flows[i].active = 0;
        cb.flows[i].bytes = 0;
        cb.flows[i].measured = 0.0;
        cb.flows[i].chunk_size = DEFAULT_CHUNK_SIZE;
    }
    
    /* start off thread handling incoming flows */
    printf("starting thread for flow handling...\n");
    if(pthread_create(&th1, NULL, (void * (*)(void *))&flow_handler, NULL)){
        error("pthread_create: flow_handler");
    }
    
    // printf("starting thread for latency monitoring...\n");
    // if(pthread_create(&th2, NULL, (void * (*)(void *))&monitor_latency, (void*)&param)){
    //     error("pthread_create: monitor_latency");
    // }
    
    /* main loop: rate calculation */
    uint32_t tput = 0;
    uint32_t target = 0.0;
    struct timespec sleep_time, remain_time;
    sleep_time.tv_nsec = 100000000;
    sleep_time.tv_sec = 0;
    while (1) {
        nanosleep(&sleep_time, &remain_time);
        /* first sweep to gather total bandwidth usage */
        cb.num_active_flows = 0;
        cb.unused = 0;
        cb.num_saturated = 0;
        cb.true = 1;
        for (i = 0; i < MAX_FLOWS; i++) {
            if (__atomic_load_n(&cb.flows[i].active, __ATOMIC_RELAXED)) {
                // printf("Flow at slot %d: throughput %d\n", i,
                //     (tput = __atomic_load_n(&cb.flows[i].measured, __ATOMIC_RELAXED)));
                tput = __atomic_load_n(&cb.flows[i].measured, __ATOMIC_RELAXED);
                printf("@@@tput of slot %d = %d\n", i, tput);
                printf("@@@target of slot %d = %d\n", i, cb.flows[i].target);
                cb.num_active_flows++;
                if (tput < cb.flows[i].target) {
                    cb.unused += cb.flows[i].target - cb.flows[i].measured;
                } else {
                    cb.num_saturated++;
                }
            } 
        }
        if (__atomic_compare_exchange_n(&cb.test, &cb.true, 0, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            target = (uint32_t)LINE_RATE_MB / cb.num_active_flows;
            printf("@@@equal target tput = %d\n", target);
            for (i = 0; i < MAX_FLOWS; i++) {
                if (__atomic_load_n(&cb.flows[i].active, __ATOMIC_RELAXED)) {
                    __atomic_store_n(&cb.flows[i].target, target, __ATOMIC_RELAXED);
                }
            }
        } else {
            if (cb.num_saturated && cb.unused) {
                cb.redistributed = cb.unused / cb.num_saturated;
                for (i = 0; i < MAX_FLOWS; i++) {
                    if (__atomic_load_n(&cb.flows[i].active, __ATOMIC_RELAXED)) {
                        tput = __atomic_load_n(&cb.flows[i].measured, __ATOMIC_RELAXED);
                        printf("@@@tput of slot %d = %d\n", i, tput);
                        printf("@@@target of slot %d = %d\n", i, cb.flows[i].target);
                        if (tput >= cb.flows[i].target) {
                            __atomic_add_fetch(&cb.flows[i].target, cb.redistributed, __ATOMIC_RELAXED);
                        } else {
                            __atomic_store_n(&cb.flows[i].target, tput, __ATOMIC_RELAXED);
                        }
                    }
                }
            }
        }
    }
}
