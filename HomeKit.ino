/*
  FujitsuAC HomeKit Controller
  ════════════════════════════════════════════════════════════════

  Native Apple HomeKit integration for Fujitsu AC units using
  HomeSpan. No MQTT broker or HomeAssistant required.

  WiFi Setup:
    On first boot, open Serial Monitor and type 'W' to configure
    WiFi credentials. Alternatively, double-press the control
    button to start the setup access point.

  HomeKit Pairing:
    1. Open the Apple Home app → Add Accessory
    2. Select "Fujitsu AC" (or scan the QR code from Serial)
    3. Default setup code: 466-37-726
       (change via Serial command 'S')

  Serial Debug Commands:
    X   - Dump current AC status
    Y   - Dump all registers
    Y xxxx - Dump specific register (hex address)
    Z   - Toggle live register-change logging
    ?   - HomeSpan help (WiFi, pairing, etc.)

  Pin Configuration (adjust for your board):
    RXD2w          - UART RX from AC unit
    TXD2          - UART TX to AC unit
    STATUS_LED    - Status LED (HomeSpan blinks during setup)
    CONTROL_BTN   - Multi-function button
                    single-press: print WiFi info
                    double-press: start AP config mode
                    long-press:   factory reset

  Project home: https://github.com/Benas09/FujitsuAC
*/

// Flash size profile (defaults are set in FujitsuAC.h).
// Turn these to 1 if you need the extra HomeKit accessories/features.
#define FUJITSU_ENABLE_OTA              1
#define FUJITSU_ENABLE_SERIAL_DEBUG     1
#define FUJITSU_ENABLE_OUTDOOR_TEMP     1
#define FUJITSU_ENABLE_FEATURE_SWITCHES 1
#define FUJITSU_ENABLE_VANES            1

#include <FujitsuAC.h>


// ── UART pins (AC unit communication) ─────────────────────────
#define RXD2 16
#define TXD2 17

// ── Board-specific GPIO ───────────────────────────────────────
// GPIO 2  = built-in LED on most ESP32 dev boards
// GPIO 0  = BOOT button (already on most ESP32 dev boards)
#if CONFIG_IDF_TARGET_ESP32S3
    #define STATUS_LED    2
    #define CONTROL_BTN   0
#elif CONFIG_IDF_TARGET_ESP32
    #define STATUS_LED    2
    #define CONTROL_BTN   0
#endif

#ifndef STATUS_LED
    #define STATUS_LED   -1
#endif

#ifndef CONTROL_BTN
    #define CONTROL_BTN  -1
#endif

FujitsuAC::FujitsuAC fujitsuAC(RXD2, TXD2, STATUS_LED, CONTROL_BTN);

void setup() {
    fujitsuAC.setup();
}

void loop() {
    fujitsuAC.loop();

}
