#include "pacer.h"


char *get_sock_path() {
    FILE *fp;
    fp = fopen(HOSTNAME_PATH, "r");
    if (fp == NULL) {
        printf("Error opening %s, use default SOCK_PATH", HOSTNAME_PATH);
        fclose(fp);
        return SOCK_PATH;
    }

    char hostname[100];
    if(fgets(hostname, 100, fp) != NULL) {
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

// join=0 -> exit; join=1 -> first join and ask pacer for slot; join=2 -> tell pacer about the type of the app (0:bw, 1:lat, 2:tput)
void contact_pacer(int join, uint64_t vaddr) {
    /* prepare unix domain socket */
    char *sock_path = get_sock_path();
    unsigned int s, len;
    struct sockaddr_un remote;
    char str[MSG_LEN];
    int vaddr_idx;

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

    if (join == 0) {
        memset(str, 0, MSG_LEN);
        if (isSmall == 0) {
            strcpy(str, "exit_app_bw");
        } else if (isSmall == 1) {
            strcpy(str, "exit_app_lat");
        } else if (isSmall == 2) {
            strcpy(str, "exit_app_tput");
        }
        if (send(s, str, strlen(str), 0) == -1) {
            perror("send: exit");
            exit(1);
        }
        close(s);
    } else if (join == 1) {
        /* send join message */
        printf("Sending join message...\n");
        //strcpy(str, "join:");
        sprintf(str, "join:%016Lx", vaddr);
        if (send(s, str, strlen(str), 0) == -1) {
            perror("send: join");
            exit(1);
        }

        /* recv sender/receiver prompt (instead of string "pid") */
        if ((len = recv(s, str, MSG_LEN, 0)) > 0) {
            str[len] = '\0';
            if (strncmp(str, "sender:xxx", 6) == 0) {
                sscanf(str, "sender:%x", &vaddr_idx);
                printf("I'm a sender; vaddr_idx = %d\n", vaddr_idx);

            } else if (strcmp(str, "recver") == 0) {
                printf("I'm a receiver.\n");
            } else {
                printf("unrecognized string. must be \"sender\" or \"recver\"\n");
                exit(1);
            }
        } else {
            if (len < 0) perror("recv");
            else printf("Server closed connection\n");
            exit(1);
        }
        memset(str, 0, MSG_LEN);

        /* send process ID */
        pid_t my_pid = getpid();
        printf("My PID is %d\n", my_pid);
        len = snprintf(str, MSG_LEN, "%d", my_pid);
        //printf("length of pid message is %d\n", len);
        if (send(s, str, len, 0) == -1) {
            perror("error in sending pid: ");
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
        printf("Received slot number: %d\n", slot);

#ifdef CPU_FRIENDLY
        flow_socket = s;
        // don't close (s) in case of join
        // this connection is the one we use to recv tokens in token_enforcement impl
#endif
    } else if (join == 2) {
        /* tell daemon about my app type */
        memset(str, 0, MSG_LEN);
        if (isSmall == 0) {
            strcpy(str, "app_bw");
        } else if (isSmall == 1) {
            strcpy(str, "app_lat");
        } else if (isSmall == 2){
            strcpy(str, "app_tput");
        } else {
            printf("unrecognized app type. Exit\n");
            exit(1);
        }
        if (send(s, str, strlen(str), 0) == -1) {
            perror("send: app type");
            exit(1);
        }
        close(s);
    }
}

void set_inactive_on_exit() {
    if (flow) {
        if (isSmall) {
            __atomic_fetch_sub(&sb->num_active_small_flows, num_active_small_flows, __ATOMIC_RELAXED);
            contact_pacer(0);
            printf("DEBUG decrement SMALL counter by %d\n", num_active_small_flows);
        } else if (__atomic_load_n(&flow->read, __ATOMIC_RELAXED)) {
            //TODO: fix READ exit later
            __atomic_store_n(&flow->read, 0, __ATOMIC_RELAXED);
            contact_pacer(0);
        } else {
            __atomic_fetch_sub(&sb->num_active_big_flows, num_active_big_flows, __ATOMIC_RELAXED);
            printf("DEBUG decrement BIG counter by %d\n", num_active_big_flows);
            contact_pacer(0);
        }
        __atomic_store_n(&flow->pending, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&flow->active, 0, __ATOMIC_RELAXED);
        printf("libmlx4 exit\n");
    }
}

void termination_handler(int sig) {
    set_inactive_on_exit();
    _exit(1);       // _exit?
}
