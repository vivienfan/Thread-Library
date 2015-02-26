#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <string.h>
#include "thread.h"
#include "interrupt.h"


typedef enum {EXITED, READY, RUNNING, BLOCKED} state_t;

/* This is the thread control block */
struct thread {
	Tid tid;
	state_t mystate;
	ucontext_t mycontext;
};

struct threadNode {
	struct thread * t;
	struct threadNode * next;
};

struct queue {
	struct threadNode * head;
	int numThreads;
};

/*  starts by calling thread_stub. The arguments to thread_stub are the
 * thread_main() function, and one argument to the thread_main() function. */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
	int enabled = interrupts_set(1);
	Tid ret;

	thread_main(arg); // call thread_main() function with arg

	interrupts_set(enabled);

	ret = thread_exit(THREAD_SELF);
	// we should only get here if we are the last thread. 
	assert(ret == THREAD_NONE);
	// all threads are done, so process should exit
	exit(0);
}

struct queue *Dqueue;
struct queue *Rqueue;

int avaIDs[THREAD_MAX_THREADS];

int
find_next_avaID(void)
{
	int i;
	for (i=0; i < THREAD_MAX_THREADS; i++) {
		if(avaIDs[i] == 0) {
			avaIDs[i] = 1;
			return i;
		}
		// else keep iterating
	}
	return -1;
}

void
thread_init(void)
{
	Rqueue = (struct queue *)malloc(sizeof(struct queue));
	Rqueue->head = (struct threadNode *)malloc(sizeof(struct threadNode));
	Rqueue->head->next = NULL;
	Rqueue->head->t = (struct thread *)malloc(sizeof(struct thread));
	Rqueue->head->t->tid = 0;
	Rqueue->head->t->mystate = RUNNING;
	Rqueue->numThreads = 1;
	
	Dqueue = (struct queue *)malloc(sizeof(struct queue));
	Dqueue->head = NULL;
	
	memset(avaIDs,0,THREAD_MAX_THREADS);
	
	avaIDs[0] = 1;
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
	int enabled = interrupts_set(0);

	if (Rqueue->numThreads >= THREAD_MAX_THREADS) {
		// reached the limit
		interrupts_set(enabled);
		return THREAD_NOMORE;
	}

	struct threadNode * newThread = (struct threadNode *)malloc(sizeof(struct threadNode));
	newThread->t = (struct thread *)malloc(sizeof(struct thread));
	getcontext(&(newThread->t->mycontext));
	newThread->t->mycontext.uc_mcontext.gregs[REG_RIP] = (unsigned long) thread_stub;
	newThread->t->mycontext.uc_mcontext.gregs[REG_RDI] = (unsigned long) fn;
	newThread->t->mycontext.uc_mcontext.gregs[REG_RSI] = (unsigned long) parg;
	
	char *new_stack = (char *)malloc((THREAD_MIN_STACK+8) * sizeof(char));
	if(new_stack == NULL) {
		// malloc failed, no more memory
		interrupts_set(enabled);
		return THREAD_NOMEMORY;
	}	

	newThread->t->mycontext.uc_stack.ss_sp = new_stack;
		
	newThread->t->mycontext.uc_stack.ss_size = THREAD_MIN_STACK;
	newThread->t->mycontext.uc_mcontext.gregs[REG_RSP] = (unsigned long)new_stack + (THREAD_MIN_STACK+8);

	newThread->t->tid = find_next_avaID();
	newThread->t->mystate = READY;
	newThread->next = NULL;
	
	struct threadNode *last = Rqueue->head;
	while(last->next) {
		last = last->next;
	}
	last->next = newThread;
	Rqueue->numThreads = Rqueue->numThreads + 1;

	interrupts_set(enabled);
	return newThread->t->tid;
}

