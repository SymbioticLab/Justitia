#ifndef VECTOR_H__
#define VECTOR_H__

typedef struct vector vector_t;

struct vector {
    void** data;
    int size;
    int count;
};

void vector_init(vector_t *);
int vector_count(vector_t *);
void vector_add(vector_t *, void*);
void vector_set(vector_t *, int, void*);
void *vector_get(vector_t *, int);
void vector_delete(vector_t *, int);
void vector_free(vector_t *);

#endif
