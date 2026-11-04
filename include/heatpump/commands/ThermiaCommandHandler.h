#pragma once

#include "heatpump/HeatPumpCommand.h"
#include "utils/ModbusRtu.h"
#include "types/Constants.h"

using namespace Constants;

class ThermiaCommandHandler : public HeatPumpCommandBase {
public:
    ThermiaCommandHandler(ICommunicationBus* bus, IHeatPumpProtocol* protocol)
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
        default:
            return CommunicationError{CommErrorCode::UNKNOWN, "Command not supported for Thermia", 0};
        }
    }

    CommunicationError boostHeating(std::chrono::seconds duration) override {
        uint16_t hours = static_cast<uint16_t>(
            std::chrono::duration_cast<std::chrono::hours>(duration).count());
        if (hours == 0) hours = 1;
        return writeModbusRegister(0x0209, hours);
    }

    CommunicationError stopHeating(bool enable) override {
        return writeModbusRegister(0x020A, enable ? 1 : 0);
    }

    CommunicationError forceElectricHeater(bool enable) override {
        return writeModbusRegister(0x0215, enable ? 1 : 0);
    }

    CommunicationError setHeatingSetpoint(float tempC) override {
        uint16_t raw = static_cast<uint16_t>((tempC - Thermia::TEMP_OFFSET) / Thermia::TEMP_SCALE);
        return writeModbusRegister(0x0107, raw);
    }

    CommunicationError setWaterSetpoint(float tempC) override {
        uint16_t raw = static_cast<uint16_t>(tempC / Thermia::TEMP_SCALE);
        return writeModbusRegister(0x010B, raw);
    }

    CommunicationError setOperatingMode(uint8_t mode) override {
        return writeModbusRegister(0x0201, mode);
    }

    CommunicationError resetAlarm() override {
        return writeModbusRegister(0x02F2, 1);
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

    CommunicationError writeRegister(uint16_t regAddr, uint16_t value) {
        return writeModbusRegister(regAddr, value);
    }

private:
    CommunicationError writeModbusRegister(uint16_t regAddr, uint16_t value) {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        auto cmd = ModbusRtu::buildWriteSingleRegisterFrame(m_slaveAddress, regAddr, value);
        auto err = m_bus->writeBytes(cmd);
        if (!err.isOk()) return err;

        return ModbusRtu::readWriteEcho(m_bus, m_slaveAddress, 0x06, regAddr, value,
                                        DEFAULT_ACK_TIMEOUT);
    }

    uint8_t m_slaveAddress = 0x01;
};
