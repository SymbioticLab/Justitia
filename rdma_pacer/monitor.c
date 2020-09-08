#include "monitor.h"
#include "pingpong.h"
#include "get_clock.h"
#include "pacer.h"
#include "countmin.h"
#include <inttypes.h>
#include <math.h>
#include <assert.h>

#define TAIL 2

#define EVENT_POLL 1    // use event-triggered polling (or busy polling) for reference flow
#define CS_OFFSET 4     // context switch offset
#define EWMA 0.5

#define WIDTH 32768
#define DEPTH 16
#define U 24
#define GRAN 4
#define WINDOW_SIZE 10000
//#define USE_CMH
#define CMH_PERCENTILE  0.99    // pencentile ask from CMH

CMH_type *cmh = NULL;

static inline void cpu_relax() __attribute__((always_inline));
static inline void cpu_relax()
{
    asm("nop");
}

// called by sender to monitor ref flow latency and so on
void monitor_latency(void *arg)
{
    printf(">>>starting monitor_latency...\n");
    struct monitor_param *params = (struct monitor_param *)arg;
    assert(params->is_client);

    double latency_target = TAIL;
    if (EVENT_POLL) {
        latency_target += CS_OFFSET;
    }
    double measured_tail = 0;
    double prev_measured_tail = 0;

    int lat; // in nanoseconds
    cycles_t start_cycle, end_cycle;
    //cycles_t prev_start_cycle = 0;
    // cycles_t cmh_start, cmh_end;
    int no_cpu_freq_warn = 1;
    double cpu_mhz = get_cpu_mhz(no_cpu_freq_warn);

    uint64_t seq = 0;
    struct pingpong_context *ctx = NULL;        // managed by each client
    struct ibv_send_wr wr, send_wr, *bad_wr = NULL;
    struct ibv_recv_wr recv_wr, *bad_recv_wr = NULL;
    struct ibv_sge sge, send_sge, recv_sge;
    struct ibv_wc wc, recv_wc, send_wc;
    int num_comp;
    int num_remote_big_reads = 0;
    uint32_t received_read_rate;
    uint32_t temp, new_remote_read_rate;
    int found_split_level = 0;

    //ctx = init_monitor_chan(servername, isclient, gid_idx);
    ctx = init_monitor_chan(params);
    if (!ctx) {
        fprintf(stderr, "failed to allocate pingpong context. exiting monitor_latency\n");
        exit(1);
    }

    cb.ctx = ctx;
    cpu_mhz = get_cpu_mhz(no_cpu_freq_warn);

    /* REF FLOW WRITE WR */
    memset(&wr, 0, sizeof wr);
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = (IBV_SEND_SIGNALED | IBV_SEND_INLINE);
    wr.wr_id = seq;
    wr.wr.rdma.rkey = ctx->rem_dest->rkey;
    wr.wr.rdma.remote_addr = ctx->rem_dest->vaddr;

    sge.addr = (uintptr_t)ctx->send_buf;
    sge.length = BUF_SIZE;
    sge.lkey = ctx->send_mr->lkey;

    /* UPDATE SEND WR */
    memset(&send_wr, 0, sizeof send_wr);
    send_wr.opcode = IBV_WR_SEND;
    send_wr.sg_list = &send_sge;
    send_wr.num_sge = 1;
    send_wr.send_flags = IBV_SEND_INLINE | IBV_SEND_SIGNALED;

    memset(&send_sge, 0, sizeof send_sge);
    memset((char *)ctx->send_buf + BUF_SIZE, 0, BUF_SIZE);
    send_sge.addr = (uintptr_t)ctx->send_buf;
    ////send_sge.addr = (uintptr_t)((char *)ctx->send_buf + BUF_SIZE);
    send_sge.length = BUF_SIZE;
    send_sge.lkey = ctx->send_mr->lkey;

    /* UPDATE RECV WR */
    memset(&recv_wr, 0, sizeof recv_wr);
    recv_wr.num_sge = 1;
    recv_wr.sg_list = &recv_sge;

    memset(&recv_sge, 0, sizeof recv_sge);
    memset(ctx->recv_buf, 0, BUF_SIZE);
    recv_sge.addr = (uintptr_t)ctx->recv_buf;
    recv_sge.length = BUF_SIZE;
    recv_sge.lkey = ctx->recv_mr->lkey;
    ibv_post_recv(ctx->qp, &recv_wr, &bad_recv_wr);


#ifdef USE_CMH
    cmh = CMH_Init(WIDTH, DEPTH, U, GRAN, WINDOW_SIZE);
    if (!cmh)
    {
        fprintf(stderr, "CMH_Init failed\n");
        exit(1);
    }
#endif

    /* monitor loop */
    uint32_t min_virtual_link_cap = 0;
    uint16_t num_active_big_flows = 0;
    uint16_t num_active_bw_flows = 0;
    uint16_t num_active_small_flows = 0;
    while (1) {
        usleep(200);
        start_cycle = get_cycles();
        if (ibv_post_send(ctx->qp, &wr, &bad_wr)) {
            perror("ibv_post_send");
            break;
        }

		void *ev_ctx;
        if (EVENT_POLL) {
            if (ibv_get_cq_event(ctx->send_channel, &ctx->send_cq, &ev_ctx)) {
                fprintf(stderr, "Failed to get CQ event.\n");
                break;
            }

            ibv_ack_cq_events(ctx->send_cq, 1);

            if (ibv_req_notify_cq(ctx->send_cq, 0)) {
                fprintf(stderr, "Couldn't request CQ notification\n");
                break;
            }
        }

        do {
            num_comp = ibv_poll_cq(ctx->send_cq, 1, &wc);
        } while (num_comp == 0);

        if (num_comp < 0 || wc.status != IBV_WC_SUCCESS) {
            perror("ibv_poll_cq");
            break;
        }

        end_cycle = get_cycles();

#ifdef USE_CMH
        lat = round((end_cycle - start_cycle) / cpu_mhz * 1000);
        if (CMH_Update(cmh, lat)) {
            fprintf(stderr, "CMH_Update failed\n");
            break;
        }

        //cmh_start = get_cycles();
        measured_tail = round(CMH_Quantile(cmh, CMH_PERCENTILE)/100.0)/10;

        ////tail_99 = (double)lat / 1000;

        //printf("measured_tail = %.1f \n", measured_tail);
        //cmh_end = get_cycles();
        //printf("CMH_Quantile 99th takes %.2f us\n", (cmh_end - cmh_start)/cpu_mhz);
        // if (prev_start_cycle)
        //     printf("time between two sends %.2f us", (start_cycle - prev_start_cycle)/cpu_mhz);
        // prev_start_cycle = start_cycle;
        // printf("median %.1f us 99th %.1f us\n", median, tail_99);
#else
        lat = (end_cycle - start_cycle) / cpu_mhz * 1000;
        measured_tail = (double)lat / 1000;
        measured_tail = EWMA * measured_tail + (1 - EWMA) * prev_measured_tail;
        prev_measured_tail = measured_tail;
        //printf("measured_tail = %.1f \n", measured_tail);
#endif
        seq++;
        wr.wr_id = seq;

        //TODO: fix READ impl later
        /* check if any remote read is registered or if read rate is received */
        /*
        num_comp = ibv_poll_cq(ctx->cq_recv, 1, &recv_wc);
        if (num_comp == 1) {
            if (recv_wc.status != IBV_WC_SUCCESS) {
                fprintf(stderr, "error bad recv_wc status: %u.%s\n", recv_wc.status, ibv_wc_status_str(recv_wc.status));
                break;
            }
            if (strcmp(ctx->remote_read_buf, "read") == 0) {
                printf("receive new big read flow registration\n");
                num_remote_big_reads++;
            } else if (strcmp(ctx->remote_read_buf, "exit") == 0) {
                printf("receive big read flow deregistration\n");
                num_remote_big_reads--;
            } else {
                received_read_rate = (uint32_t)strtol((const char *)ctx->remote_read_buf, NULL, 10);
                printf("receive new big read rate %" PRIu32 "\n", received_read_rate);
                __atomic_store_n(&cb.local_read_rate, received_read_rate, __ATOMIC_RELAXED);
            }
            if (ibv_post_recv(ctx->qp_read, &recv_wr, &bad_recv_wr))
                perror("ibv_post_recv: recv_wr");
        } else if (num_comp < 0) {
            perror("ibv_poll_cq: recv_wc");
            break;
        }
        */



        num_active_big_flows = __atomic_load_n(&cb.sb->num_active_big_flows, __ATOMIC_RELAXED);
        num_active_small_flows = __atomic_load_n(&cb.sb->num_active_small_flows, __ATOMIC_RELAXED);
        num_active_bw_flows = __atomic_load_n(&cb.sb->num_active_bw_flows, __ATOMIC_RELAXED);
        //printf("num_active_big_flows = %d\n", num_active_big_flows);
        //printf("num_active_small_flows = %d\n", num_active_small_flows);
        // TODO: remove this hardcode for bw write vs lat read
        //// READ HACK
        /*
        __atomic_store_n(&cb.sb->virtual_link_cap, 3000, __ATOMIC_RELAXED);
        __atomic_store_n(&cb.sb->split_level, 2, __ATOMIC_RELAXED);
        continue;
        */
        ////
        if (num_active_big_flows + num_remote_big_reads)        // TODO: simplfiy the logic here later (can just check num_active_bw_flows + num_remote_big_reads)
        {
            ////if (num_active_small_flows && (num_active_bw_flows || num_remote_big_reads))    // READ HACK
            if (num_active_small_flows && num_active_bw_flows)
            {
#ifndef TREAT_L_AS_ONE
                min_virtual_link_cap = round((double)(num_active_big_flows + num_remote_big_reads) 
                    / (num_active_big_flows + num_active_small_flows + num_remote_big_reads) * LINE_RATE_MB);
#else
                min_virtual_link_cap = round((double)(num_active_big_flows + num_remote_big_reads) 
                    / (num_active_big_flows + 1 + num_remote_big_reads) * LINE_RATE_MB);
#endif
                temp = __atomic_load_n(&cb.sb->virtual_link_cap, __ATOMIC_RELAXED);

                if (measured_tail > latency_target)
                {
                    /* Multiplicative Decrease */
                    temp >>= 1;
                    if (ELEPHANT_HAS_LOWER_BOUND && temp < min_virtual_link_cap) {
                        temp = min_virtual_link_cap;
                    }
                }
                else    // target met
                {
                    /* Additive Increase */
                    if (__atomic_load_n(&cb.sb->virtual_link_cap, __ATOMIC_RELAXED) < LINE_RATE_MB) {
                        temp++;
                    }
                }
                if (num_remote_big_reads) {
                    //TODO: fix READ impl later
                    /*
                    new_remote_read_rate = round((double)num_remote_big_reads
                        / (num_remote_big_reads + num_active_big_flows) * temp);
                    //// READ HACK
                    //new_remote_read_rate = 3000;    // TODO: fix HARDCODE later
                    ////
                    if (new_remote_read_rate != cb.remote_read_rate) {
                        cb.remote_read_rate = new_remote_read_rate;
                        memset((char *)ctx->local_read_buf + BUF_READ_SIZE, 0, BUF_READ_SIZE);
                        sprintf((char*)ctx->local_read_buf + BUF_READ_SIZE, "%" PRIu32, cb.remote_read_rate);
                        printf("new remote read rate %s\n", (char*)ctx->local_read_buf + BUF_READ_SIZE);
                        if (ibv_post_send(ctx->qp_read, &send_wr, &bad_wr))
                        {
                            perror("ibv_post_send: remote read rate");
                        }
                        do {
                            num_comp = ibv_poll_cq(ctx->cq_send, 1, &send_wc);      //TODO: event-triggered polling
                        } while(num_comp == 0);
                        if (num_comp < 0) {
                            perror("ibv_poll_cq: send_wr");
                            break;
                        }
                        if (wc.status != IBV_WC_SUCCESS) {
                            fprintf(stderr, "bad wc status: %s\n", ibv_wc_status_str(wc.status));
                        }
                    }
                    temp -= new_remote_read_rate;
                    */
                }
                __atomic_store_n(&cb.sb->virtual_link_cap, temp, __ATOMIC_RELAXED);
            }
            else {  // if no small flows
                if (__atomic_load_n(&cb.sb->virtual_link_cap, __ATOMIC_RELAXED) != LINE_RATE_MB) {   
                    temp = LINE_RATE_MB;
                }

                //TODO: figure out what's going on with the big read logic here. Why handle big reads only if there is no small flows?
                if (num_remote_big_reads) {
                    //TODO: fix READ impl later
                    /*
                    new_remote_read_rate = round((double)num_remote_big_reads
                        / (num_remote_big_reads + num_active_big_flows) * temp);
                    if (new_remote_read_rate != cb.remote_read_rate) {
                        cb.remote_read_rate = new_remote_read_rate;
                        memset((char *)ctx->local_read_buf + BUF_READ_SIZE, 0, BUF_READ_SIZE);
                        sprintf((char*)ctx->local_read_buf + BUF_READ_SIZE, "%" PRIu32, cb.remote_read_rate);
                        printf("new remote read rate %s\n", (char*)ctx->local_read_buf + BUF_READ_SIZE);
                        if (ibv_post_send(ctx->qp_read, &send_wr, &bad_wr))
                        {
                            perror("ibv_post_send: remote read rate");
                        }
                        do {
                            num_comp = ibv_poll_cq(ctx->cq_send, 1, &send_wc);      //TODO: event-triggered polling
                        } while(num_comp == 0);
                        if (num_comp < 0) {
                            perror("ibv_poll_cq: send_wr");
                            break;
                        }
                        if (wc.status != IBV_WC_SUCCESS) {
                            fprintf(stderr, "bad wc status: %s\n", ibv_wc_status_str(wc.status));
                        }
                    }
                    temp -= new_remote_read_rate;
                    */
                }
                __atomic_store_n(&cb.sb->virtual_link_cap, temp, __ATOMIC_RELAXED);

            }
            //printf(">>>> virtual link cap: %" PRIu32 "\n", __atomic_load_n(&cb.sb->virtual_link_cap, __ATOMIC_RELAXED));
        }

    }
    printf("Out of while loop. exiting...\n");

#ifdef USE_CMH
    CMH_Destroy(cmh);
#endif

    exit(1);
}


