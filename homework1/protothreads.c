#include <stdio.h>
#include "contiki.h"
#include "dev/button-sensor.h"
#include "dev/light-sensor.h"
#include "dev/leds.h"

PROCESS(protothread1, "Protothread Process 1");
// 1st new process i.e Process 2
PROCESS(protothread2, "Protothread Process 2");
// 2nd new process i.e Process 3
PROCESS(protothread3, "Protothread Process 3");

AUTOSTART_PROCESSES(&protothread1, &protothread2);

PROCESS_THREAD(protothread1, ev, data) {
	PROCESS_BEGIN();

	printf("Protothread 1!\n");

	SENSORS_ACTIVATE(button_sensor);
	// wait for button click event
	PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && data == &button_sensor);		
	// start the process 3 on user button click
	process_start(&protothread3, NULL);

	PROCESS_END();
}

// Thread for 1st new process i.e process 2.
PROCESS_THREAD(protothread2, ev, data) {
	PROCESS_BEGIN();

	printf("Protothread 2!\n");

	while (1) {
		printf("Message from Protothread 2\n");
		// yield the process temporarily so that other process can also execute
		PROCESS_PAUSE();
	}

	PROCESS_END();
}

// Thread for 2nd new process i.e process 3.
PROCESS_THREAD(protothread3, ev, data) {
	PROCESS_BEGIN();

	printf("Protothread 3!\n");

	while (1) {
		printf("Message from Protothread 3\n");
		// yield the process temporarily so that other process can also execute
		PROCESS_PAUSE();
	}
	PROCESS_END();
}
