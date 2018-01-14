#include "queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

jmp_buf queue_error;

#define QUEUE_FULL_ERROR 1
#define QUEUE_EMPTY_ERROR 2

Queue *queue_init(int size) {
    Queue *q = malloc(sizeof(Queue));
    q->array = malloc(sizeof(DATA_TYPE) * size);
    q->max_size = size;
    q->first = 0;
    q->last = 0;
    q->size = 0;
    return q;
}

void queue_push(Queue *q, DATA_TYPE a) {
    if (q->size >= q->max_size)
        longjmp(queue_error, QUEUE_FULL_ERROR);
    q->array[q->last] = a;
    q->size++;
    q->last = (q->last + 1) % q->max_size;
}

DATA_TYPE queue_pop(Queue *q) {
    if (q->size == 0)
        longjmp(queue_error, QUEUE_EMPTY_ERROR);
    DATA_TYPE tmp = q->array[q->first];
    q->first = (q->first + 1) % q->max_size;
    q->size--;
    return tmp;
}

void queue_free(Queue *q) {
    free(q->array);
    free(q);
}