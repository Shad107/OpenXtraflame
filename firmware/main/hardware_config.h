/**
 * openextraflame - hardware config
 *
 * Selects GPIO mapping based on target:
 *  - TARGET_EXTERNAL   : ESP32 spare board wired to stove UART
 *  - TARGET_BLACKLABEL : firmware replaces Extraflame Black Label original
 *
 * Defined via CMake -DTARGET=external|blacklabel
 */

#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"

/* --------------------------------------------------------------------------
 * TARGET_EXTERNAL - spare ESP32 wired manually to the stove
 * ------------------------------------------------------------------------ */
#ifdef TARGET_EXTERNAL

    /* UART to stove (=Micronova bus) - config Micronova standard.
     *
     * PIN CHOICE 2026-07-07 : match M5Stack Atom Lite exposed GPIOs.
     * Atom Lite bottom pins (=accessible sans démontage) : G19 G21 G22 G23 G25 G33.
     * On utilise :
     *   TX = GPIO23 : câble MARRON du connecteur SERIAL 4-pin du poêle
     *   RX = GPIO19 : câble BLANC
     *   GND = pin GND : câble VERT (=jamais toucher JAUNE = +12V !)
     *
     * Baud d'application forcé à 1200 8N2 dans micronova.c au boot (=cf. reverse
     * philibertc/ridiculouslab). Les constantes ci-dessous sont ignorées côté
     * runtime, elles servent uniquement à la première configuration UART. */
    #define STOVE_UART_NUM              UART_NUM_1
    #define STOVE_UART_TX_PIN           GPIO_NUM_23  /* MARRON câble poêle */
    #define STOVE_UART_RX_PIN           GPIO_NUM_19  /* BLANC câble poêle */
    #define STOVE_UART_BAUD             1200
    #define STOVE_UART_DATA_BITS        UART_DATA_8_BITS
    #define STOVE_UART_PARITY           UART_PARITY_DISABLE
    #define STOVE_UART_STOP_BITS        UART_STOP_BITS_2

    // Status LED (single onboard)
    #define STATUS_LED_ENABLED          1
    #define STATUS_LED_PIN              GPIO_NUM_2
    #define STATUS_LED_INVERTED         0

    // Config button (=onboard BOOT button)
    #define CONFIG_BUTTON_ENABLED       1
    #define CONFIG_BUTTON_PIN           GPIO_NUM_0
    #define CONFIG_BUTTON_ACTIVE_LOW    1

    // Multiple LEDs disabled (=single onboard LED only)
    #define LED_POWER_PIN               (-1)
    #define LED_BLE_PIN                 (-1)
    #define LED_WIFI_PIN                (-1)
    #define LED_SERVER_PIN              (-1)

    // Board identity
    #define BOARD_NAME                  "external-esp32"
    #define AP_SSID_PREFIX              "OpenXtraflame_"

#endif

/* --------------------------------------------------------------------------
 * TARGET_BLACKLABEL - reflash Extraflame Black Label T009_3
 *
 * ⚠️ GPIO mapping is INFERRED from datasheet + educated guess.
 *    Must be verified via Ghidra decompilation of ota0.bin OR
 *    empirical testing (LED lights up when pin is toggled).
 * ------------------------------------------------------------------------ */
#ifdef TARGET_BLACKLABEL

    // UART to stove (=SERIAL 4-pin cable)
    // Wires : green=GND, brown=TX, white=RX, yellow=+12V
    // ⭐ CONFIRMÉ 100% VIA QEMU + GDB 2026-07-03 :
    //   Breakpoint sur uart_set_pin de Extraflame binary
    //   Args capturés à runtime :
    //   → uart=1 tx=23 rx=5 rts=255 cts=255
    //   Boot log serial confirme : Baud 38400, 8N1, flow_ctrl 0
    #define STOVE_UART_NUM              UART_NUM_1
    #define STOVE_UART_TX_PIN           GPIO_NUM_23  // ⭐ QEMU GDB captured
    #define STOVE_UART_RX_PIN           GPIO_NUM_5   // ⭐ QEMU GDB captured
    #define STOVE_UART_BAUD             38400        // ⭐ QEMU confirmed
    #define STOVE_UART_DATA_BITS        UART_DATA_8_BITS
    #define STOVE_UART_PARITY           UART_PARITY_DISABLE
    #define STOVE_UART_STOP_BITS        UART_STOP_BITS_1

    // 4 LEDs status POWER, BLE, WIFI, SERVER
    // ⭐ CAPTURÉ VIA QEMU + GDB 2026-07-03 :
    //   GPIOs observés changeant d'état (gpio_set_level) :
    //   → GPIO 22, 25, 26, 32, 33 = 5 GPIOs LEDs actives
    //   Mapping exact aux 4 LEDs (POWER/BLE/WIFI/SERVER) à finaliser
    //   par observation quel LED s'allume physiquement quand chaque GPIO change.
    #define STATUS_LED_ENABLED          0  // Uses per-status LEDs instead
    #define LED_POWER_PIN               GPIO_NUM_25  // ⭐ QEMU observed
    #define LED_BLE_PIN                 GPIO_NUM_26  // ⭐ QEMU observed
    #define LED_WIFI_PIN                GPIO_NUM_32  // ⭐ QEMU observed
    #define LED_SERVER_PIN              GPIO_NUM_33  // ⭐ QEMU observed
    // GPIO 22 : additional LED or GPIO output (=à identifier)
    #define LED_ACTIVE_HIGH             1

    // Reset button (=tactile switch chromé sur PCB Black Label)
    // TODO : capture via QEMU en simulant press du bouton (=GPIO input)
    #define CONFIG_BUTTON_ENABLED       1
    #define CONFIG_BUTTON_PIN           GPIO_NUM_0   // TODO confirm via test
    #define CONFIG_BUTTON_ACTIVE_LOW    1

    // Board identity - reste compatible avec sticker Black Label
    #define BOARD_NAME                  "blacklabel-t009_3"
    #define AP_SSID_PREFIX              "OpenXtraflame_"

#endif

/* --------------------------------------------------------------------------
 * Guard: at least one target must be defined
 * ------------------------------------------------------------------------ */
#if !defined(TARGET_EXTERNAL) && !defined(TARGET_BLACKLABEL)
    #error "No target defined. Use -DTARGET=external or -DTARGET=blacklabel"
#endif
