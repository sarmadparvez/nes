#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include <stdio.h>
#include <stdlib.h>

#include "lib/list.h"
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
struct table_record
{
    struct table_record *next; // pointer to the next record in table
    linkaddr_t dest_addr;  // address of the destination node
    linkaddr_t next_addr;  // address of the next node
    uint8_t distance;      // distance to destination node (hop count)
    uint32_t dest_seq;     // sequence number for destination node
    clock_time_t interval; // route discovery timeout
    uint32_t broadcast_id; // the unique brodcast id for the message
};

// a struct representing a message that is sent from source to destination
struct collect_msg
{
    linkaddr_t source_addr; // address of the source node
    uint32_t source_seq;    // sequence number of source node
    uint32_t broadcast_id;  // the unique brodcast id for the message
    uint32_t dest_seq;      // sequence number of destination node
    linkaddr_t dest_addr;   // address of the destination node
    uint8_t distance;       // distance travelled so far (hope count)
};

// declare a list representing the routing table
LIST(routing_table);
MEMB(routing_table_mem, struct table_record, TABLE_SIZE);


/**
 * Create a new entry in routing table
*/
static void insert_row(struct collect_msg msg, const linkaddr_t *from) {
    struct table_record *tr = NULL;
    tr = memb_alloc(&routing_table_mem);
    linkaddr_copy((linkaddr_t *)&tr->dest_addr, &msg.source_addr);
    linkaddr_copy((linkaddr_t *)&tr->next_addr, from);
    tr->dest_seq = msg.source_seq;
    tr->distance = msg.distance;
    tr->interval = CLOCK_SECOND * 3;
    tr->broadcast_id = msg.broadcast_id;
    list_push(routing_table, tr);
}

/**
 * Search a record in routing table
*/

static struct table_record *search_row(struct table_record filter) {
    struct table_record *tr = malloc(sizeof(struct table_record));
    for (tr = list_head(routing_table); tr != NULL; tr = list_item_next(tr))
    {
        if (linkaddr_cmp(&tr->dest_addr, &filter.dest_addr)) {
            // found on based of destination address
            return tr;
        } 
    }
    free(tr);
    return NULL;
}

/**
 * Print the routing table.
*/
static void print_routing_table()
{
    printf("printing routing table \n");
    struct table_record *tr = NULL;
    // print the contents of routing table
    for (tr = list_head(routing_table); tr != NULL; tr = list_item_next(tr))
    {
        printf("dest addr %d.%d, next %d.%d, distance %u, dest seq %lu, broadcast id %lu \n", tr->dest_addr.u8[0], tr->dest_addr.u8[1], tr->next_addr.u8[0], tr->next_addr.u8[1] , tr->distance, tr->dest_seq, tr->broadcast_id);
        // ToDo: Print the all fields of the table 
    }
}

/**
 * Print a message
*/
static void print_message(struct collect_msg msg)
{
    printf("source address: %d.%d, source seq: %lu, broadcast id: %lu, dest address: %d.%d, dest seq: %lu, hop count: %u \n",
           msg.source_addr.u8[0], msg.source_addr.u8[1], msg.source_seq, msg.broadcast_id, msg.dest_addr.u8[0], msg.dest_addr.u8[1], msg.dest_seq, msg.distance);
}

/*************************************************************************/
/* 
 * Callback function for broadcast
 * Called when a packet has been received by the broadcast module
 */