Tid
thread_yield(Tid want_tid)
{
	int enabled = interrupts_set(0);

	int flag = 0;

	if(want_tid == THREAD_ANY) {
		if (!Rqueue->head) {
			// no thread in the queue, nothing to yield
			interrupts_set(enabled);
			return THREAD_NONE;
		}
		if (!Rqueue->head->next) {
			// only one thread in the queue, nothing to yield
			interrupts_set(enabled);
			return THREAD_NONE;
		}
		// else change queue before getcontext()
		struct threadNode * last = Rqueue->head;
		while (last->next) {
			last = last->next;
		}
		// now last is the last node in the queue
		last->next = Rqueue->head;
		Rqueue->head = Rqueue->head->next;
		last->next->next = NULL;
		
		last->next->t->mystate = READY;
		Rqueue->head->t->mystate = RUNNING;
		getcontext(&(last->next->t->mycontext));
		Tid returnID = Rqueue->head->t->tid;
		if (!flag) {
			// if the context 
			flag = 1;
			setcontext(&(Rqueue->head->t->mycontext));	
		}
		else {
			flag = 0;
			interrupts_set(enabled);
			return returnID;
		}		
	}
	else if (want_tid == THREAD_SELF) {
		interrupts_set(enabled);
		return Rqueue->head->t->tid;
	}
	else {
		if (!Rqueue->head) {
			// no thread in the queue, nothing to yield
			interrupts_set(enabled);
			return THREAD_NONE;
		}
		// yield a thread with given Tid
		struct threadNode * prev = NULL;
		struct threadNode * curr = Rqueue->head;
		if (want_tid == curr->t->tid) {
			getcontext(&(curr->t->mycontext));
			if (!flag) {
				flag = 1;
				setcontext(&(curr->t->mycontext));	
			}
			else {
				flag = 0;
				interrupts_set(enabled);
				return Rqueue->head->t->tid;
			}			
		}
		
		else {
			while (curr) {
				if(curr->t->tid == want_tid) {
					// found the wanted thread
					break;
				}
				// else, keep looping
				prev = curr;
				curr = curr->next;
			}

			if (curr) {
				prev->next = curr->next;
				curr->next = Rqueue->head;
				Rqueue->head = curr;

				Rqueue->head->next->t->mystate = READY;
				Rqueue->head->t->mystate = RUNNING;
	
				Tid returnID = Rqueue->head->t->tid;

				getcontext(&(Rqueue->head->next->t->mycontext));
				if (!flag) {
					flag = 1;
					setcontext(&(Rqueue->head->t->mycontext));	
				}
				else {
					flag = 0;
					interrupts_set(enabled);
					return returnID;
				}		
			}
		}
		// else, return thread invalid
		interrupts_set(enabled);
		return THREAD_INVALID;		
	}
	interrupts_set(enabled);
	return THREAD_FAILED;
}

Tid
thread_exit(Tid tid)
{
	int enabled = interrupts_set(0);

	if (tid == THREAD_SELF || Rqueue->head->t->tid == tid) {
		// put self into the destroy queue
		if(!Rqueue->head->next) {
			interrupts_set(enabled);
			return THREAD_NONE;
		}

		// else
		Rqueue->numThreads = Rqueue->numThreads - 1;
		Rqueue->head->t->mystate = EXITED;
		struct threadNode *temp = Rqueue->head;
		Rqueue->head = Rqueue->head->next;
		temp->next = Dqueue->head;
		Dqueue->head = temp;
		avaIDs[temp->t->tid] = 0;
		setcontext(&(Rqueue->head->t->mycontext));		
	}

	else if (tid == THREAD_ANY) {
		if (!Rqueue->head->next) {
			interrupts_set(enabled);
			return THREAD_NONE;
		}
		// else
		Rqueue->numThreads = Rqueue->numThreads - 1;
		Rqueue->head->next->t->mystate = EXITED;
		struct threadNode *temp = Rqueue->head->next;
		Rqueue->head->next = Rqueue->head->next->next;
		temp->next = Dqueue->head;
		Dqueue->head = temp;
		avaIDs[temp->t->tid] = 0;
		interrupts_set(enabled);
		return temp->t->tid;
	}

	else {	// tid == THREAD_ID
		struct threadNode * prev = Rqueue->head;
		struct threadNode * curr = Rqueue->head->next;
		while (curr) {
			if (curr->t->tid == tid) {
				Rqueue->numThreads = Rqueue->numThreads - 1;
				curr->t->mystate = EXITED;
				struct threadNode *temp = curr;
				prev->next = curr->next;
				temp->next = Dqueue->head;
				Dqueue->head = temp;
				avaIDs[tid] = 0;
				interrupts_set(enabled);
				return tid; 
			}
			prev = curr;
			curr = curr->next;
		}
		if (!curr) {
			interrupts_set(enabled);
			return THREAD_INVALID;
		}
	}
	interrupts_set(enabled);
	return THREAD_FAILED;
}

