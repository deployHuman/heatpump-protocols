#pragma once

#include "heatpump/HeatPumpCommand.h"
#include "utils/ModbusRtu.h"
#include "types/Constants.h"
#include <vector>

using namespace Constants;

/**
 * Thermia Mega/Genesis command handler for the official Modbus RTU map.
 *
 * Verification status:
 *   - Coil and holding-register addresses are checked against the public
 *     Thermia Mega/Genesis Modbus table.
 *   - Coils use the wire/reference address (de-facto address minus 1).
 *   - Holding registers use the de-facto address minus 40001.
 *   - Temperature values use `tempC * 100`.
 *
 * This is an opt-in alternative to ThermiaCommandHandler. The existing
 * ThermiaCommandHandler uses the simplified Rego-style register map; this
 * handler uses the official Thermia Mega/Genesis map.
 *
 * See docs/RESEARCH_JOURNAL.md for sources and verification notes.
 */
class ThermiaGenesisCommandHandler : public HeatPumpCommandBase {
public:
    static constexpr uint16_t COIL_RESET_ALARMS = 3;
    static constexpr uint16_t COIL_EXTERNAL_ADDITIONAL_HEATER = 5;
    static constexpr uint16_t COIL_ENABLE_TAP_WATER = 8;
    static constexpr uint16_t COIL_ENABLE_HEAT = 9;
    static constexpr uint16_t COIL_ENABLE_ACTIVE_COOLING = 10;
    static constexpr uint16_t COIL_ENABLE_POOL = 28;
    static constexpr uint16_t COIL_ENABLE_PASSIVE_COOLING = 33;
    static constexpr uint16_t COIL_ENABLE_FIXED_SUPPLY_SETPOINT = 41;

    static constexpr uint16_t HR_OPERATING_MODE = 40001;
    static constexpr uint16_t HR_MAX_RADIATOR_SETPOINT = 40004;
    static constexpr uint16_t HR_MIN_RADIATOR_SETPOINT = 40005;
    static constexpr uint16_t HR_COMFORT_WHEEL = 40006;
    static constexpr uint16_t HR_HEAT_CURVE_POINT_1 = 40007;
    static constexpr uint16_t HR_HEATING_SEASON_STOP = 40017;
    static constexpr uint16_t HR_TAP_WATER_START = 40023;
    static constexpr uint16_t HR_TAP_WATER_STOP = 40024;
    static constexpr uint16_t HR_MIN_GEAR_HEATING = 40027;
    static constexpr uint16_t HR_MAX_GEAR_HEATING = 40028;
    static constexpr uint16_t HR_MAX_GEAR_TAP_WATER = 40029;
    static constexpr uint16_t HR_MIN_GEAR_TAP_WATER = 40030;
    static constexpr uint16_t HR_COOLING_MIX_VALVE_SETPOINT = 40031;
    static constexpr uint16_t HR_POOL_CHARGE_SETPOINT = 40059;
    static constexpr uint16_t HR_FIXED_SUPPLY_SETPOINT = 40117;

    ThermiaGenesisCommandHandler(ICommunicationBus* bus, IHeatPumpProtocol* protocol,
                                 uint8_t slaveAddress = 0x01)
        : HeatPumpCommandBase(bus, protocol)
        , m_slaveAddress(slaveAddress) {}

    CommunicationError sendCommand(const HeatPumpCommand& cmd) override {
        switch (cmd.type) {
        case HeatPumpCommandType::STOP_HEATING:
            return stopHeating(cmd.value != 0);
        case HeatPumpCommandType::FORCE_ELECTRIC_HEATER:
            return forceElectricHeater(cmd.value != 0);
        case HeatPumpCommandType::SET_WATER_SETPOINT:
            return setWaterSetpoint(static_cast<float>(cmd.value) / 10.0f);
        case HeatPumpCommandType::SET_OPERATING_MODE:
            return setOperatingMode(static_cast<uint8_t>(cmd.value));
        case HeatPumpCommandType::RESET_ALARM:
            return resetAlarm();
        default:
            return unsupported("command not supported by the Thermia Modbus map");
        }
    }

    CommunicationError boostHeating(std::chrono::seconds /*duration*/) override {
        return unsupported("boost heating is not exposed by the Thermia Modbus map");
    }

    CommunicationError stopHeating(bool enable) override {
        return enableHeating(!enable);
    }

    CommunicationError forceElectricHeater(bool enable) override {
        return enableExternalAdditionalHeater(enable);
    }

    CommunicationError setHeatingSetpoint(float /*tempC*/) override {
        return unsupported("room setpoint is not exposed; use setFixedSupplySetpoint()");
    }

    CommunicationError setWaterSetpoint(float tempC) override {
        return setTapWaterStop(tempC);
    }

