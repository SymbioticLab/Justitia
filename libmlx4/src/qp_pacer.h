#ifndef QP_PACER_H
#define QP_PACER_H

#include "pacer.h"
#include <immintrin.h> // For _mm_pause

uint64_t bytes = 0;
int start_flag = 1;
int go = 0;
struct timespec start, last_check;

static inline double subtract_time(struct timespec *time2) __attribute__((always_inline));
static inline double subtract_time(struct timespec *time2)
{
    struct timespec time1;
    clock_gettime(CLOCK_MONOTONIC, &time1);
    return difftime(time1.tv_sec, time2->tv_sec) * 1000000 + (double) (time1.tv_nsec - time2->tv_nsec) / 1000;
}

static inline void cpu_relax() __attribute__((always_inline));
static inline void cpu_relax()
{
#if (COMPILER == MVCC)
    _mm_pause();
#elif (COMPILER == GCC || COMPILER == LLVM)
    asm("pause");
#endif
}

static void check_target() {
    // /* Set affinity mask to include CPU 1 */
    // cpu_set_t cpuset;
    // pthread_t thread;
    // thread = pthread_self();
    // CPU_ZERO(&cpuset);
    // CPU_SET(1, &cpuset);
    // s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

    struct timespec sleep_time, remain_time;
    uint32_t target;
    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = 10000000;
    while (1) {
        nanosleep(&sleep_time, &remain_time);
        /* use busy waiting to backoff */
        target = __atomic_load_n(&flow->target, __ATOMIC_RELAXED);
        if (__atomic_load_n(&bytes, __ATOMIC_RELAXED) / subtract_time(&start) > target) {
            while (!__atomic_exchange_n(&go, 1, __ATOMIC_RELAXED));
            while (__atomic_load_n(&bytes, __ATOMIC_RELAXED) / subtract_time(&start) > target) {
                // printf("Waiting...\n");
                cpu_relax();
            }
            __atomic_store_n(&go, 0, __ATOMIC_RELAXED);
        }
    }
}

static void write_byte_count() {
    // /* Set affinity mask to include CPU 0 */
    // cpu_set_t cpuset;
    // pthread_t thread;
    // thread = pthread_self();
    // CPU_ZERO(&cpuset);
    // CPU_SET(0, &cpuset);
    // s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

    struct timespec sleep_time, remain_time;
    uint32_t  new_measured;
    // uint32_t old_measured;
    // double alpha = 0.9;

    // old_measured = 0.0;
    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = 50000000;
    while (1) {
        nanosleep(&sleep_time, &remain_time);
        new_measured = __atomic_load_n(&bytes, __ATOMIC_RELAXED) / subtract_time(&start);
        __atomic_store_n(&flow->measured, new_measured, __ATOMIC_RELAXED);
        printf("measured throughput: %u\n", new_measured);
        // old_measured = new_measured;
    }
}

#endif
