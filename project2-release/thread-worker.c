// File:	thread-worker.c
// List all group member's name: Vashisht Borkar, Saransh Batwe
// username of iLab: vb471, ssb209
// iLab Server: ilab1.cs.rutgers.edu

#include "thread-worker.h"
#include "datastructures.h"
#include <time.h>

//Global counter for total context switches and 
//average turn around and response time
long tot_cntx_switches=0;
double avg_turn_time=0;
double avg_resp_time=0;
long total_turnaround_time=0;
long total_response_time=0;
long completed_threads=0;

// INITAILIZE ALL YOUR OTHER VARIABLES HERE
static tcb_t *main_tcb = NULL; // main thread TCB
static ucontext_t scheduler_context; // scheduler context
static tcb_t *running_tcb = NULL;

// Global thread table
static tcb_t* thread_table[MAX_THREADS];
static int thread_count = 0;

// Scheduler data structures
static Queue_t rr_queue; 
static MinHeap_t psjf_heap;
Queue_t mlfq_levels[MAX_MLFQ_LEVELS];
static MinHeap_t cfs_heap;

// Forward declaration
static void schedule();

// Timer and signal handling
static struct itimerval timer;
static struct sigaction sa;
static int global_ticks = 0;


// Thread table functions
tcb_t *find_tcb_by_id(worker_t id) {
    for (int i = 0; i < MAX_THREADS; i++) {
		if (thread_table[i] != NULL && thread_table[i]->id == id) {
			return thread_table[i];
		}
	}

	return NULL; // not found 
}

int add_thread_to_table(tcb_t *thread) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (thread_table[i] == NULL) {
            thread_table[i] = thread;
            thread_count++;
            return i;
        }
    }
    return -1; // Table full
}

void remove_thread_from_table(worker_t id) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (thread_table[i] != NULL && thread_table[i]->id == id) {
			thread_table[i] = NULL;
			thread_count--;
			return;
        }
    }
}

int has_blocked_threads() {
	for (int i = 0; i < MAX_THREADS; i++) {
		if (thread_table[i] != NULL && thread_table[i]->state == BLOCKED) {
			return 1;
		}
	}
	return 0;
}

void cleanup_thread(tcb_t *thread) {
	remove_thread_from_table(thread->id);
	if (thread->stack) {
		free(thread->stack);
	}
	free(thread);
}


// Timer functions
void timer_handler(int signum) {
	if (running_tcb) {
		//printf("Switching to scheduler\n");
		if(swapcontext(&running_tcb->context, &scheduler_context) == -1) {
				perror("swapcontext in timer_handler");
				exit(1);
		}
	}
}

void set_timer(long time_slice) {
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = time_slice;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = time_slice;

    if (setitimer(ITIMER_PROF, &timer, NULL) == -1) {
        perror("setitimer");
        exit(1);
    }
}

void block_timer_signal() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPROF);
    sigprocmask(SIG_BLOCK, &set, NULL);
}

void unblock_timer_signal() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPROF);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
}

long time_diff_microseconds (struct timespec start, struct timespec end) {
	return (end.tv_sec - start.tv_sec) * 1000000L + (end.tv_nsec - start.tv_nsec) / 1000L;
}


// Heap comparators
int psjf_cmp(tcb_t *a, tcb_t *b) {
    if (a->elapsed_quanta != b->elapsed_quanta) {
        return a->elapsed_quanta - b->elapsed_quanta;
	}
    return a->id - b->id; // use id for tiebreaker
}

int cfs_cmp(tcb_t *a, tcb_t *b) {
    if (a->vruntime != b->vruntime) {
        return a->vruntime - b->vruntime;
	}
    return a->id - b->id; // use id for tiebreaker
}


