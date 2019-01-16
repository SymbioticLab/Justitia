#include "monitor.h"
#include "pingpong.h"
#include "get_clock.h"
#include "pacer.h"
#include "countmin.h"
#include <inttypes.h>
#include <math.h>

#define TAIL 2

#define WIDTH 32768
#define DEPTH 16
#define U 24
#define GRAN 4
//#define WINDOW_SIZE 10000
#define WINDOW_SIZE 1000
// Time to wait in milliseconds when latency target can't not be met (before giving back bandwidth)
#define LAT_TARGET_WAIT_TIME 5000
// Time to wait in milliseconds when latency target can't not be met before increasing num_split_qps
#define LAT_TARGET_UNMET_WAIT_TIME 2
// Time to wait in microseconds when latency target is met
#define RMF_FREQENCY 800
//#define TIMEKEEP
#define NUM_SAMPLE 2000 // sampling interval (seconds) for timekeeping
#define SAMPLE_INTERVAL 0.04
#define USE_CMH
//#define CMH_PERCENTILE  0.99    // pencentile ask from CMH
#define CMH_PERCENTILE  0.99    // pencentile ask from CMH
#define improvement_factor 0.25  // improvement of current measured tail over previous tail that needs to be achieved to keep the current num_split_qps

CMH_type *cmh = NULL;

static inline void cpu_relax() __attribute__((always_inline));
static inline void cpu_relax()
{
    asm("nop");
}