/* This is the wait queue structure */
struct wait_queue {
	struct threadNode* head;
	int numThreads;
};

struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	wq->head = NULL;

	return wq;
}

Tid
thread_sleep(struct wait_queue *queue)
{
	int enabled = interrupts_set(0);
	int flag = 0;
	if(!queue) {
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	if(!Rqueue->head->next) {
		interrupts_set(enabled);
		return THREAD_NONE;
	}
	
	// else change queue before getcontext()
	struct threadNode * wlast = queue->head;
	if(!wlast) {
		wlast = Rqueue->head;
		Rqueue->head = Rqueue->head->next;
		wlast->next = NULL;
		wlast->t->mystate = BLOCKED;
		Rqueue->head->t->mystate = RUNNING;
		queue->numThreads++;
		getcontext(&(wlast->t->mycontext));
		Tid returnID = Rqueue->head->t->tid;
		if (!flag) {
			// if the context 
			flag = 1;
			setcontext(&(Rqueue->head->t->mycontext));	
		}
		else {
			flag = 0;
			interrupts_set(enabled);
			return returnID;
		}
	}
	while (wlast->next) {
		wlast = wlast->next;
	}
	// now last is the last node in the waiting queue
	// run the next thread in ready queue
	// put self at the end of waiting queue
	wlast->next = Rqueue->head;
	Rqueue->head = Rqueue->head->next;
	wlast->next->next = NULL;
	
	wlast->next->t->mystate = BLOCKED;
	Rqueue->head->t->mystate = RUNNING;
	queue->numThreads++;
	getcontext(&(wlast->next->t->mycontext));
	Tid returnID = Rqueue->head->t->tid;
	if (!flag) {
		// if the context 
		flag = 1;
		setcontext(&(Rqueue->head->t->mycontext));	
	}
	else {
		flag = 0;
		interrupts_set(enabled);
		return returnID;
	}

	return THREAD_FAILED;
}

int
thread_wakeup(struct wait_queue *queue, int all)
{
	int enabled = interrupts_set(0); 
	if(!queue) {
		interrupts_set(enabled);
		return 0;
	}

	if(!queue->head) {
		interrupts_set(enabled);
		return 0;
	}

	if(all == 0) {
		struct threadNode * last = Rqueue->head;
		while (last->next) {
			last = last->next;
		}
		queue->head->t->mystate = READY;
		last->next = queue->head;
		queue->head = queue->head->next;
		last->next->next = NULL;
		queue->numThreads--;
		interrupts_set(enabled);
		return 1;
	}
	else if(all != 0) {
		struct threadNode * curr = queue->head;
		while(curr) {
			curr->t->mystate = READY;
			curr = curr->next;
		}
		struct threadNode * last = Rqueue->head;
		while (last->next) {
			last = last->next;
		}
		last->next = queue->head;
		queue->head = NULL;
		queue->numThreads = 0;
		interrupts_set(enabled);
		return 1;
	}
	return 0;
}