// Main thread set up
void init_timer() {
	memset(&timer, 0, sizeof(timer));
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = &timer_handler;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	
    sigaction(SIGPROF, &sa, NULL);

    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = QUANTUM * 1000; // how often it repeats in micro seconds

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = QUANTUM * 1000;    // initial delay before first fire in micro seconds

    if (setitimer(ITIMER_PROF, &timer, NULL) == -1) {
        perror("setitimer");
        exit(1);
    }
}

void init_scheduler() {
	static int scheduler_initialized = 0;
	if (scheduler_initialized) {
		return; // already initialized
	}
	scheduler_initialized = 1;
	
	// initialize data structures
	init_queue(&rr_queue);
	init_heap(&psjf_heap, psjf_cmp);
	init_heap(&cfs_heap, cfs_cmp);
	for (int i = 0; i < MAX_MLFQ_LEVELS; i++) {
		init_queue(&mlfq_levels[i]);
	}

	if (getcontext(&scheduler_context) == -1) {
		perror("getcontext for scheduler");
		exit(1);
	}

	char *stack = malloc(SIGSTKSZ);
	if (!stack) {
		perror("malloc for scheduler stack");
		exit(1);
	}

	scheduler_context.uc_stack.ss_sp = stack;
	scheduler_context.uc_stack.ss_size = SIGSTKSZ;
	scheduler_context.uc_stack.ss_flags = 0;
	scheduler_context.uc_link = NULL;

	makecontext(&scheduler_context, (void (*)(void))schedule, 0);
}

void init_thread_table() {
	for (int i = 0; i < MAX_THREADS; i++) {
		thread_table[i] = NULL;
	}
	thread_count = 0;
}

void init_main_thread() {
	if (main_tcb != NULL) {
		return; // already initialized
	}

	init_scheduler();
    init_timer();
	init_thread_table();

	main_tcb = malloc(sizeof(tcb_t));
	if (!main_tcb) {
		perror("malloc for main TCB failed");
		exit(1);
	}

	if (getcontext(&main_tcb->context) == -1) {
		perror("getcontext for main thread");
		free(main_tcb);
		exit(1);
	}

	main_tcb->id = 0; // main thread id
	main_tcb->state = RUNNING;
	main_tcb->stack = NULL; // main thread uses existing stack
	main_tcb->retval = NULL;
	main_tcb->has_started = 1;
	// main_tcb->waiting_for = -1;

	running_tcb = main_tcb;
}


// Worker thread functions
static void worker_start(void *(*func)(void *), void *arg) {
	/* Helper function for worker_create */
    void *ret = func(arg);  // run the user's function
    worker_exit(ret);       // call our thread cleanup logic
}

