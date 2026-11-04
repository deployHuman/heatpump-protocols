#pragma once

#include "heatpump/HeatPumpCommand.h"
#include "types/Constants.h"

using namespace Constants;

class EmsCommandHandler : public HeatPumpCommandBase {
public:
    EmsCommandHandler(ICommunicationBus* bus, IHeatPumpProtocol* protocol)
        : HeatPumpCommandBase(bus, protocol) {}

    CommunicationError sendCommand(const HeatPumpCommand& cmd) override {
        switch (cmd.type) {
        case HeatPumpCommandType::BOOST_HEATING:
            return boostHeating(cmd.duration);
        case HeatPumpCommandType::STOP_HEATING:
            return stopHeating(cmd.value != 0);
        case HeatPumpCommandType::FORCE_ELECTRIC_HEATER:
            return forceElectricHeater(cmd.value != 0);
        case HeatPumpCommandType::SET_HEATING_SETPOINT:
            return setHeatingSetpoint(static_cast<float>(cmd.value) / 10.0f);
        case HeatPumpCommandType::SET_WATER_SETPOINT:
            return setWaterSetpoint(static_cast<float>(cmd.value) / 10.0f);
        case HeatPumpCommandType::SET_OPERATING_MODE:
            return setOperatingMode(static_cast<uint8_t>(cmd.value));
        default:
            return CommunicationError{CommErrorCode::UNKNOWN, "Command not supported for EMS", 0};
        }
    }

    CommunicationError boostHeating(std::chrono::seconds duration) override {
        uint16_t hours = static_cast<uint16_t>(
            std::chrono::duration_cast<std::chrono::hours>(duration).count());
        if (hours == 0) hours = 1;
        return writeEmsRegister(0x3209, hours);
    }

    CommunicationError stopHeating(bool enable) override {
        return writeEmsRegister(0x320A, enable ? 1 : 0);
    }

    CommunicationError forceElectricHeater(bool enable) override {
        return writeEmsRegister(0x3215, enable ? 1 : 0);
    }

    CommunicationError setHeatingSetpoint(float tempC) override {
        uint16_t raw = static_cast<uint16_t>((tempC - EmsBosch::TEMP_OFFSET) / EmsBosch::TEMP_SCALE);
        return writeEmsRegister(0x3211, raw);
    }

    CommunicationError setWaterSetpoint(float tempC) override {
        uint16_t raw = static_cast<uint16_t>(tempC / EmsBosch::TEMP_SCALE);
        return writeEmsRegister(0x321A, raw);
    }

    CommunicationError setOperatingMode(uint8_t mode) override {
        return writeEmsRegister(0x3276, mode);
    }

    CommunicationError resetAlarm() override {
        return writeEmsRegister(0x32F2, 1);
    }

    [[nodiscard]] bool supportsCommand(HeatPumpCommandType type) const override {
        switch (type) {
        case HeatPumpCommandType::BOOST_HEATING:
        case HeatPumpCommandType::STOP_HEATING:
        case HeatPumpCommandType::FORCE_ELECTRIC_HEATER:
        case HeatPumpCommandType::SET_HEATING_SETPOINT:
        case HeatPumpCommandType::SET_WATER_SETPOINT:
        case HeatPumpCommandType::SET_OPERATING_MODE:
        case HeatPumpCommandType::RESET_ALARM:
            return true;
        default:
            return false;
        }
    }

private:
    CommunicationError writeEmsRegister(uint16_t regAddr, uint16_t value) {
        std::vector<uint8_t> cmd = {
            0xDC, 0x02,
            static_cast<uint8_t>(regAddr >> 8),
            static_cast<uint8_t>(regAddr & ByteMasks::LOW_BYTE),
            0x02,
            static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(value & ByteMasks::LOW_BYTE),
        };

        uint8_t checksum = Checksums::Xor8::INITIAL;
        for (auto b : cmd) checksum ^= b;
        cmd.push_back(checksum);

        return writeAndWaitAck(cmd, DEFAULT_ACK_TIMEOUT);
    }
};
