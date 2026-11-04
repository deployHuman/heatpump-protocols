#pragma once

#include "heatpump/HeatPumpCommand.h"
#include "comm/emulation/Rc100HEmulation.h"
#include "types/Constants.h"

using namespace Constants;

class NibeCommandHandler : public HeatPumpCommandBase {
public:
    NibeCommandHandler(ICommunicationBus* bus, IHeatPumpProtocol* protocol)
        : HeatPumpCommandBase(bus, protocol)
        , m_emulation(nullptr) {}

    void setEmulation(Rc100HEmulation* emulation) {
        m_emulation = emulation;
    }

    CommunicationError sendXW00Write(uint64_t variableId, uint32_t value) {
        if (!m_emulation) {
            return CommunicationError{CommErrorCode::UNKNOWN, "No CAN emulation configured for XW00", 0};
        }
        return m_emulation->sendXW00Write(variableId, value);
    }

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
        case HeatPumpCommandType::RESET_ALARM:
            return resetAlarm();
        default:
            return CommunicationError{CommErrorCode::UNKNOWN, "Command not supported for NIBE", 0};
        }
    }

    CommunicationError boostHeating(std::chrono::seconds duration) override {
        uint8_t hours = static_cast<uint8_t>(
            std::chrono::duration_cast<std::chrono::hours>(duration).count());
        if (hours == 0) hours = 1;

        auto err = writeNibeAndVerify(0x09, hours);
        if (!err.isOk()) return err;
        return writeNibeAndVerify(0xD1, 1);
    }

    CommunicationError stopHeating(bool enable) override {
        return writeNibeAndVerify(0x0A, enable ? 1 : 0);
    }

    CommunicationError setHolidayMode(bool enable) override {
        return writeNibeAndVerify(0x10, enable ? 1 : 0);
    }

    CommunicationError forceElectricHeater(bool enable) override {
        return writeNibeAndVerify(0x15, enable ? 1 : 0);
    }

    CommunicationError setHeatingSetpoint(float tempC) override {
        uint8_t raw = static_cast<uint8_t>((tempC - Nibe::TEMP_OFFSET) / Nibe::TEMP_SCALE);
        return writeNibeAndVerify(0x07, raw);
    }

    CommunicationError setWaterSetpoint(float tempC) override {
        uint8_t raw = static_cast<uint8_t>(tempC / Nibe::TEMP_SCALE);
        return writeNibeAndVerify(0x0B, raw);
    }

    CommunicationError setOperatingMode(uint8_t mode) override {
        return writeNibeAndVerify(0x01, mode);
    }

    CommunicationError setFanSpeed(uint8_t percent) override {
        if (percent > 100) percent = 100;
        return writeNibeAndVerify(0x1A, percent);
    }

    CommunicationError setPowerLimit(uint16_t watts) override {
        uint8_t raw = static_cast<uint8_t>(watts / 100);
        return writeNibeAndVerify(0x1B, raw);
    }

    CommunicationError setExtPort(uint8_t /*port*/, bool /*state*/) override {
        return CommunicationError{CommErrorCode::UNKNOWN, "EXT ports not supported for NIBE", 0};
    }

    CommunicationError resetAlarm() override {
        return writeNibeRegister(0xF2, 1);
    }

    CommunicationError resetAlarmWithRestart() {
        std::vector<uint8_t> alarmClear = buildNibeWriteCmd(0xF2, 1);
        auto err = writeAndWaitAck(alarmClear);
        if (!err.isOk()) return err;

        std::vector<uint8_t> restart = {0x38, 0x10, 0xFF, 0x00, 0x03, 0x7B, 0x00, 0x00};
        err = writeAndWaitAck(restart);
        if (!err.isOk()) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "Clearing alarms, resetting EB100 failed", 0};
        }
        return CommunicationError::ok();
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
        case HeatPumpCommandType::RESET_ALARM:
            return true;
        default:
            return false;
        }
    }

    CommunicationError writeRegister(uint8_t regAddr, uint8_t value) {
        return writeNibeAndVerify(regAddr, value);
    }

    CommunicationError writeRegisterRaw(uint8_t regAddr, uint8_t value) {
        return writeNibeRegister(regAddr, value);
    }

    // UNVERIFIED: 16-bit register write frame. The reference capture only
    // contains 8-byte one-byte-register writes. Confirm the payload length on
    // the target bridge; use writeRegister16Raw() to send a custom frame.
    CommunicationError writeRegister16(uint16_t regAddr, uint16_t value) {
        return writeAndVerify(buildNibeWriteCmd16(regAddr, value), regAddr, value);
    }

    CommunicationError writeRegister16Raw(uint16_t regAddr, uint16_t value) {
        return writeAndWaitAck(buildNibeWriteCmd16(regAddr, value));
    }

private:
    CommunicationError writeNibeRegister(uint8_t regAddr, uint8_t value) {
        std::vector<uint8_t> cmd = {
            0x38, 0x10, 0xFF, 0x00, 0x03, 0x2B, regAddr, value
        };

        auto err = writeAndWaitAck(cmd);
        if (!err.isOk()) return err;

        uint8_t nack;
        err = m_bus->readByte(nack, std::chrono::milliseconds(500));
        if (err.isOk()) {
            if (nack == 'W') return CommunicationError::ok();
        }
        return CommunicationError::ok();
    }

    CommunicationError writeNibeAndVerify(uint8_t regAddr, uint8_t value) {
        auto cmd = buildNibeWriteCmd(regAddr, value);
        return writeAndVerify(cmd, regAddr, value);
    }

    std::vector<uint8_t> buildNibeWriteCmd(uint8_t regAddr, uint8_t value) {
        return {0x38, 0x10, 0xFF, 0x00, 0x03, 0x2B, regAddr, value};
    }

    // UNVERIFIED: little-endian 16-bit register/value extension of the
    // 8-byte write frame. See docs/RESEARCH_JOURNAL.md before using it on
    // hardware.
    static std::vector<uint8_t> buildNibeWriteCmd16(uint16_t regAddr, uint16_t value) {
        return {
            0x38, 0x10, 0xFF, 0x00, 0x03, 0x2B,
            static_cast<uint8_t>(regAddr & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>(regAddr >> 8),
            static_cast<uint8_t>(value & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>(value >> 8),
        };
    }

    Rc100HEmulation* m_emulation;
};
