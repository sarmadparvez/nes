#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include <stdio.h>
#include <stdlib.h>

#include "lib/list.h"
#include "lib/memb.h"

#include <stdbool.h>

PROCESS(pt_source, "Message source");
PROCESS(pt_timer, "Timer process");
PROCESS(pt_delete_reverse_pointer, "Reverse pointer deletion process");

AUTOSTART_PROCESSES(&pt_source);


// Maximum number of enteries in routing table
#define TABLE_SIZE 32

/** The number of seconds to wait for RREP before deleting the reverse pointer entries from routing table **/
/** Please set this to number of seconds based on the numnber of nodes/network size */
/** If number of nodes are increased for testing, increase it accordingly */
static uint8_t ACTIVE_ROUTE_TIMEOUT = 4;

// for broadcast connection
static struct broadcast_conn broadcast;

// for unicast connection
static struct unicast_conn uc;

// counter for sequence number
static uint32_t seq_no = 1;
// counter for broadcast id
static uint32_t broadcast_id = 1;

static struct etimer et1, et2;

// a struct representing a single record/row for routing table
struct table_record
{
    struct table_record *next; // pointer to the next record in table
    linkaddr_t dest_addr;  // address of the destination node
    linkaddr_t next_addr;  // address of the next node
    uint8_t distance;      // distance to destination node (hop count)
    uint32_t dest_seq;     // sequence number for destination node
    uint32_t broadcast_id; // the unique brodcast id for the message
};

// a struct representing a message that is sent from source to destination
struct route_msg
{
    linkaddr_t source_addr; // address of the source node
    uint32_t source_seq;    // sequence number of source node
    uint32_t broadcast_id;  // the unique brodcast id for the message
    uint32_t dest_seq;      // sequence number of destination node
    linkaddr_t dest_addr;   // address of the destination node
    uint8_t distance;       // distance travelled so far (hope count)
    bool is_print_only; // when this is true, just print the route/path to destination
};

// declare a list representing the routing table
LIST(routing_table);
MEMB(routing_table_mem, struct table_record, TABLE_SIZE);

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
 * Create a new entry in routing table
*/
static void insert_row(struct route_msg msg, const linkaddr_t *from) {
    struct table_record *tr = NULL;
    tr = memb_alloc(&routing_table_mem);
    linkaddr_copy((linkaddr_t *)&tr->dest_addr, &msg.source_addr);
    linkaddr_copy((linkaddr_t *)&tr->next_addr, from);
    tr->dest_seq = msg.source_seq;
    tr->distance = msg.distance;
    tr->broadcast_id = msg.broadcast_id;

    // create a new entry in routing table if it not exists previously
    struct table_record filter;
    struct table_record *table_entry = NULL;
    linkaddr_copy((linkaddr_t *)&filter.dest_addr, &msg.source_addr);
    table_entry = search_row(filter);
    if (table_entry != NULL) {
        linkaddr_copy((linkaddr_t *)&table_entry->dest_addr, &msg.source_addr);
        linkaddr_copy((linkaddr_t *)&table_entry->next_addr, from);
        table_entry->dest_seq = msg.source_seq;
        table_entry->distance = msg.distance;
        table_entry->broadcast_id = msg.broadcast_id;
        free(table_entry);
    } else {
        list_push(routing_table, tr);
    }
}

/**
 * This function inserts or updates a route in routing table on RREP (route reply request)
*/
static bool upsert_route_for_REP(struct route_msg msg, const linkaddr_t *from) {
    struct table_record filter;
    linkaddr_copy((linkaddr_t *)&filter.dest_addr, &msg.source_addr);
    struct table_record *table_entry = search_row(filter);
    // Check if RREP for this request is already sent, if it is already sent
    if (table_entry != NULL && table_entry->broadcast_id == msg.broadcast_id) {
        printf("This RREP is already sent------------------------------------------- \n");
        // this RREP is already forwarded, only resend it if either
        // 1. This RREP has greater dest seq number OR
        // 2. Same dest seq number with smaller hop count
        if ((msg.dest_seq > table_entry->dest_seq ) || 
            (msg.dest_seq == table_entry->dest_seq && table_entry->distance > msg.distance)) {
            printf("This is a better route and is updated in routing table \n");
            // update the route infromation in table
            table_entry->distance = msg.distance;
            linkaddr_copy((linkaddr_t *)&table_entry->next_addr, from);
            table_entry->dest_seq = msg.source_seq;
        } else {
            // don't reforward RREP
            return false;
        }
    } else {
        // set forward pointer i.e store the information in routing table
        insert_row(msg, from);
    }
    return true;
}

