/**
 * openextraflame - LED status indicators
 *
 * TARGET_EXTERNAL: single LED, blinks per state pattern
 * TARGET_BLACKLABEL: 4 LEDs (POWER, BLE, WIFI, SERVER) - repurposed
 */

#include "leds.h"
#include "hardware_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static led_state_t current_state = LED_STATE_UNKNOWN;

static void gpio_output(int pin)
{
    if (pin < 0) return;
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
}

void leds_init(void)
{
#if STATUS_LED_ENABLED
    gpio_output(STATUS_LED_PIN);
#endif
#ifdef TARGET_BLACKLABEL
    gpio_output(LED_POWER_PIN);
    gpio_output(LED_BLE_PIN);
    gpio_output(LED_WIFI_PIN);
    gpio_output(LED_SERVER_PIN);
#endif
    leds_set_state(LED_STATE_BOOT);
}

static void set_pin(int pin, int on)
{
    if (pin < 0) return;
#ifdef LED_ACTIVE_HIGH
    gpio_set_level(pin, on ? 1 : 0);
#else
    gpio_set_level(pin, on ? 0 : 1);
#endif
}

void leds_set_state(led_state_t state)
{
    current_state = state;

#if STATUS_LED_ENABLED
    /* Simple on/off for the single onboard LED - could be blink patterns */
    int on = (state == LED_STATE_ALL_OK) ? 1 : 0;
    #if STATUS_LED_INVERTED
    gpio_set_level(STATUS_LED_PIN, on ? 0 : 1);
    #else
    gpio_set_level(STATUS_LED_PIN, on ? 1 : 0);
    #endif
#endif

#ifdef TARGET_BLACKLABEL
    /* Reuse Extraflame's 4 LEDs as our own status */
    set_pin(LED_POWER_PIN,  1);                     /* always on when powered */
    set_pin(LED_BLE_PIN,    state == LED_STATE_AP_MODE);
    set_pin(LED_WIFI_PIN,   state != LED_STATE_WIFI_OFFLINE
                          && state != LED_STATE_AP_MODE
                          && state != LED_STATE_BOOT);
    set_pin(LED_SERVER_PIN, state == LED_STATE_ALL_OK
                          || state == LED_STATE_STOVE_OFFLINE);
#endif
}
