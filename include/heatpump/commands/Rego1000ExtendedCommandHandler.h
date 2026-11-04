#pragma once

#include "heatpump/commands/BridgeTemperatureEncoding.h"
#include "heatpump/commands/RegoCommandHandler.h"
#include "types/Constants.h"

using namespace Constants;

/**
 * Rego 1000 command handler using the extended bridge register map.
 *
 * Verification status:
 *   - Register IDs match the public Rego 1000 register list.
 *   - Temperature values default to the documented bridge encoding
 *     (`tempC * 10`). Use `PumpRawOffset50` only if a read shows the
 *     pump-raw `(tempC + 50) * 10` value.
 *   - The write frame is the bridge/UART abstraction, not a direct Rego 1000
 *     service-port frame.
 *
 * See docs/RESEARCH_JOURNAL.md for sources, verification notes and open
 * questions.
 */
class Rego1000ExtendedCommandHandler : public RegoCommandHandler {
public:
    Rego1000ExtendedCommandHandler(
        ICommunicationBus* bus,
        IHeatPumpProtocol* protocol,
        BridgeTemperatureEncoding encoding = BridgeTemperatureEncoding::BridgeDegrees)
        : RegoCommandHandler(bus, protocol)
        , m_temperatureEncoding(encoding) {}

    CommunicationError setHeatingSetpoint(float tempC) override {
        return writeRegister(0x0107, encodeTemperature(tempC));
    }

    CommunicationError boostHeating(std::chrono::seconds duration) override {
        auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
        return setExtraWarmWater(hours);
    }

    CommunicationError setWaterSetpoint(float tempC) override {
        return writeRegister(0x0111, encodeTemperature(tempC));
    }

    CommunicationError stopHeating(bool /*enable*/) override {
        return unsupported("stop heating on the documented Rego 1000 register map");
    }

    CommunicationError forceElectricHeater(bool /*enable*/) override {
        return unsupported("electric heater on the documented Rego 1000 register map");
    }

    CommunicationError setOperatingMode(uint8_t /*mode*/) override {
        return unsupported("operating mode on the documented Rego 1000 register map");
    }

    CommunicationError setHolidayMode(bool enable) override {
        return writeRegister(0x1210, enable ? 1 : 0);
    }

    CommunicationError resetAlarm() override {
        return writeRegister(0x22F2, 1);
    }

    CommunicationError setPowerLimit(uint16_t /*watts*/) override {
        return unsupported("power limit; use the model-specific limit registers");
    }

    CommunicationError setFanSpeed(uint8_t /*percent*/) override {
        return unsupported("fan speed on the documented Rego 1000 register map");
    }

    CommunicationError setExtPort(uint8_t /*port*/, bool /*state*/) override {
        return unsupported("external port; use setExternalControl() on Rego 1000");
    }

    CommunicationError setExtraWarmWater(std::chrono::hours hours) {
        auto count = hours.count();
        if (count == 0) count = 1;
        if (count < 0) count = 1;
        if (count > 65535) count = 65535;
        return writeRegister(0x6209, static_cast<uint16_t>(count));
    }

    CommunicationError setSummerMode(uint8_t mode) {
        if (mode > 2) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "Rego 1000 summer mode must be 0, 1 or 2", 0};
        }
        return writeRegister(0xB20A, mode);
    }

    CommunicationError setWarmWaterProgram(uint8_t program) {
        if (program > 1) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "Rego 1000 warm water program must be 0 or 1", 0};
        }
        return writeRegister(0x2213, program);
    }

    CommunicationError setWarmWaterState(uint8_t state) {
        if (state > 2) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "Rego 1000 warm water state must be 0, 1 or 2", 0};
        }
        return writeRegister(0x2216, state);
    }

    CommunicationError setWarmWaterTimeProgram(uint8_t program) {
        if (program > 2) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "Rego 1000 warm water time program must be 0, 1 or 2", 0};
        }
        return writeRegister(0x221C, program);
    }

    CommunicationError setExternalControl(uint8_t port, bool enable) {
        if (port < 1 || port > 2) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "Rego 1000 external control port must be 1 or 2", 0};
        }
        return writeRegister(port == 1 ? 0x2233 : 0x2234, enable ? 1 : 0);
    }

    CommunicationError setOutdoorTempOffset(float deltaC) {
        auto raw = static_cast<int16_t>(deltaC / Rego1000::TEMP_SCALE);
        return writeRegister(0x0217, static_cast<uint16_t>(raw));
    }

    CommunicationError setHeatingSetpoint2(float tempC) {
        return writeRegister(0x0120, encodeTemperature(tempC));
    }

    CommunicationError setRoomSetpoint2(float tempC) {
        return writeRegister(0x0220, encodeTemperature(tempC));
    }

    CommunicationError setHeatCurve(uint16_t curveLeft) {
        return writeRegister(0x0205, curveLeft);
    }

    CommunicationError setHeatCurveRight(uint16_t curveRight) {
        return writeRegister(0x0206, curveRight);
    }

    CommunicationError setHeatCurve2(uint16_t curve) {
        return writeRegister(0x0222, curve);
    }

    CommunicationError setHeatCurveParallel2(uint16_t parallel) {
        return writeRegister(0x0223, parallel);
    }

    CommunicationError setPoolSetpoint(float tempC) {
        return writeRegister(0x0219, encodeTemperature(tempC));
    }

    CommunicationError setRoomController(float tempC) {
        return writeRegister(0x02F1, encodeTemperature(tempC));
    }

    CommunicationError setRoomControllerDialRange(uint8_t range) {
        return writeRegister(0x22F3, range);
    }

    [[nodiscard]] bool supportsCommand(HeatPumpCommandType type) const override {
        switch (type) {
        case HeatPumpCommandType::BOOST_HEATING:
        case HeatPumpCommandType::SET_HEATING_SETPOINT:
        case HeatPumpCommandType::SET_WATER_SETPOINT:
        case HeatPumpCommandType::SET_HOLIDAY_MODE:
        case HeatPumpCommandType::SET_TIMER_OVERRIDE:
        case HeatPumpCommandType::RESET_ALARM:
            return true;
        default:
            return false;
        }
    }

private:
    uint16_t encodeTemperature(float tempC) const {
        return encodeBridgeTemperature(tempC, m_temperatureEncoding);
    }

    BridgeTemperatureEncoding m_temperatureEncoding;
};
