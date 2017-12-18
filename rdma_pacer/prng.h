// Probabilistic Random Number Generators
// Collected from various sources by Graham Cormode, 2000-2003
// 

#include <math.h>

#ifndef _PRNG

#define MOD 2147483647
#define HL 31

extern long hash31(long long, long long, long long);
extern long fourwise(long long, long long, long long, long long, long long);

#define KK  17
#define NTAB 32

typedef struct prng_type{
    int usenric; // which prng to use
    float scale;             /* 2^(- integer size) */
    long floatidum;
    long intidum; // needed to keep track of where we are in the 
    // nric random number generators
    long iy;
    long iv[NTAB];
    /* global variables */
    unsigned long randbuffer[KK];  /* history buffer */
    int r_p1, r_p2;          /* indexes into history buffer */
    int iset;
    double gset;
} prng_type;

#define _PRNG 1

#endif

extern long prng_int(prng_type *);
extern float prng_float(prng_type *);
extern prng_type * prng_Init(long, int);
extern void prng_Destroy(prng_type * prng);
void prng_Reseed(prng_type *, long);

//extern long double zipf(double, long) ;
extern double fastzipf(double, long, double, prng_type *);
extern double zeta(long, double);
extern double prng_normal(prng_type * prng);
extern double prng_stable(prng_type * prng, double);

//extern double stable(double); // stable distributions 
//extern double stabledevd(float) ;
//extern long double stabledevl(float) ;
//extern double altstab(double); 

