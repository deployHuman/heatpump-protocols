#pragma once

#include "heatpump/commands/BridgeTemperatureEncoding.h"
#include "heatpump/commands/RegoCommandHandler.h"
#include "types/Constants.h"

using namespace Constants;

/**
 * Rego 800 command handler using the extended bridge register map.
 *
 * Verification status:
 *   - Register IDs match the public Rego 800 register list.
 *   - Temperature values default to the documented bridge encoding
 *     (`tempC * 10`). Use `PumpRawOffset50` only if a read shows the
 *     pump-raw `(tempC + 50) * 10` value.
 *   - The write frame is the bridge/UART abstraction, not a direct Rego 800
 *     service-port frame.
 *
 * See docs/RESEARCH_JOURNAL.md for sources, verification notes and open
 * questions.
 */
class Rego800ExtendedCommandHandler : public RegoCommandHandler {
public:
    Rego800ExtendedCommandHandler(
        ICommunicationBus* bus,
        IHeatPumpProtocol* protocol,
        BridgeTemperatureEncoding encoding = BridgeTemperatureEncoding::BridgeDegrees)
        : RegoCommandHandler(bus, protocol)
        , m_temperatureEncoding(encoding) {}

    CommunicationError setHeatingSetpoint(float tempC) override {
        return writeRegister(0x0107, encodeTemperature(tempC));
    }

    CommunicationError boostHeating(std::chrono::seconds /*duration*/) override {
        return unsupported("boost heating on the documented Rego 800 register map");
    }

    CommunicationError stopHeating(bool /*enable*/) override {
        return unsupported("stop heating on the documented Rego 800 register map");
    }

    CommunicationError forceElectricHeater(bool /*enable*/) override {
        return unsupported("electric heater on the documented Rego 800 register map");
    }

    CommunicationError setWaterSetpoint(float /*tempC*/) override {
        return unsupported("hot water setpoint is not in the Rego 800 extended list");
    }

    CommunicationError setOperatingMode(uint8_t /*mode*/) override {
        return unsupported("operating mode on Rego 800");
    }

    CommunicationError setHolidayMode(bool /*enable*/) override {
        return unsupported("holiday mode on the documented Rego 800 register map");
    }

    CommunicationError setFanSpeed(uint8_t /*percent*/) override {
        return unsupported("fan speed on Rego 800");
    }

    CommunicationError setPowerLimit(uint16_t /*watts*/) override {
        return unsupported("power limit; use setElectricHeaterMaxPowerKw() on Rego 800");
    }

    CommunicationError setExtPort(uint8_t /*port*/, bool /*state*/) override {
        return unsupported("external port on Rego 800");
    }

    CommunicationError resetAlarm() override {
        return unsupported("alarm reset is not in the Rego 800 extended list");
    }

    CommunicationError setRoomSetpoint(float tempC) {
        return writeRegister(0x0203, encodeTemperature(tempC));
    }

    CommunicationError setRoomSensorInfluence(uint16_t influence) {
        return writeRegister(0x2204, influence);
    }

    CommunicationError setHeatCurve(uint16_t curveLeft) {
        return writeRegister(0x0205, curveLeft);
    }

    CommunicationError setHeatCurveRight(uint16_t curveRight) {
        return writeRegister(0x0206, curveRight);
    }

    CommunicationError setWarmWaterStopTemp(float tempC) {
        return writeRegister(0x0208, encodeTemperature(tempC));
    }

    CommunicationError setWarmWaterStartTemp(float tempC) {
        return writeRegister(0x0212, encodeTemperature(tempC));
    }

    CommunicationError setElectricHeaterMaxPowerKw(uint16_t kilowatts) {
        return writeRegister(0x921B, kilowatts);
    }

    [[nodiscard]] bool supportsCommand(HeatPumpCommandType type) const override {
        switch (type) {
        case HeatPumpCommandType::SET_HEATING_SETPOINT:
        case HeatPumpCommandType::SET_TIMER_OVERRIDE:
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
