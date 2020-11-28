#include <stdio.h>
#include <stdlib.h>

#define RUNNING 0
#define DONE    1

int protothread() {
	// static variable to count the execution steps
	static int step_count = 0;
	step_count++;
	printf("protothread current execution step is %d\n", step_count);
	if (step_count == 4) {
		return DONE;
	}
	return RUNNING;
}

int main(int argc, char *argv[]) {

	int i = 0;
	int ret;

	do {
		i++;
		ret = protothread();

	} while (ret != DONE);

	printf("thread stopped after %d calls\n", i);

	return 0;
}
