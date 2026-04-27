/*
  FujitsuAC - ESP32 library for controlling Fujitsu AC via HomeKit (HomeSpan)
  Based on original work by Benas Ragauskas.

  Project home: https://github.com/Benas09/FujitsuAC
*/

#include "HomeSpanBridge.h"

namespace FujitsuAC {

    // ═══════════════════════════════════════════════════════════════
    //  Thermostat
    // ═══════════════════════════════════════════════════════════════

    FujitsuThermostat::FujitsuThermostat(FujitsuController *ctrl)
        : Service::Thermostat()
    {
        controller = ctrl;

        currentState = new Characteristic::CurrentHeatingCoolingState(0);
        targetState  = new Characteristic::TargetHeatingCoolingState(0);
        currentTemp  = new Characteristic::CurrentTemperature(22.0);
        targetTemp   = new Characteristic::TargetTemperature(22.0);
        targetTemp->setRange(16, 30, 0.5);
        units = new Characteristic::TemperatureDisplayUnits(0);
    }

    boolean FujitsuThermostat::update() {
        if (targetState->updated()) {
            int val = targetState->getNewVal();

            switch (val) {
                case 0: // Off
                    isPoweringOn = false;
                    controller->setPower(Enums::Power::Off);
                    break;

                case 1: // Heat
                    controller->setMode(Enums::Mode::Heat);
                    if (!controller->isPoweredOn()) isPoweringOn = true;
                    break;

                case 2: // Cool
                    controller->setMode(Enums::Mode::Cool);
                    if (!controller->isPoweredOn()) isPoweringOn = true;
                    break;

                case 3: // Auto
                    controller->setMode(Enums::Mode::Auto);
                    if (!controller->isPoweredOn()) isPoweringOn = true;
                    break;
            }
        }

        if (targetTemp->updated()) {
            float t = targetTemp->getNewVal<float>();
            char buf[8];
            snprintf(buf, sizeof(buf), "%.1f", t);
            controller->setTemp(buf);
        }

        return true;
    }

    void FujitsuThermostat::loop() {
        // Retry power-on if mode was set while unit was off
        if (isPoweringOn && !controller->isPoweredOn()) {
            controller->setPower(Enums::Power::On);
        }

        Register *pr = controller->getRegister(Address::Power);
        Register *mr = controller->getRegister(Address::Mode);
        Register *tr = controller->getRegister(Address::SetpointTemp);
        Register *ar = controller->getRegister(Address::ActualTemp);

        // Power or mode changed → update current / target state
        bool stateChanged = false;

        if (pr && pr->value != lastPower) {
            lastPower = pr->value;
            if (pr->value == static_cast<uint16_t>(Enums::Power::On)) {
                isPoweringOn = false;
            }
            stateChanged = true;
        }

        if (mr && mr->value != lastMode) {
            lastMode = mr->value;
            stateChanged = true;
        }

        if (stateChanged) {
            refreshCurrentState();
            refreshTargetState();
        }

        // Setpoint temperature
        if (tr && tr->value != lastSetpoint && tr->value != 0xFFFF) {
            lastSetpoint = tr->value;
            float temp = tr->value / 10.0f;

            if (temp >= 16.0f && temp <= 30.0f) {
                targetTemp->setVal(temp);
            }
        }

        // Actual (indoor) temperature
        if (ar && ar->value != lastActual && ar->value != 0) {
            lastActual = ar->value;
            float temp = (static_cast<int>(ar->value) - 5025) / 100.0f;

            if (temp > -20.0f && temp < 60.0f) {
                currentTemp->setVal(temp);
            }
        }
    }

    void FujitsuThermostat::refreshCurrentState() {
        Register *pr = controller->getRegister(Address::Power);
        Register *mr = controller->getRegister(Address::Mode);

        if (!pr || pr->value == static_cast<uint16_t>(Enums::Power::Off)) {
            currentState->setVal(0);
            return;
        }

        if (!mr) return;

        switch (static_cast<Enums::Mode>(mr->value)) {
            case Enums::Mode::Heat:
                currentState->setVal(1);
                break;
            default:
                currentState->setVal(2); // Cool for Cool/Dry/Auto/Fan
                break;
        }
    }

