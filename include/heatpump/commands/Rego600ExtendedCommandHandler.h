#pragma once

#include "heatpump/commands/BridgeTemperatureEncoding.h"
#include "heatpump/commands/RegoCommandHandler.h"
#include "types/Constants.h"

using namespace Constants;

/**
 * Rego 600 command handler using the extended bridge register map.
 *
 * Verification status:
 *   - Register IDs match the public Rego 600 register list.
 *   - Temperature values default to the documented bridge encoding
 *     (`tempC * 10`). Use `PumpRawOffset50` only if a read shows the
 *     pump-raw `(tempC + 50) * 10` value.
 *   - The write frame is the bridge/UART abstraction, not a direct Rego 600
 *     service-port frame.
 *
 * See docs/RESEARCH_JOURNAL.md for sources, verification notes and open
 * questions.
 */
class Rego600ExtendedCommandHandler : public RegoCommandHandler {
public:
    Rego600ExtendedCommandHandler(
        ICommunicationBus* bus,
        IHeatPumpProtocol* protocol,
        BridgeTemperatureEncoding encoding = BridgeTemperatureEncoding::BridgeDegrees)
        : RegoCommandHandler(bus, protocol)
        , m_temperatureEncoding(encoding) {}

    CommunicationError boostHeating(std::chrono::seconds duration) override {
        // The public register list has no heating-boost register. Extra warm
        // water is the closest documented feature; use setExtraWarmWater()
        // directly if you want to control it without the boost semantic.
        auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
        return setExtraWarmWater(minutes);
    }

    CommunicationError setHeatingSetpoint(float tempC) override {
        return writeRegister(0x0107, encodeTemperature(tempC));
    }

    CommunicationError stopHeating(bool /*enable*/) override {
        return unsupported("stop heating; use setExternalControl() on Rego 600");
    }

    CommunicationError forceElectricHeater(bool enable) override {
        return setElectricHeaterEnabled(enable);
    }

    CommunicationError setOperatingMode(uint8_t /*mode*/) override {
        return unsupported("operating mode on the documented Rego 600 register map");
    }

    CommunicationError setFanSpeed(uint8_t /*percent*/) override {
        return unsupported("fan speed on Rego 600");
    }

    CommunicationError setPowerLimit(uint16_t /*watts*/) override {
        return unsupported("power limit; use setElectricHeaterMaxPower() on Rego 600");
    }

    CommunicationError setExtPort(uint8_t /*port*/, bool /*state*/) override {
        return unsupported("external port; use setExternalControl() on Rego 600");
    }

    CommunicationError setWaterSetpoint(float tempC) override {
        return writeRegister(0x0111, encodeTemperature(tempC));
    }

    CommunicationError resetAlarm() override {
        return writeRegisterRaw(0x12F2, 0);
    }

    CommunicationError setHolidayMode(bool enable) override {
        // The public register list expects 0..30 days, not a boolean. "On"
        // maps to the maximum documented duration; use setHolidayDays() for a
        // precise value.
        return writeRegister(0x2210, enable ? 30 : 0);
    }

    CommunicationError setExtraWarmWater(std::chrono::minutes minutes) {
        auto count = minutes.count();
        if (count <= 0) count = 1;
        if (count > 65535) count = 65535;
        return writeRegister(0x7209, static_cast<uint16_t>(count));
    }

    CommunicationError setHolidayDays(uint16_t days) {
        if (days > 30) days = 30;
        return writeRegister(0x2210, days);
    }

    CommunicationError setElectricHeaterEnabled(bool enable) {
        return writeRegister(0x1215, enable ? 1 : 0);
    }

    CommunicationError setElectricHeaterMaxPower(uint8_t steps) {
        if (steps != 1 && steps != 2 && steps != 4) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "Rego 600 electric heater steps must be 1, 2 or 4", 0};
        }
        return writeRegister(0xB21B, steps);
    }

    CommunicationError setExternalControl(bool enable) {
        return writeRegisterRaw(0x1233, enable ? 1 : 0);
    }

    CommunicationError setSummerMode(float tempC) {
        return writeRegister(0x020A, encodeTemperature(tempC));
    }

    CommunicationError setWarmWaterStopTemp(float tempC) {
        return writeRegister(0x0208, encodeTemperature(tempC));
    }

    CommunicationError setWarmWaterDiff(float deltaC) {
        return writeRegister(0x020B, encodeDelta(deltaC));
    }

    CommunicationError setRoomSetpoint(float tempC) {
        return writeRegister(0x0203, encodeTemperature(tempC));
    }

    CommunicationError setRoomSensorInfluence(uint16_t influence) {
        return writeRegister(0x2204, influence);
    }

    CommunicationError setHeatCurve(uint16_t curve) {
        return writeRegister(0x2205, curve);
    }

    CommunicationError setHeatCurveParallel(uint16_t parallel) {
        return writeRegister(0x0207, parallel);
    }

    [[nodiscard]] bool supportsCommand(HeatPumpCommandType type) const override {
        switch (type) {
        case HeatPumpCommandType::BOOST_HEATING:
        case HeatPumpCommandType::FORCE_ELECTRIC_HEATER:
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

    static uint16_t encodeDelta(float deltaC) {
        return encodeScaled(deltaC / Rego600::TEMP_SCALE);
    }

    static uint16_t encodeScaled(float value) {
        if (value <= 0.0f) return 0;
        if (value >= 65535.0f) return 65535;
        return static_cast<uint16_t>(value + 0.5f);
    }

    BridgeTemperatureEncoding m_temperatureEncoding;
};
