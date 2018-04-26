#ifndef LIST_H
#define LIST_H

#include <stdio.h>
#include <stdlib.h>

typedef struct Node node_t;
typedef struct List list_t;

/* The node is used to store a pointer to an object, not the actual data*/
struct Node
{
    void *data;
    struct Node *next; // Pointer to next node in DLL
    struct Node *prev; // Pointer to previous node in DLL
};

struct List
{
    node_t *head;
    size_t length;
};

void list_init(list_t *list)
{
    list->head = NULL;
    list->length = 0;
}

/* push at the head */
void list_push(list_t *list, void *data)
{
    node_t* new_node = calloc(1, sizeof(node_t));

    new_node->data = data;

    new_node->prev = NULL;
	new_node->next = list->head; 

    if (list->head != NULL)
	    list->head->prev = new_node;

	list->head = new_node;
    list->length++;
}

void list_remove(list_t *list, node_t *del)
{
    if (!del)
        printf("list_remove: del is NULL!!!!!!!\n");
    if (list->head == NULL || del == NULL)
        return;
 
    /* check if need to update head */
    if (list->head == del)
        list->head = del->next;

    /* update next; check for tail */
    if (del->next != NULL)
        del->next->prev = del->prev;
 
    /* update prev; check for head */
    if (del->prev != NULL)
        del->prev->next = del->next;     

    free(del);
    list->length--;
}

#endif