int worker_create(worker_t * thread, pthread_attr_t * attr, 
                      void *(*function)(void*), void * arg) {

	// - create Thread Control Block (TCB)
	// - create and initialize the context of this worker thread
	// - allocate space of stack for this thread to run
	// after everything is set, push this thread into run queue and 
	// - make it ready for the execution.

	// YOUR CODE HERE

	if (!main_tcb) {
		init_main_thread();
	}
	
	// Create TCB
	tcb_t *new_tcb = malloc(sizeof(tcb_t));
	if (!new_tcb) {
		perror("malloc for TCB failed");
		return -1;
	}

	// Increment thread counter 
	static int next_thread_id = 1;
	new_tcb->id = next_thread_id++;
	thread_count++;

	// Set arrival time
	clock_gettime(CLOCK_MONOTONIC, &new_tcb->arrival_time);
	new_tcb->has_started = 0;

	// Allocate stack
	new_tcb->stack = malloc(SIGSTKSZ);
	if (!new_tcb->stack) {
		perror("malloc stack");
		free(new_tcb);
		return -1;
	}

	// Get context
	if (getcontext(&new_tcb->context) == -1) {
		perror("getcontext");
		free(new_tcb);
		return -1;
	}

	// Set context data
	new_tcb->context.uc_stack.ss_sp = new_tcb->stack;
	new_tcb->context.uc_stack.ss_size = SIGSTKSZ;
	new_tcb->context.uc_stack.ss_flags = 0;
	new_tcb->context.uc_link = &scheduler_context;

	makecontext(&new_tcb->context, (void (*)(void))worker_start, 2, function, arg);

	// TCB Fields
	new_tcb->state = READY;
    new_tcb->retval = NULL;
	new_tcb->joined = 0;

	// Scheduling fields
	new_tcb->elapsed_quanta = 0;
	new_tcb->priority = 0;
	new_tcb->time_slice = 1;
	new_tcb->time_used = 0;
	new_tcb->vruntime = 0;

	add_thread_to_table(new_tcb);
	add_to_scheduler(new_tcb);

	*thread = new_tcb->id;

    return 0;
}

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield() {
	
	// - change worker thread's state from Running to Ready
	// - save context of this thread to its thread control block
	// - switch from thread context to scheduler context

	// YOUR CODE HERE
	tcb_t *current = running_tcb;
	if(!current) {
		perror("No running thread");
		return -1;
	}

	current->state = READY;

	if(swapcontext(&current->context, &scheduler_context) == -1) {
		perror("swapcontext to scheduler");
		return -1;
	}

	return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr) {
	// - de-allocate any dynamic memory created when starting this thread
	// YOUR CODE HERE

	if (!running_tcb) {
		perror("No running thread");
		exit(1);
	}

	// dont exit main thread if there are still threads
	// if (current == main_tcb) {
	// 	while (thread_count > 1 || has_blocked_threads()) {
    //         swapcontext(&main_tcb->context, &scheduler_context);
    //     }
		
	// 	return;
	// }

	running_tcb->state = TERMINATED;
	running_tcb->retval = value_ptr;

	// Calculate stats
	clock_gettime(CLOCK_MONOTONIC, &running_tcb->finish_time);
	long turnaround_time = time_diff_microseconds(running_tcb->arrival_time, running_tcb->finish_time);
	long response_time = time_diff_microseconds(running_tcb->arrival_time, running_tcb->start_time);

	thread_count--;
	completed_threads++;

	total_turnaround_time += turnaround_time;
	total_response_time += response_time;

	avg_turn_time = (double) total_turnaround_time / completed_threads;
	avg_resp_time = (double) total_response_time / completed_threads;

	// wake up waiting thread
	if (running_tcb->waiting_thread != NULL) {
		tcb_t *waiting = running_tcb->waiting_thread;
		waiting->state = READY;
		add_to_scheduler(running_tcb->waiting_thread);
		running_tcb->waiting_thread = NULL;
	}

	// setcontext(&scheduler_context);
	swapcontext(&running_tcb->context, &scheduler_context);
	// setcontext(&scheduler_context);
}

/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr) {
	
	// - wait for a specific thread to terminate
	// - de-allocate any dynamic memory created by the joining thread
  
	// YOUR CODE HERE
	tcb_t *target = find_tcb_by_id(thread);
	
	if (!target) {
		fprintf(stderr, "worker_join: no such thread %u\n", thread);
		return -1;
	}

	if (running_tcb->id == thread) {
		fprintf(stderr, "worker_join: thread cannot join itself\n");
		return -1;
	}

	// Prevent multiple joins
	if (target->joined) {
        fprintf(stderr, "worker_join: thread already joined\n");
        return -1;
    }
	
	target->joined = 1;

	// If already terminated return
	if (target->state == TERMINATED) {
		printf("The thread you are trying to join is already terminated\n");
		if (value_ptr) {
			*value_ptr = target->retval;
		}

		cleanup_thread(target);
		return 0;
		// add_to_scheduler(running_tcb);
		// set_context(&scheduler_context);
	}

	// Otherwise block current and join
	target->joined = 1;

	running_tcb->state = BLOCKED;
	target->waiting_thread = running_tcb;

	swapcontext(&running_tcb->context, &scheduler_context);

	if (value_ptr) {
        *value_ptr = target->retval;
    }

	cleanup_thread(target);

	return 0;
}


