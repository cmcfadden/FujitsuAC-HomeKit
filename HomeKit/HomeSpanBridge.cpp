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

    FujitsuFan::FujitsuFan(FujitsuController *ctrl, bool enableSwing)
        : Service::Fan()
    {
        controller = ctrl;
        exposeSwing = enableSwing;

        active = new Characteristic::Active(0);
        speed  = new Characteristic::RotationSpeed(0);
        speed->setRange(0, 80, 20);           // 0,20,40,60,80 → Auto / Quiet / Low / Medium / High
        swing = nullptr;
        if (exposeSwing) {
            swing = new Characteristic::SwingMode(0);
        }
    }

    boolean FujitsuFan::update() {
        bool speedChanged = speed->updated();
        int speedPct = 0;

        if (speedChanged) {
            speedPct = speed->getNewVal();
            controller->setFanSpeed(speedFromRotation(speedPct));
        }

        if (active->updated()) {
            int val = active->getNewVal();

            // Some Home clients send Active=0 when RotationSpeed is set to 0.
            // For this accessory, speed 0 is Auto, so keep power on in that case.
            if (!val && speedChanged && speedPct == 0) {
                controller->setPower(Enums::Power::On);
            } else {
                controller->setPower(val ? Enums::Power::On : Enums::Power::Off);
            }
        }

        if (swing && swing->updated()) {
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
            int pct = rotationFromSpeed(fr->value);
            if (pct >= 0) {
                lastFan = fr->value;
                speed->setVal(pct);
            }
        }

        if (swing && sr && sr->value != lastSwing) {
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
            default:                      return -1;
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
    //  Vane Position (Fan service hack)
    // ═══════════════════════════════════════════════════════════════

    FujitsuVane::FujitsuVane(FujitsuController *ctrl, bool vertical)
        : Service::Fan()
    {
        controller = ctrl;
        isVertical = vertical;

        active = new Characteristic::Active(0);
        speed = new Characteristic::RotationSpeed(50);
        speed->setRange(0, 100, 25);   // 0,25,50,75 = positions 1-4, 0 = swing
    }

    boolean FujitsuVane::update() {
        // Active toggle is ignored for commands — vane position is
        // controlled entirely through the speed slider.
        // HomeKit sends Active=1 alongside speed changes; if we
        // mapped Active→swing here it would race with the speed
        // handler and cause the unit to enter swing mode briefly
        // before the position command arrives (the "position 32" bug).

        if (speed->updated()) {
            int pct = speed->getNewVal();

            // 0% = swing mode; everything else = fixed position.
            // NOTE: The controller has a single-use send queue — only one
            // command can be queued per loop tick.  setVerticalAirflow()
            // already sends swing=Off + position in one atomic batch,
            // so we must NOT call setVerticalSwing separately here.
            if (isSwingPercent(pct)) {
                if (isVertical) {
                    controller->setVerticalSwing(Enums::VerticalSwing::On);
                } else {
                    controller->setHorizontalSwing(Enums::HorizontalSwing::On);
                }
            } else {
                if (isVertical) {
                    controller->setVerticalAirflow(verticalAirflowFromRotation(pct));
                } else {
                    controller->setHorizontalAirflow(horizontalAirflowFromRotation(pct));
                }
            }
        }

        return true;
    }

    void FujitsuVane::loop() {
        static const uint32_t kDebounceMs = 2000;

        Address addr = isVertical ? Address::VerticalAirflow : Address::HorizontalAirflow;
        Register *r = controller->getRegister(addr);

        if (!r) {
            return;
        }

        uint16_t raw = r->value;

        if (raw != pendingAirflow) {
            pendingAirflow = raw;
            pendingSinceMs = millis();
            return;
        }

        if (raw == lastAirflow) {
            return;
        }

        if ((millis() - pendingSinceMs) < kDebounceMs) {
            return;
        }

        lastAirflow = raw;

        if (isSwingAirflow(raw)) {
            active->setVal(1);
            speed->setVal(0);
        } else {
            int pct = rotationFromAirflow(raw);
            if (pct < 0) {
                return;
            }

            active->setVal(1);
            speed->setVal(pct);
        }
    }

    bool FujitsuVane::isSwingPercent(int pct) {
        return pct <= 0;
    }

    bool FujitsuVane::isSwingAirflow(uint16_t raw) {
        return raw == static_cast<uint16_t>(Enums::VerticalAirflow::Swing);
    }

    int FujitsuVane::rotationFromAirflow(uint16_t raw) {
        switch (static_cast<Enums::VerticalAirflow>(raw)) {
            case Enums::VerticalAirflow::Position1:
                return 25;
            case Enums::VerticalAirflow::Position2:
                return 50;
            case Enums::VerticalAirflow::Position3:
                return 75;
            case Enums::VerticalAirflow::Position4:
                return 100;
            default:
                return -1;
        }
    }

    Enums::VerticalAirflow FujitsuVane::verticalAirflowFromRotation(int pct) {
        int clamped = pct;
        if (clamped < 0) clamped = 0;
        if (clamped > 99) clamped = 99;

        int bucket = (clamped + 12) / 25;

        switch (bucket) {
            case 1:
                return Enums::VerticalAirflow::Position1;
            case 2:
                return Enums::VerticalAirflow::Position2;
            case 3:
                return Enums::VerticalAirflow::Position3;
            default:
                return Enums::VerticalAirflow::Position4;
        }
    }

    Enums::HorizontalAirflow FujitsuVane::horizontalAirflowFromRotation(int pct) {
        switch (verticalAirflowFromRotation(pct)) {
            case Enums::VerticalAirflow::Position1:
                return Enums::HorizontalAirflow::Position1;
            case Enums::VerticalAirflow::Position2:
                return Enums::HorizontalAirflow::Position2;
            case Enums::VerticalAirflow::Position3:
                return Enums::HorizontalAirflow::Position3;
            default:
                return Enums::HorizontalAirflow::Position4;
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
            new FujitsuFan(&controller, false);
            new FujitsuVane(&controller, true);

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

// #if FUJITSU_ENABLE_VANES
        // ── Accessory: Vertical Vane ──────────────────────────────
        // new SpanAccessory();
        //     new Service::AccessoryInformation();
        //         new Characteristic::Identify();
        //         new Characteristic::Name("Vertical Vane");

        //     new FujitsuVane(&controller, true);

        // ── Accessory: Horizontal Vane ────────────────────────────
        // new SpanAccessory();
        //     new Service::AccessoryInformation();
        //         new Characteristic::Identify();
        //         new Characteristic::Name("Horizontal Vane");

        //     new FujitsuVane(&controller, false);
// #endif
    }

}
