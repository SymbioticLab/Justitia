#ifndef PRIORITY_QUEUE_H__
#define PRIORITY_QUEUE_H__

typedef struct {
    int priority;
    void *data;
} node_t;
 
typedef struct {
    node_t *nodes;
    int len;
    int size;
} heap_t;

void pq_push (heap_t *, int, void *);
void *pq_pop (heap_t *);

#endif
