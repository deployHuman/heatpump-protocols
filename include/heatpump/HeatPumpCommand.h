#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <chrono>
#include <functional>
#include "comm/abstract/ICommunicationBus.h"
#include "comm/abstract/IHeatPumpProtocol.h"
#include "types/CommunicationError.h"
#include "types/Constants.h"

enum class HeatPumpCommandType : uint8_t {
    BOOST_HEATING,
    STOP_HEATING,
    FORCE_ELECTRIC_HEATER,
    SET_HEATING_SETPOINT,
    SET_WATER_SETPOINT,
    SET_OPERATING_MODE,
    SET_HOLIDAY_MODE,
    SET_TIMER_OVERRIDE,
    SET_FAN_SPEED,
    SET_POWER_LIMIT,
    SET_EXT_PORT,
    SET_ROOM_TEMPERATURE,
    SET_ROOM_HUMIDITY,
    SET_THERMOSTAT_MODE,
    RESET_ALARM,
};

struct HeatPumpCommand {
    HeatPumpCommandType type;
    uint16_t registerAddress;
    uint16_t value;
    std::chrono::seconds duration;
    std::string description;
};

class IHeatPumpCommandHandler {
public:
    virtual ~IHeatPumpCommandHandler() = default;

    virtual CommunicationError sendCommand(const HeatPumpCommand& cmd) = 0;
    virtual CommunicationError boostHeating(std::chrono::seconds duration) = 0;
    virtual CommunicationError stopHeating(bool enable) = 0;
    virtual CommunicationError forceElectricHeater(bool enable) = 0;
    virtual CommunicationError setHeatingSetpoint(float tempC) = 0;
    virtual CommunicationError setWaterSetpoint(float tempC) = 0;
    virtual CommunicationError setOperatingMode(uint8_t mode) = 0;
    virtual CommunicationError setHolidayMode(bool enable) = 0;
    virtual CommunicationError setFanSpeed(uint8_t percent) = 0;
    virtual CommunicationError setPowerLimit(uint16_t watts) = 0;
    virtual CommunicationError setExtPort(uint8_t port, bool state) = 0;
    virtual CommunicationError resetAlarm() = 0;

    [[nodiscard]] virtual bool supportsCommand(HeatPumpCommandType type) const = 0;
};

class HeatPumpCommandBase : public IHeatPumpCommandHandler {
public:
    explicit HeatPumpCommandBase(ICommunicationBus* bus, IHeatPumpProtocol* protocol)
        : m_bus(bus), m_protocol(protocol) {}

    using ReadBackFunc = std::function<CommunicationError(uint16_t regAddr, uint16_t& value)>;

    void setReadBackCallback(ReadBackFunc callback) {
        m_readBack = std::move(callback);
    }

    CommunicationError setHolidayMode(bool) override {
        return unsupported("holiday mode");
    }

    CommunicationError setFanSpeed(uint8_t) override {
        return unsupported("fan speed");
    }

    CommunicationError setPowerLimit(uint16_t) override {
        return unsupported("power limit");
    }

    CommunicationError setExtPort(uint8_t, bool) override {
        return unsupported("external port");
    }

protected:
    static CommunicationError unsupported(const char* feature) {
        return CommunicationError{CommErrorCode::UNKNOWN,
                                   std::string("Command not supported: ") + feature, 0};
    }
    CommunicationError writeAndWaitAck(const std::vector<uint8_t>& cmd,
                                        std::chrono::milliseconds timeout = DEFAULT_ACK_TIMEOUT) {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};
        auto err = m_bus->writeBytes(cmd);
        if (!err.isOk()) return err;

        uint8_t ack;
        err = m_bus->readByte(ack, timeout);
        if (!err.isOk()) return err;

        if (ack == 0x06) return CommunicationError::ok();
        if (ack == 0x15) return CommunicationError{CommErrorCode::WRITE_REJECTED, "Write rejected by heat pump (NAK)", 0};
        return CommunicationError{CommErrorCode::WRITE_REJECTED, "Unexpected response: 0x" +
                                   std::to_string(ack), 0};
    }

    CommunicationError readBackVerify(uint16_t regAddr, uint16_t expectedValue,
                                        int maxRetries = DEFAULT_READBACK_RETRIES) {
        if (!m_readBack) {
            return CommunicationError::ok();
        }

        for (int attempt = 0; attempt < maxRetries; attempt++) {
            uint16_t actualValue = 0;
            auto err = m_readBack(regAddr, actualValue);
            if (!err.isOk()) return err;

            if (actualValue == expectedValue) {
                return CommunicationError::ok();
            }
        }

        return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                   "Write verification failed: value mismatch after " +
                                   std::to_string(maxRetries) + " attempts", 0};
    }

    CommunicationError writeAndVerify(const std::vector<uint8_t>& cmd, uint16_t regAddr,
                                       uint16_t value) {
        auto err = writeAndWaitAck(cmd);
        if (!err.isOk()) return err;
        return readBackVerify(regAddr, value);
    }

    static constexpr std::chrono::milliseconds DEFAULT_ACK_TIMEOUT{3000};
    static constexpr int DEFAULT_READBACK_RETRIES = 3;

    ICommunicationBus* m_bus;
    IHeatPumpProtocol* m_protocol;
    ReadBackFunc m_readBack;
};