/**
 * Print the routing table.
*/
static void print_routing_table()
{
    printf("printing routing table \n");
    struct table_record *tr = NULL;
    uint8_t row = 1;
    // print the contents of routing table
    for (tr = list_head(routing_table); tr != NULL; tr = list_item_next(tr))
    {
        printf("row %u: dest addr %d.%d, next %d.%d, distance %u, dest seq %lu, broadcast id %lu \n",row, tr->dest_addr.u8[0], tr->dest_addr.u8[1], tr->next_addr.u8[0], tr->next_addr.u8[1] , tr->distance, tr->dest_seq, tr->broadcast_id);
        row++;
    }
}

/**
 * Print a message
*/
static void print_message(struct route_msg msg)
{
    printf("message: source address: %d.%d, source seq: %lu, broadcast id: %lu, dest address: %d.%d, dest seq: %lu, hop count: %u \n",
           msg.source_addr.u8[0], msg.source_addr.u8[1], msg.source_seq, msg.broadcast_id, msg.dest_addr.u8[0], msg.dest_addr.u8[1], msg.dest_seq, msg.distance);
}

/**
 * Send a unicast message
 */
static void send_unicast_msg(struct route_msg msg, linkaddr_t dest, struct table_record *table_entry) {
    /* Copy data to the packet buffer */
    packetbuf_copyfrom(&msg, sizeof(struct route_msg));
    unicast_send(&uc, &dest);
    free(table_entry);
    table_entry = NULL;  
}

// Start broadcasting a message from source node
static void start_broadcast(struct route_msg msg) {
    /* Copy data to the packet buffer */
    packetbuf_copyfrom(&msg, sizeof(struct route_msg));
    /* Send broadcast packet RREQ */
    broadcast_send(&broadcast);
}

/*************************************************************************/
/* 
 * Callback function for broadcast
 * Called when a packet has been received by the broadcast module
 */
