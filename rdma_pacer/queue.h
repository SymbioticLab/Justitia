#ifndef QUEUE_H
#define QUEUE_H
// O(1) queue implementation
#define DATA_TYPE int
typedef struct {
    int first;
    int last;
    int size;
    int max_size;
    DATA_TYPE *array;
} Queue;

Queue *queue_init(int size);
void queue_push(Queue *q, DATA_TYPE a);
DATA_TYPE queue_pop(Queue *q);
void queue_free(Queue *q);

#endif