// Mutex functions
/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, 
                          const pthread_mutexattr_t *mutexattr) {
	//- initialize data structures for this mutex
	if (!mutex) {
		fprintf(stderr, "worker_mutex_init: mutex is NULL\n");
		return -1;
	}

	atomic_flag_clear(&mutex->locked);
	mutex->owner_tcb = NULL;
	init_queue(&mutex->wait_queue);
	return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex) {

        // - use the built-in test-and-set atomic function to test the mutex
        // - if the mutex is acquired successfully, enter the critical section
        // - if acquiring mutex fails, push current thread into block list and
        // context switch to the scheduler thread

	// YOUR CODE HERE
	if (!mutex) {
		fprintf(stderr, "worker_mutex_lock: mutex is NULL\n");
		return -1;
	}

	while (atomic_flag_test_and_set(&mutex->locked)) {
		if (running_tcb->state != BLOCKED) {
			running_tcb->state = BLOCKED;
			enqueue(&mutex->wait_queue, running_tcb);
		}
		
		if (swapcontext(&running_tcb->context, &scheduler_context) == -1) {
			perror("swapcontext to scheduler");
			return -1;
		}
	}
	
	// acquired the lock		
	mutex->owner_tcb = running_tcb;
	return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {
	// - release mutex and make it available again. 
	// - put threads in block list to run queue 
	// so that they could compete for mutex later.

	// YOUR CODE HERE
	if (!mutex) {
		fprintf(stderr, "worker_mutex_unlock: mutex is NULL\n");
		return -1;
	}

	if (mutex->owner_tcb != running_tcb) {
		fprintf(stderr, "worker_mutex_unlock: current thread does not own the mutex\n");
		return -1;
	}
	
	mutex->owner_tcb = NULL;
	atomic_flag_clear(&mutex->locked);

	// Wake up waiting thread
	if (!is_empty_queue(&mutex->wait_queue)) {
		tcb_t *wait_thread = dequeue(&mutex->wait_queue);
        wait_thread->state = READY;
        add_to_scheduler(wait_thread);
	} 
	
	return 0;
}

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex) {
	// - de-allocate dynamic memory created in worker_mutex_init
	if (!mutex) {
		fprintf(stderr, "worker_mutex_destroy: mutex is NULL\n");
		return -1;
	}

	if (atomic_flag_test_and_set(&mutex->locked)) {
		fprintf(stderr, "worker_mutex_destroy: mutex is still locked\n");
		return -1;
	}

	atomic_flag_clear(&mutex->locked);

	if (!is_empty_queue(&mutex->wait_queue)) {
		fprintf(stderr, "worker_mutex_destroy: threads are still waiting on the mutex\n");
		return -1;
	}

	return 0;
};


// Scheduling functions
/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf() {
	// - your own implementation of PSJF
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
	while (1) {
		// print_heap(&psjf_heap);
		tcb_t* next = heap_pop(&psjf_heap);

		if (next) {
			next->state = RUNNING;

			// Set start time if this is first time on cpu
			if (!next->has_started) {
				clock_gettime(CLOCK_MONOTONIC, &next->start_time);
				next->has_started = 1;
			}
			
			running_tcb = next;
			tot_cntx_switches++;
			unblock_timer_signal();
			setcontext(&running_tcb->context);
		}

		if (thread_count <= 1) {
			break;
			unblock_timer_signal();
			setcontext(&main_tcb->context);
			return;
		}

		if (!has_blocked_threads()) {
			break;
		}
	}
		
	// No threads ready
	print_app_stats();
	printf("Threads finished going back to main\n");
	block_timer_signal();
	setcontext(&main_tcb->context);
	return;
}

