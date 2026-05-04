/*
  FujitsuAC - ESP32 library for controlling Fujitsu AC via HomeKit (HomeSpan)
  Serial debug console for live register inspection.

  Project home: https://github.com/Benas09/FujitsuAC
*/

#pragma once

#include <HomeSpan.h>
#include "FujitsuController.h"
#include "Enums.h"

namespace FujitsuAC {

    class SerialDebug {
    public:
        SerialDebug(FujitsuController &controller);

        void setup();

    private:
        FujitsuController &controller;
        bool liveLogging = false;

        void printStatus();
        void printAllRegisters();
        void printRegister(const char *hexAddr);

        static const char* modeString(uint16_t val);
        static const char* fanString(uint16_t val);
        static const char* onOff(uint16_t val);
        static const char* addressName(Address addr);
        static float tempFromRaw(uint16_t raw);
        static float setpointFromRaw(uint16_t raw);
    };

}
