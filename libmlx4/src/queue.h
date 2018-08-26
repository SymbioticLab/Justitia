#ifndef QUEUE_H
#define QUEUE_H

#include <stdlib.h>
#include <stdio.h>
//#include <setjmp.h>

//jmp_buf queue_error;

//#define QUEUE_FULL_ERROR 1
//#define QUEUE_EMPTY_ERROR 2

// O(1) queue implementation
typedef struct {
    int read;
    int write;
    int size;
    int *array;
} Queue;

static inline Queue *queue_init(int size)
{
    Queue *q = calloc(1, sizeof(Queue));
    q->array = calloc(size + 1, sizeof(int));
    q->read = 0;
    q->write = 0;
    q->size = size + 1;
    return q;
}

static inline void queue_push(Queue *q, int a)
{
    if (q->read == (q->write + 1) % q->size)
        printf("QUEUE_FULL_ERROR\n");
    q->array[q->write] = a;
    q->write = (q->write + 1) % q->size;
}

static inline int queue_pop(Queue *q)
{
    if (q->read == q->write)
        printf("QUEUE_EMPTY_ERROR\n");
    int tmp = q->array[q->read];
    q->read = (q->read + 1) % q->size;
    return tmp;
}

static inline void queue_free(Queue *q)
{
    free(q->array);
    free(q);
}

#endif