static void
recv_broadcast(struct broadcast_conn *c, const linkaddr_t *from) {
	struct collect_msg msg;
	msg = *((struct collect_msg *)packetbuf_dataptr());
    // if it is a source node and it received request from its neighbours, discard it
    if (linkaddr_cmp(&msg.source_addr, &linkaddr_node_addr)) {
        return;
    }

    struct table_record filter;
    struct table_record *table_entry = NULL;
    // check if same request is received again, discard it.
    // the source address and broadcast id uniquely identifies a request
    // prepare filter to search in table
    linkaddr_copy((linkaddr_t *)&filter.dest_addr, &msg.source_addr);
    filter.broadcast_id = msg.broadcast_id;
    table_entry = search_row(filter);
    if (table_entry != NULL && table_entry->broadcast_id == msg.broadcast_id) {
        // its a duplicate request, discard it
        free(table_entry);
        table_entry = NULL;
        return;
    }

    // print the received message details
	printf("broadcast message received from %d.%d: \n", from->u8[0], from->u8[1]);
    print_message(msg);
    // check if current node is the destination node
    if (linkaddr_cmp(&msg.dest_addr, &linkaddr_node_addr)) {
        // this is the destination node, prepare a route reply RREP
        // search in the routing table, the route to source node (to send RREP)
        // unicast_send(&uc, &addr);
        // create a new entry in routing table
        insert_row(msg, from);
        // printing routing table
        print_routing_table();

        // prepare RREP (route reply)
        // the source becomes destination
        linkaddr_copy((linkaddr_t *)&msg.dest_addr, &msg.source_addr);
        // the destination becomes source
        linkaddr_copy((linkaddr_t *)&msg.source_addr, &linkaddr_node_addr);
        msg.distance = 1;
        printf("Message has received its destination. Now sending RREP \n");
        // return;
    }
    // check if route to destination exist in routing table, otherwise re-broadcast
    // prepare search filter, find by destination address
    linkaddr_copy((linkaddr_t *)&filter.dest_addr, &msg.dest_addr);
    table_entry = search_row(filter);
    if (table_entry != NULL) {
        // record found in routing table
        printf("record found in table for destination %d.%d \n", table_entry->dest_addr.u8[0], table_entry->dest_addr.u8[1]);
        // start uni casting from here
        /* Copy data to the packet buffer */
        packetbuf_copyfrom(&msg, sizeof(struct collect_msg));
        msg.dest_seq = msg.source_seq;
        msg.source_seq = seq_no;
        msg.distance = 1;
        seq_no++;
        unicast_send(&uc, &table_entry->next_addr);
        free(table_entry);
        table_entry = NULL;
    } else {
        // route to destination not found in routing table, re-broadcast and insert routing table
        // create a new entry in routing table
        insert_row(msg, from);
        // printing routing table
        print_routing_table();
        // re-broadcasting
        msg.distance++;
        packetbuf_copyfrom(&msg, sizeof(struct collect_msg));
        /* Send broadcast packet RREQ */
        broadcast_send(&broadcast);
    }
}

static const struct broadcast_callbacks broadcast_callbacks = {recv_broadcast};

/*************************************************************************/
/* 
 * Callback function for unicast
 * Called when a packet has been received by the broadcast module
 */
static void
unicast_recv(struct unicast_conn *c, const linkaddr_t *from) {
	struct collect_msg msg;
	msg = *((struct collect_msg *)packetbuf_dataptr());
    printf("unicast message received from %d.%d:\n",
           from->u8[0], from->u8[1]);
    print_message(msg);

    // check if current node is the destintation node
    printf("Current node address %d.%d \n", linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
    if (linkaddr_cmp(&msg.dest_addr, &linkaddr_node_addr)) {
        // this is the destination node
        printf("Source node received acknowledgment");
        return;
    }
    // check if route to destination exist in routing table
    // prepare search filter, find by destination address
    struct table_record filter;
    struct table_record *table_entry = NULL;
    linkaddr_copy((linkaddr_t *)&filter.dest_addr, &msg.dest_addr);
    table_entry = search_row(filter);
    if (table_entry != NULL) {
        // record found in routing table, unicast it to destination
        printf("record found in table for destination %d.%d \n", table_entry->dest_addr.u8[0], table_entry->dest_addr.u8[1]);
        // start uni casting from here
        // msg.dest_seq = msg.source_seq;
        // msg.source_seq = seq_no;
        // msg.distance++ = 1;
        // seq_no++;
        // set forward pointer i.e store the information in routing table
        insert_row(msg, from);
        print_routing_table();
        msg.distance++;
        /* Copy data to the packet buffer */
        packetbuf_copyfrom(&msg, sizeof(struct collect_msg));
        unicast_send(&uc, &table_entry->next_addr);
        free(table_entry);
        table_entry = NULL;
    }
}

static const struct unicast_callbacks unicast_cb = {unicast_recv};

/******************************************************************************/

/**
 * Source node sending the message RREQ
 * When a user button is pressed, send the message to desitnation node i.e 8.0
*/
PROCESS_THREAD(pt_source, ev, data) {
	PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
	PROCESS_BEGIN();

    // set a broadcast connection at channel 130
	broadcast_open(&broadcast, 130, &broadcast_callbacks);
    // Set up a unicast connection at channel 146
    unicast_open(&uc, 146, &unicast_cb);
    
	SENSORS_ACTIVATE(button_sensor);

    // initialize the routing table
    list_init(routing_table);
    memb_init(&routing_table_mem);

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
            /* Send broadcast packet RREQ */
            broadcast_send(&broadcast);
            broadcast_id++;
            seq_no++;
        }
    }

    PROCESS_END();
}
