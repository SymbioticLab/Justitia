#ifndef VERBS_PACER_H
#define VERBS_PACER_H

#include "pacer.h"

static void contact_pacer() {
    /* prepare unix domain socket */
    unsigned int s, len;
    struct sockaddr_un remote;
    char str[MSG_LEN];

    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    printf("Trying to connect...\n");

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, SOCK_PATH);
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if (connect(s, (struct sockaddr *)&remote, len) == -1) {
        perror("connect");
        exit(1);
    }

    printf("Connected.\n");

    /* send join message */
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

    close(s);
}

static void set_inactive_on_exit() {
    if (flow) {
        __atomic_store_n(&flow->active, 0, __ATOMIC_RELAXED);
        printf("libmlx4 exit\n");
    }
}
#endif
