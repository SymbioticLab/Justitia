#include <stdlib.h>
#include <stdio.h>
#include "prng.h"
#include "massdal.h"
#include "countmin.h"
#include <math.h>

/* Code modified from implementations found on
 * https://www.cs.rutgers.edu/~muthu/massdal-code-index.html
 *
 * Reference paper http://dx.doi.org/10.1016/j.jalgor.2003.12.001
 * and paper https://hal.archives-ouvertes.fr/hal-01073877/document
 *
 * Perfect Windowed Count-Min sketches
 */

CMH_type *CMH_Init(int width, int depth, int U, int gran, int windowSize)
{
    CMH_type *cmh;
    int i, j, k;
    prng_type *prng;

    if (U <= 0 || U >= 32)
        return NULL;
    // U is the log size of the universe in bits

    if (gran > U || gran < 1)
        return NULL;
    // gran is the granularity to look at the universe in
    // check that the parameters make sense...

    cmh = (CMH_type *)malloc(sizeof(CMH_type));
    if (!cmh)
    {
        perror("malloc: cmh");
        return NULL;
    }

    prng = prng_Init(-12784, 2);
    // initialize the generator for picking the hash functions
    if (!prng)
    {
        fprintf(stderr, "prng_Init failed\n");
        return NULL;
    }

    cmh->depth = depth;
    cmh->width = width;
    cmh->count = 0;
    cmh->U = U;
    cmh->gran = gran;
    cmh->windowSize = windowSize;
    cmh->levels = (int)ceil((double)U / gran);
    cmh->items = queue_init(windowSize);
    for (j = 0; j < cmh->levels; j++)
    {
        if ((1u << (cmh->gran * j)) <= cmh->depth * cmh->width)
        {
            cmh->freelim = j;
        }
        else
        {
            break;
        }
    }
    //find the level up to which it is cheaper to keep exact counts
    cmh->freelim = cmh->levels - cmh->freelim;
    /* cmh->freelim to 31 are levels keeping exact counts */

    cmh->counts = (int **)calloc(1 + cmh->levels, sizeof(int *));
    cmh->hasha = (unsigned int **)calloc(1 + cmh->levels, sizeof(unsigned int *));
    cmh->hashb = (unsigned int **)calloc(1 + cmh->levels, sizeof(unsigned int *));
    j = 1;
    for (i = cmh->levels - 1; i >= 0; i--)
    {
        if (i >= cmh->freelim)
        { // allocate space for representing things exactly at high levels
            cmh->counts[i] = calloc(1 << (cmh->gran * j), sizeof(int));
            j++;
            cmh->hasha[i] = NULL;
            cmh->hashb[i] = NULL;
        }
        else
        { // allocate space for a sketch
            cmh->counts[i] = (int *)calloc(cmh->depth * cmh->width, sizeof(int));
            cmh->hasha[i] = (unsigned int *)calloc(cmh->depth, sizeof(unsigned int));
            cmh->hashb[i] = (unsigned int *)calloc(cmh->depth, sizeof(unsigned int));

            if (cmh->hasha[i] && cmh->hashb[i])
                for (k = 0; k < cmh->depth; k++)
                { // pick the hash functions
                    cmh->hasha[i][k] = prng_int(prng) & MOD;
                    cmh->hashb[i][k] = prng_int(prng) & MOD;
                }
        }
    }

    return cmh;
}

// free up the space
void CMH_Destroy(CMH_type *cmh)
{
    int i;
    if (!cmh) return;
    for (i=0;i<cmh->levels;i++)
    {
        if (i>=cmh->freelim)
        {
            free(cmh->counts[i]);
        }
        else 
        {
            free(cmh->hasha[i]);
            free(cmh->hashb[i]);
            free(cmh->counts[i]);
        }
    }
    free(cmh->counts);
    free(cmh->hasha);
    free(cmh->hashb);
    queue_free(cmh->items);
    free(cmh);
    cmh = NULL;
}

static void CMH_Delete(CMH_type *cmh, int item)
{
    int i, j, offset;
    for (i = 0; i < cmh->levels; i++)
    {
        offset = 0;
        if (i >= cmh->freelim)
        {
            // printf("DEBUG: update exact counts at level %d\n", i);
            cmh->counts[i][item]--;
            // keep exact counts at high levels in the hierarchy
        }
        else
        {
            // printf("DEBUG: update the sketch at level %d\n", i);
            for (j = 0; j < cmh->depth; j++)
            {
                // printf("DEBUG before increment\n");
                cmh->counts[i][(hash31(cmh->hasha[i][j], cmh->hashb[i][j], item) % cmh->width) + offset]--;
                // printf("DEBUG after increment\n");
                // this can be done more efficiently if the width is a power of two
                offset += cmh->width;
                /* 2D array represented as 1D array so offset needs to be
                   incremented by cmh->width */
            }
        }
        item >>= cmh->gran;
    }
}
/* update with a new value item and increment cmh->count by diff
 * return 0 on success
 * return 1 on count overflow or NULL cmh pointer
 */
