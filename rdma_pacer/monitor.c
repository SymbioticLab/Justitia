#include "monitor.h"
#include "pingpong.h"
#include "get_clock.h"
#include "pacer.h"
#include "countmin.h"
#include <inttypes.h>
#include <math.h>

#define MEDIAN 2
#define TAIL 2

#define WIDTH 32768
#define DEPTH 16
#define U 24
#define GRAN 4
#define WINDOW_SIZE 10000

CMH_type *cmh = NULL;

void monitor_latency(void *arg)
{
    printf(">>>starting monitor_latency...\n");

    double median, tail_99;

    int lat; // in nanoseconds
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
    int gid_idx = ((struct monitor_param *)arg)->gid_idx;
    int num_comp;
    uint32_t temp;

    ctx = init_monitor_chan(servername, isclient, gid_idx);
    if (!ctx)
    {
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
    wr.wr.rdma.rkey = ctx->rem_dest->rkey;
    wr.wr.rdma.remote_addr = ctx->rem_dest->vaddr;

    sge.addr = (uintptr_t)ctx->send_buf;
    sge.length = BUF_SIZE;
    sge.lkey = ctx->send_mr->lkey;

    /* get first WINDOW_SIZE data points before start the loop */
    cmh = CMH_Init(WIDTH, DEPTH, U, GRAN, WINDOW_SIZE);
    if (!cmh)
    {
        fprintf(stderr, "CMH_Init failed\n");
        exit(1);
    }

    /* monitor loop */
    uint32_t min_virtual_link_cap;
    uint16_t num_active_big_flows;
    uint16_t num_active_small_flows;
    while (1)
    {
        start_cycle = get_cycles();
        if (ibv_post_send(ctx->qp, &wr, &bad_wr))
        {
            perror("ibv_post_send");
            break;
        }

        do
        {
            num_comp = ibv_poll_cq(ctx->cq, 1, &wc);
        } while (num_comp == 0);

        if (num_comp < 0 || wc.status != IBV_WC_SUCCESS)
        {
            perror("ibv_poll_cq");
            break;
        }

        end_cycle = get_cycles();

        lat = round((end_cycle - start_cycle) / cpu_mhz * 1000);
        // printf("DEBUG latency %.1f us\n", round(lat/100.0)/10);
        if (CMH_Update(cmh, lat))
        {
            fprintf(stderr, "CMH_Update failed\n");
            break;
        }
        median = round(CMH_Quantile(cmh, 0.5)/100.0)/10;
        tail_99 = round(CMH_Quantile(cmh, 0.99)/100.0)/10;
        // if (seq >= WINDOW_SIZE)
        // {
        //     printf("DEBUG latency %.1f us, median %.1f us, 99th tail %.1f us\n", round(lat/100.0)/10, median, tail_99);
        // }
        seq++;
        wr.wr_id = seq;
        num_active_big_flows = __atomic_load_n(&cb.sb->num_active_big_flows, __ATOMIC_RELAXED);
        num_active_small_flows = __atomic_load_n(&cb.sb->num_active_small_flows, __ATOMIC_RELAXED);
        printf("num_active_big_flows = %d\n", num_active_big_flows);
        printf("num_active_small_flows = %d\n", num_active_small_flows);
        if (num_active_big_flows)
        {
            if (num_active_small_flows)
            {
                min_virtual_link_cap = round((double)num_active_big_flows / (num_active_big_flows + num_active_small_flows) * LINE_RATE_MB);
                temp = __atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED);
                if (median > MEDIAN || tail_99 > TAIL)
                {
                    /* Multiplicative Decrease */
                    temp >>= 1;
                    if (ELEPHANT_HAS_LOWER_BOUND && temp < min_virtual_link_cap)
                    {
                        temp = min_virtual_link_cap;
                        __atomic_store_n(&cb.virtual_link_cap, min_virtual_link_cap, __ATOMIC_RELAXED);
                    }
                    else
                    {
                        __atomic_store_n(&cb.virtual_link_cap, temp, __ATOMIC_RELAXED);
                    }
                }
                else if (__atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED) < LINE_RATE_MB)
                {
                    /* Additive Increase */
                    __atomic_fetch_add(&cb.virtual_link_cap, 1, __ATOMIC_RELAXED);
                }
            }
            else if (__atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED) != LINE_RATE_MB)
            {
                __atomic_store_n(&cb.virtual_link_cap, LINE_RATE_MB, __ATOMIC_RELAXED);
            }
            printf(">>>> virtual link cap: %" PRIu32 "\n", __atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED));
        }
    }
    printf("Out of while loop. exiting...\n");
    CMH_Destroy(cmh);
    exit(1);
}
