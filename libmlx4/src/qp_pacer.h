#ifndef QP_PACER_H
#define QP_PACER_H

#include "pacer.h"
#include <immintrin.h> /* For _mm_pause */  // remember to take off this header file and __mm_pause() when running on ConFlux

static inline void cpu_relax() __attribute__((always_inline));
static inline void cpu_relax() {
#if (COMPILER == MVCC)
    _mm_pause();
#elif (COMPILER == GCC || COMPILER == LLVM)
    asm("pause");
#endif
}

//static void contact_pacer_read_join(struct flow_info *flow) {
static void contact_pacer_read_join() {
    /* prepare unix domain socket */
    char *sock_path = get_sock_path();
    unsigned int s, len;
    struct sockaddr_un remote;
    char str[MSG_LEN];

    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    printf("Contacting pacer...\n");

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, sock_path);
    free(sock_path);
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if (connect(s, (struct sockaddr *)&remote, len) == -1) {
        perror("connect");
        exit(1);
    }
   
    printf("Sending read message...\n");
    strcpy(str, "READjoin");
    if (send(s, str, strlen(str), 0) == -1) {
        perror("send: READjoin");
        exit(1);
    }

    len = snprintf(str, MSG_LEN, "%d", flow->slot);
    if (send(s, str, MSG_LEN, 0) == -1) {
        perror("send: flow slot");
        exit(1);
    }

    close(s);
}

/* used in post_send to inform central arbiter that a big write/send flow joins*/
//static void contact_pacer_write_join(struct flow_info *flow) {
static void contact_pacer_write_join() {
    /* prepare unix domain socket */
    char *sock_path = get_sock_path();
    unsigned int s, len;
    struct sockaddr_un remote;
    char str[MSG_LEN];

    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    printf("Contacting pacer for BIG flow JOIN...\n");

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, sock_path);
    free(sock_path);
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if (connect(s, (struct sockaddr *)&remote, len) == -1) {
        perror("connect");
        exit(1);
    }
   
    strcpy(str, "WRITEjoin");
    if (send(s, str, strlen(str), 0) == -1) {
        perror("send: WRITEjoin");
        exit(1);
    }

    len = snprintf(str, MSG_LEN, "%d", flow->slot);
    if (send(s, str, len, 0) == -1) {
        perror("send: flow slot");
        exit(1);
    }

    close(s);
}
#endif
