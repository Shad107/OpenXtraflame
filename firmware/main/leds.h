/**
 * openextraflame - status LEDs
 */

#pragma once

typedef enum {
    LED_STATE_UNKNOWN,
    LED_STATE_BOOT,
    LED_STATE_AP_MODE,
    LED_STATE_WIFI_OFFLINE,
    LED_STATE_MQTT_OFFLINE,
    LED_STATE_STOVE_OFFLINE,
    LED_STATE_ALL_OK,
    LED_STATE_ERROR,
} led_state_t;

void leds_init(void);
void leds_set_state(led_state_t state);
