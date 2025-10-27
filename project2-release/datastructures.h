#ifndef DATASTRUCTURES_H
#define DATASTRUCTURES_H
 
#define MAX_THREADS 128 // 64 on piazza


typedef struct TCB tcb_t;
typedef int (*heap_cmp_t)(tcb_t *a, tcb_t *b);

// Queue
typedef struct Node_t {
    tcb_t *data;
    struct Node_t *next;
} Node_t;

typedef struct Queue_t {
    Node_t *head;
    Node_t *tail;
} Queue_t;

void init_queue(Queue_t *queue);
void enqueue(Queue_t *queue, tcb_t *data);
tcb_t *dequeue(Queue_t *queue);
int is_empty_queue(Queue_t *queue);


// Heap
typedef struct MinHeap_t {
    tcb_t *threads[MAX_THREADS];
    int size;
    heap_cmp_t cmp;
} MinHeap_t;

void init_heap(MinHeap_t *heap, heap_cmp_t cmp);
void heap_push(MinHeap_t *heap, tcb_t *thread);
tcb_t *heap_pop(MinHeap_t *heap);
int is_empty_heap(MinHeap_t *heap);


#endif