void monitor_latency(void *arg)
{
    printf(">>>starting monitor_latency...\n");
#ifdef DYNAMIC_NUM_SPLIT_QPS
    printf("Dynamic num_split_qps adjustment: ON\n");
#else
    printf("Dynamic num_split_qps adjustment: OFF\n");
#endif
#ifdef TIMEKEEP
    int arr_idx = 0;
    double time_arr[NUM_SAMPLE] = {0};  // array of timestamps (in seconds)
    double lat_arr[NUM_SAMPLE] = {0};   // array of actual latency measured by monitor (in microseconds)
    double tail_arr[NUM_SAMPLE] = {0};   // array of tail latency estimated by CMH (in microseconds)
    double curr_time = 0;       // current time in microseconds
    double loop_time = 0;       // used to keep track of when we reach the next sample interval (unit is microsecond)
    cycles_t loop_start = 0, loop_end = 0;
    int big_flow_flag = 0;
    double initial_wait_time = 0;   // initial wait time before an elephant came in
    int stop_timekeep = 0;
#endif

    double measured_tail;

    int lat; // in nanoseconds
    cycles_t start_cycle, end_cycle;
    //cycles_t prev_start_cycle = 0;
    // cycles_t cmh_start, cmh_end;
    int no_cpu_freq_warn = 1;
    double cpu_mhz = get_cpu_mhz(no_cpu_freq_warn);

    uint64_t seq = 0;
    struct pingpong_context *ctx = NULL;
    struct ibv_send_wr wr, send_wr, *bad_wr = NULL;
    struct ibv_recv_wr recv_wr, *bad_recv_wr = NULL;
    struct ibv_sge sge, send_sge, recv_sge;
    struct ibv_wc wc, recv_wc, send_wc;
    const char *servername = ((struct monitor_param *)arg)->addr;
    int isclient = ((struct monitor_param *)arg)->isclient;
    int gid_idx = ((struct monitor_param *)arg)->gid_idx;
    int num_comp;
    int num_remote_big_reads = 0;
    uint32_t received_read_rate;
    uint32_t temp, new_remote_read_rate;
    int found_split_level = 0;

    ctx = init_monitor_chan(servername, isclient, gid_idx);
    if (!ctx)
    {
        fprintf(stderr, "failed to allocate pingpong context. exiting monitor_latency\n");
        exit(1);
    }
    cb.ctx = ctx;
    cpu_mhz = get_cpu_mhz(no_cpu_freq_warn);

    /* SEND WR */
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

    /* READ SEND WR */
    memset(&send_wr, 0, sizeof send_wr);
    send_wr.opcode = IBV_WR_SEND;
    send_wr.sg_list = &send_sge;
    send_wr.num_sge = 1;
    send_wr.send_flags = IBV_SEND_INLINE | IBV_SEND_SIGNALED;

    memset(&send_sge, 0, sizeof send_sge);
    memset((char *)ctx->local_read_buf + BUF_READ_SIZE, 0, BUF_READ_SIZE);
    send_sge.addr = (uintptr_t)((char *)ctx->local_read_buf + BUF_READ_SIZE);
    send_sge.length = BUF_READ_SIZE;
    send_sge.lkey = ctx->local_read_mr->lkey;

    /* READ RECV WR */
    memset(&recv_wr, 0, sizeof recv_wr);
    recv_wr.num_sge = 1;
    recv_wr.sg_list = &recv_sge;

    memset(&recv_sge, 0, sizeof send_sge);
    memset(ctx->remote_read_buf, 0, BUF_READ_SIZE);
    recv_sge.addr = (uintptr_t)ctx->remote_read_buf;
    recv_sge.length = BUF_READ_SIZE;
    recv_sge.lkey = ctx->remote_read_mr->lkey;
    ibv_post_recv(ctx->qp_read, &recv_wr, &bad_recv_wr);

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
    uint16_t num_active_small_flows = 0;
#ifdef DYNAMIC_NUM_SPLIT_QPS
    uint16_t num_split_qps = 1;
    cycles_t target_unmet_counter_start = 0;
    cycles_t target_unmet_counter_end = 0;
    cycles_t started_counting_target_unmet = 0;
    uint32_t num_samples = 0;
    double prev_tail = 0;
#endif
#ifdef FAVOR_BIG_FLOW
    uint16_t prev_num_small_flows = 0;
    cycles_t counter_start = 0;
    cycles_t counter_end = 0;
    cycles_t started_counting = 0;
    int AIMD_off = 0;
#endif
#ifdef SMART_RMF
    int lat_above_target = 0;
    cycles_t counter_rmf = 0;
#endif
    while (1)
    {
#ifdef TIMEKEEP
        loop_start = get_cycles();
#endif
#ifdef SMART_RMF
        if (lat_above_target) {
            counter_rmf = get_cycles();

            while ((get_cycles() - counter_rmf) / cpu_mhz < RMF_FREQENCY)
                cpu_relax();
            lat_above_target = 0;
        }
#endif
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

#ifdef USE_CMH
        lat = round((end_cycle - start_cycle) / cpu_mhz * 1000);
        if (CMH_Update(cmh, lat))
        {
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
#endif
        seq++;
        wr.wr_id = seq;

        /* check if any remote read is registered or if read rate is received */
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


        num_active_big_flows = __atomic_load_n(&cb.sb->num_active_big_flows, __ATOMIC_RELAXED);
        num_active_small_flows = __atomic_load_n(&cb.sb->num_active_small_flows, __ATOMIC_RELAXED);
        //printf("num_active_big_flows = %d\n", num_active_big_flows);
        //printf("num_active_small_flows = %d\n", num_active_small_flows);
#ifdef FAVOR_BIG_FLOW
        if (num_active_small_flows > prev_num_small_flows) {
            AIMD_off = 0;
        }
        prev_num_small_flows = num_active_small_flows;
        //// If can't meet latency target in a given interval, keep the current virtual link cap which was set to LINE_RATE
        if (AIMD_off) {
            continue;
        }
#endif
        if (num_active_big_flows + num_remote_big_reads)
        {
            if (num_active_small_flows)
            {
                min_virtual_link_cap = round((double)(num_active_big_flows + num_remote_big_reads) 
                    / (num_active_big_flows + num_active_small_flows + num_remote_big_reads) * LINE_RATE_MB);
                temp = __atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED);
                if (measured_tail > TAIL)
                {
#ifdef SMART_RMF
                    lat_above_target = 1;
#endif
                    /* Multiplicative Decrease */
                    temp >>= 1;
                    ////temp -= 1000;
#ifdef DYNAMIC_NUM_SPLIT_QPS
                    num_samples++;
#endif
                    if (ELEPHANT_HAS_LOWER_BOUND && temp < min_virtual_link_cap)
                    {
                        temp = min_virtual_link_cap;

#ifdef DYNAMIC_NUM_SPLIT_QPS
                        if (!started_counting_target_unmet) {
                            target_unmet_counter_start = get_cycles();
                            started_counting_target_unmet = 1;
                        }
                        if (num_samples == WINDOW_SIZE) {
                            num_samples = 0;
                            target_unmet_counter_end = get_cycles();
                            started_counting_target_unmet = 0;
                            if (!found_split_level) {
                                num_split_qps = __atomic_load_n(&cb.sb->num_active_split_qps, __ATOMIC_RELAXED);

                                if (num_split_qps == 1) {
                                    num_split_qps++;
                                    __atomic_store_n(&cb.sb->num_active_split_qps, num_split_qps, __ATOMIC_RELAXED);
                                    printf("Elapsed time is %.2f us; Increase num_split_qps to %d\n", (target_unmet_counter_end - target_unmet_counter_start) / cpu_mhz, num_split_qps);
                                    prev_tail = measured_tail;

                                } else {    // num_split_qps > 1
                                    if (measured_tail < prev_tail && (prev_tail - measured_tail)/prev_tail > improvement_factor) {
                                        if (num_split_qps < MAX_NUM_SPLIT_QPS) {
                                            printf("Elapsed time is %.2f us; num_split_qps = %d performs well with %.2f improvement. (prev, curr) = (%.2f, %.2f). Increase num_split_qps to %d to see if we can do even better\n",
                                                (target_unmet_counter_end - target_unmet_counter_start) / cpu_mhz, num_split_qps, (prev_tail - measured_tail)/prev_tail, prev_tail, measured_tail, num_split_qps + 1);
                                            num_split_qps++;
                                            __atomic_store_n(&cb.sb->num_active_split_qps, num_split_qps, __ATOMIC_RELAXED);
                                        } else {
                                            printf("Elapsed time is %.2f us; num_split_qps = %d performs well with %.2f improvement. (prev, curr) = (%.2f, %.2f). Want to but cannot not increase num_split_qps above MAX_NUM_SPLIT_QPS = %d.\n",
                                                (target_unmet_counter_end - target_unmet_counter_start) / cpu_mhz, num_split_qps, (prev_tail - measured_tail)/prev_tail, prev_tail, measured_tail, MAX_NUM_SPLIT_QPS);
                                        }
                                        prev_tail = measured_tail;        
                                        found_split_level = 0;

                                    } else {
                                        if (num_split_qps > 1) {
                                            printf("Elapsed time is %.2f us; num_split_qps = %d does not perform well with %.2f improvement. (prev, curr) = (%.2f, %.2f). Decreae num_split_qps back to %d and stays there\n",
                                                (target_unmet_counter_end - target_unmet_counter_start) / cpu_mhz, num_split_qps, (prev_tail - measured_tail)/prev_tail, prev_tail, measured_tail, num_split_qps - 1);
                                            num_split_qps--;
                                            __atomic_store_n(&cb.sb->num_active_split_qps, num_split_qps, __ATOMIC_RELAXED);
                                        } else {
                                            printf("Elapsed time is %.2f us; num_split_qps = %d performs well with %.2f improvement. (prev, curr) = (%.2f, %.2f). Want to but cannot not decrease num_split_qps below 1.\n",
                                                (target_unmet_counter_end - target_unmet_counter_start) / cpu_mhz, num_split_qps, (prev_tail - measured_tail)/prev_tail, prev_tail, measured_tail);
                                        }
                                        prev_tail = measured_tail;        
                                        found_split_level = 1;
                                    }

                                    /*
                                    target_unmet_counter_end = get_cycles();
                                    if (((target_unmet_counter_end - target_unmet_counter_start) / cpu_mhz) > LAT_TARGET_UNMET_WAIT_TIME * 1000) {
                                        //// increase num_split_qps
                                        num_split_qps = __atomic_load_n(&cb.sb->num_active_split_qps, __ATOMIC_RELAXED);
                                        if (num_split_qps < MAX_NUM_SPLIT_QPS) {
                                            num_split_qps++;
                                            __atomic_store_n(&cb.sb->num_active_split_qps, num_split_qps, __ATOMIC_RELAXED);
                                            printf("elapsed time is %.2f us; increase num_split_qps to %d\n", (target_unmet_counter_end - target_unmet_counter_start) / cpu_mhz, num_split_qps);
                                        }
                                        started_counting_target_unmet = 0;
                                    */
                                }
                            } else {    // if stabalized at a split level
                            }
                        }
                            

#endif

#ifdef FAVOR_BIG_FLOW
//TODO: will need to modify the favor_big_flow logic after introducing the "dynamically change num_split_qps" feature
                        //// counter to count the time since we can't meet the latency target
                        if (!started_counting) {
                            counter_start = get_cycles();
                            started_counting = 1;
                        }
                        counter_end = get_cycles();
                        if (((counter_end - counter_start) / cpu_mhz) > LAT_TARGET_WAIT_TIME * 1000) {
                            printf("elapsed time is %.2f us\n", (counter_end - counter_start) / cpu_mhz);
                            //// Give bandwidth back to big flows until another small flow joins
                            temp = LINE_RATE_MB;
                            AIMD_off = 1;
                            started_counting = 0;
                        }
#endif
                    }
                }
                else    // target met
                {
                    printf("Target Met!\n");
                    if (found_split_level) {
                        //TODO: keep monitoring latency changes
                    }
                    /* Additive Increase */
                    if (__atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED) < LINE_RATE_MB) {
                        temp++;
                    }
#ifdef DYNAMIC_NUM_SPLIT_QPS
                    //// invalidate latency target counter
                    started_counting_target_unmet = 0;
#endif
#ifdef FAVOR_BIG_FLOW
                    //// invalidate latency target counter
                    started_counting = 0;
#endif
                }
                if (num_remote_big_reads) {
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
                            num_comp = ibv_poll_cq(ctx->cq_send, 1, &send_wc);
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
                }
                __atomic_store_n(&cb.virtual_link_cap, temp, __ATOMIC_RELAXED);
            }
            else if (__atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED) != LINE_RATE_MB)
            {   
                temp = LINE_RATE_MB;
                if (num_remote_big_reads) {
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
                            num_comp = ibv_poll_cq(ctx->cq_send, 1, &send_wc);
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
                }
                __atomic_store_n(&cb.virtual_link_cap, temp, __ATOMIC_RELAXED);
#ifdef DYNAMIC_NUM_SPLIT_QPS
                /* decrease num_split_qps back to 1 when no small flow present; chunk size will change accordingly */
                __atomic_store_n(&cb.sb->num_active_split_qps, 1, __ATOMIC_RELAXED);
                num_split_qps = 1;
                started_counting_target_unmet = 0;
                printf("Decrease num_split_qps to 1 given no small flow present\n");
#endif
            }
#ifdef DYNAMIC_NUM_SPLIT_QPS
            else if (num_split_qps != 1)
            {
                /* decrease num_split_qps back to 1 when no small flow present; chunk size will change accordingly */
                __atomic_store_n(&cb.sb->num_active_split_qps, 1, __ATOMIC_RELAXED);
                num_split_qps = 1;
                started_counting_target_unmet = 0;
                printf("Decrease num_split_qps to 1 given no small flow present\n");
            }
#endif
            //printf(">>>> virtual link cap: %" PRIu32 "\n", __atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED));
        }

