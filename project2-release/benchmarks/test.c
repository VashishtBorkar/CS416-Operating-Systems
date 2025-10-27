#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "../thread-worker.h"
#include "../datastructures.h"

/* A scratch program template on which to call and
 * test thread-worker library functions as you implement
 * them.
 *
 * You can modify and use this program as much as possible.
 * This will not be graded.
 */

 #define NUM_THREADS 3

 void* thread_func(void* arg) {
    int id = *(int*)arg;
    for (int i = 0; i < 3; i++) {
        printf("Thread %d: iteration %d\n", id, i);
        worker_yield();
    }
    return NULL;
}

void test_threads() {
	printf("Testing thread creation and yielding:\n");
	int a = 1, b = 2;
    worker_t t1, t2;

    printf("Main: creating threads...\n");
    worker_create(&t1, NULL, thread_func, &a);
    worker_create(&t2, NULL, thread_func, &b);

    printf("Main: joining threads...\n");
    worker_join(t1, NULL);
    worker_join(t2, NULL);

    printf("Main: all threads finished!\n");
}

worker_mutex_t lock;
int counter = 0;

void* increment(void* arg) {
    for (int i = 0; i < 5; i++) {
        worker_mutex_lock(&lock);
        printf("Thread %ld incrementing counter to %d\n", (long)arg, ++counter);
        worker_mutex_unlock(&lock);
        worker_yield();
    }
    return NULL;
}

void test_mutex() {
	printf("\nTesting mutex locks:\n");
	worker_t t1, t2, t3;
    counter = 0;

    worker_mutex_init(&lock, NULL);

    worker_create(&t1, NULL, increment, (void*)1);
    worker_create(&t2, NULL, increment, (void*)2);
    worker_create(&t3, NULL, increment, (void*)3);

    worker_join(t1, NULL);
    worker_join(t2, NULL);
    worker_join(t3, NULL);

    printf("Final counter value: %d\n\n", counter);
}

void* sched_thread_func(void* arg) {
    int id = *(int*)arg;
    printf("Thread %d running.\n", id);
    for (int i = 0; i < 5; i++) {
        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < 10000000; j++) {
                // waiting
            }
        }
        printf("Thread %d finished iteration %d.\n", id, i + 1);
    }
    printf("Thread %d finished.\n", id);
    return NULL;
}

void simple_schedule_test() {
    printf("Testing Scheduling Policy:\n");
    worker_t threads[NUM_THREADS];
    int ids[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        ids[i] = i + 1;
        worker_create(&threads[i], NULL, sched_thread_func, &ids[i]);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        worker_join(threads[i], NULL);
    }

    printf("All threads finished!\n");
    return 0;
}
/*

void test_heap_sched(int num_threads) {
    MinHeap_t heap;
    init_heap(&heap);

    // Create some mock TCBs

    tcb_t* threads[num_threads];
    for (int i = 0; i < num_threads; i++) {
        threads[i] = malloc(sizeof(tcb_t));
        threads[i]->id = i+1;
        threads[i]->elapsed_quanta = 0;
        heap_push(&heap, threads[i]);
    } 

    printf("Initial Heap: \n");
    print_heap(&heap);

    for (int i = 0; i < num_threads * 2; i++) {
        printf("Removing Min...\n");
        tcb_t* min = heap_pop(&heap);
        printf("MIN: T%d(q=%d)\n", min->id, min->elapsed_quanta);

        printf("Incrementing min and adding back to heap\n");
        min->elapsed_quanta++;
        heap_push(&heap, min);
        print_heap(&heap);
    }

    printf("Heap test completed\n\n");
}

void insert_max_test(int num_threads) {
    MinHeap_t heap;
    init_heap(&heap);

    // Create some mock TCBs

    tcb_t* threads[num_threads];
    for (int i = 0; i < num_threads; i++) {
        threads[i] = malloc(sizeof(tcb_t));
        threads[i]->id = i+1;
        threads[i]->elapsed_quanta = 0;
        heap_push(&heap, threads[i]);
    } 

    printf("Initial Heap: \n");
    print_heap(&heap);
    
    printf("Inserting big thread\n");
    tcb_t* big_thread = malloc(sizeof(tcb_t));
    big_thread->id = 10;
    big_thread->elapsed_quanta = 1;
    heap_push(&heap, big_thread);

    printf("Heap after insert: \n");
    print_heap(&heap);
   
    printf("Heap test completed\n\n");
}

void test_heap_basic(int num_threads) {
    MinHeap_t heap;
    init_heap(&heap);

    // Create some mock TCBs

    tcb_t* threads[num_threads];
    for (int i = 0; i < num_threads; i++) {
        threads[i] = malloc(sizeof(tcb_t));
        threads[i]->id = i;
        threads[i]->elapsed_quanta = (num_threads-i-1) * 10;
        heap_push(&heap, threads[i]);
    }

    printf("Initial Heap: \n");
    print_heap(&heap);

    for (int i = 0; i < num_threads; i++) {
        printf("Removing Min...\n");
        tcb_t* min = heap_pop(&heap);
        printf("MIN: T%d(q=%d)\n", min->id, min->elapsed_quanta);
        free(min);
    }

    printf("Heap test completed\n\n");
}
*/


int main(int argc, char **argv) {

	/* Implement HERE */
    simple_schedule_test();
    print_app_stats();

	return 0;
}
