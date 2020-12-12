#include <stdio.h>
#include "contiki.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include <stdbool.h> 

PROCESS(led_pt, "Blink the LED");
PROCESS(btn_pt, "Handle button pressed");
PROCESS(energy_pt, "Energy estimation");

AUTOSTART_PROCESSES(&led_pt, &btn_pt, &energy_pt);

/* LED on/off durations */ 
// Homework Part 2 (3): duration is set to 1 clock second for "On" state, and half clock second for "Off" state
static float time_on = CLOCK_SECOND;
static float time_off = CLOCK_SECOND / 2;
static int brightness_level = 0; // to calculate the level i.e 10%, 50% or 90%

PROCESS_THREAD(btn_pt, ev, data) {
    PROCESS_BEGIN();
    
    SENSORS_ACTIVATE(button_sensor);
    
    // TODO: Implement here
    while (1)
    {
        PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && data == &button_sensor);
        // change brightness level on button click
        brightness_level++;
        // calculation of duty cycle:
        // For example: for 10% brightness
        // Given frequencey = 50hz i.e t = 1/50 = 0.02s
        // For 10% duty cycle , led should be on 10% of the time and off 90% of the time.
        if (brightness_level % 3 == 1) {
            printf("Brightness changed to 10%%\n");
            // change to 10 % brightness
            time_on = 0.02 * 0.10 * CLOCK_SECOND;
            time_off = 0.02 * 0.90 * CLOCK_SECOND;
        } else if (brightness_level % 3 == 2 ) {
            printf("Brightness changed to 50%%\n");
            // change to 50% brightness  
            time_on = 0.02 * 0.50 * CLOCK_SECOND;
            time_off = 0.02 * 0.50 * CLOCK_SECOND;    
        } else if (brightness_level % 3 == 0 ) {
            printf("Brightness changed to 90%%\n");
            // change to 90% brightness
            time_on = 0.02 * 0.90 * CLOCK_SECOND;
            time_off = 0.02 * 0.10 * CLOCK_SECOND;      
        }
        PROCESS_PAUSE();
    }
    
    PROCESS_END();
}

PROCESS_THREAD(led_pt, ev, data) {
    static struct etimer timer;
    PROCESS_BEGIN();
    // TODO: Implement here
    static bool off = true; // stores on/off state for LED

    etimer_set(&timer, time_off);
    while(1) {
        // wait until timer is finished
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
        // toggle the leds
        if (off) {
            // led is off, turn it on
            leds_on(LEDS_RED);
            etimer_set(&timer, time_on);
        } else {
            // led is on, turn it off
            leds_off(LEDS_RED);
            etimer_set(&timer, time_off);
        }
        // toggle the boolean variable
        off = !off;
    }
    
    PROCESS_END();
}

PROCESS_THREAD(energy_pt, ev, data) {
    PROCESS_BEGIN();
    
    static struct etimer et;
    /* Real-time clock */
    printf("RTIMER_SECOND: %u\n", RTIMER_SECOND);

    // TODO: Implement here
    static unsigned long old_time_cpu, old_time_led, new_time_cpu, new_time_led, time_cpu, time_led = 0;

    energest_init();
    // calculate cpu and led time
    old_time_cpu = energest_type_time(ENERGEST_TYPE_CPU);
    old_time_led = energest_type_time(ENERGEST_TYPE_LED_RED);
    etimer_set(&et, CLOCK_SECOND);
    // calculate cpu and led time for each second till the program is running
    while(1) {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
        // flush the times
        energest_flush();
        
        // calculate cpu and led time for last second
        new_time_cpu = energest_type_time(ENERGEST_TYPE_CPU);
        new_time_led = energest_type_time(ENERGEST_TYPE_LED_RED);

        // calculate time in microseconds i.e by dividing time difference(no of ticks) with RTIMER_SECOND (tick possible in 1 second) and
        // multiplying with 1000 to convert it to milli seconds
        time_cpu = (new_time_cpu - old_time_cpu) * 1000 / RTIMER_SECOND;
        time_led = (new_time_led - old_time_led) * 1000 / RTIMER_SECOND;
        
        printf("Time: cpu = %lu (ms), led = %lu (ms)\n", time_cpu, time_led);

        /* Store the new values */
        old_time_cpu = new_time_cpu;
        old_time_led = new_time_led;
        // reset timer
        etimer_reset(&et);
    }

    PROCESS_END();
}
