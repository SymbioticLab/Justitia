#include "queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

jmp_buf queue_error;

#define first(q) q->deletions % q->max_size
#define last(q) (q->deletions + (q->size - 1)) % q->max_size
#define QUEUE_FULL_ERROR 1
#define QUEUE_EMPTY_ERROR 2

Queue *queue_init(int size) {
    Queue *q = malloc(sizeof(Queue));
    q->array = malloc(sizeof(double) * size);
    q->max_size = size;
    q->deletions = q->size = 0;
    return q;
}

void queue_push(Queue *q, double a) {
    if ((q->size - q->deletions) >= q->max_size)
        longjmp(queue_error, QUEUE_FULL_ERROR);
    q->array[last(q)] = a;
    q->size += 1;
}

double queue_pop(Queue *q) {
    if ((q->size - q->deletions) <= 0)
        longjmp(queue_error, QUEUE_EMPTY_ERROR);
    double tmp = q->array[first(q)];
    q->deletions += 1;
    return tmp;
}

void queue_free(Queue *q) {
    free(q->array);
    free(q);
}