static void
recv_broadcast(struct broadcast_conn *c, const linkaddr_t *from) {
	struct route_msg msg;
	msg = *((struct route_msg *)packetbuf_dataptr());
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
    bool is_destination = false; // true if this is the destination node
    // check if current node is the destination node
    if (linkaddr_cmp(&msg.dest_addr, &linkaddr_node_addr)) {
        is_destination = true;
        // this is the destination node, prepare a route reply RREP
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
        printf("Message has received its destination. \n");
    }
    // check if route to destination exist in routing table, otherwise re-broadcast
    // prepare search filter, find by destination address
    linkaddr_copy((linkaddr_t *)&filter.dest_addr, &msg.dest_addr);
    table_entry = search_row(filter);
    linkaddr_t next_addr;

    // an intermediate node can only reply on behalf of destination if destination
    // sequence number in route table is grater than or equal to the one which is in REQUEST
    if (table_entry != NULL && ((table_entry->dest_seq >= msg.dest_seq) || is_destination ) && table_entry->distance != UINT8_MAX) {
        // record found in routing table
        printf("record found in table for destination %d.%d. Now sending RREP \n", table_entry->dest_addr.u8[0], table_entry->dest_addr.u8[1]);
        if (!linkaddr_cmp(&msg.dest_addr, &linkaddr_node_addr) && !linkaddr_cmp(&msg.source_addr, &linkaddr_node_addr)) {
            // This is not destination nor source node
            // create a new entry in routing table
            insert_row(msg, from);
            print_routing_table();
            // The route to destination is found on this is an intermediate node, send RREP
            msg.dest_seq = table_entry->dest_seq;
            msg.distance = table_entry->distance + 1;
            // the source becomes destination
            linkaddr_copy((linkaddr_t *)&msg.dest_addr, &msg.source_addr);
            // the destination becomes source
            linkaddr_copy((linkaddr_t *)&msg.source_addr, &table_entry->dest_addr);
            linkaddr_copy((linkaddr_t *)&next_addr, from);
        } else {
            // this is the destination node
            uint8_t temp_dest_seq = msg.source_seq;
            if (msg.dest_seq > seq_no) {
                msg.source_seq = msg.dest_seq;
            } else {
                msg.source_seq = seq_no;
            }
            msg.dest_seq = temp_dest_seq;
            msg.distance = 1;
            linkaddr_copy((linkaddr_t *)&next_addr, &table_entry->next_addr);
        }
        
        // start uni casting from here
        // send unicast message
        send_unicast_msg(msg, next_addr, table_entry);
        // seq_no++;
    } else {
        // route to destination not found in routing table, re-broadcast and insert in routing table
        insert_row(msg, from);
        // printing routing table
        print_routing_table();
        // re-broadcasting
        msg.distance++;
        packetbuf_copyfrom(&msg, sizeof(struct route_msg));
        printf("Broadcasting again \n");
        /* Send broadcast packet RREQ */
        broadcast_send(&broadcast);
        process_start(&pt_delete_reverse_pointer, (linkaddr_t *)&msg.source_addr);
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
    char *ackk = packetbuf_dataptr();
    // if the acknowledgment is received from neighbour node, exit the timer process
    // we no longer need to send RERR because our immediate neighbour towards the destination
    // replied back, which means the link is not broken.
    if (strcmp(ackk, "ack") == 0 && etimer_pending()) {
        // expire the timer
        // etimer_stop(&et);
        process_exit(&pt_timer);
        return;
    }
    
    // struct route_msg msg;
    struct route_msg msg;
    msg = *((struct route_msg *)packetbuf_dataptr());
    
    // prepare search filter,  and search route to destination address in routing table
    struct table_record filter;
    struct table_record *table_entry = NULL;
    linkaddr_copy((linkaddr_t *)&filter.dest_addr, &msg.dest_addr);
    table_entry = search_row(filter);

    if (msg.is_print_only == true) {
        // this request is only for printing route to destination
        // every node till destination just prints its address
        printf("%d.%d \n", linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
        if (table_entry != NULL) {
            msg.distance++;
            if (!linkaddr_cmp(&table_entry->next_addr, from)) {
                // send unicast message
                send_unicast_msg(msg, table_entry->next_addr, table_entry);
            }
            // initiate the timer process
            process_start(&pt_timer, (struct route_msg*)&msg);
        } 
        // sending an acknowledgment back to previous neighbour
        packetbuf_copyfrom("ack", 3);
        unicast_send(&uc, from);
        return;
    } else if (msg.distance == UINT8_MAX) {
        // this is a RERR, set hop count to infinity
        struct table_record filter1;
        struct table_record *table_entry1 = NULL;
        linkaddr_copy((linkaddr_t *)&filter1.dest_addr, &msg.source_addr);
        table_entry1 = search_row(filter1);   
        if (table_entry1 != NULL) {
            table_entry1->distance = UINT8_MAX;
            free(table_entry1);
            print_routing_table();
            if (linkaddr_cmp(&msg.dest_addr, &linkaddr_node_addr)) {
                // if current node is the actual source node which initiated request
                return;
            } else {
                // send unicast message i.e propagate RERR backwards
                printf("Propagating RERR backwards to %d.%d ", table_entry->next_addr.u8[0], table_entry->next_addr.u8[1]);
                send_unicast_msg(msg, table_entry->next_addr, table_entry);
                return;
            }
        }
    }
    
    // unicast message is received which means this node is on the path of RREP
    // we need to exit the timer process which deletes reverse pointer entries
    if (etimer_pending()) {
        process_exit(&pt_delete_reverse_pointer);
    }

    printf("unicast message received from %d.%d:\n",
           from->u8[0], from->u8[1]);
    print_message(msg);

    // check if current node is the destintation node for RREP (i.e the source which initiated RREQ)
    if (linkaddr_cmp(&msg.dest_addr, &linkaddr_node_addr)) {
        // this is the destination node
        printf("Source node received acknowledgment -------------------------------------- \n");
        // this may be a re-acknowledgment due to a better route found. update the route information if required
        // Or if this route is not saved, save it in routing table.
        upsert_route_for_REP(msg, from);
        print_routing_table();
        return;
    }

    // check if route to destination exist in routing table
    if (table_entry != NULL) {
        // record found in routing table i.e this node has route to destination.
        printf("record found in table for destination %d.%d \n", table_entry->dest_addr.u8[0], table_entry->dest_addr.u8[1]);
        // send RREP on behalf of destination only if destination sequence number in
        // routing table >= destination sequence number in request.
        if (table_entry->dest_seq >= msg.dest_seq) {
            if (!upsert_route_for_REP(msg, from)) {
                return;
            }
            print_routing_table();
            // start uni casting from here
            msg.distance++;
            // send unicast message
            send_unicast_msg(msg, table_entry->next_addr, table_entry);
        } else {
            // the route is outdated
        }
    }
}

static const struct unicast_callbacks unicast_cb = {unicast_recv};

/******************************************************************************/

/**
 * This process waits for user button press, when the user button is pressed,
 * if a route to destination node is not available, it initiates route discovery (RREQ)
*/
PROCESS_THREAD(pt_source, ev, data)
{
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
        // first check in routing table if the route to destination is available
        struct table_record filter;
        struct table_record *table_entry = NULL;
        // prepare filter to search in table
        linkaddr_copy((linkaddr_t *)&filter.dest_addr, &addr);
        table_entry = search_row(filter);
        // if this is not the destination node itself
        if (!linkaddr_cmp(&addr, &linkaddr_node_addr))
        {
            // Prepare RREQ
            struct route_msg msg;
            msg.broadcast_id = broadcast_id;
            msg.distance = 1;
            msg.source_seq = seq_no;
            msg.dest_seq = 0; // the destination seq number is unknown initially
            msg.is_print_only = false;
            // copy source address
            linkaddr_copy((linkaddr_t *)&msg.source_addr, &linkaddr_node_addr);
            // copy destination address
            linkaddr_copy((linkaddr_t *)&msg.dest_addr, &addr);
            if (table_entry == NULL)
            {
                start_broadcast(msg);
            }
            else if (table_entry->distance == UINT8_MAX)
            {
                // hop count is infinity, do the broadcast after incrementing the sequence number
                msg.dest_seq = table_entry->dest_seq + 1;
                start_broadcast(msg);
            }
            else
            {
                // we have a route to destination and we can send data
                printf("This node already has route to the destination. The route is printed below. \n");
                // print_routing_table();
                msg.dest_seq = table_entry->dest_seq;
                // sending actual data to destination, this request just prints the path from
                // source node to destination node to prove that the routing table is built correctly
                // and the node has path to destination
                msg.is_print_only = true; // if true, its just a route printing request, not a route discovery
                // every node till destination just prints its address
                printf("%d.%d \n", linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1]);
                /* Copy data to the packet buffer */
                packetbuf_copyfrom(&msg, sizeof(struct route_msg));
                unicast_send(&uc, &table_entry->next_addr);
                process_start(&pt_timer, (struct route_msg*)&msg);
            }
        }
        broadcast_id++;
    }
    PROCESS_END();
}

