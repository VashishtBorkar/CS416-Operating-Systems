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
void init_heap(MinHeap_t* heap, heap_cmp_t cmp) {
    if (!heap) {
        fprintf(stderr, "init_heap: heap is NULL\n");
        return;
    }
    heap->cmp = cmp;
    heap->size = 0;
    for (int i = 0; i < MAX_THREADS; i++) {
        heap->threads[i] = NULL;
    }

}

void heap_push(MinHeap_t *heap, tcb_t *thread) {
    int i = heap->size;
    heap->threads[i] = thread;
    heap->size++;

    while (i > 0) {
        int parent = (i - 1) / 2;

        if (heap->cmp(heap->threads[i], heap->threads[parent]) < 0) { 
            // child less than parent swap
            tcb_t *temp = heap->threads[i];
            heap->threads[i] = heap->threads[parent];
            heap->threads[parent] = temp;

            i = parent;
        } else {
            break;
        }
    }
}

tcb_t* heap_pop(MinHeap_t *heap) {

    tcb_t *min = heap->threads[0];

    heap->size--;
    heap->threads[0] = heap->threads[heap->size];

    int i = 0;
    while (1) {
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        int smallest = i;

        if (left < heap->size && heap->cmp(heap->threads[left], heap->threads[smallest]) < 0) {
            smallest = left;
        }

        if (right < heap->size && heap->cmp(heap->threads[right], heap->threads[smallest]) < 0) {
            smallest = right;
        }

        if (smallest == i) {
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

void print_heap(MinHeap_t *heap) {
    
    printf("[");
    for (int i = 0; i < heap->size; i++) {
        printf("(Thread %d: %d) ", heap->threads[i]->id, heap->threads[i]->elapsed_quanta);
    }
    printf("] size: %d\n", heap->size);
}

