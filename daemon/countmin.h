// Two different structures: 
//   1 -- The basic CM Sketch (removed by Yue Tan)
//   2 -- The hierarchical CM Sketch: with log n levels, for range sums etc.
#ifndef COUNTMIN_H
#define COUNTMIN_H

#include "queue.h"
#define min(x,y)	((x) < (y) ? (x) : (y))
#define max(x,y)	((x) > (y) ? (x) : (y))

typedef struct CMH_type{
    int U; // size of the universe in bits
    int gran; // granularity: eg 1, 4 or 8 bits
    int levels; // function of U and gran
    int freelim; // up to which level to keep exact counts
    int depth;
    int width;
    int ** counts;
    int count;
    int windowSize;
    Queue *items;
    unsigned int **hasha, * *hashb;
} CMH_type;

extern CMH_type * CMH_Init(int width, int depth, int U, int gran, int windowSize);
extern void CMH_Destroy(CMH_type *cmh);
// extern int CMH_Size(CMH_type *cmh);
extern int CMH_Update(CMH_type *cmh, int item);
extern int CMH_Quantile(CMH_type *cmh, double frac);

#endif