// To store the message for timer process
static struct route_msg msg_global;

/**
 * On timeout perform RERR . Set hop count to infinity and propagate this message back to actual source node 
*/
PROCESS_THREAD(pt_timer, ev, data)
{
	PROCESS_BEGIN();
    // 3 seconds are enough to wait for acknowledgment from immediate neighbour
	msg_global = *((struct route_msg *)data);
    etimer_set(&et1, CLOCK_SECOND * 4);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et1));
    
    // set the hope count to infinity (i.e UINT8_MAX) in current node first 
    struct table_record filter1;
    linkaddr_copy((linkaddr_t *)&filter1.dest_addr, &msg_global.dest_addr);
    struct table_record *table_entry1 = search_row(filter1);
    if (table_entry1 != NULL) {
        printf("Error detected. Reply not received within timout from node %d.%d.\n", table_entry1->next_addr.u8[0], table_entry1->next_addr.u8[1]);
        printf("Setting hop count to infinity for this and previous nodes \n");
        table_entry1->distance = UINT8_MAX;
        free(table_entry1);
    }
    
    // Now send RERR to source node i.e informing about the error so that all nodes
    // till source node set the hop count to infinity

    linkaddr_t temp_addr;
    linkaddr_copy((linkaddr_t *)&temp_addr, &msg_global.dest_addr);
    // the source becomes destination and destination becomes soruce (For sending RERR)
    linkaddr_copy((linkaddr_t *)&msg_global.dest_addr, &msg_global.source_addr);
    linkaddr_copy((linkaddr_t *)&msg_global.source_addr, &temp_addr);

    msg_global.distance = UINT8_MAX;

    struct table_record filter;
    linkaddr_copy((linkaddr_t *)&filter.dest_addr, &msg_global.dest_addr);
    struct table_record *table_entry = search_row(filter);
    if (table_entry != NULL && !linkaddr_cmp(&table_entry->next_addr, &linkaddr_node_addr)) {
        // list_remove(routing_table, table_entry);
        print_routing_table();
        // printf("Broadcasting again to find new route \n");
        msg_global.is_print_only = false;
        // send unicast message
        send_unicast_msg(msg_global, table_entry->next_addr, table_entry);
        // broadcast_id++;
    }
	PROCESS_END();
}

/**
 * The nodes that are on the path discovered by RREP are deleted after a certain timeout
*/
PROCESS_THREAD(pt_delete_reverse_pointer, ev, data)
{
	PROCESS_BEGIN();
    // 5 seconds are enough to wait for RREP from destination
	static linkaddr_t dest_addr;
    linkaddr_copy((linkaddr_t *)&dest_addr, &(*((linkaddr_t *)data)));
    etimer_set(&et2, CLOCK_SECOND * ACTIVE_ROUTE_TIMEOUT);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et2));
    struct table_record filter;
    linkaddr_copy((linkaddr_t *)&filter.dest_addr, &dest_addr);
    struct table_record *table_entry = search_row(filter);

    if (table_entry != NULL) {
        list_remove(routing_table, table_entry);
        printf("Reverse pointer deleted because the node was not on the path of RREP.\n");
        print_routing_table();
    }
	PROCESS_END();
}
