#pragma once

#include "heatpump/HeatPumpCommand.h"
#include "types/Constants.h"

using namespace Constants;

class RegoCommandHandler : public HeatPumpCommandBase {
public:
    RegoCommandHandler(ICommunicationBus* bus, IHeatPumpProtocol* protocol)
        : HeatPumpCommandBase(bus, protocol) {}

    CommunicationError sendCommand(const HeatPumpCommand& cmd) override {
        switch (cmd.type) {
        case HeatPumpCommandType::BOOST_HEATING:
            return boostHeating(cmd.duration);
        case HeatPumpCommandType::STOP_HEATING:
            return stopHeating(cmd.value != 0);
        case HeatPumpCommandType::SET_HOLIDAY_MODE:
            return setHolidayMode(cmd.value != 0);
        case HeatPumpCommandType::FORCE_ELECTRIC_HEATER:
            return forceElectricHeater(cmd.value != 0);
        case HeatPumpCommandType::SET_HEATING_SETPOINT:
            return setHeatingSetpoint(static_cast<float>(cmd.value) / 10.0f);
        case HeatPumpCommandType::SET_WATER_SETPOINT:
            return setWaterSetpoint(static_cast<float>(cmd.value) / 10.0f);
        case HeatPumpCommandType::SET_OPERATING_MODE:
            return setOperatingMode(static_cast<uint8_t>(cmd.value));
        case HeatPumpCommandType::SET_FAN_SPEED:
            return setFanSpeed(static_cast<uint8_t>(cmd.value));
        case HeatPumpCommandType::SET_POWER_LIMIT:
            return setPowerLimit(cmd.value);
        case HeatPumpCommandType::SET_EXT_PORT:
            return setExtPort(static_cast<uint8_t>(cmd.registerAddress & 0xFF), cmd.value != 0);
        case HeatPumpCommandType::RESET_ALARM:
            return resetAlarm();
        case HeatPumpCommandType::SET_TIMER_OVERRIDE:
            return writeRegisterTimed(cmd.registerAddress, cmd.value, cmd.duration);
        default:
            return CommunicationError{CommErrorCode::UNKNOWN, "Command not supported for REGO", 0};
        }
    }

    CommunicationError boostHeating(std::chrono::seconds duration) override {
        uint16_t hours = static_cast<uint16_t>(
            std::chrono::duration_cast<std::chrono::hours>(duration).count());
        if (hours == 0) hours = 1;
        return writeRegoAndVerify(0x0209, hours);
    }

    CommunicationError stopHeating(bool enable) override {
        return writeRegoAndVerify(0x020A, enable ? 1 : 0);
    }

    CommunicationError setHolidayMode(bool enable) override {
        return writeRegoAndVerify(0x0210, enable ? 1 : 0);
    }

    CommunicationError forceElectricHeater(bool enable) override {
        return writeRegoAndVerify(0x0215, enable ? 1 : 0);
    }

    CommunicationError setHeatingSetpoint(float tempC) override {
        uint16_t raw = static_cast<uint16_t>((tempC - Rego800::TEMP_OFFSET) / Rego800::TEMP_SCALE);
        return writeRegoAndVerify(0x0107, raw);
    }

    CommunicationError setWaterSetpoint(float tempC) override {
        uint16_t raw = static_cast<uint16_t>((tempC - Rego800::TEMP_OFFSET_ZERO) / Rego800::TEMP_SCALE);
        return writeRegoAndVerify(0x010B, raw);
    }

    CommunicationError setOperatingMode(uint8_t mode) override {
        return writeRegoAndVerify(0x0201, mode);
    }

    CommunicationError setFanSpeed(uint8_t percent) override {
        if (percent > 100) percent = 100;
        return writeRegoAndVerify(0x021A, percent);
    }

    CommunicationError setPowerLimit(uint16_t watts) override {
        return writeRegoAndVerify(0x0226, watts);
    }

    CommunicationError setExtPort(uint8_t port, bool state) override {
        uint16_t regAddr = (port == 1) ? 0x0FA1 : 0x0FA2;
        return writeRegoAndVerify(regAddr, state ? 1 : 0);
    }

    CommunicationError resetAlarm() override {
        return writeRegoRegister(0x02F2, 1);
    }

    [[nodiscard]] bool supportsCommand(HeatPumpCommandType type) const override {
        switch (type) {
        case HeatPumpCommandType::BOOST_HEATING:
        case HeatPumpCommandType::STOP_HEATING:
        case HeatPumpCommandType::SET_HOLIDAY_MODE:
        case HeatPumpCommandType::FORCE_ELECTRIC_HEATER:
        case HeatPumpCommandType::SET_HEATING_SETPOINT:
        case HeatPumpCommandType::SET_WATER_SETPOINT:
        case HeatPumpCommandType::SET_OPERATING_MODE:
        case HeatPumpCommandType::SET_FAN_SPEED:
        case HeatPumpCommandType::SET_POWER_LIMIT:
        case HeatPumpCommandType::SET_EXT_PORT:
        case HeatPumpCommandType::SET_TIMER_OVERRIDE:
        case HeatPumpCommandType::RESET_ALARM:
            return true;
        default:
            return false;
        }
    }

private:
    CommunicationError writeRegoRegister(uint16_t regAddr, uint16_t value) {
        std::vector<uint8_t> cmd = {
            0x0B, 0x90,
            0xFF,
            static_cast<uint8_t>(regAddr >> 8),
            static_cast<uint8_t>(regAddr & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(value & ByteMasks::LOW_BYTE),
        };

        uint8_t checksum = Checksums::Xor8::INITIAL;
        for (auto b : cmd) checksum ^= b;
        cmd.push_back(checksum);

        return writeAndWaitAck(cmd);
    }

    CommunicationError writeRegoAndVerify(uint16_t regAddr, uint16_t value) {
        auto cmd = buildRegoWriteCmd(regAddr, value);
        return writeAndVerify(cmd, regAddr, value);
    }

    std::vector<uint8_t> buildRegoWriteCmd(uint16_t regAddr, uint16_t value) {
        std::vector<uint8_t> cmd = {
            0x0B, 0x90,
            0xFF,
            static_cast<uint8_t>(regAddr >> 8),
            static_cast<uint8_t>(regAddr & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(value & ByteMasks::LOW_BYTE),
        };

        uint8_t checksum = Checksums::Xor8::INITIAL;
        for (auto b : cmd) checksum ^= b;
        cmd.push_back(checksum);
        return cmd;
    }

    CommunicationError writeRegisterTimed(uint16_t regAddr, uint16_t value,
                                           std::chrono::seconds duration) {
        auto err = writeRegoAndVerify(regAddr, value);
        if (!err.isOk()) return err;

        m_timerRemaining = duration;
        m_timerRegAddr = regAddr;
        m_timerRestoreValue = value;
        return CommunicationError::ok();
    }

    std::chrono::seconds m_timerRemaining{0};
    uint16_t m_timerRegAddr = 0;
    uint16_t m_timerRestoreValue = 0;
};
