// File:	thread-worker.c
// List all group member's name:
// username of iLab:
// iLab Server:

#include "thread-worker.h"

//Global counter for total context switches and 
//average turn around and response time
long tot_cntx_switches=0;
double avg_turn_time=0;
double avg_resp_time=0;


// INITAILIZE ALL YOUR OTHER VARIABLES HERE
static tcb *main_tcb = NULL; // main thread TCB
static ucontext_t scheduler_context; // scheduler context

// Ready queue
static tcb *ready_head = NULL;
static tcb *ready_tail = NULL;
static tcb *running_tcb = NULL;

// Timer and signal handling
static struct itimerval timer;
static struct sigaction sa;


// YOUR CODE HERE

static void worker_start(void *(*func)(void *), void *arg) {
    void *ret = func(arg);  // run the user's function
    worker_exit(ret);       // call our thread cleanup logic
}

/* create a new thread */
void enqueue_ready(tcb *thread) {
    thread->next = NULL;
    if (ready_tail) {
        ready_tail->next = thread;
        ready_tail = thread;
    } else {
        ready_head = ready_tail = thread;
    }
}

tcb *dequeue_ready() {
    if (!ready_head) {
		return NULL;
	}

    tcb *t = ready_head;
    ready_head = ready_head->next;

	// Empty queue
    if (!ready_head){ 
		ready_tail = NULL;
	} 

    t->next = NULL;
    return t;
}

void timer_handler(int signum) {
    printf("Switching Threads \n");
    // schedule();
}

void init_timer() {
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &timer_handler;
    sigaction(SIGPROF, &sa, NULL);

    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = QUANTUM; // how often it repeats

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = QUANTUM;    // initial delay before first fire

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

	// makecontext(&scheduler_context, schedule, 0);
}

void init_main_thread() {
	if (main_tcb != NULL) {
		return; // already initialized
	}

	init_scheduler();
    init_timer();

	main_tcb = malloc(sizeof(tcb));
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
	main_tcb->waiting_for = -1;

	running_tcb = main_tcb;
}

int worker_create(worker_t * thread, pthread_attr_t * attr, 
                      void *(*function)(void*), void * arg) {

	// - create Thread Control Block (TCB)
	// - create and initialize the context of this worker thread
	// - allocate space of stack for this thread to run
	// after everything is set, push this thread into run queue and 
	// - make it ready for the execution.

	// YOUR CODE HERE

	// Create TCB
	tcb *new_tcb = malloc(sizeof(tcb));
	if (!new_tcb) {
		perror("malloc for TCB failed");
		return -1;
	}

	// Increment thread counter 
	static int next_thread_id = 1;
	new_tcb->id = next_thread_id++;

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
	new_tcb->context.uc_link = NULL;

	// Set start for routine for context
	makecontext(&new_tcb->context, (void (*)(void))worker_start, 2, function, arg);

	// TCB Fields 
	new_tcb->state = READY;
    new_tcb->retval = NULL;
    new_tcb->waiting_for = -1;
	new_tcb->joined = 0;
	new_tcb->next = NULL;

	enqueue_ready(new_tcb);

	*thread = new_tcb->id;

    return 0;
};

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield() {
	
	// - change worker thread's state from Running to Ready
	// - save context of this thread to its thread control block
	// - switch from thread context to scheduler context

	// YOUR CODE HERE
	init_main_thread();
	init_scheduler();

	tcb *current = running_tcb;
	if(!current) {
		perror("No running thread");
		return -1;
	}

	current->state = READY;
	enqueue_ready(current);

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
	init_scheduler();
	init_main_thread();
	
	tcb *current = running_tcb;
	if (!current) {
		perror("No running thread");
		exit(1);
	}
	current->state = TERMINATED;
	current->retval = value_ptr;
	if (current->stack) {
		free(current->stack);
		current->stack = NULL;
	}
};



/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr) {
	
	// - wait for a specific thread to terminate
	// - de-allocate any dynamic memory created by the joining thread
  
	// YOUR CODE HERE
	return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, 
                          const pthread_mutexattr_t *mutexattr) {
	//- initialize data structures for this mutex

	// YOUR CODE HERE
	return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex) {

        // - use the built-in test-and-set atomic function to test the mutex
        // - if the mutex is acquired successfully, enter the critical section
        // - if acquiring mutex fails, push current thread into block list and
        // context switch to the scheduler thread

        // YOUR CODE HERE
        return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {
	// - release mutex and make it available again. 
	// - put threads in block list to run queue 
	// so that they could compete for mutex later.

	// YOUR CODE HERE
	return 0;
};


/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex) {
	// - de-allocate dynamic memory created in worker_mutex_init

	return 0;
};

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf() {
	// - your own implementation of PSJF
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
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
}


/* scheduler */
static void schedule() {
	// - every time a timer interrupt occurs, your worker thread library 
	// should be contexted switched from a thread context to this 
	// schedule() function
	
	//YOUR CODE HERE

	// - invoke scheduling algorithms according to the policy (PSJF or MLFQ or CFS)
#if defined(PSJF)
    	sched_psjf();
#elif defined(MLFQ)
	sched_mlfq();
#elif defined(CFS)
    	sched_cfs();  
#else
	//# error "Define one of PSJF, MLFQ, or CFS when compiling. e.g. make SCHED=MLFQ"
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

