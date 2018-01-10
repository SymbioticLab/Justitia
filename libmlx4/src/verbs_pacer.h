#ifndef VERBS_PACER_H
#define VERBS_PACER_H

#include "pacer.h"

unsigned int slot;

static void contact_pacer() {
    /* prepare unix domain socket */
    unsigned int s, len;
    struct sockaddr_un remote;
    char str[MSG_LEN];

    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    printf("Contacting pacer...\n");

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, SOCK_PATH);
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if (connect(s, (struct sockaddr *)&remote, len) == -1) {
        perror("connect");
        exit(1);
    }

    /* send join message */
    printf("Sending join message...\n");
    strcpy(str, "join");
    if (send(s, str, strlen(str), 0) == -1) {
        perror("send");
        exit(1);
    }

    /* receive the slot number */
    if ((len = recv(s, str, MSG_LEN, 0)) > 0) {
        str[len] = '\0';
    } else {
        if (len < 0) perror("recv");
        else printf("Server closed connection\n");
        exit(1);
    }
    slot = strtol(str, NULL, 10);
    printf("Received slot number.\n");
    close(s);
}

static void set_inactive_on_exit() {
    if (flow) {
        if (isSmall) {
            __atomic_fetch_sub(&sb->num_active_small_flows, 1, __ATOMIC_RELAXED);
        } else {
            __atomic_fetch_sub(&sb->num_active_big_flows, 1, __ATOMIC_RELAXED);
        }
        __atomic_store_n(&flow->pending, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&flow->active, 0, __ATOMIC_RELAXED);
        printf("libmlx4 exit\n");
    }
}
#endif
