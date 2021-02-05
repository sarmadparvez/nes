#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include <stdio.h>

// #include "lib/list.h"
#include "lib/memb.h"

PROCESS(pt_source, "Message source");

AUTOSTART_PROCESSES(&pt_source);


// Size of the mote address
#define LINKADDR_SIZE 2

// Maximum number of enteries in routing table
#define TABLE_SIZE 32

// for broadcast connection
static struct broadcast_conn broadcast;

// for unicast connection
static struct unicast_conn uc;

// counter for sequence number
static uint32_t seq_no = 1;
// counter for broadcast id
static uint32_t broadcast_id = 1;

// a struct representing a single record/row for routing table
struct table_record {
	linkaddr_t dest_addr; // address of the destination node
    linkaddr_t next_addr; // address of the next node
    uint8_t distance; // distance to destination node (hop count)
    uint32_t  dest_seq; // sequence number for destination node
    clock_time_t interval; // route discovery timeout
};

// a struct representing a message that is sent from source to destination
struct collect_msg {
	linkaddr_t source_addr; // address of the source node
	uint32_t  source_seq; // sequence number of source node
    uint32_t broadcast_id; // the unique brodcast id for the message
    uint32_t dest_seq; // sequence number of destination node
    linkaddr_t dest_addr; // address of the destination node
    uint8_t distance; // distance travelled so far (hope count)
};


// declare a list representing the routing table
// LIST(routing_table);

// list_init(routing_table);

// handler for receiving a broadcast message
static void
recv_broadcast(struct broadcast_conn *c, const linkaddr_t *from) {
	struct collect_msg *msg;
	msg = (struct collect_msg *)packetbuf_dataptr();

    // print the received message details
	printf("broadcast message received from %d.%d: \n", from->u8[0], from->u8[1]);
    
    printf("source address: %d.%d, source seq: %lu, broadcast id: %lu, dest address: %d.%d, dest seq: %lu, hop count: %u \n",
    msg->source_addr.u8[0],msg->source_addr.u8[1], msg->source_seq, msg->broadcast_id, msg->dest_addr.u8[0], msg->dest_addr.u8[1], msg->dest_seq, msg->distance);
}

static const struct broadcast_callbacks broadcast_callbacks = {recv_broadcast};

/**
 * Source node sending the message
 * When a user button is pressed, send the message to desitnation node i.e 5.0
*/
PROCESS_THREAD(pt_source, ev, data) {
	PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
	PROCESS_BEGIN();

	broadcast_open(&broadcast, 130, &broadcast_callbacks);

	SENSORS_ACTIVATE(button_sensor);

    while (1)
    {
        // wait for user button press
        PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && data == &button_sensor);
        // Set address of the destination node (8.0)
        linkaddr_t addr;
        addr.u8[0] = 8;
        addr.u8[1] = 0;

        // prepare the message to be sent
        struct collect_msg msg;
        // copy source and destination addresses
        linkaddr_copy((linkaddr_t *)&msg.source_addr, &linkaddr_node_addr);
        linkaddr_copy((linkaddr_t *)&msg.dest_addr, &addr);
        msg.broadcast_id = broadcast_id;
        msg.source_seq = seq_no;
        msg.dest_seq = 0;
        msg.distance = 1;

        // if this is not the destination node, broadcast the message
        if (!linkaddr_cmp(&addr, &linkaddr_node_addr))
        {
            /* Copy data to the packet buffer */
            packetbuf_copyfrom(&msg, sizeof(struct collect_msg));
            /* Send broadcast packet */
            broadcast_send(&broadcast);
            broadcast_id++;
            seq_no++;
        }
    }

    PROCESS_END();
}