    void FujitsuThermostat::refreshTargetState() {
        Register *pr = controller->getRegister(Address::Power);
        Register *mr = controller->getRegister(Address::Mode);

        if (!pr || pr->value == static_cast<uint16_t>(Enums::Power::Off)) {
            targetState->setVal(0);
            return;
        }

        if (!mr) return;

        switch (static_cast<Enums::Mode>(mr->value)) {
            case Enums::Mode::Heat:
                targetState->setVal(1);
                break;
            case Enums::Mode::Cool:
            case Enums::Mode::Dry:
                targetState->setVal(2);
                break;
            default:
                targetState->setVal(3); // Auto for Auto/Fan
                break;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    //  Fan
    // ═══════════════════════════════════════════════════════════════

    FujitsuFan::FujitsuFan(FujitsuController *ctrl)
        : Service::Fan()
    {
        controller = ctrl;

        active = new Characteristic::Active(0);
        speed  = new Characteristic::RotationSpeed(0);
        speed->setRange(0, 100, 20);          // 0,20,40,60,80,100 → 5 named speeds + auto
        swing  = new Characteristic::SwingMode(0);
    }

    boolean FujitsuFan::update() {
        if (active->updated()) {
            int val = active->getNewVal();
            controller->setPower(val ? Enums::Power::On : Enums::Power::Off);
        }

        if (speed->updated()) {
            int pct = speed->getNewVal();
            controller->setFanSpeed(speedFromRotation(pct));
        }

        if (swing->updated()) {
            int val = swing->getNewVal();
            controller->setVerticalSwing(
                val ? Enums::VerticalSwing::On : Enums::VerticalSwing::Off
            );
        }

        return true;
    }

    void FujitsuFan::loop() {
        Register *pr = controller->getRegister(Address::Power);
        Register *fr = controller->getRegister(Address::FanSpeed);
        Register *sr = controller->getRegister(Address::VerticalSwing);

        if (pr && pr->value != lastPower) {
            lastPower = pr->value;
            active->setVal(
                pr->value == static_cast<uint16_t>(Enums::Power::On) ? 1 : 0
            );
        }

        if (fr && fr->value != lastFan) {
            lastFan = fr->value;
            speed->setVal(rotationFromSpeed(fr->value));
        }

        if (sr && sr->value != lastSwing) {
            lastSwing = sr->value;
            swing->setVal(
                sr->value == static_cast<uint16_t>(Enums::VerticalSwing::On) ? 1 : 0
            );
        }
    }

    Enums::FanSpeed FujitsuFan::speedFromRotation(int pct) {
        if (pct <= 10)  return Enums::FanSpeed::Auto;
        if (pct <= 30)  return Enums::FanSpeed::Quiet;
        if (pct <= 50)  return Enums::FanSpeed::Low;
        if (pct <= 70)  return Enums::FanSpeed::Medium;
        return Enums::FanSpeed::High;
    }

    int FujitsuFan::rotationFromSpeed(uint16_t val) {
        switch (static_cast<Enums::FanSpeed>(val)) {
            case Enums::FanSpeed::Auto:   return 0;
            case Enums::FanSpeed::Quiet:  return 20;
            case Enums::FanSpeed::Low:    return 40;
            case Enums::FanSpeed::Medium: return 60;
            case Enums::FanSpeed::High:   return 80;
            default:                      return 0;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    //  Outdoor Temperature Sensor
    // ═══════════════════════════════════════════════════════════════

    FujitsuOutdoorTemp::FujitsuOutdoorTemp(FujitsuController *ctrl)
        : Service::TemperatureSensor()
    {
        controller = ctrl;
        temp = new Characteristic::CurrentTemperature(0);
        temp->setRange(-40, 60, 0.1);
    }

    void FujitsuOutdoorTemp::loop() {
        Register *r = controller->getRegister(Address::OutdoorTemp);

        if (r && r->value != lastVal && r->value != 0) {
            lastVal = r->value;
            float t = (static_cast<int>(r->value) - 5025) / 100.0f;

            if (t > -40.0f && t < 60.0f) {
                temp->setVal(t);
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    //  Generic On/Off Switch
    // ═══════════════════════════════════════════════════════════════

    FujitsuSwitch::FujitsuSwitch(FujitsuController *ctrl, Address addr)
        : Service::Switch()
    {
        controller = ctrl;
        address = addr;
        on = new Characteristic::On(false);
    }

    boolean FujitsuSwitch::update() {
        bool val = on->getNewVal();
        uint16_t raw = val ? 0x0001 : 0x0000;

        switch (address) {
            case Address::Powerful:
                controller->setPowerful(static_cast<Enums::Powerful>(raw));
                break;
            case Address::EconomyMode:
                controller->setEconomy(static_cast<Enums::EconomyMode>(raw));
                break;
            case Address::EnergySavingFan:
                controller->setEnergySavingFan(static_cast<Enums::EnergySavingFan>(raw));
                break;
            case Address::OutdoorUnitLowNoise:
                controller->setOutdoorUnitLowNoise(static_cast<Enums::OutdoorUnitLowNoise>(raw));
                break;
            case Address::CoilDry:
                controller->setCoilDry(static_cast<Enums::CoilDry>(raw));
                break;
            case Address::HumanSensor:
                controller->setHumanSensor(static_cast<Enums::HumanSensor>(raw));
                break;
            default:
                break;
        }

        return true;
    }

    void FujitsuSwitch::loop() {
        Register *r = controller->getRegister(address);

        if (r && r->value != lastVal) {
            lastVal = r->value;
            on->setVal(r->value != 0);
        }
    }

    // ═══════════════════════════════════════════════════════════════
    //  Vane Position (Lightbulb brightness hack)
    // ═══════════════════════════════════════════════════════════════

    FujitsuVane::FujitsuVane(FujitsuController *ctrl, bool vertical)
        : Service::LightBulb()
    {
        controller = ctrl;
        isVertical = vertical;

        on = new Characteristic::On(false);
        brightness = new Characteristic::Brightness(50);
        brightness->setRange(0, 100, 17);   // ~6 steps for 6 positions
    }

    boolean FujitsuVane::update() {
        if (on->updated()) {
            bool val = on->getNewVal();

            if (isVertical) {
                controller->setVerticalSwing(
                    val ? Enums::VerticalSwing::On : Enums::VerticalSwing::Off
                );
            } else {
                controller->setHorizontalSwing(
                    val ? Enums::HorizontalSwing::On : Enums::HorizontalSwing::Off
                );
            }
        }

        if (brightness->updated()) {
            int pct = brightness->getNewVal();

            // Map 0-100 → position 1-6
            int pos = 1 + (pct * 5) / 100;
            if (pos < 1) pos = 1;
            if (pos > 6) pos = 6;

            auto position = static_cast<Enums::VerticalAirflow>(pos);

            if (isVertical) {
                controller->setVerticalAirflow(static_cast<Enums::VerticalAirflow>(pos));
            } else {
                controller->setHorizontalAirflow(static_cast<Enums::HorizontalAirflow>(pos));
            }
        }

        return true;
    }

    void FujitsuVane::loop() {
        Address addr = isVertical ? Address::VerticalAirflow : Address::HorizontalAirflow;
        Register *r = controller->getRegister(addr);

        if (r && r->value != lastAirflow) {
            lastAirflow = r->value;

            uint16_t raw = r->value;
            if (raw == static_cast<uint16_t>(Enums::VerticalAirflow::Swing)) {
                on->setVal(true);
            } else if (raw >= 1 && raw <= 6) {
                on->setVal(false);
                int pct = ((raw - 1) * 100) / 5;
                brightness->setVal(pct);
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    //  Bridge Coordinator
    // ═══════════════════════════════════════════════════════════════

    HomeSpanBridge::HomeSpanBridge(
        FujitsuController &controller,
        const char *name,
        const char *version
    ) : controller(controller), name(name), version(version) {}

    void HomeSpanBridge::setup() {
        // Generate serial number from MAC
        char serial[13];
        snprintf(serial, sizeof(serial), "%012llX", ESP.getEfuseMac());

        // ── Accessory 1: AC Thermostat + Fan ──────────────────────
        new SpanAccessory();
            new Service::AccessoryInformation();
                new Characteristic::Identify();
                new Characteristic::Name(name);
                new Characteristic::Manufacturer("fAir");
                new Characteristic::Model("FujitsuAC");
                new Characteristic::SerialNumber(serial);
                new Characteristic::FirmwareRevision(version);

            new FujitsuThermostat(&controller);
            new FujitsuFan(&controller);

#if FUJITSU_ENABLE_OUTDOOR_TEMP
        // ── Accessory 2: Outdoor Temperature ──────────────────────
        new SpanAccessory();
            new Service::AccessoryInformation();
                new Characteristic::Identify();
                new Characteristic::Name("Outdoor Temp");

            new FujitsuOutdoorTemp(&controller);
#endif

#if FUJITSU_ENABLE_FEATURE_SWITCHES
        // ── Accessory 3: Feature Switches ─────────────────────────
        //    All switches are created up-front; the controller
        //    silently ignores commands for unsupported features.

        struct SwitchDef { Address addr; const char *label; };

        static const SwitchDef switches[] = {
            { Address::Powerful,            "Powerful" },
            { Address::EconomyMode,         "Economy Mode" },
            { Address::EnergySavingFan,     "Energy Saving Fan" },
            { Address::OutdoorUnitLowNoise, "Low Noise" },
            { Address::CoilDry,             "Coil Dry" },
            { Address::HumanSensor,         "Human Sensor" },
        };

        for (auto &sw : switches) {
            new SpanAccessory();
                new Service::AccessoryInformation();
                    new Characteristic::Identify();
                    new Characteristic::Name(sw.label);

                new FujitsuSwitch(&controller, sw.addr);
        }
#endif

#if FUJITSU_ENABLE_VANES
        // ── Accessory: Vertical Vane ──────────────────────────────
        new SpanAccessory();
            new Service::AccessoryInformation();
                new Characteristic::Identify();
                new Characteristic::Name("Vertical Vane");

            new FujitsuVane(&controller, true);

        // ── Accessory: Horizontal Vane ────────────────────────────
        new SpanAccessory();
            new Service::AccessoryInformation();
                new Characteristic::Identify();
                new Characteristic::Name("Horizontal Vane");

            new FujitsuVane(&controller, false);
#endif
    }

}
