/*
  FujitsuAC - ESP32 library for controlling Fujitsu AC via HomeKit (HomeSpan)
  Serial debug console for live register inspection.

  Uses HomeSpan's SpanUserCommand so custom commands coexist with
  HomeSpan's built-in serial interface (W=WiFi, S=setup code, etc.).

  Custom commands (type letter then press Enter):
        X         Dump current AC status
        Y         Dump all registers
        Y xxxx    Dump specific register (hex address, e.g. Y 1000)
        Z         Toggle live register-change logging

  Project home: https://github.com/Benas09/FujitsuAC
*/

#include "SerialDebug.h"
#include <WiFi.h>

namespace FujitsuAC {

    // Pointer used by the static SpanUserCommand callbacks
    static SerialDebug *_instance = nullptr;

    SerialDebug::SerialDebug(FujitsuController &controller)
        : controller(controller) {}

    void SerialDebug::setup() {
        _instance = this;

        // ── 'X' – Dump AC status ──────────────────────────────────
        new SpanUserCommand('X', "AC Status - show power, mode, temps, fan, features", [](const char *) {
            if (_instance) _instance->printStatus();
        });

        // ── 'Y' – Dump registers ──────────────────────────────────
        new SpanUserCommand('Y', "Registers - dump all, or 'Y xxxx' for specific address", [](const char *buf) {
            if (!_instance) return;

            // Handle both callback styles: "1000" and "Y 1000"
            // Skip leading spaces
            while (*buf == ' ') buf++;

            if (*buf == 'Y' || *buf == 'y') {
                buf++;
                while (*buf == ' ') buf++;
            }

            if (*buf == '\0') {
                _instance->printAllRegisters();
            } else {
                _instance->printRegister(buf);
            }
        });

        // ── 'Z' – Toggle live logging ─────────────────────────────
        new SpanUserCommand('Z', "Live Log - toggle real-time register change logging", [](const char *) {
            if (!_instance) return;

            _instance->liveLogging = !_instance->liveLogging;

            if (_instance->liveLogging) {
                Serial.println(F("\n=== Live register logging ENABLED ==="));

                _instance->controller.setDebugCallback([](const char *name, const char *msg) {
                    Serial.printf("[%s] %s\n", name, msg);
                });
            } else {
                Serial.println(F("\n=== Live register logging DISABLED ==="));

                _instance->controller.setDebugCallback(nullptr);
            }
        });

        if (_instance) _instance->printStatus();
    }

    // ─── Status dump ──────────────────────────────────────────────

