// File:	worker_t.h

// List all group member's name:
// username of iLab:
// iLab Server:

#ifndef WORKER_T_H
#define WORKER_T_H

#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, you have to comment the USE_WORKERS macro */
#define USE_WORKERS 1

/* Targeted latency in milliseconds */
#define TARGET_LATENCY   20  

/* Minimum scheduling granularity in milliseconds */
#define MIN_SCHED_GRN    1

/* Time slice quantum in milliseconds */
#define QUANTUM 10

#define MAX_MLFQ_LEVELS 4
#define S_PERIOD 100

#define MAX_THREADS 128 // 64 on piazza

/* include lib header files that you need here: */
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ucontext.h>
#include <signal.h>
#include <sys/time.h>
#include <stdatomic.h>
#include <time.h>

#include "datastructures.h"

typedef uint worker_t;
enum thread_state { READY, RUNNING, BLOCKED, TERMINATED };

typedef struct TCB {
	/* add important states in a thread control block */
	// thread Id
	// thread status
	// thread context
	// thread stack
	// thread priority
	// And more ...
	worker_t id;
	ucontext_t context;
	void* stack;
	void* retval;
	enum thread_state state;

	// stats
	struct timespec arrival_time;
	struct timespec start_time;
	struct timespec finish_time;
	int has_started;

	// Scheduling
	long elapsed_quanta;
	int priority; // MLFQ level
	int time_slice;
	int time_used;
	long vruntime;
	struct timespec last_start;
	int cfs_timeslice;
	
	// Synchronization
	// worker_t waiting_for; // thread id this thread is waiting for
	struct TCB *waiting_thread; // thread waiting on this thread
	int joined;

} tcb_t; 

/* mutex struct definition */
typedef struct worker_mutex_t {
	/* add something here */
	atomic_flag locked;
	worker_t owner;
	tcb_t *owner_tcb;
	Queue_t wait_queue;

} worker_mutex_t;

/* define your data structures here: */
// Feel free to add your own auxiliary data structures (linked list or queue etc...)

// YOUR CODE HERE


/* Function Declarations: */

/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, void
    *(*function)(void*), void * arg);

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield();

/* terminate a thread */
void worker_exit(void *value_ptr);

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr);

/* initial the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t
    *mutexattr);

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex);

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex);

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex);


/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void);

#ifdef USE_WORKERS
#define pthread_t worker_t
#define pthread_mutex_t worker_mutex_t
#define pthread_create worker_create
#define pthread_exit worker_exit
#define pthread_join worker_join
#define pthread_mutex_init worker_mutex_init
#define pthread_mutex_lock worker_mutex_lock
#define pthread_mutex_unlock worker_mutex_unlock
#define pthread_mutex_destroy worker_mutex_destroy
#endif
#endif
