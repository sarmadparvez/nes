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
static float time_on = CLOCK_SECOND;
static float time_off = CLOCK_SECOND / 2;
static int brightness_level = 0;

PROCESS_THREAD(btn_pt, ev, data) {
    PROCESS_BEGIN();
    
    SENSORS_ACTIVATE(button_sensor);
    
    // TODO: Implement here
    while (1)
    {
        PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && data == &button_sensor);
        // change brightness level on button click
        brightness_level++;
        if (brightness_level % 3 == 1) {
            printf("Brightness changed to 10%%\n");
            // change to 10 % brightness
            // time_on = (CLOCK_SECOND / 50 ) * 0.10;
            // time_off = (CLOCK_SECOND / 50 ) * 0.90;
            time_on = 0.02 * 0.10 * CLOCK_SECOND;
            time_off = 0.02 * 0.90 * CLOCK_SECOND;
        } else if (brightness_level % 3 == 2 ) {
            printf("Brightness changed to 50%%\n");
            // change to 50% brightness
            // time_on = (CLOCK_SECOND / 50 ) * 0.50;
            // time_off = (CLOCK_SECOND / 50 ) * 0.50;      
            time_on = 0.02 * 0.50 * CLOCK_SECOND;
            time_off = 0.02 * 0.50 * CLOCK_SECOND;    
        } else if (brightness_level % 3 == 0 ) {
            printf("Brightness changed to 90%%\n");
            // change to 90% brightness
            // time_on = (CLOCK_SECOND / 50 ) * 0.90;
            // time_off = (CLOCK_SECOND / 50 ) * 0.10;
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
    static bool off = true; // stores if the LED is on or off

    etimer_set(&timer, time_off);
    while(1) {
        // wait until timer is finished
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
        // toggle the leds
        //leds_toggle(LEDS_RED);
        // set the timer to toggle leds
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
    static unsigned long cpu_time = 0;
    static unsigned long led_time = 0;

    energest_init();
    etimer_set(&et, CLOCK_SECOND);
    while(1) {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
        // flush the times
        energest_flush();
        
        unsigned long new_cpu = energest_type_time(ENERGEST_TYPE_CPU);
        unsigned long new_led = energest_type_time(ENERGEST_TYPE_LED_RED);

        // unsigned long time_cpu = (new_cpu - cpu_time) * 1000 / RTIMER_SECOND;
        // unsigned long time_led = (new_led - led_time) * 1000 / RTIMER_SECOND;

        unsigned long time_cpu = (new_cpu - cpu_time) * 1000 / RTIMER_SECOND;
        unsigned long time_led = (new_led - led_time) * 1000 / RTIMER_SECOND;
        
        printf("Time - cpu: %lu (ms), leds: %lu (ms)\n", time_cpu, time_led);

        // calculate cpu and led time for last second
        // cpu_time = energest_type_time(ENERGEST_TYPE_CPU) - cpu_time;
        // led_time = energest_type_time(ENERGEST_TYPE_LED_RED) - led_time;
        // printf("CPU was active for %lu time ticks\n", cpu_time);
        // printf("RED light was active for %lu time ticks\n\n", led_time);

        /* Store the new values */
        cpu_time = new_cpu;
        led_time = new_led;
        // restart timer
        //etimer_restart(&et);
        etimer_reset(&et);
    }

    PROCESS_END();
}
