#ifndef QUEUE_H
#define QUEUE_H
 
#define MAX_THREADS 128 // 64 on piazza


typedef struct TCB tcb_t;

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
} MinHeap_t;

void init_heap(MinHeap_t *heap);
void heap_insert(MinHeap_t *heap, tcb_t *thread);
tcb_t *heap_extract_min(MinHeap_t *heap);
int is_empty_heap(MinHeap_t *heap);


#endif
