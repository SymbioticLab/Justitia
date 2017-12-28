// Two different structures: 
//   1 -- The basic CM Sketch (removed by Yue Tan)
//   2 -- The hierarchical CM Sketch: with log n levels, for range sums etc. 

#define min(x,y)	((x) < (y) ? (x) : (y))
#define max(x,y)	((x) > (y) ? (x) : (y))

typedef struct CMH_type{
    long long count;
    int max; // maximum observation ever seen
    int U; // size of the universe in bits
    int gran; // granularity: eg 1, 4 or 8 bits
    int levels; // function of U and gran
    int freelim; // up to which level to keep exact counts
    int depth;
    int width;
    int ** counts;
    unsigned int **hasha, * *hashb;
} CMH_type;

extern CMH_type * CMH_Init(int, int, int, int);
// extern CMH_type * CMH_Copy(CMH_type *);
extern void CMH_Destroy(CMH_type *);
extern int CMH_Size(CMH_type *);

extern int CMH_Update(CMH_type *, unsigned int, int);

// extern int CMH_Rangesum(CMH_type *, int, int);
// extern int CMH_FindRange(CMH_type * cmh, long long);
// extern int CMH_AltFindRange(CMH_type * cmh, long long);
extern int CMH_Quantile(CMH_type *cmh, double);
