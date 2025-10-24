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

void* rr_thread_func(void* arg) {
    int id = *(int*)arg;
    printf("Thread %d running.\n", id);
    for (int i = 0; i < 5; i++) {
        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < 1000000; j++) {
                // waiting
            }
        }
        printf("Thread %d finished iteration %d.\n", id, i + 1);
    }
    printf("Thread %d finished.\n", id);
    return NULL;
}

void test_rr() {
    printf("Testing Round Robin Scheduling:\n");
    worker_t threads[NUM_THREADS];
    int ids[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        ids[i] = i + 1;
        worker_create(&threads[i], NULL, rr_thread_func, &ids[i]);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        worker_join(threads[i], NULL);
    }

    printf("All RR threads finished!\n");
    return 0;
}

int main(int argc, char **argv) {

	/* Implement HERE */
	test_threads();
	test_mutex();
    test_rr();

	return 0;
}
