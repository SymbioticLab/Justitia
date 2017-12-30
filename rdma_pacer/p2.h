#ifndef P2_H
#define P2_H

#include <stdlib.h>

struct p2_meta_data {
    double  markers[5];
    double  desired_positions[5];
    double  increments[5];
    double  percentile;
    int     positions[5];
    int     counter;
};

extern void init_p2_meta_data(struct p2_meta_data *);
extern double query_tail_p2(double, struct p2_meta_data *);


#endif