// handle receiver-side updates and coordinate with all senders
void server_loop(void *arg)
{
    printf(">>>starting server loop...\n");
    struct monitor_param *params = (struct monitor_param *)arg;
    assert(!params->is_client);

    struct pingpong_context *ctx = NULL;
    struct ibv_send_wr send_wr[MAX_CLIENTS];
    struct ibv_recv_wr recv_wr[MAX_CLIENTS], *bad_recv_wr[MAX_CLIENTS];
    struct ibv_sge send_sge[MAX_CLIENTS], recv_sge[MAX_CLIENTS];
    struct ibv_wc recv_wc[MAX_CLIENTS];
    int num_comp;
    uint32_t current_receiver_fan_in = 0;

    int i = 0;
    for (i = 0; i < params->num_clients; i++) {
        ctx = init_monitor_chan(params);        // server will get stuck in socket listen()
        if (!ctx) {
            fprintf(stderr, "failed to allocate pingpong context. exiting monitor_latency\n");
            exit(1);
        }
        cb.ctx_per_client[i] = ctx;

        /* UPDATE SEND WR */
        memset(&send_wr[i], 0, sizeof send_wr[i]);
        send_wr[i].opcode = IBV_WR_SEND;
        send_wr[i].sg_list = &send_sge[i];
        send_wr[i].num_sge = 1;
        send_wr[i].send_flags = IBV_SEND_INLINE | IBV_SEND_SIGNALED;

        memset(&send_sge[i], 0, sizeof send_sge[i]);
        memset((char *)ctx->send_buf + BUF_SIZE, 0, BUF_SIZE);
        send_sge[i].addr = (uintptr_t)ctx->send_buf;
        ////send_sge.addr = (uintptr_t)((char *)ctx->send_buf + BUF_SIZE);
        send_sge[i].length = BUF_SIZE;
        send_sge[i].lkey = ctx->send_mr->lkey;

        /* UPDATE RECV WR */
        memset(&recv_wr[i], 0, sizeof recv_wr[i]);
        recv_wr[i].num_sge = 1;
        recv_wr[i].sg_list = &recv_sge[i];

        memset(&recv_sge[i], 0, sizeof recv_sge[i]);
        memset(ctx->recv_buf, 0, BUF_SIZE);
        recv_sge[i].addr = (uintptr_t)ctx->recv_buf;
        recv_sge[i].length = BUF_SIZE;
        recv_sge[i].lkey = ctx->recv_mr->lkey;
        ibv_post_recv(ctx->qp, &recv_wr[i], &bad_recv_wr[i]);
    }



#ifdef USE_CMH
    cmh = CMH_Init(WIDTH, DEPTH, U, GRAN, WINDOW_SIZE);
    if (!cmh)
    {
        fprintf(stderr, "CMH_Init failed\n");
        exit(1);
    }
#endif

    while (1) {
        //TODO: poll via channel
        /* check for receiver-side updates */
        int i;
        for (i = 0; i < params->num_clients; i++) {
            ctx = cb.ctx_per_client[i];

            num_comp = ibv_poll_cq(ctx->recv_cq, 1, &recv_wc[i]);
            if (num_comp > 0) {
                while (num_comp > 0) {
                    if (recv_wc[i].status != IBV_WC_SUCCESS) {
                        fprintf(stderr, "error bad recv_wc status: %u.%s\n", recv_wc[i].status, ibv_wc_status_str(recv_wc[i].status));
                        break;
                    }

                    //remote_receiver_fan_in = (uint32_t)strtol((const char *)ctx->update_recv_buf, NULL, 10);
                    if (strcmp(ctx->recv_buf, "send_inc") == 0) {
                        current_receiver_fan_in++;
                    } else if (strcmp(ctx->recv_buf, "send_dec") == 0) {
                        current_receiver_fan_in--;
                    } else {
                        printf("Unrecognized receiver-update msg. exit\n");
                        exit(1);
                    }

                    num_comp--;
                }
                printf("current receiver fan in: %" PRIu32 "\n", current_receiver_fan_in);

                if (ibv_post_recv(ctx->qp, &recv_wr[i], &bad_recv_wr[i])) {
                    perror("ibv_post_recv: recv_wr");
                }
            } else if (num_comp < 0) {
                perror("ibv_poll_cq: update_recv_wc");
                exit(1);
            }
        }
    }
}
