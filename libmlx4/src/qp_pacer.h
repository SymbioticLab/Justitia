#ifndef QP_PACER_H
#define QP_PACER_H

#include "pacer.h"
#include <immintrin.h> /* For _mm_pause */  // remember to take off this header file and __mm_pause() when running on ConFlux

//int start_flag = 0;
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
