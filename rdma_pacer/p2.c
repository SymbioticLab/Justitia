#include "p2.h"

void init_p2_meta_data(struct p2_meta_data *p2) {
    int i;
    for (i = 0; i < 5; i++) {
        p2->positions[i] = i + 1;
    }
    p2->desired_positions[0] = 1;
    p2->desired_positions[1] = 1 + 2 * p2->percentile;
    p2->desired_positions[2] = 1 + 4 * p2->percentile;
    p2->desired_positions[3] = 3 + 2 * p2->percentile;
    p2->desired_positions[4] = 5;

    p2->increments[0] = 0;
    p2->increments[1] = p2->percentile / 2;
    p2->increments[2] = p2->percentile;
    p2->increments[3] = (1 + p2->percentile) / 2;
    p2->increments[4] = 1;

    p2->counter = 0;
}

static int cmpdoublep (const void *p1, const void *p2) {
    return *(double *)p1 < *(double *)p2;
}

static double parabolic(int i, int diff, struct p2_meta_data *p2) {
    int diff1 = p2->positions[i] - p2->positions[i - 1];
    int diff2 = p2->positions[i + 1] - p2->positions[i];
    return p2->markers[i] + diff * 
        ((diff1 + diff) * (p2->markers[i + 1] - p2->markers[i]) / diff2 + 
         (diff2 - diff) * (p2->markers[i] - p2->markers[i - 1]) / diff1) /
        (p2->positions[i + 1] - p2->positions[i - 1]);
}

static double linear(int i, int diff, struct p2_meta_data *p2) {
    return p2->markers[i] + diff * (p2->markers[i + diff] - p2->markers[i]) / (p2->positions[i + diff] - p2->positions[i]);
}

double query_tail_p2(double new_observation, struct p2_meta_data *p2) {
    int i, j, diff;
    double marker_adjusted;
    if (p2->counter < 5) {
        p2->markers[p2->counter] = new_observation;
        p2->counter++;
        qsort(p2->markers, p2->counter, sizeof(double), cmpdoublep);
        return p2->markers[p2->counter - 1]; // return the max value
    }
    for (i = 0; i < 5; i++) {
        if (p2->markers[i] > new_observation) {
            break;
        }
    }
    if (i == 0) p2->markers[i] = new_observation;
    if (i == 5) {
        p2->markers[4] = new_observation;
        i = 4;
    }
    for (j = i; j < 5; j++) {
        p2->positions[j]++;
    }
    for (i = 0; i < 5; i++) {
        p2->desired_positions[i] += p2->increments[i];
    }
    for (i = 1; i < 4; i++) {
        diff = p2->desired_positions[i] - p2->positions[i];
        // only adjust markers that is at least one position away from its desired position
        if ((diff >= 1 && p2->positions[i + 1] - p2->positions[i] > 1)
        || (diff <= -1 && p2->positions[i - 1] - p2->positions[i] < -1)) {
            diff = diff > 0 ? 1 : -1;
            marker_adjusted = parabolic(i, diff, p2);
            if (p2->markers[i - 1] < marker_adjusted && marker_adjusted < p2->markers[i + 1]) {
                p2->markers[i] = marker_adjusted;
            } else {
                p2->markers[i] = linear(i, diff, p2);
            }
            p2->positions[i] += diff;
        }
    }
    p2->counter++;

    if (p2->counter < 100) 
        return p2->markers[4];
    else
        return p2->markers[2];
}