void promote_all_threads() {
	for (int i = 1; i < MAX_MLFQ_LEVELS; i++) {
		while (!is_empty_queue(&mlfq_levels[i])) {
			tcb_t* t = dequeue(&mlfq_levels[i]);
			enqueue(&mlfq_levels[i], t);
		}
	}
}
/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
	// - your own implementation of MLFQ
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE

	/* Step-by-step guidances */
	// Step1: Calculate the time current thread actually ran
	// Step2.1: If current thread uses up its allotment, demote it to the low priority queue (Rule 4)
	// Step2.2: Otherwise, push the thread back to its origin queue
	// Step3: If time period S passes, promote all threads to the topmost queue (Rule 5)
	// Step4: Apply RR on the topmost queue with entries and run next thread

	while (1) {
		// print_heap(&psjf_heap);
		tcb_t* next = NULL;
		
		// Find next thread 
		for (int i = 0; i < MAX_MLFQ_LEVELS; i++) {
		if (!is_empty_queue(&mlfq_levels[i])) {
			next = dequeue(&mlfq_levels[i]);
			break;
			}
		}

		if (next) {
			next->state = RUNNING;

			// Set start time if this is first time on cpu
			if (!next->has_started) {
				clock_gettime(CLOCK_MONOTONIC, &next->start_time);
				next->has_started = 1;
			}
			
			running_tcb = next;
			tot_cntx_switches++;
			unblock_timer_signal();
			setcontext(&running_tcb->context);
		}

		if (thread_count <= 1) {
			unblock_timer_signal();
			setcontext(&main_tcb->context);
			return;
		}

		if (!has_blocked_threads()) {
			break;
		}
	}
		
	// No threads ready
	print_app_stats();
	printf("Threads finished going back to main\n");
	block_timer_signal();
	setcontext(&main_tcb->context);
	return;

}

void update_vruntime(tcb_t *thread) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	
	thread->vruntime += time_diff_microseconds(thread->last_start, now);
}

long calculate_timeslice_cfs(tcb_t* thread) {
	int num_threads = cfs_heap.size;
	if (num_threads <= 0) {
		thread->cfs_timeslice = MIN_SCHED_GRN * 1000;
		return thread->cfs_timeslice;
	}
	long slice = TARGET_LATENCY * 1000 / num_threads;

	if (slice < MIN_SCHED_GRN * 1000) {
		slice = MIN_SCHED_GRN * 1000;
	}

	thread->cfs_timeslice = slice;
	return slice;
}
/* Completely fair scheduling algorithm */
static void sched_cfs(){
	// - your own implementation of CFS
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE

	/* Step-by-step guidances */

	// Step1: Update current thread's vruntime by adding the time it actually ran
	// Step2: Insert current thread into the runqueue (min heap)
	// Step3: Pop the runqueue to get the thread with a minimum vruntime
	// Step4: Calculate time slice based on target_latency (TARGET_LATENCY), number of threads within the runqueue
	// Step5: If the ideal time slice is smaller than minimum_granularity (MIN_SCHED_GRN), use MIN_SCHED_GRN instead
	// Step5: Setup next time interrupt based on the time slice
	// Step6: Run the selected thread

	while (1) {
		// print_heap(&psjf_heap);
		tcb_t* next = heap_pop(&cfs_heap);

		if (next) {
			next->state = RUNNING;

			// Set start time if this is first time on cpu
			if (!next->has_started) {
				clock_gettime(CLOCK_MONOTONIC, &next->start_time);
				next->has_started = 1;
			}

			clock_gettime(CLOCK_MONOTONIC, &next->last_start);
			
			running_tcb = next;
			long time_slice = calculate_timeslice_cfs(running_tcb);
			set_timer(time_slice);
			tot_cntx_switches++;
			unblock_timer_signal();
			setcontext(&running_tcb->context);
		}

		if (thread_count <= 1) {
			unblock_timer_signal();
			setcontext(&main_tcb->context);
			return;
		}

		if (!has_blocked_threads()) {
			break;
		}
	}
		
	// No threads ready
	print_app_stats();
	printf("Threads finished going back to main\n");
	block_timer_signal();
	setcontext(&main_tcb->context);
	return;
}