#ifdef TIMEKEEP
        if (!stop_timekeep) {
            loop_end = get_cycles();    
            loop_time += ((double)(loop_end - loop_start) / cpu_mhz);
            if (loop_time > SAMPLE_INTERVAL * 1000000) {
                curr_time += loop_time;
                loop_time = 0;
                time_arr[arr_idx] = curr_time / 1000000;
                lat_arr[arr_idx] = (double)(end_cycle - start_cycle) / cpu_mhz;
                tail_arr[arr_idx] = tail_99;
                arr_idx++;
                if (arr_idx == NUM_SAMPLE) {
                    stop_timekeep = 1;
                }
            }
            if (!big_flow_flag && (num_active_big_flows > 0)) {
                big_flow_flag = 1;
                initial_wait_time = curr_time / 1000000;
            }
        }
#endif
    }
    printf("Out of while loop. exiting...\n");

#ifdef USE_CMH
    CMH_Destroy(cmh);
#endif

#ifdef TIMEKEEP // To get of the while loop, do CTRL+C on the remote side
    //TODO: write to file...
    FILE *f = fopen("lat_result.txt", "w");
    fprintf(f, "Initial Wait Time before Elephant came in: %.2f(s)\n", initial_wait_time);
    fprintf(f, "sample_cnt\tTime(s)\t\tLatency(us)\tTail(us)\n");
    int i;
    for (i = 0; i < NUM_SAMPLE; i++) {
        fprintf(f, "%d\t\t%.2f\t\t%.2f\t\t%.2f\n", i + 1, time_arr[i], lat_arr[i], tail_arr[i]);
    }
    fclose(f);
#endif
    exit(1);
}
