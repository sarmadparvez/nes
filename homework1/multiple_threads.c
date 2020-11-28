#include <stdio.h>
#include <stdlib.h>

#define RUNNING 0
#define DONE    1


typedef struct thread {
	int (*run)();
	struct thread *next;
} thread_t;

thread_t *threads = NULL;

void threads_add_tail(thread_t *t) {
	if (!threads) {
		threads = t;
		t->next = NULL;
		return;
	}

	thread_t *current = threads;
	while (current->next != NULL) {
		current = current->next;
	}

	current->next = t;
	t->next = NULL;
}

int thread_available() {
	if (threads) {
		return 1;
	} else {
		return 0;
	}
}

thread_t* threads_pop_front() {
	thread_t* front = threads;
	threads = front->next;
	return front;
}

int protothread1() {
	// static variable to count the execution steps
	static int step_count1 = 0;
	// local variable is changed to static variable.
	// This variable is changed to static to preserve the value between multiple function calls. See pdf file for details.
	static int local1;
	// increment static variable for multiple execution steps
	step_count1++;
	printf("protothread1:\n");
	// print exectution step
	printf("Current execution step is %d\n", step_count1);
	
	// first execution step
	// initialize the local (static) variable and print it
	if (step_count1 == 1) {
		local1 = 0;
		// print value of local (static) variable
		printf("local variable is %d\n\n", local1);
	} else {
		// futher execution steps
		// increment local (static) variable 
		local1++;
		// print value of local (static) variable
		printf("local variable is %d\n\n", local1);
	}

	// if it is last execution step, finish execution.
	if (step_count1 == 4) {
		return DONE;
	}
	return RUNNING;
}

int protothread2() {
	// static variable to count the execution steps
	static int step_count2 = 0;
	// increment static variable for multiple execution steps
	step_count2++;
	printf("protothread2:\n");
	// print exectution step
	printf("Current execution step is %d\n\n", step_count2);

	if (step_count2 == 2) {
		return DONE;
	}
	return RUNNING;
}

int main(int argc, char *argv[]) {

	// create two protothreads
	thread_t *t1 = (thread_t*)malloc(sizeof(thread_t));
	t1->run = &protothread1;
	thread_t *t2 = (thread_t*)malloc(sizeof(thread_t));
	t2->run = &protothread2;

	threads_add_tail(t1);
	threads_add_tail(t2);

	while (thread_available()) {

		// get first thread and exectue
		thread_t* t = threads_pop_front();
		int ret = t->run();
		// remove completed threads from pool
		if (ret != DONE) {
			threads_add_tail(t);
		}
	}

	return 0;
}