/* Round robin scheduling algorithm */
static void sched_rr() {
	while (1) {
		tcb_t *next = dequeue(&rr_queue);
		if (next) {
			next->state = RUNNING;
			running_tcb = next;

			// Set start time if this is first time on cpu
			if (!next->has_started) {
				clock_gettime(CLOCK_MONOTONIC, &next->start_time);
				next->has_started = 1;
			}
			unblock_timer_signal();
			// swapcontext(&scheduler_context, &running_tcb->context);
			setcontext(&running_tcb->context);
		}

        if (thread_count <= 1) {
			break;
            printf("Thread count <= 1. Returning to main.\n");
            unblock_timer_signal();
            setcontext(&main_tcb->context);
            return;
        }

		if (has_blocked_threads()) {
			printf("No ready threads and no blocked threads. Returning to main");
			break;
		}
		
		printf("Waiting on blocked threads \n");
	} 
	
	print_app_stats();
	printf("Threads finished going back to main\n");
	unblock_timer_signal();
	setcontext(&main_tcb->context);
	return;
}

// Scheduler helpers
void add_to_scheduler(tcb_t* thread) {
#if defined(PSJF)
	// add to PSJF heap
	heap_push(&psjf_heap, thread);
#elif defined(MLFQ)
	enqueue(&mlfq_levels[thread->priority], thread);
#elif defined(CFS)
	heap_push(&cfs_heap, thread);
#elif defined(RR)
	enqueue(&rr_queue, thread);
#else
	perror("No scheduling policy defined");
#endif


}

/* scheduler */
static void schedule() {
	// - every time a timer interrupt occurs, your worker thread library 
	// should be contexted switched from a thread context to this 
	// schedule() function
	
	//YOUR CODE HERE
	
	// - invoke scheduling algorithms according to the policy (PSJF or MLFQ or CFS)
	block_timer_signal();
	global_ticks++;
	if (running_tcb != NULL) {
		switch (running_tcb->state) {
			case RUNNING: // preemted thread
				running_tcb->state = READY;
#if defined(PSJF)
                running_tcb->elapsed_quanta++;
#elif defined(MLFQ)
				// handle promoting all threads to top level
				if (global_ticks % S_PERIOD == 0) {
					promote_all_threads();
				}
				
                running_tcb->time_used++;
                if (running_tcb->time_used >= running_tcb->time_slice &&
					running_tcb->priority < MAX_MLFQ_LEVELS - 1) {
                    // Demote thread to lower priority
                    running_tcb->priority++;
					running_tcb->time_slice = 1 << running_tcb->priority;
                }
				running_tcb->time_used = 0; // reset allotment
#elif defined(CFS)
                update_vruntime(running_tcb);
#elif defined(RR)
                running_tcb->elapsed_quanta++;
#endif
				add_to_scheduler(running_tcb);
				break;
 
			case READY: // yielded thread
				running_tcb->time_used = 0; // reset allotment
				add_to_scheduler(running_tcb);
				break;

			case BLOCKED: 
				break;

			case TERMINATED:
				running_tcb = NULL; // set freed pointer to NULL
				break;
				
				
			default:
				break;

		}
	}
	
	tot_cntx_switches++;

#if defined(PSJF)
	sched_psjf();
#elif defined(MLFQ)
	sched_mlfq();
#elif defined(CFS)
	sched_cfs();
#elif defined(RR)
	sched_rr();
#else
	perror("No scheduling policy defined");
#endif
}


//DO NOT MODIFY THIS FUNCTION
/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void) {

       fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
       fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
       fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}


// Feel free to add any other functions you need

// YOUR CODE HERE

