#ifndef VERBS_PACER_H
#define VERBS_PACER_H

#include "pacer.h"
#include <signal.h>

unsigned int slot;
static int registered = 0;

char *get_sock_path() {
    FILE *fp;
    fp = fopen(HOSTNAME_PATH, "r");
    if (fp == NULL) {
        printf("Error opening %s, use default SOCK_PATH", HOSTNAME_PATH);
        fclose(fp);
        return SOCK_PATH;
    }

    char hostname[60];
    if(fgets(hostname, 60, fp) != NULL) {
        //char *sock_path = (char *)malloc(108 * sizeof(char));
        char *sock_path = (char *)calloc(108, sizeof(char));
        //printf("DE hostname:%s\n", hostname);
        int len = strlen(hostname);
        if (len > 0 && hostname[len-1] == '\n') hostname[len-1] = '\0';
        strcat(hostname, "_rdma_socket");
        strcpy(sock_path, getenv("HOME"));
        len = strlen(sock_path);
        sock_path[len] = '/';
        //printf("DE: len(sock_path) = %d\n", len);
        //printf("DE: sock_path:%s\n", sock_path);
        strcat(sock_path, hostname);
        fclose(fp);
        return sock_path;
    }

    fclose(fp);
    return SOCK_PATH;
}

static void contact_pacer(int join) {
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
    printf("SUN_PATH = %s\n", remote.sun_path);
    printf("SOCK_PATH = %s\n", SOCK_PATH);
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if (connect(s, (struct sockaddr *)&remote, len) == -1) {
        perror("connect");
        exit(1);
    }

    if (!join) {
        strcpy(str, "exit");
        if (send(s, str, strlen(str), 0) == -1) {
            perror("send: exit");
            exit(1);
        }
    } else {
        /* send join message */
        printf("Sending join message...\n");
        strcpy(str, "join");
        if (send(s, str, strlen(str), 0) == -1) {
            perror("send: join");
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
    }
    close(s);
}

static void set_inactive_on_exit() {
    if (flow) {
        if (isSmall) {
            __atomic_fetch_sub(&sb->num_active_small_flows, num_active_small_flows, __ATOMIC_RELAXED);
            printf("DEBUG decrement SMALL counter by %d\n", num_active_small_flows);
        } else if (__atomic_load_n(&flow->read, __ATOMIC_RELAXED)) {
            __atomic_store_n(&flow->read, 0, __ATOMIC_RELAXED);
            contact_pacer(0);
        } else {
            __atomic_fetch_sub(&sb->num_active_big_flows, num_active_big_flows, __ATOMIC_RELAXED);
            printf("DEBUG decrement BIG counter by %d\n", num_active_big_flows);
        }
        __atomic_store_n(&flow->pending, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&flow->active, 0, __ATOMIC_RELAXED);
        printf("libmlx4 exit\n");
    }
}

static void termination_handler(int sig)
{
    set_inactive_on_exit();
    _exit(1);
}
#endif
