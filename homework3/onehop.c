#include <stdio.h>
#include "contiki.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"

#include "net/rime/rime.h"

PROCESS(pt_btn, "Handle button presses");
PROCESS(pt_listen, "Listen Alarm");

AUTOSTART_PROCESSES(&pt_btn);

static struct broadcast_conn broadcast;

struct collect_msg {
	char *identifier;
	uint8_t group_id;
	uint8_t alert;
};

/**
 * O means alram is off, 1 means alram is on
*/
static int alarm = 0; 
static struct etimer et;

static void
recv_broadcast(struct broadcast_conn *c, const linkaddr_t *from) {
	struct collect_msg *msg;
	msg = packetbuf_dataptr();

	printf("broadcast message received from %d.%d: %s %u %u\n", 
		from->u8[0], from->u8[1], msg->identifier, msg->group_id, msg->alert);


	if (msg->alert == 1) {
		leds_on(LEDS_BLUE);
		// start another process and set the alarm varaiable
		alarm = 1;
		process_start(&pt_listen, NULL);
	} else {
		// if there is a pending timer, clear that first, and exit the process for resetting the alert
		if (etimer_pending()) {
			etimer_stop(&et);
			process_exit(&pt_listen);
		}
		alarm = 0;
		leds_off(LEDS_ALL);
		printf("Alarm turned off message received from %d.%d \n", from->u8[0], from->u8[1]);
	}
}

static const struct broadcast_callbacks broadcast_callbacks = {recv_broadcast};

/**
 * When a user button is pressed, turn on the red light and inform neighbouring motes
*/
PROCESS_THREAD(pt_btn, ev, data) {
	PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
	PROCESS_BEGIN();

	broadcast_open(&broadcast, 130, &broadcast_callbacks);

	SENSORS_ACTIVATE(button_sensor);

	// static struct etimer et;

	// inform neighbouring motes about the fire alert
	static struct collect_msg msg;
	msg.identifier = "AUA";
	msg.group_id = 15;

	while (1)
	{
		// wait for user button press
		PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && data == &button_sensor);
		// if the alarm is off, turn it on
		if (alarm == 0)
		{
			leds_on(LEDS_RED);
			msg.alert = 1;
			/* Copy data to the packet buffer */
			packetbuf_copyfrom(&msg, 5);
			/* Send broadcast packet */
			broadcast_send(&broadcast);
			alarm = 1;
			printf("Alarm triggered\n");
			// start another process to reset the alarm
			process_start(&pt_listen, NULL);
		} else {
			// alarm is already on, turn it off and also tell other nodes
			// if there is a pending timer, clear that first, and exit the process for resetting the alert
			if (etimer_pending()) {
				etimer_stop(&et);
				process_exit(&pt_listen);
			}
			leds_off(LEDS_ALL);
			msg.alert = 0;
			alarm = 0;
			/* Copy data to the packet buffer */
			packetbuf_copyfrom(&msg, 5);
			/* Send broadcast packet */
			broadcast_send(&broadcast);
			printf("Alarm turned off\n");
		}
	}

	PROCESS_END();
}


// reset the alarm after 20 seconds
PROCESS_THREAD(pt_listen, ev, data)
{
	PROCESS_BEGIN();
	// reset the alert after 20 seconds if it is still on
	etimer_set(&et, CLOCK_SECOND * 20);
	PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
	if (alarm == 1)
	{
		printf("resetting the alarm after 20 seconds\n");
		leds_off(LEDS_ALL);
		alarm = 0;
	}
	PROCESS_END();
}