    void SerialDebug::printStatus() {
        Serial.println(F("\n╔══════════════════════════════════╗"));
        Serial.println(F("║        FUJITSU AC STATUS         ║"));
        Serial.println(F("╠══════════════════════════════════╣"));

        Register *pr = controller.getRegister(Address::Power);
        Register *mr = controller.getRegister(Address::Mode);
        Register *tr = controller.getRegister(Address::SetpointTemp);
        Register *ar = controller.getRegister(Address::ActualTemp);
        Register *or_ = controller.getRegister(Address::OutdoorTemp);
        Register *fr = controller.getRegister(Address::FanSpeed);
        Register *vs = controller.getRegister(Address::VerticalSwing);
        Register *va = controller.getRegister(Address::VerticalAirflow);
        Register *hs = controller.getRegister(Address::HorizontalSwing);
        Register *ha = controller.getRegister(Address::HorizontalAirflow);

        Serial.printf("║  Power:        %-18s║\n", pr ? onOff(pr->value) : "N/A");
        Serial.printf("║  Mode:         %-18s║\n", mr ? modeString(mr->value) : "N/A");

        if (tr && tr->value != 0xFFFF) {
            Serial.printf("║  Set Temp:     %-14.1f °C ║\n", setpointFromRaw(tr->value));
        } else {
            Serial.printf("║  Set Temp:     %-18s║\n", "N/A");
        }

        if (ar && ar->value != 0) {
            Serial.printf("║  Actual Temp:  %-14.1f °C ║\n", tempFromRaw(ar->value));
        } else {
            Serial.printf("║  Actual Temp:  %-18s║\n", "N/A");
        }

        if (or_ && or_->value != 0) {
            Serial.printf("║  Outdoor Temp: %-14.1f °C ║\n", tempFromRaw(or_->value));
        } else {
            Serial.printf("║  Outdoor Temp: %-18s║\n", "N/A");
        }

        Serial.printf("║  Fan Speed:    %-18s║\n", fr ? fanString(fr->value) : "N/A");
        Serial.printf("║  V. Swing:     %-18s║\n", vs ? onOff(vs->value) : "N/A");
        Serial.printf("║  V. Airflow:   Position %-9d║\n", va ? va->value : 0);
        Serial.printf("║  H. Swing:     %-18s║\n", hs ? onOff(hs->value) : "N/A");
        Serial.printf("║  H. Airflow:   Position %-9d║\n", ha ? ha->value : 0);

        Serial.println(F("╠──────────────────────────────────╣"));

        struct Feature { Address addr; const char *label; };
        static const Feature features[] = {
            { Address::Powerful,            "Powerful" },
            { Address::EconomyMode,         "Economy" },
            { Address::EnergySavingFan,     "Energy Save" },
            { Address::OutdoorUnitLowNoise, "Low Noise" },
            { Address::CoilDry,             "Coil Dry" },
            { Address::HumanSensor,         "Human Sensor" },
            { Address::MinimumHeat,         "Min Heat" },
        };

        for (auto &f : features) {
            Register *r = controller.getRegister(f.addr);
            Serial.printf("║  %-14s %-18s║\n", f.label, r ? onOff(r->value) : "N/A");
        }

        Serial.println(F("╠──────────────────────────────────╣"));

        Serial.printf("║  WiFi RSSI:    %-14d dB ║\n", WiFi.RSSI());
        Serial.printf("║  IP:           %-18s║\n", WiFi.localIP().toString().c_str());
        Serial.printf("║  Free Heap:    %-14lu B  ║\n", (unsigned long)ESP.getFreeHeap());

        Serial.println(F("╚══════════════════════════════════╝"));
    }

    // ─── Register dump ────────────────────────────────────────────

    void SerialDebug::printAllRegisters() {
        size_t count;
        const Register *regs = controller.getAllRegisters(count);

        Serial.println(F("\n  ADDR  │ VALUE  │ NAME"));
        Serial.println(F("────────┼────────┼──────────────────────────"));

        for (size_t i = 0; i < count; ++i) {
            const char *name = addressName(regs[i].address);

            Serial.printf(" 0x%04X │ 0x%04X │ %s\n",
                static_cast<uint16_t>(regs[i].address),
                regs[i].value,
                name
            );
        }

        Serial.printf("\n  Total: %d registers\n", (int)count);
    }

    void SerialDebug::printRegister(const char *hexAddr) {
        unsigned int addr;

        if (sscanf(hexAddr, "%x", &addr) != 1) {
            Serial.println(F("  Invalid address. Use hex, e.g.: Y 1000"));
            return;
        }

        Register *r = controller.getRegister(static_cast<Address>(addr));

        if (!r) {
            Serial.printf("  Register 0x%04X not found\n", addr);
            return;
        }

        const char *name = addressName(r->address);

        Serial.printf("\n  0x%04X = 0x%04X  (%s)\n",
            static_cast<uint16_t>(r->address),
            r->value,
            name
        );

        // Print human-readable value for known registers
        switch (r->address) {
            case Address::Power:
                Serial.printf("  → %s\n", onOff(r->value));
                break;
            case Address::Mode:
                Serial.printf("  → %s\n", modeString(r->value));
                break;
            case Address::FanSpeed:
                Serial.printf("  → %s\n", fanString(r->value));
                break;
            case Address::SetpointTemp:
                if (r->value != 0xFFFF)
                    Serial.printf("  → %.1f °C\n", setpointFromRaw(r->value));
                break;
            case Address::ActualTemp:
            case Address::OutdoorTemp:
                if (r->value != 0)
                    Serial.printf("  → %.1f °C\n", tempFromRaw(r->value));
                break;
            default:
                break;
        }
    }

    // ─── Helpers ──────────────────────────────────────────────────

