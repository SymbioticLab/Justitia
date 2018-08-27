#include  "massdal.h"
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>

// simple timing routines for testing
// these use global variables, so they will not interleave well

long secs, usecs;
struct timeval tt;

void StartTheClock()
{
  gettimeofday(&tt,NULL);
  secs=tt.tv_sec;
  usecs=tt.tv_usec;
}

long StopTheClock()
{
  gettimeofday(&tt,NULL);
  secs=tt.tv_sec-secs;
  usecs=tt.tv_usec-usecs;
  return (long) 1000*secs+(usecs/1000);
}

#define SWAP(a,b) temp=(a);(a)=(b);(b)=temp;
// defined for the purposes of the median finding procedures below


     /* Routine from Numerical Recipes --
	find the k'th element out of n that are in arr (assumed 
	indexed from 1 to n)
     */
#define MEDIAN \
\
  int i, ir, j, mid, l;\
\
  l=1; \
  ir=n; \
  for (;;) { \
    if (ir <= l+1) { \
      if (ir == l+1 && arr[ir] < arr[l]) { \
	SWAP(arr[l],arr[ir]) \
	  } \
      return arr[k]; \
    } \
    else \
      { \
	mid=(l+ir) >> 1; \
	SWAP(arr[mid],arr[l+1]) \
	  if (arr[l] > arr[ir]) { \
	    SWAP(arr[l],arr[ir]) \
	      } \
	if (arr[l+1] > arr[ir]) { \
	  SWAP(arr[l+1],arr[ir]) \
	    } \
	if (arr[l] > arr[l+1]) { \
	  SWAP(arr[l],arr[l+1]) \
	    } \
	i=l+1; \
	j=ir; \
	a=arr[l+1]; \
	for (;;) { \
	  do i++; while (arr[i] < a);\
	  do j--; while (arr[j] > a);\
	  if (j < i) break;\
	  SWAP(arr[i],arr[j])\
	    }\
	arr[l+1]=arr[j];\
	arr[j]=a;\
	if (j >= k) ir=j-1;\
	if (j <= k) l=i;\
      }\
  }\


int MedSelect(int k, int n, int arr[]) {
  int a, temp;

  MEDIAN
    }

long long LLMedSelect(int k, int n, long long arr[]) {
  long long a, temp;

  MEDIAN
    }

double DMedSelect(int k, int n, double arr[]) {
  double a, temp;

  MEDIAN
    }

long LMedSelect(int k, int n, long arr[]) {
  long a, temp;

  MEDIAN
    }


void CheckMemory(void * ptr)
{
  if (!ptr) 
    {    
      fprintf(stderr,"Out of memory error\n");
      exit(-1);
    }
}
