#include "monitor.h"
#include "pingpong.h"
#include "p2.h"
#include "get_clock.h"
#include "countmin.h"
#include "pacer.h"
#include <inttypes.h>
#include <math.h>

int comp (const void * left, const void * right) {
    const double * a = left;
    const double * b = right;
    if (*a < *b) return -1;
    if (*a > *b) return 1;
    return 0;
}

void monitor_latency(void *arg) {
    printf(">>>starting monitor_latency...\n");

    // struct p2_meta_data p2;
    CMH_type *cmh_ns, *cmh_us;
    struct pingpong_context *ctx;
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;
    struct ibv_wc wc;
    cycles_t start_cycle, end_cycle;
    uint32_t median_ns, tail_99_ns, tail_999_ns;
    uint32_t lat_ns; // in nanosecond
    uint32_t median_us, tail_99_us, tail_999_us;
    uint32_t base_tail_99 = 2500, base_tail_999 = 14000; // get the numbers from observation
    uint32_t lat_us; // in microsecond
    // double lat_td; // for t-digest
    // TDigest *digest = NULL;
    int no_cpu_freq_warn = 1;
    double cpu_mhz;
    uint64_t seq = 0;

    const char *servername = ((struct monitor_param *)arg)->addr;
    int isclient = ((struct monitor_param *)arg)->isclient;
    int num_comp;
    uint32_t temp;
    // int i;
    // uint32_t chunk_size;
    
    ctx = init_monitor_chan(servername, isclient);
    if (!ctx) {
        fprintf(stderr, ">>>exiting monitor_latency\n");
        return;
    }

    // init_p2_meta_data(&p2);
    cmh_ns = CMH_Init(32768, 16, 30, 1);
    cmh_us = CMH_Init(32768, 16, 10, 1);
    if (!cmh_ns) {
        printf(">>>Failed to allocate hierarchical count-min sketches: cmmh_ns\n");
        return;
    }
    if (!cmh_us) {
        printf("Failed to allocate hierarchical count-min sketches: cmh_us\n");
    }
    printf(">>>The size of count-min sketches cmh_ns is %d Bytes\n", CMH_Size(cmh_ns));
    printf(">>>The size of count-min sketches cmh_us is %d Bytes\n", CMH_Size(cmh_us));
    // digest = TDigest_create();
    // if (!digest) {
    //     return;
    // }

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

    // test accuracy of the percentiles given by count-min sketches
    // int i, total = 10000; 
    // cycles_t *tposted, *tcompleted;
    // double *delta;
    // tposted = (cycles_t*)malloc(total * sizeof(cycles_t));
    // if (!tposted) {
    //     perror("malloc: tposted");
    //     _exit(1);
    // }
    // tcompleted = (cycles_t*)malloc(total * sizeof(cycles_t));
    // if (!tcompleted) {
    //     perror("malloc: tcompleted");
    //     _exit(1);
    // }
    // delta = (double*)malloc(total * sizeof(double));
    // if (!delta) {
    //     perror("malloc: delta");
    //     _exit(1);
    // }

    // for (i = 0; i < total; i++) {
    /* monitor loop */
    while (1) {
        start_cycle = get_cycles();
        // tposted[i] = get_cycles();
        if(ibv_post_send(ctx->qp, &wr, &bad_wr)) {
            fprintf(stderr, ">>>Failed to send wr\n");
        } else {
            do {
                num_comp = ibv_poll_cq(ctx->cq, 1, &wc);
            } while (num_comp == 0);

            if (wc.status == IBV_WC_SUCCESS) {
                // tcompleted[i] = get_cycles();
                end_cycle = get_cycles();
                if ((end_cycle - start_cycle) * 1000 / cpu_mhz <= (1u << 30)) {
                    // tail = query_tail_p2(lat_ns, &p2);
                    // printf("@@@latency = %u\n", lat_ns);
                    // printf("@@@tail = %u\n", tail);
                    lat_ns = round((end_cycle - start_cycle) * 1000 / cpu_mhz);
                    if (CMH_Update(cmh_ns, lat_ns, 1)) {
                        printf("The old CMH is full... Destroying the old CMH... Creating a new CMH...\n");
                        CMH_Destroy(cmh_ns);
                        cmh_ns = CMH_Init(32768, 16, 30, 1);
                    }

                    lat_us = round((end_cycle - start_cycle) / cpu_mhz);
                    if (CMH_Update(cmh_us, lat_us, 1)) {
                        printf("The old CMH is full... Destroying the old CMH... Creating a new CMH...\n");
                        CMH_Destroy(cmh_us);
                        cmh_us = CMH_Init(32768, 16, 10, 1);
                    }

                    // fprintf(stderr, "DEBUG1\n");
                    median_ns = CMH_Quantile(cmh_ns, 0.5);
                    tail_99_ns = CMH_Quantile(cmh_ns, 0.99);
                    tail_999_ns = CMH_Quantile(cmh_ns, 0.999);
                    
                    median_us = CMH_Quantile(cmh_us, 0.5);
                    tail_99_us = CMH_Quantile(cmh_us, 0.99);
                    tail_999_us = CMH_Quantile(cmh_us, 0.999);

                    printf(">>>measured latency is %" PRIu32 "ns\n", lat_ns);
                    printf(">>>measured median is %" PRIu32 "ns\n", median_ns);
                    printf(">>>measured 99 percentile is %" PRIu32 "ns\n", tail_99_ns);
                    printf(">>>measured 99.9 percentile is %" PRIu32 "ns\n", tail_999_ns);
                    printf(">>>measured latency is %" PRIu32 "us\n", lat_us);
                    printf(">>>measured median is %" PRIu32 "us\n", median_us);
                    printf(">>>measured 99 percentile is %" PRIu32 "us\n", tail_99_us);
                    printf(">>>measured 99.9 percentile is %" PRIu32 "us\n", tail_999_us);
                } else {
                    fprintf(stderr, "!!!measured latency is greater than the set maximum 2^30ns\n");
                }
                  

                if (__atomic_load_n(&cb.num_active_big_flows, __ATOMIC_RELAXED)) {
                    if (__atomic_load_n(&cb.num_active_small_flows, __ATOMIC_RELAXED)) {
                        if (tail_99_ns > base_tail_99 * 2 || tail_999_ns > base_tail_999 * 2) {
                            /* Multiplicative Decrease */
                            temp = __atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED) / 2;
                            __atomic_store_n(&cb.virtual_link_cap, temp, __ATOMIC_RELAXED);
                            __atomic_store_n(&cb.active_chunk_size, DEFAULT_CHUNK_SIZE, __ATOMIC_RELAXED);
                        } else {
                            /* Additive Increase */
                            __atomic_fetch_add(&cb.virtual_link_cap, 1, __ATOMIC_RELAXED);
                        }
                    } else if (__atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED) != LINE_RATE_MB) {
                        __atomic_store_n(&cb.virtual_link_cap, LINE_RATE_MB, __ATOMIC_RELAXED);
                        __atomic_store_n(&cb.active_chunk_size, DEFAULT_CHUNK_SIZE, __ATOMIC_RELAXED);
                    }
                } 
                // else {
                //     base_tail_99 = tail_99_ns;
                //     base_tail_999 = tail_999_ns;
                //     if (__atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED) != LINE_RATE_MB) {
                //         __atomic_store_n(&cb.virtual_link_cap, LINE_RATE_MB, __ATOMIC_RELAXED);
                //     }
                // }
            } else {
                perror("ibv_poll_cq");
                // int j;
                // for (j = 0; j < i; j++) {
                //     delta[i] = (double)(tcompleted[j] - tposted[j]) / cpu_mhz;
                // }

                // qsort(delta, i, sizeof(*delta), comp);
                // printf("real median is %.2f\n", delta[(int)ceil(i * 0.5)]);
                // printf("real 99 percentile is %.2f\n", delta[(int)ceil(i * 0.99)]);
                // printf("real 99.9 percentile is %.2f\n", delta[(int)ceil(i * 0.999)]);
                _exit(1);
            }
        }
    }

    // for (i = 0; i < total; i++) {
    //     delta[i] = (double)(tcompleted[i] - tposted[i]) / cpu_mhz;
    // }

    // qsort(delta, total, sizeof(*delta), comp);
    // printf("real median is %.2f\n", delta[(int)ceil(total * 0.5)]);
    // printf("real 99 percentile is %.2f\n", delta[(int)ceil(total * 0.99)]);
    // printf("real 99.9 percentile is %.2f\n", delta[(int)ceil(total * 0.999)]);
    // _exit(0);
}