    const char* SerialDebug::modeString(uint16_t val) {
        switch (static_cast<Enums::Mode>(val)) {
            case Enums::Mode::Auto: return "AUTO";
            case Enums::Mode::Cool: return "COOL";
            case Enums::Mode::Dry:  return "DRY";
            case Enums::Mode::Fan:  return "FAN";
            case Enums::Mode::Heat: return "HEAT";
            default:                return "UNKNOWN";
        }
    }

    const char* SerialDebug::fanString(uint16_t val) {
        switch (static_cast<Enums::FanSpeed>(val)) {
            case Enums::FanSpeed::Auto:   return "AUTO";
            case Enums::FanSpeed::Quiet:  return "QUIET";
            case Enums::FanSpeed::Low:    return "LOW";
            case Enums::FanSpeed::Medium: return "MEDIUM";
            case Enums::FanSpeed::High:   return "HIGH";
            default:                      return "UNKNOWN";
        }
    }

    const char* SerialDebug::onOff(uint16_t val) {
        return val ? "ON" : "OFF";
    }

    float SerialDebug::tempFromRaw(uint16_t raw) {
        return (static_cast<int>(raw) - 5025) / 100.0f;
    }

    float SerialDebug::setpointFromRaw(uint16_t raw) {
        return raw / 10.0f;
    }

    const char* SerialDebug::addressName(Address addr) {
        switch (addr) {
            case Address::Initial0:  return "Initial0";
            case Address::Initial1:  return "Initial1";
            case Address::Initial2:  return "Initial2";
            case Address::Initial3:  return "Initial3";
            case Address::Initial4:  return "Initial4";
            case Address::Initial5:  return "Initial5";
            case Address::Initial6:  return "Initial6";
            case Address::Initial7:  return "Initial7";
            case Address::Initial8:  return "Initial8";
            case Address::Initial9:  return "Initial9";
            case Address::Initial10: return "Initial10";
            case Address::Initial11: return "Initial11";

            case Address::VerticalAirflowDirectionCount:   return "V.Airflow Count";
            case Address::VerticalSwingSupported:           return "V.Swing Supported";
            case Address::HorizontalAirflowDirectionCount: return "H.Airflow Count";
            case Address::HorizontalSwingSupported:         return "H.Swing Supported";

            case Address::EconomyModeSupported:       return "Economy Supported";
            case Address::MinimumHeatSupported:       return "Min Heat Supported";
            case Address::HumanSensorSupported:       return "Human Sensor Supported";
            case Address::EnergySavingFanSupported:   return "Energy Save Supported";
            case Address::Initial20:                  return "Initial20";
            case Address::Initial21:                  return "Initial21";
            case Address::Initial22:                  return "Initial22";
            case Address::PowerfulSupported:          return "Powerful Supported";
            case Address::OutdoorUnitLowNoiseSupported: return "Low Noise Supported";
            case Address::CoilDrySupported:           return "Coil Dry Supported";

            case Address::Power:                         return "Power";
            case Address::Mode:                          return "Mode";
            case Address::SetpointTemp:                  return "Setpoint Temp";
            case Address::FanSpeed:                      return "Fan Speed";
            case Address::VerticalAirflowSetterRegistry: return "V.Airflow Setter";
            case Address::VerticalSwing:                 return "V.Swing";
            case Address::VerticalAirflow:               return "V.Airflow";
            case Address::HorizontalAirflowSetterRegistry: return "H.Airflow Setter";
            case Address::HorizontalSwing:               return "H.Swing";
            case Address::HorizontalAirflow:             return "H.Airflow";
            case Address::ActualTemp:                    return "Actual Temp";
            case Address::OutdoorTemp:                   return "Outdoor Temp";

            case Address::EconomyMode:         return "Economy Mode";
            case Address::MinimumHeat:         return "Minimum Heat";
            case Address::HumanSensor:         return "Human Sensor";
            case Address::EnergySavingFan:     return "Energy Saving Fan";
            case Address::Powerful:            return "Powerful";
            case Address::OutdoorUnitLowNoise: return "Outdoor Low Noise";
            case Address::CoilDry:             return "Coil Dry";

            default: {
                static char buf[16];
                snprintf(buf, sizeof(buf), "Reg_%04X", static_cast<uint16_t>(addr));
                return buf;
            }
        }
    }

}
