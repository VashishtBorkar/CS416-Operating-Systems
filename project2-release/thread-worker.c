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
static tcb_t *main_tcb = NULL; // main thread TCB
static ucontext_t scheduler_context; // scheduler context

// Ready queue
static Queue_t ready_queue;
static tcb_t *ready_head = NULL;
static tcb_t *ready_tail = NULL;
static tcb_t *running_tcb = NULL;

// Timer and signal handling
static struct itimerval timer;
static struct sigaction sa;

// YOUR CODE HERE

// General queue implementation
void init_queue(Queue_t *queue) {
	queue->head = NULL;
	queue->tail = NULL;
}

void enqueue(Queue_t *queue, tcb_t* data) {
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

tcb_t *dequeue(Queue_t *q) {
    if (!q->head) {
		return NULL;
	}

    Node_t *node = q->head;
    tcb_t *data = node->data;
    q->head = node->next;

    if (!q->head)
        q->tail = NULL;
    free(node);

    return data;
}

int is_empty(Queue_t *q) {
	return q->head == NULL;
}

// Ready queue functions
void enqueue_ready(tcb_t* thread) {
	thread->state = READY;
	enqueue(&ready_queue, thread);
}

tcb_t* dequeue_ready() {
	return dequeue(&ready_queue);
}


void timer_handler(int signum) {
    if (running_tcb) {
        // save current and switch to scheduler
        swapcontext(&running_tcb->context, &scheduler_context);
    } else {
        swapcontext(&scheduler_context, &scheduler_context);
    }
}

// Main thread set up
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

	init_queue(&ready_queue);
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

void init_main_thread() {
	if (main_tcb != NULL) {
		return; // already initialized
	}

	init_scheduler();
    init_timer();

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
	// main_tcb->waiting_for = -1;

	running_tcb = main_tcb;
}


tcb_t *find_tcb_by_id(worker_t id) {
    tcb_t *temp = ready_head; // wherever you store TCBs
    while (temp) {
        if (temp->id == id)
            return temp;
        temp = temp->next;
    }
    return NULL;
}

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

	// Create TCB
	tcb_t *new_tcb = malloc(sizeof(tcb_t));
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
    // new_tcb->waiting_for = -1;
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
	tcb_t *current = running_tcb;
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

	tcb_t *current = running_tcb;
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

	if (current->waiting_thread != NULL) {
		tcb_t *waiting = current->waiting_thread;
		waiting->state = READY;
		enqueue_ready(current->waiting_thread);
		current->waiting_thread = NULL;
	}
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

	// Prevent multiple joins
	if (target->joined) {
        fprintf(stderr, "worker_join: thread already joined\n");
        return -1;
    }

	// If already terminated, clean up and return
	if (target->state == TERMINATED) {
		if (value_ptr) {
			*value_ptr = target->retval;
		}
		free(target);
		return 0;
	}

	// Otherwise block current and join
	target->joined = 1;

	tcb_t *current = running_tcb;
	current->state = BLOCKED;
	target->waiting_thread = current;

	swapcontext(&current->context, &scheduler_context);

	if (value_ptr) {
        *value_ptr = target->retval;
	}

	free(target->stack);
	free(target);
	
	return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, 
                          const pthread_mutexattr_t *mutexattr) {
	//- initialize data structures for this mutex
	if (!mutex) {
		return -1;
	}

	mutex->locked = 0;
	mutex->owner_tcb = NULL;
	mutex->wait_queue = init_queue(malloc(sizeof(Queue_t)));
	return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex) {

        // - use the built-in test-and-set atomic function to test the mutex
        // - if the mutex is acquired successfully, enter the critical section
        // - if acquiring mutex fails, push current thread into block list and
        // context switch to the scheduler thread

        // YOUR CODE HERE
		int expected = 0;

		if (!mutex->locked) {
			mutex->locked = 1;
			mutex->owner_tcb = running_tcb;
			return 0;
		}
		// Mutex is locked

		enqueue(mutex->wait_queue, running_tcb);
		worker_yield();

		mutex->owner_tcb = running_tcb;
        return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {
	// - release mutex and make it available again. 
	// - put threads in block list to run queue 
	// so that they could compete for mutex later.

	// YOUR CODE HERE
	if (mutex->owner_tcb != running_tcb) {
		fprintf(stderr, "worker_mutex_unlock: current thread does not own the mutex\n");
		return -1;
	}
	
	if (is_empty(mutex->wait_queue)) {
		// No waiting threads
		mutex->owner_tcb = NULL;
		mutex->locked = 0;
	} else {
		mutex->owner_tcb = dequeue(mutex->wait_queue);
	}


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
    if (running_tcb && running_tcb->state == RUNNING) {
        running_tcb->state = READY;
        enqueue_ready(running_tcb);
    }
	
	// - invoke scheduling algorithms according to the policy (PSJF or MLFQ or CFS)
#if defined(PSJF)
    	sched_psjf();
#elif defined(MLFQ)
		sched_mlfq();
#elif defined(CFS)
    	sched_cfs();  
#else
	//# error "Define one of PSJF, MLFQ, or CFS when compiling. e.g. make SCHED=MLFQ"
	// Simple round robin
	printf("Simple RR Scheduling\n");
	tcb_t *next = dequeue_ready();
    if (next) {
        next->state = RUNNING;
        running_tcb = next;
        setcontext(&running_tcb->context);
    } else {
		printf("No ready threads. Returning to main thread.\n");
		setcontext(&main_tcb->context);
		return;
	}

	// perror("No scheduling policy defined");
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