    CommunicationError setOperatingMode(uint8_t mode) override {
        if (mode < 1 || mode > 3) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "Thermia operating mode must be 1 (off), 2 (standby) or 3 (auto)", 0};
        }
        return writeHoldingRegister(HR_OPERATING_MODE, mode);
    }

    CommunicationError resetAlarm() override {
        return writeCoil(COIL_RESET_ALARMS, true);
    }

    CommunicationError enableHeating(bool enable) {
        return writeCoil(COIL_ENABLE_HEAT, enable);
    }

    CommunicationError enableTapWater(bool enable) {
        return writeCoil(COIL_ENABLE_TAP_WATER, enable);
    }

    CommunicationError enableActiveCooling(bool enable) {
        return writeCoil(COIL_ENABLE_ACTIVE_COOLING, enable);
    }

    CommunicationError enablePassiveCooling(bool enable) {
        return writeCoil(COIL_ENABLE_PASSIVE_COOLING, enable);
    }

    CommunicationError enablePool(bool enable) {
        return writeCoil(COIL_ENABLE_POOL, enable);
    }

    CommunicationError enableExternalAdditionalHeater(bool enable) {
        return writeCoil(COIL_EXTERNAL_ADDITIONAL_HEATER, enable);
    }

    CommunicationError setMaxRadiatorSetpoint(float tempC) {
        return writeHoldingRegister(HR_MAX_RADIATOR_SETPOINT, encodeTemperature(tempC));
    }

    CommunicationError setMinRadiatorSetpoint(float tempC) {
        return writeHoldingRegister(HR_MIN_RADIATOR_SETPOINT, encodeTemperature(tempC));
    }

    CommunicationError setComfortWheel(float tempC) {
        return writeHoldingRegister(HR_COMFORT_WHEEL, encodeTemperature(tempC));
    }

    CommunicationError setHeatCurvePoint(uint8_t index, float tempC) {
        if (index < 1 || index > 7) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "Thermia heat curve point must be 1..7", 0};
        }
        return writeHoldingRegister(
            static_cast<uint16_t>(HR_HEAT_CURVE_POINT_1 + index - 1),
            encodeTemperature(tempC));
    }

    CommunicationError setHeatingSeasonStop(float tempC) {
        return writeHoldingRegister(HR_HEATING_SEASON_STOP, encodeTemperature(tempC));
    }

    CommunicationError setTapWaterStart(float tempC) {
        return writeHoldingRegister(HR_TAP_WATER_START, encodeTemperature(tempC));
    }

    CommunicationError setTapWaterStop(float tempC) {
        return writeHoldingRegister(HR_TAP_WATER_STOP, encodeTemperature(tempC));
    }

    CommunicationError setMinGearHeating(uint16_t gear) {
        return writeHoldingRegister(HR_MIN_GEAR_HEATING, gear);
    }

    CommunicationError setMaxGearHeating(uint16_t gear) {
        return writeHoldingRegister(HR_MAX_GEAR_HEATING, gear);
    }

    CommunicationError setMaxGearTapWater(uint16_t gear) {
        return writeHoldingRegister(HR_MAX_GEAR_TAP_WATER, gear);
    }

    CommunicationError setMinGearTapWater(uint16_t gear) {
        return writeHoldingRegister(HR_MIN_GEAR_TAP_WATER, gear);
    }

    CommunicationError setCoolingMixValvePercent(float percent) {
        return writeHoldingRegister(HR_COOLING_MIX_VALVE_SETPOINT, encodePercent(percent));
    }

    CommunicationError setPoolChargePercent(float percent) {
        return writeHoldingRegister(HR_POOL_CHARGE_SETPOINT, encodePercent(percent));
    }

    CommunicationError setFixedSupplySetpoint(float tempC) {
        auto err = writeHoldingRegister(HR_FIXED_SUPPLY_SETPOINT, encodeTemperature(tempC));
        if (!err.isOk()) return err;
        return writeCoil(COIL_ENABLE_FIXED_SUPPLY_SETPOINT, true);
    }

    CommunicationError clearFixedSupplySetpoint() {
        return writeCoil(COIL_ENABLE_FIXED_SUPPLY_SETPOINT, false);
    }

    [[nodiscard]] bool supportsCommand(HeatPumpCommandType type) const override {
        switch (type) {
        case HeatPumpCommandType::STOP_HEATING:
        case HeatPumpCommandType::FORCE_ELECTRIC_HEATER:
        case HeatPumpCommandType::SET_WATER_SETPOINT:
        case HeatPumpCommandType::SET_OPERATING_MODE:
        case HeatPumpCommandType::RESET_ALARM:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] uint8_t getSlaveAddress() const { return m_slaveAddress; }

private:
    CommunicationError writeHoldingRegister(uint16_t deFactoAddress, uint16_t value) {
        if (deFactoAddress < 40001) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "Thermia holding register must be 40001 or greater", 0};
        }
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        uint16_t address = static_cast<uint16_t>(deFactoAddress - 40001);
        auto cmd = ModbusRtu::buildWriteSingleRegisterFrame(m_slaveAddress, address, value);
        auto err = m_bus->writeBytes(cmd);
        if (!err.isOk()) return err;

        return ModbusRtu::readWriteEcho(m_bus, m_slaveAddress, 0x06, address, value,
                                        DEFAULT_ACK_TIMEOUT);
    }

    CommunicationError writeCoil(uint16_t coilAddress, bool enable) {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        uint16_t value = enable ? 0xFF00 : 0x0000;
        auto cmd = ModbusRtu::buildWriteSingleCoilFrame(m_slaveAddress, coilAddress, enable);
        auto err = m_bus->writeBytes(cmd);
        if (!err.isOk()) return err;

        return ModbusRtu::readWriteEcho(m_bus, m_slaveAddress, 0x05, coilAddress, value,
                                        DEFAULT_ACK_TIMEOUT);
    }

    static uint16_t encodeTemperature(float tempC) {
        auto value = tempC * 100.0f;
        if (value <= 0.0f) return 0;
        if (value >= 65535.0f) return 65535;
        return static_cast<uint16_t>(value + 0.5f);
    }

    static uint16_t encodePercent(float percent) {
        auto value = percent * 100.0f;
        if (value <= 0.0f) return 0;
        if (value >= 65535.0f) return 65535;
        return static_cast<uint16_t>(value + 0.5f);
    }

    uint8_t m_slaveAddress;
};
