#include <stdio.h>
#include <stdlib.h>
#include "thread-worker.h"
#include "datastructures.h"

// Queue
void init_queue(Queue_t *queue) {
    if (!queue) {
        fprintf(stderr, "init_queue: queue is NULL\n");
        return;
    }
    queue->head = NULL;
    queue->tail = NULL;
}

void enqueue(Queue_t *queue, tcb_t* data) {
	if (!queue) {
		fprintf(stderr, "enqueue: queue not initialized\n");
		return;
	}
	Node_t *node = malloc(sizeof(Node_t));
	node->data = data;
	node->next = NULL;

	if (queue->tail) {
		queue->tail->next = node;
	} else {
		queue->head = node;
	}

	queue->tail = node;
}

tcb_t *dequeue(Queue_t *queue) {
    if (!queue) {
        fprintf(stderr, "dequeue: queue not initialized\n");
        return NULL;
    }

    if (!queue->head) {
        return NULL;
    }

    Node_t *node = queue->head;
    tcb_t *data = node->data;
    queue->head = node->next;

    if (!queue->head) {
        queue->tail = NULL;
    }
    
    free(node);
    return data;
}

int is_empty_queue(Queue_t *queue) {
    if (!queue) {
        return 1; // NULL queue is empty
    }
    return queue->head == NULL;
}


// Heap
void init_heap(MinHeap_t *heap) {
    if (!heap) {
        fprintf(stderr, "init_heap: heap is NULL\n");
        return;
    }
    heap->size = 0;
}

void heap_insert(MinHeap_t *heap, tcb_t *thread) {
    if (!heap) {
        fprintf(stderr, "heap_insert: heap is NULL\n");
        return;

    } 

    if (heap->size >= MAX_THREADS) {
        fprintf(stderr, "heap_insert: heap is full\n");
        return;
    }

    int i = heap->size;
    heap->size++;
    heap->threads[i] = thread;

    while (i > 0) {
        int smallest = (i - 1) / 2;

        #if defined(CFS)
            if (heap->threads[smallest]->vruntime <= heap->threads[i]->vruntime) {
                break;
            }
            
        #elif defined(PSJF)
            if (heap->threads[smallest]->elapsed_quanta <= heap->threads[i]->elapsed_quanta){
                break;
            }
        #endif
        
        // Less than smallest swap
        tcb_t *temp = heap->threads[i];
        heap->threads[i] = heap->threads[smallest];
        heap->threads[smallest] = temp;
        i = smallest;
    }
}

tcb_t *heap_extract_min(MinHeap_t *heap) {
    if (!heap) {
        fprintf(stderr, "heap_extract_min: heap is NULL\n");
        return NULL;
        
    }

    if (heap->size == 0) {
        return NULL;
    }

    tcb_t *min = heap->threads[0];
    heap->size--;
    heap->threads[0] = heap->threads[heap->size];

    int i = 0;
    while (1) {
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        int smallest = i;
        
        #if defined(CFS)
            if (left < heap->size && heap->threads[left]->vruntime < heap->threads[smallest]->vruntime) {
                smallest = left;
            }
            if (right < heap->size && heap->threads[right]->vruntime < heap->threads[smallest]->vruntime) {
                smallest = right;
            }
        #elif defined(PSJF)
            if (left < heap->size && heap->threads[left]->elapsed_quanta < heap->threads[smallest]->elapsed_quanta) {
                smallest = left;
            }
            if (right < heap->size && heap->threads[right]->elapsed_quanta < heap->threads[smallest]->elapsed_quanta) {
                smallest = right;
            }
        #endif


        if (smallest == i) { // not smaller than children
            break;
        }

        tcb_t *temp = heap->threads[i];
        heap->threads[i] = heap->threads[smallest];
        heap->threads[smallest] = temp;
        i = smallest;
    }

    return min;
}

int is_empty_heap(MinHeap_t *heap) {
    if (!heap) {
        return 1; // NULL heap is empty
    }

    return heap->size == 0;
}
