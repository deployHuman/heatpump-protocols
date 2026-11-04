#pragma once

#include "heatpump/commands/BridgeTemperatureEncoding.h"
#include "heatpump/commands/NibeCommandHandler.h"
#include "types/Constants.h"

using namespace Constants;

/**
 * NIBE EB100 command handler using the extended 16-bit bridge register map.
 *
 * Verification status:
 *   - Register IDs match the public NIBE EB100 register list.
 *   - Temperature values default to the documented bridge encoding
 *     (`tempC * 10`). Use `PumpRawOffset50` only if a read shows the
 *     pump-raw `(tempC + 50) * 10` value.
 *   - The 16-bit write frame
 *     `38 10 FF 00 03 2B regLo regHi valLo valHi` is UNVERIFIED. The reference
 *     capture only contains 8-byte one-byte-register writes. Use
 *     `writeRegister16Raw()` if the target bridge expects a different frame.
 *
 * See docs/RESEARCH_JOURNAL.md for sources, verification notes and open
 * questions.
 */
class NibeEb100ExtendedCommandHandler : public NibeCommandHandler {
public:
    NibeEb100ExtendedCommandHandler(
        ICommunicationBus* bus,
        IHeatPumpProtocol* protocol,
        BridgeTemperatureEncoding encoding = BridgeTemperatureEncoding::BridgeDegrees)
        : NibeCommandHandler(bus, protocol)
        , m_temperatureEncoding(encoding) {}

    CommunicationError boostHeating(std::chrono::seconds duration) override {
        // The public register list has no boost register. Keep the legacy
        // 8-bit path for now, but treat it as unverified on an EB100 bridge.
        return NibeCommandHandler::boostHeating(duration);
    }

    CommunicationError stopHeating(bool /*enable*/) override {
        return unsupported("stop heating is not exposed by the NIBE EB100 register list");
    }

    CommunicationError setHeatingSetpoint(float tempC) override {
        return writeRegister16(0x0107, encodeTemperature(tempC));
    }

    CommunicationError setWaterSetpoint(float /*tempC*/) override {
        return unsupported("hot water setpoint is not in the NIBE EB100 register list");
    }

    CommunicationError setHolidayMode(bool /*enable*/) override {
        return unsupported("holiday mode is not in the NIBE EB100 register list");
    }

    CommunicationError forceElectricHeater(bool /*enable*/) override {
        return unsupported("electric heater on the documented NIBE EB100 register map");
    }

    CommunicationError setOperatingMode(uint8_t mode) override {
        if (mode > 2) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "NIBE EB100 operating mode must be 0, 1 or 2", 0};
        }
        return writeRegister16(0x2201, mode);
    }

    CommunicationError setFanSpeed(uint8_t /*percent*/) override {
        return unsupported("fan speed on the documented NIBE EB100 register map");
    }

    CommunicationError setPowerLimit(uint16_t /*watts*/) override {
        return unsupported("power limit; use setMaxAddHeat() on NIBE EB100");
    }

    CommunicationError resetAlarm() override {
        return writeRegister16(0x22F2, 1);
    }

    CommunicationError setRoomSetpoint(float tempC) {
        return writeRegister16(0x0203, encodeTemperature(tempC));
    }

    CommunicationError setRoomSensorInfluence(uint16_t influence) {
        return writeRegister16(0x2204, influence);
    }

    CommunicationError setHeatCurve(uint16_t curve) {
        return writeRegister16(0x2205, curve);
    }

    CommunicationError setHeatCurveParallel(uint16_t parallel) {
        return writeRegister16(0x2207, parallel);
    }

    CommunicationError setWarmWaterProgram(uint8_t program) {
        if (program != 0 && program != 1 && program != 2 && program != 4) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "NIBE EB100 warm water program must be 0, 1, 2 or 4", 0};
        }
        return writeRegister16(0x2213, program);
    }

    CommunicationError setPoolMode(bool enable) {
        return writeRegister16(0x2218, enable ? 1 : 0);
    }

    CommunicationError setRoomSetpoint2(float tempC) {
        return writeRegister16(0x0220, encodeTemperature(tempC));
    }

    CommunicationError setHeatCurve2(uint16_t curve) {
        return writeRegister16(0x2222, curve);
    }

    CommunicationError setHeatCurveParallel2(uint16_t parallel) {
        return writeRegister16(0x2224, parallel);
    }

    CommunicationError setMaxAddHeat(uint16_t kilowatts) {
        return writeRegister16(0x9226, kilowatts);
    }

    CommunicationError setRoomController(float tempC) {
        return writeRegister16(0x02F1, encodeTemperature(tempC));
    }

    CommunicationError setDegreeMinutes(int16_t degreeMinutes) {
        return writeRegister16(0x8255, static_cast<uint16_t>(degreeMinutes));
    }

    [[nodiscard]] bool supportsCommand(HeatPumpCommandType type) const override {
        switch (type) {
        case HeatPumpCommandType::BOOST_HEATING:
        case HeatPumpCommandType::SET_HEATING_SETPOINT:
        case HeatPumpCommandType::SET_OPERATING_MODE:
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
