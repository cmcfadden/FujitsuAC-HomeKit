/*
  FujitsuAC - ESP32 library for controlling Fujitsu AC via HomeKit (HomeSpan)
  Based on original work by Benas Ragauskas.

  HomeSpan handles: WiFi provisioning, HAP pairing, status LED,
  control button (factory reset), and OTA updates.

  Project home: https://github.com/Benas09/FujitsuAC
*/

#include "FujitsuAC.h"
#include <WiFi.h>

#define VERSION "2.0.0"

namespace FujitsuAC {

    FujitsuAC::FujitsuAC(int rxPin, int txPin, int statusLedPin, int controlPin)
        : preferences(),
          uart(UART_NUM_2, rxPin, txPin),
          controller(uart),
          statusLedPin(statusLedPin),
          controlPin(controlPin)
    {}

    void FujitsuAC::setup() {
        Serial.begin(115200);

        this->loadConfig();

        // ── HomeSpan configuration ────────────────────────────────
        //  Status LED    – blinks during WiFi/pairing, solid when paired
        //  Control pin   – single-press: WiFi info
        //                  double-press: start AP mode
        //                  long-press:   factory reset

        if (statusLedPin >= 0) {
            homeSpan.setStatusPin(statusLedPin);
        }

        if (controlPin >= 0) {
            homeSpan.setControlPin(controlPin);
        }

        // If no credentials are stored, HomeSpan auto-starts setup AP.
        homeSpan.enableAutoStartAP();

    // #if FUJITSU_ENABLE_OTA
        homeSpan.enableOTA();
    // #endif
        Serial.println(F("Starting HomeSpan..."));
        homeSpan.begin(
            Category::AirConditioners,
            deviceName.c_str(),
            "FujitsuAC",
            "fAir"
        );

        bootTimeMs          = millis();
        wifiConnected       = false;
        apFallbackTriggered = false;

        // ── Create HomeKit accessories ────────────────────────────
        bridge = new HomeSpanBridge(controller, deviceName.c_str(), VERSION);
        bridge->setup();

        // ── Serial debug commands ─────────────────────────────────
    #if FUJITSU_ENABLE_SERIAL_DEBUG
        serialDebug = new SerialDebug(controller);
        serialDebug->setup();
    #endif

        // ── Start the AC protocol controller ──────────────────────
        controller.setup();

    #if FUJITSU_ENABLE_SERIAL_DEBUG
        Serial.println(F("\nFujitsuAC HomeKit (" VERSION ")"));
        Serial.println(F("Type '@X' for status, '@Y' for regs, '@Z' for live data"));
        Serial.println(F("Type '?' for all HomeSpan commands\n"));
    #endif
    }

    void FujitsuAC::loop() {
        homeSpan.poll();

        if (!wifiConnected && WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
        }

        if (!wifiConnected && !apFallbackTriggered && millis() >= bootTimeMs) {
            auto [status, statusDurationSec] = homeSpan.getStatus();

            if (
                status == HS_WIFI_CONNECTING &&
                statusDurationSec >= WIFI_CONNECT_FALLBACK_TIMEOUT_SEC
            ) {
                Serial.println(F("WiFi connect timeout - starting HomeSpan setup AP..."));
                homeSpan.processSerialCommand("A");
                apFallbackTriggered = true;
            }
        }

        controller.loop();
        
    }

    void FujitsuAC::loadConfig() {
        preferences.begin("fujitsu_ac", true);
        deviceName = preferences.getString("device-name", "Fujitsu AC");
        preferences.end();

        if (deviceName.isEmpty()) {
            deviceName = "Fujitsu AC";
        }
    }

}

