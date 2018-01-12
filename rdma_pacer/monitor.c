#include "monitor.h"
#include "pingpong.h"
#include "get_clock.h"
#include "pacer.h"
#include "queue.h"
#include <inttypes.h>
#include <math.h>

#define WINDOW_SIZE 1000
#define BASE 2
#define MIN_CHUNK_SIZE 2000
#define STEP 1000

void monitor_latency(void *arg) {
    printf(">>>starting monitor_latency...\n");

    Queue *q;
    double lat, sum_lat = 0.0;
    cycles_t start_cycle, end_cycle;
    int no_cpu_freq_warn = 1;
    double cpu_mhz = get_cpu_mhz(no_cpu_freq_warn);
    uint64_t seq = 0;

    struct pingpong_context *ctx = NULL;
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;
    struct ibv_wc wc;
    const char *servername = ((struct monitor_param *)arg)->addr;
    int isclient = ((struct monitor_param *)arg)->isclient;
    int num_comp;
    uint32_t temp;

    ctx = init_monitor_chan(servername, isclient);
    if (!ctx) {
        fprintf(stderr, ">>>exiting monitor_latency\n");
        exit(1);
    }
    cpu_mhz = get_cpu_mhz(no_cpu_freq_warn);

    /* WR */
    memset(&wr, 0, sizeof wr);
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = (IBV_SEND_SIGNALED | IBV_SEND_INLINE);
    wr.wr_id = seq;
    seq++;
    wr.wr.rdma.rkey = ctx->rem_dest->rkey;
    wr.wr.rdma.remote_addr = ctx->rem_dest->vaddr;

    sge.addr = (uintptr_t)ctx->send_buf;
    sge.length = BUF_SIZE;
    sge.lkey = ctx->send_mr->lkey;

    /* get first WINDOW_SIZE data points before start the loop */
    q = queue_init(WINDOW_SIZE);
    int i;
    for (i = 0; i < WINDOW_SIZE; i++) {
        start_cycle = get_cycles();
        if (ibv_post_send(ctx->qp, &wr, &bad_wr)) {
            perror("ibv_post_send");
            exit(1);
        }
        do {
            num_comp = ibv_poll_cq(ctx->cq, 1, &wc);
        } while (num_comp == 0);

        if (num_comp < 0 || wc.status != IBV_WC_SUCCESS) {
            perror("ibv_poll_cq");
            exit(1);
        }

        end_cycle = get_cycles();

        lat = (end_cycle - start_cycle) / cpu_mhz;
        queue_push(q, lat);
        sum_lat += lat;
    }

    /* monitor loop */
    uint32_t min_virtual_link_cap;
    uint16_t num_active_big_flows;
    uint16_t num_active_small_flows;
    while (1) {
        start_cycle = get_cycles();
        if(ibv_post_send(ctx->qp, &wr, &bad_wr)) {
            perror("ibv_post_send");
            goto cleanup_exit;
        }

        do {
            num_comp = ibv_poll_cq(ctx->cq, 1, &wc);
        } while (num_comp == 0);

        if (num_comp < 0 || wc.status != IBV_WC_SUCCESS) {
            perror("ibv_poll_cq");
            goto cleanup_exit;
        }

        end_cycle = get_cycles();

        lat = (end_cycle - start_cycle) / cpu_mhz;
        sum_lat -= queue_pop(q);
        sum_lat += lat;
        queue_push(q, lat);

        num_active_big_flows = __atomic_load_n(&cb.sb->num_active_big_flows, __ATOMIC_RELAXED);
        num_active_small_flows = __atomic_load_n(&cb.sb->num_active_small_flows, __ATOMIC_RELAXED);
        if (num_active_big_flows) {
            if (num_active_small_flows) {
                min_virtual_link_cap = round((double) num_active_big_flows / (num_active_big_flows + num_active_small_flows) * LINE_RATE_MB);
                temp = __atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED);
                if (sum_lat / WINDOW_SIZE > BASE) {
                    /* Multiplicative Decrease */
                    temp >>= 1;
                    if (ELEPHANT_HAS_LOWER_BOUND && temp < min_virtual_link_cap) {
                        temp = min_virtual_link_cap;
                        __atomic_store_n(&cb.virtual_link_cap, min_virtual_link_cap, __ATOMIC_RELAXED);
                    } else {
                        __atomic_store_n(&cb.virtual_link_cap, temp, __ATOMIC_RELAXED);
                    }
                } else if (__atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED) < LINE_RATE_MB) {
                    /* Additive Increase */
                    __atomic_fetch_add(&cb.virtual_link_cap, 1, __ATOMIC_RELAXED);
                }
            } else if (__atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED) != LINE_RATE_MB) {
                __atomic_store_n(&cb.virtual_link_cap, LINE_RATE_MB, __ATOMIC_RELAXED);
            }
            //printf(">>>> virtual link cap: %" PRIu32 "\n", __atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED));
        }
    }

cleanup_exit:
    queue_free(q);

    exit(1);
}
