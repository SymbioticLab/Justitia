#include "queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

jmp_buf queue_error;

#define QUEUE_FULL_ERROR 1
#define QUEUE_EMPTY_ERROR 2

Queue *queue_init(int size)
{
    Queue *q = calloc(1, sizeof(Queue));
    q->array = calloc(size + 1, sizeof(int));
    q->read = 0;
    q->write = 0;
    q->size = size + 1;
    return q;
}

void queue_push(Queue *q, int a)
{
    if (q->read == (q->write + 1) % q->size)
        longjmp(queue_error, QUEUE_FULL_ERROR);
    q->array[q->write] = a;
    q->write = (q->write + 1) % q->size;
}

int queue_pop(Queue *q)
{
    if (q->read == q->write)
        longjmp(queue_error, QUEUE_EMPTY_ERROR);
    int tmp = q->array[q->read];
    q->read = (q->read + 1) % q->size;
    return tmp;
}

void queue_free(Queue *q)
{
    free(q->array);
    free(q);
}