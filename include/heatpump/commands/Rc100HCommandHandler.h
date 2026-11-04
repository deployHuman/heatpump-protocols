#pragma once

#include "heatpump/HeatPumpCommand.h"
#include "comm/emulation/Rc100HEmulation.h"
#include "types/Constants.h"

using namespace Constants;

class Rc100HCommandHandler : public HeatPumpCommandBase {
public:
    explicit Rc100HCommandHandler(Rc100HEmulation* emulation)
        : HeatPumpCommandBase(nullptr, nullptr)
        , m_emulation(emulation) {}

    CommunicationError sendCommand(const HeatPumpCommand& cmd) override {
        switch (cmd.type) {
        case HeatPumpCommandType::SET_ROOM_TEMPERATURE:
            return setRoomTemperature(cmd.registerAddress, static_cast<float>(cmd.value) / 10.0f);
        case HeatPumpCommandType::SET_ROOM_HUMIDITY:
            return setRoomHumidity(static_cast<float>(cmd.value));
        case HeatPumpCommandType::SET_THERMOSTAT_MODE:
            return setThermostatMode(static_cast<Rc100H::Mode>(cmd.value));
        default:
            return CommunicationError{CommErrorCode::UNKNOWN, "Command not supported for RC100H", 0};
        }
    }

    CommunicationError setRoomTemperature(uint8_t circuit, float tempC) {
        if (!m_emulation) return makeNotReadyError();
        auto err = m_emulation->setTemperature(circuit, tempC);
        if (!err.isOk()) return err;
        return m_emulation->update();
    }

    CommunicationError setRoomHumidity(float humidity) {
        if (!m_emulation) return makeNotReadyError();
        auto err = m_emulation->setHumidity(humidity);
        if (!err.isOk()) return err;
        return m_emulation->update();
    }

    CommunicationError setThermostatMode(Rc100H::Mode mode) {
        if (!m_emulation) return makeNotReadyError();
        auto err = m_emulation->setMode(mode);
        if (!err.isOk()) return err;
        return m_emulation->update();
    }

    CommunicationError setDialRange(uint8_t range) {
        if (!m_emulation) return makeNotReadyError();
        return m_emulation->setDialRange(range);
    }

    [[nodiscard]] Rc100HThermostatState getState() const {
        return m_emulation ? m_emulation->getState() : Rc100HThermostatState{};
    }

    [[nodiscard]] bool isPaired() const {
        return m_emulation && m_emulation->isPaired();
    }

    CommunicationError boostHeating(std::chrono::seconds) override {
        return CommunicationError{CommErrorCode::UNKNOWN, "Boost via setpoint - use setRoomTemperature", 0};
    }
    CommunicationError stopHeating(bool) override {
        return CommunicationError{CommErrorCode::UNKNOWN, "Stop via mode - use setThermostatMode", 0};
    }
    CommunicationError forceElectricHeater(bool) override {
        return CommunicationError{CommErrorCode::UNKNOWN, "Not applicable for thermostat emulation", 0};
    }
    CommunicationError setHeatingSetpoint(float) override {
        return CommunicationError{CommErrorCode::UNKNOWN, "Use setRoomTemperature instead", 0};
    }
    CommunicationError setWaterSetpoint(float) override {
        return CommunicationError{CommErrorCode::UNKNOWN, "Not applicable for thermostat emulation", 0};
    }
    CommunicationError setOperatingMode(uint8_t) override {
        return CommunicationError{CommErrorCode::UNKNOWN, "Use setThermostatMode for RC100H modes", 0};
    }
    CommunicationError setHolidayMode(bool) override {
        return CommunicationError{CommErrorCode::UNKNOWN, "Not applicable for thermostat emulation", 0};
    }
    CommunicationError setFanSpeed(uint8_t) override {
        return CommunicationError{CommErrorCode::UNKNOWN, "Not applicable for thermostat emulation", 0};
    }
    CommunicationError setPowerLimit(uint16_t) override {
        return CommunicationError{CommErrorCode::UNKNOWN, "Not applicable for thermostat emulation", 0};
    }
    CommunicationError setExtPort(uint8_t, bool) override {
        return CommunicationError{CommErrorCode::UNKNOWN, "Not applicable for thermostat emulation", 0};
    }
    CommunicationError resetAlarm() override {
        return CommunicationError{CommErrorCode::UNKNOWN, "Not applicable for thermostat emulation", 0};
    }

    [[nodiscard]] bool supportsCommand(HeatPumpCommandType type) const override {
        switch (type) {
        case HeatPumpCommandType::SET_ROOM_TEMPERATURE:
        case HeatPumpCommandType::SET_ROOM_HUMIDITY:
        case HeatPumpCommandType::SET_THERMOSTAT_MODE:
            return true;
        default:
            return false;
        }
    }

private:
    static CommunicationError makeNotReadyError() {
        return CommunicationError{CommErrorCode::BUS_NOT_READY, "RC100H emulation not initialized", 0};
    }

    Rc100HEmulation* m_emulation;
};
