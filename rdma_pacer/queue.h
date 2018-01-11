#ifndef QUEUE_H
#define QUEUE_H
// O(1) queue implementation

typedef struct {
    int deletions;
    int size;
    int max_size;
    int *array;
} Queue;

Queue *queue_init(int size);
void queue_push(Queue *q, double a);
double queue_pop(Queue *q);
void queue_free(Queue *q);

#endif