/*
  FujitsuAC - ESP32 library for controlling Fujitsu AC via HomeKit (HomeSpan)
  Based on original work by Benas Ragauskas.

  Project home: https://github.com/Benas09/FujitsuAC
*/

#pragma once

#include <HomeSpan.h>
#include <Preferences.h>

#ifndef FUJITSU_ENABLE_OTA
    #define FUJITSU_ENABLE_OTA 0
#endif

#ifndef FUJITSU_ENABLE_SERIAL_DEBUG
    #define FUJITSU_ENABLE_SERIAL_DEBUG 1
#endif

#ifndef FUJITSU_ENABLE_OUTDOOR_TEMP
    #define FUJITSU_ENABLE_OUTDOOR_TEMP 1
#endif

#ifndef FUJITSU_ENABLE_FEATURE_SWITCHES
    #define FUJITSU_ENABLE_FEATURE_SWITCHES 1
#endif

#ifndef FUJITSU_ENABLE_VANES
    #define FUJITSU_ENABLE_VANES 1
#endif

#include "Uart.h"
#include "FujitsuController.h"
#include "HomeSpanBridge.h"
#if FUJITSU_ENABLE_SERIAL_DEBUG
    #include "SerialDebug.h"
#endif

namespace FujitsuAC {

    class FujitsuAC {
    public:
        FujitsuAC(
            int rxPin,
            int txPin,
            int statusLedPin = -1,
            int controlPin   = -1
        );

        void setup();
        void loop();

    private:
        Preferences preferences;

        Uart uart;
        FujitsuController controller;

        HomeSpanBridge *bridge      = nullptr;
    #if FUJITSU_ENABLE_SERIAL_DEBUG
        SerialDebug    *serialDebug = nullptr;
    #endif

        String deviceName;

        int statusLedPin;
        int controlPin;

        void loadConfig();
    };

}