int CMH_Update(CMH_type *cmh, int item)
{
    int i, j, offset;

    if (!cmh)
        return 1;

    // if (cmh->ts + 1 < cmh->ts)
    // {
    //     fprintf(stderr, "count overflow\n");
    //     return 1;
    // }

    if (item > (1 << cmh->U))
    {
        fprintf(stderr, "item exceeds the maximum supported value\n");
        return 1;
    }

    if (item < 0)
    {
        fprintf(stderr, "item is negative\n");
        return 1;
    }

    if (cmh->count < cmh->windowSize)
    {
        cmh->count++;
        queue_push(cmh->items, item);
    }
    else
    {
        CMH_Delete(cmh, queue_pop(cmh->items));
        queue_push(cmh->items, item);
    }

    for (i = 0; i < cmh->levels; i++)
    {
        offset = 0;
        if (i >= cmh->freelim)
        {
            // printf("DEBUG: update exact counts at level %d\n", i);
            cmh->counts[i][item]++;
            // keep exact counts at high levels in the hierarchy
        }
        else
        {
            // printf("DEBUG: update the sketch at level %d\n", i);
            for (j = 0; j < cmh->depth; j++)
            {
                // printf("DEBUG before increment\n");
                cmh->counts[i][(hash31(cmh->hasha[i][j], cmh->hashb[i][j], item) % cmh->width) + offset]++;
                // printf("DEBUG after increment\n");
                // this can be done more efficiently if the width is a power of two
                offset += cmh->width;
                /* 2D array represented as 1D array so offset needs to be
                   incremented by cmh->width */
            }
        }
        item >>= cmh->gran;
    }
    return 0;
}

// return the size used in bytes
// int CMH_Size(CMH_type * cmh) {
//     int counts, hashes, admin,i;
//     if (!cmh) return 0;
//     admin = sizeof(CMH_type);
//     counts = cmh->levels * sizeof(int **);
//     for (i = 0; i < cmh->levels; i++)
//     if (i >= cmh->freelim)
//       counts += (1 << (cmh->gran * (cmh->levels - i))) * sizeof(int);
//     else
//       counts += cmh->width * cmh->depth * sizeof(int);
//     hashes = (cmh->levels - cmh->freelim) * cmh->depth * 2 * sizeof(unsigned int);
//     hashes += (cmh->levels) * sizeof(unsigned int *);
//     return admin + hashes + counts;
// }

// return an estimate of item at level depth
int CMH_count(CMH_type *cmh, int depth, int item)
{
    int j;
    int offset;
    int estimate;

    if (depth >= cmh->levels)
        return cmh->windowSize;
    if (depth >= cmh->freelim)
        return cmh->counts[depth][item];
    // else, use the appropriate sketch to make an estimate
    offset = 0;
    estimate = cmh->counts[depth][(hash31(cmh->hasha[depth][0], cmh->hashb[depth][0], item) % cmh->width) + offset];
    for (j = 1; j < cmh->depth; j++)
    {
        offset += cmh->width;
        estimate = min(estimate,
                       cmh->counts[depth][(hash31(cmh->hasha[depth][j], cmh->hashb[depth][j], item) % cmh->width) + offset]);
    }
    return estimate;
}

// compute a range sum:
// start at lowest level
// compute any estimates needed at each level
// work upwards
int CMH_Rangesum(CMH_type *cmh, int start, int end)
{
    int leftend, rightend, i, level, result, topend;

    topend = 1 << cmh->U;
    // end = min(topend, end);
    if ((end > topend) && (start == 0))
        return cmh->windowSize;
    end = min(topend, end);

    end += 1; // adjust for end effects
    result = 0;
    for (level = 0; level <= cmh->levels; level++)
    {
        if (start == end)
            break;
        if ((end - start + 1) < (1 << cmh->gran))
        {
            // at the highest level, avoid overcounting
            for (i = start; i < end; i++)
                result += CMH_count(cmh, level, i);
            break;
        }
        else
        {
            // figure out what needs to be done at each end
            leftend = (((start >> cmh->gran) + 1) << cmh->gran) - start;
            rightend = (end) - ((end >> cmh->gran) << cmh->gran);
            if ((leftend > 0) && (start < end))
                for (i = 0; i < leftend; i++)
                {
                    result += CMH_count(cmh, level, start + i);
                }
            if ((rightend > 0) && (start < end))
                for (i = 0; i < rightend; i++)
                {
                    result += CMH_count(cmh, level, end - i - 1);
                }
            start = start >> cmh->gran;
            if (leftend > 0)
                start++;
            end = end >> cmh->gran;
        }
    }
    return result;
}

// find a range starting from zero that adds up to sum
int CMH_FindRange(CMH_type *cmh, int sum)
{
    unsigned long low, high, mid = 0;
    int est;
    int i;

    low = 0;
    high = 1 << cmh->U;
    for (i = 0; i < cmh->U; i++)
    {
        mid = (low + high) / 2;
        est = CMH_Rangesum(cmh, 0, mid);
        if (est > sum)
            high = mid;
        else
            low = mid;
    }
    return mid;
}

// find a range starting from the right hand side that adds up to sum
int CMH_AltFindRange(CMH_type *cmh, int sum)
{
    unsigned long low, high, mid = 0, top;
    int i;
    int est;

    low = 0;
    top = 1 << cmh->U;
    high = top;
    for (i = 0; i < cmh->U; i++)
    {
        mid = (low + high) / 2;
        est = CMH_Rangesum(cmh, mid, top);
        if (est < sum)
            high = mid;
        else
            low = mid;
    }
    return mid;
}

// find a quantile by doing the appropriate range search
int CMH_Quantile(CMH_type *cmh, double frac)
{
    if (frac < 0 || cmh->count < cmh->windowSize)
        return 0;
    if (frac > 1)
        return 1 << cmh->U;
    int res = (CMH_FindRange(cmh, cmh->windowSize * frac) + CMH_AltFindRange(cmh, cmh->windowSize * (1 - frac))) / 2;
    // each result gives a lower/upper bound on the location of the quantile
    // with high probability, these will be close: only a small number of values
    // will be between the estimates.

    //printf("COUNT-MIN: %f-percentile = %d\n", frac, res);
    return res;
}
