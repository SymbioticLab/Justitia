#ifndef QP_PACER_H
#define QP_PACER_H

#include "pacer.h"
#include <immintrin.h> /* For _mm_pause */

int start_flag = 1;
int go = 0;

static inline void cpu_relax() __attribute__((always_inline));
static inline void cpu_relax() {
#if (COMPILER == MVCC)
    _mm_pause();
#elif (COMPILER == GCC || COMPILER == LLVM)
    asm("pause");
#endif
}

#endif
