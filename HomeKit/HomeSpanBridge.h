/*
  FujitsuAC - ESP32 library for controlling Fujitsu AC via HomeKit (HomeSpan)
  Based on original work by Benas Ragauskas.

  Project home: https://github.com/Benas09/FujitsuAC
*/

#pragma once

#include <HomeSpan.h>
#include "FujitsuController.h"
#include "Enums.h"

namespace FujitsuAC {

    // ─── Thermostat ────────────────────────────────────────────────
    // Maps AC power/mode/temperature to HomeKit Thermostat service.
    //   TargetHeatingCoolingState: Off(0) Heat(1) Cool(2) Auto(3)
    //   CurrentHeatingCoolingState: Off(0) Heat(1) Cool(2)
    //   TargetTemperature / CurrentTemperature in °C

    struct FujitsuThermostat : Service::Thermostat {

        FujitsuController *controller;

        SpanCharacteristic *currentState;
        SpanCharacteristic *targetState;
        SpanCharacteristic *currentTemp;
        SpanCharacteristic *targetTemp;
        SpanCharacteristic *units;

        bool isPoweringOn = false;
        uint16_t lastPower  = 0xFFFF;
        uint16_t lastMode   = 0xFFFF;
        uint16_t lastSetpoint = 0xFFFF;
        uint16_t lastActual = 0xFFFF;

        FujitsuThermostat(FujitsuController *ctrl);
        boolean update() override;
        void loop() override;

    private:
        void refreshCurrentState();
        void refreshTargetState();
    };

    // ─── Fan ───────────────────────────────────────────────────────
    // Maps fan speed and vertical swing to HomeKit Fan v2 service.
    //   RotationSpeed  0-100  → Auto / Quiet / Low / Medium / High
    //   SwingMode      0/1    → Vertical swing off/on

    struct FujitsuFan : Service::Fan {

        FujitsuController *controller;

        SpanCharacteristic *active;
        SpanCharacteristic *speed;
        SpanCharacteristic *swing;
        bool exposeSwing;

        uint16_t lastPower = 0xFFFF;
        uint16_t lastFan   = 0xFFFF;
        uint16_t lastSwing = 0xFFFF;

        FujitsuFan(FujitsuController *ctrl, bool enableSwing = true);
        boolean update() override;
        void loop() override;

        static Enums::FanSpeed speedFromRotation(int pct);
        static int rotationFromSpeed(uint16_t val);
    };

    // ─── Outdoor Temperature Sensor ────────────────────────────────

    struct FujitsuOutdoorTemp : Service::TemperatureSensor {

        FujitsuController *controller;

        SpanCharacteristic *temp;
        uint16_t lastVal = 0xFFFF;

        FujitsuOutdoorTemp(FujitsuController *ctrl);
        void loop() override;
    };

    // ─── Generic On/Off Switch ─────────────────────────────────────
    // Used for: Powerful, Economy, Energy Saving Fan, Low Noise,
    //           Coil Dry, Human Sensor, Dry Mode, Fan-Only Mode

    struct FujitsuSwitch : Service::Switch {

        FujitsuController *controller;
        Address address;

        SpanCharacteristic *on;
        uint16_t lastVal = 0xFFFF;

        FujitsuSwitch(FujitsuController *ctrl, Address addr);
        boolean update() override;
        void loop() override;
    };

    // ─── Vane Position (as Fan speed) ───────────────────────────────
    // HomeKit has no native "vane position" type.
    // We expose it as a second Fan:
    //   Active(0/1) = swing off/on
    //   RotationSpeed(0-100 %) mapped to positions 1-4, with 100 as swing

    struct FujitsuVane : Service::Fan {

        FujitsuController *controller;
        bool isVertical;

        SpanCharacteristic *active;
        SpanCharacteristic *speed;
        uint16_t lastAirflow = 0xFFFF;
        uint16_t pendingAirflow = 0xFFFF;
        uint32_t pendingSinceMs = 0;

        FujitsuVane(FujitsuController *ctrl, bool vertical);
        boolean update() override;
        void loop() override;

        static bool isSwingPercent(int pct);
        static bool isSwingAirflow(uint16_t raw);
        static int rotationFromAirflow(uint16_t raw);
        static Enums::VerticalAirflow verticalAirflowFromRotation(int pct);
        static Enums::HorizontalAirflow horizontalAirflowFromRotation(int pct);
    };

    // ─── Bridge Coordinator ────────────────────────────────────────
    // Creates all HomeSpan accessories and services.

    class HomeSpanBridge {
    public:
        HomeSpanBridge(FujitsuController &controller, const char *name, const char *version);
        void setup();

    private:
        FujitsuController &controller;
        const char *name;
        const char *version;
    };

}
