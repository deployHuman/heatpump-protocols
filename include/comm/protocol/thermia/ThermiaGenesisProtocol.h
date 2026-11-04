#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "comm/protocol/HeatPumpProtocolBase.h"
#include "comm/sensor/ModbusSensorParser.h"
#include "comm/sensor/ParserRegistry.h"
#include "comm/sensor/SensorCatalog.h"
#include "types/Constants.h"
#include "utils/ModbusRtu.h"

using namespace Constants;

/**
 * Thermia Atlas / Calibra / Calibra E / Calibra Cool / Calibra RXT /
 * Diplomat Inverter protocol using the official Genesis-platform Modbus map.
 *
 * Verification status:
 *   - Register addresses and scales come from the public Thermia Genesis
 *     Modbus protocol v17.1 document (see docs/RESEARCH_JOURNAL.md).
 *   - Modbus RTU is 19200 8E1 by default; Modbus TCP uses port 502.
 *   - The sensor list covers the useful telemetry subset; it is not the full
 *     address list.
 *   - Writes use function 0x06 and read back the 8-byte echo/CRC.
 *
 * This is a new protocol class; the existing ThermiaProtocol keeps the older
 * Rego-style register map for Diplomat/Villa Classic compatibility.
 */
class ThermiaGenesisProtocol : public HeatPumpProtocolBase {
public:
    ThermiaGenesisProtocol()
        : HeatPumpProtocolBase(HeatPumpProtocol::THERMIA_GENESIS,
                               buildSensorCatalog(),
                               buildParserRegistry())
        , m_scanActive(false)
        , m_lastFunction(ThermiaGenesis::READ_INPUT_REGISTERS) {}

    CommunicationError detectModel(HeatPumpModelInfo& modelInfo) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        SensorMapping versionMapping{
            0xFFFF, "control_version", "Control Software Version",
            SensorType::INTEGER, SensorUnit::NONE,
            ThermiaGenesis::VERSION_MAJOR_REG, 6, 1.0f, 0.0f, false,
            ThermiaGenesis::READ_INPUT_REGISTERS};

        auto err = sendQuery(versionMapping);
        if (!err.isOk()) return err;

        CommunicationError readErr;
        auto frame = readResponse(readErr);
        if (!readErr.isOk()) return readErr;
        if (frame.data.size() < 6) {
            return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                       "Thermia Genesis version response too short", 0};
        }

        uint16_t major = static_cast<uint16_t>((frame.data[0] << 8) | frame.data[1]);
        uint16_t minor = static_cast<uint16_t>((frame.data[2] << 8) | frame.data[3]);
        uint16_t micro = static_cast<uint16_t>((frame.data[4] << 8) | frame.data[5]);

        if (major > 99 || minor > 99 || micro > 99 ||
            (major == 0 && minor == 0 && micro == 0)) {
            return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                       "Not a Thermia Genesis controller", 0};
        }

        modelInfo.protocol = HeatPumpProtocol::THERMIA_GENESIS;
        modelInfo.brand = HeatPumpBrand::THERMIA;
        modelInfo.hpType = HeatPumpType::LW;
        modelInfo.modelName = "Thermia Atlas/Calibra (Genesis)";
        modelInfo.version = std::to_string(major) + "." + std::to_string(minor) +
                            "." + std::to_string(micro);
        modelInfo.writeEnabled = true;
        modelInfo.readOnly = false;
        m_modelInfo = modelInfo;
        return CommunicationError::ok();
    }

    [[nodiscard]] ProtocolCapabilities getCapabilities() const override {
        return {true, true, true, true, m_sensorCatalog.count(), Frame::MAX_DATA_SIZE};
    }

protected:
    CommunicationError onInit() override {
        m_scanActive = false;
        return CommunicationError::ok();
    }

    CommunicationError sendQuery(const SensorMapping& mapping) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};
        if (m_scanActive) {
            return CommunicationError{CommErrorCode::UNKNOWN, "Scan already in progress", 0};
        }

        uint16_t count = static_cast<uint16_t>((mapping.dataLength + 1) / 2);
        if (count == 0) count = 1;

        m_lastFunction = mapping.readFunction;
        m_scanActive = true;

        auto frame = ModbusRtu::buildReadRequestFrame(
            ThermiaGenesis::SLAVE_ADDRESS, mapping.readFunction,
            mapping.registerAddress, count);
        auto err = m_bus->writeBytes(frame);
        if (!err.isOk()) m_scanActive = false;
        return err;
    }

    Frame readResponse(CommunicationError& outErr) override {
        Frame frame;
        if (!m_scanActive) {
            outErr = CommunicationError::ok();
            return frame;
        }

        auto start = std::chrono::steady_clock::now();
        std::vector<uint8_t> response;
        response.reserve(ThermiaGenesis::RESPONSE_BUFFER_RESERVE);

        while (true) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > ThermiaGenesis::READ_TIMEOUT) {
                outErr = CommunicationError::timeout();
                m_scanActive = false;
                return frame;
            }

            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                ThermiaGenesis::READ_TIMEOUT - elapsed);

            uint8_t byte = 0;
            auto err = m_bus->readByte(byte, remaining);
            if (!err.isOk()) {
                outErr = err;
                m_scanActive = false;
                return frame;
            }
            response.push_back(byte);

            if (response.size() == 2 && (response[1] & 0x80) != 0) {
                while (response.size() < 5) {
                    uint8_t exceptionByte = 0;
                    auto exceptionErr = m_bus->readByte(exceptionByte, remaining);
                    if (!exceptionErr.isOk()) {
                        outErr = exceptionErr;
                        m_scanActive = false;
                        return frame;
                    }
                    response.push_back(exceptionByte);
                }
                outErr = CommunicationError{
                    CommErrorCode::WRITE_REJECTED,
                    "Modbus exception " + std::to_string(response[2]), 0};
                m_scanActive = false;
                return frame;
            }

            if (response.size() >= 3 &&
                response.size() >= static_cast<size_t>(response[2]) + 5) {
                break;
            }
        }

        frame.address = response[0];
        frame.command = response[1];
        frame.length = response[2];

        size_t dataEnd = 3 + frame.length;
        if (response.size() < dataEnd + Checksums::Crc16Modbus::SIZE) {
            outErr = CommunicationError::invalidFrame("Modbus CRC missing");
            m_scanActive = false;
            return frame;
        }

        frame.data.assign(response.begin() + 3, response.begin() + dataEnd);
        uint16_t receivedCrc = static_cast<uint16_t>(response[dataEnd]) |
                               (static_cast<uint16_t>(response[dataEnd + 1]) << 8);
        uint16_t computedCrc = Utils::crc16Modbus(
            std::span<const uint8_t>(response.data(), dataEnd));
        frame.checksum = receivedCrc;

        if (frame.address != ThermiaGenesis::SLAVE_ADDRESS ||
            frame.command != m_lastFunction) {
            outErr = CommunicationError{CommErrorCode::PROTOCOL_MISMATCH,
                                         "Modbus response does not match request", 0};
            m_scanActive = false;
            return frame;
        }

        if (computedCrc != receivedCrc) {
            outErr = CommunicationError::checksum(receivedCrc, computedCrc);
            frame.valid = false;
        } else {
            outErr = CommunicationError::ok();
            frame.valid = true;
        }

        m_scanActive = false;
        return frame;
    }

    CommunicationError doWriteValue(const SensorMapping& mapping,
                                     const SensorValue& value) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};
        if (mapping.readFunction != ThermiaGenesis::READ_HOLDING_REGISTERS) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "Sensor is not a holding register", 0};
        }

        uint16_t rawValue = static_cast<uint16_t>(
            (value.asFloat() - mapping.offset) / mapping.scale);
        auto frame = ModbusRtu::buildWriteSingleRegisterFrame(
            ThermiaGenesis::SLAVE_ADDRESS, mapping.registerAddress, rawValue);
        auto err = m_bus->writeBytes(frame);
        if (!err.isOk()) return err;

        return ModbusRtu::readWriteEcho(
            m_bus, ThermiaGenesis::SLAVE_ADDRESS,
            ThermiaGenesis::WRITE_HOLDING_REGISTER,
            mapping.registerAddress, rawValue, ThermiaGenesis::WRITE_TIMEOUT);
    }

    // doWriteValue() consumes the full Modbus echo, so the base-class single
    // ACK wait must not run again.
    CommunicationError waitForWriteAck(std::chrono::milliseconds) override {
        return CommunicationError::ok();
    }

private:
    static SensorCatalog buildSensorCatalog() {
        SensorCatalog catalog;

        auto temp = [](uint16_t id, const char* name, const char* display,
                       uint16_t reg, bool writable = false,
                       uint8_t function = ThermiaGenesis::READ_INPUT_REGISTERS,
                       float scale = ThermiaGenesis::TEMP_SCALE) {
            return SensorMapping{id, name, display, SensorType::TEMPERATURE,
                                 SensorUnit::CELSIUS, reg, 2,
                                 scale, ThermiaGenesis::TEMP_OFFSET,
                                 writable, function};
        };
        auto percent = [](uint16_t id, const char* name, const char* display,
                          uint16_t reg, float scale = 0.01f, bool writable = false,
                          uint8_t function = ThermiaGenesis::READ_INPUT_REGISTERS) {
            return SensorMapping{id, name, display, SensorType::PERCENT,
                                 SensorUnit::PERCENT, reg, 2, scale, 0.0f,
                                 writable, function};
        };
        auto power = [](uint16_t id, const char* name, const char* display,
                        uint16_t reg, float scale, bool writable = false,
                        uint8_t function = ThermiaGenesis::READ_INPUT_REGISTERS) {
            return SensorMapping{id, name, display, SensorType::POWER,
                                 SensorUnit::KILOWATT, reg, 2, scale, 0.0f,
                                 writable, function};
        };
        auto integer = [](uint16_t id, const char* name, const char* display,
                          uint16_t reg, bool writable = false,
                          uint8_t function = ThermiaGenesis::READ_INPUT_REGISTERS) {
            return SensorMapping{id, name, display, SensorType::INTEGER,
                                 SensorUnit::NONE, reg, 2, 1.0f, 0.0f,
                                 writable, function};
        };
        auto pressure = [](uint16_t id, const char* name, const char* display,
                           uint16_t reg) {
            return SensorMapping{id, name, display, SensorType::PRESSURE,
                                 SensorUnit::BAR, reg, 2, 0.01f, 0.0f,
                                 false, ThermiaGenesis::READ_INPUT_REGISTERS};
        };

        catalog.addAll({
            temp(0, "outdoor_temp", "Outdoor Temperature", 13),
            temp(1, "discharge_pipe_temp", "Discharge Pipe Temperature", 7),
            temp(2, "condenser_in_temp", "Condenser In Temperature", 8),
            temp(3, "condenser_out_temp", "Condenser Out Temperature", 9),
            temp(4, "brine_in_temp", "Brine In Temperature", 10),
            temp(5, "brine_out_temp", "Brine Out Temperature", 11),
            temp(6, "system_supply_line_temp", "System Supply Line Temperature", 12),
            temp(7, "tap_water_top_temp", "Tap Water Top Temperature", 15),
            temp(8, "tap_water_lower_temp", "Tap Water Lower Temperature", 16),
            temp(9, "tap_water_weighted_temp", "Tap Water Weighted Temperature", 17),
            temp(10, "supply_setpoint", "Supply Line Calculated Setpoint", 18),
            temp(11, "system_return_line_temp", "System Return Line Temperature", 27),
            temp(12, "calculated_demand_heat", "Calculated Heat Demand", 30),
            percent(13, "condenser_pump_speed", "Condenser Pump Speed", 39),
            temp(14, "mix_valve_1_supply_temp", "Mix Valve 1 Supply Temperature", 40),
            temp(15, "buffer_tank_temp", "Buffer Tank Temperature", 41),
            percent(16, "mix_valve_1_position", "Mix Valve 1 Position", 43),
            percent(17, "brine_pump_speed", "Brine Pump Speed", 44),
            percent(18, "tap_water_valve_position", "Tap Water Valve Position", 47, 1.0f),
            SensorMapping{19, "compressor_speed", "Compressor Speed",
                          SensorType::SPEED, SensorUnit::RPM, 5, 2, 1.0f, 0.0f,
                          false, ThermiaGenesis::READ_INPUT_REGISTERS},
            percent(20, "compressor_speed_percent", "Compressor Speed Percent", 54),
            power(21, "current_power_output", "Current Heat Output", 157, 0.01f),
            power(22, "current_consumed_power", "Current Consumed Power", 158, 0.01f),
            percent(23, "expansion_valve_opening", "Expansion Valve Opening", 315),
            temp(24, "inverter_temp", "Inverter Temperature", 319, false,
                 ThermiaGenesis::READ_INPUT_REGISTERS, 1.0f),
            temp(25, "room_temp", "Room Temperature", 121, false,
                 ThermiaGenesis::READ_INPUT_REGISTERS, 0.1f),
            pressure(26, "low_pressure", "Low Pressure Side", 127),
            pressure(27, "high_pressure", "High Pressure Side", 128),
            SensorMapping{28, "compressor_hours", "Compressor Operating Hours",
                          SensorType::RUNTIME, SensorUnit::HOURS, 48, 4, 1.0f, 0.0f,
                          false, ThermiaGenesis::READ_INPUT_REGISTERS},
            integer(29, "operating_mode", "Operational Mode", 0, true,
                    ThermiaGenesis::READ_HOLDING_REGISTERS),
            temp(30, "max_radiator_setpoint", "Max Radiator Setpoint", 3, true,
                 ThermiaGenesis::READ_HOLDING_REGISTERS),
            temp(31, "min_radiator_setpoint", "Min Radiator Setpoint", 4, true,
                 ThermiaGenesis::READ_HOLDING_REGISTERS),
            temp(32, "comfort_wheel", "Comfort Wheel", 5, true,
                 ThermiaGenesis::READ_HOLDING_REGISTERS),
            temp(33, "heating_season_stop", "Heating Season Stop", 16, true,
                 ThermiaGenesis::READ_HOLDING_REGISTERS),
            temp(34, "tap_water_start", "Tap Water Start Temperature", 22, true,
                 ThermiaGenesis::READ_HOLDING_REGISTERS),
            temp(35, "tap_water_stop", "Tap Water Stop Temperature", 23, true,
                 ThermiaGenesis::READ_HOLDING_REGISTERS),
            temp(36, "fixed_supply_setpoint", "Fixed Supply Setpoint", 116, true,
                 ThermiaGenesis::READ_HOLDING_REGISTERS),
            integer(37, "outdoor_temp_source", "Outdoor Temperature Source", 117, true,
                    ThermiaGenesis::READ_HOLDING_REGISTERS),
            temp(38, "bms_outdoor_temp", "BMS Outdoor Temperature", 118, true,
                 ThermiaGenesis::READ_HOLDING_REGISTERS),
            integer(39, "power_limit_mode", "Power Limit Mode", 124, true,
                    ThermiaGenesis::READ_HOLDING_REGISTERS),
            power(40, "power_limit_kw", "Power Limit", 125, 0.1f, true,
                  ThermiaGenesis::READ_HOLDING_REGISTERS),
            percent(41, "pool_charge_setpoint", "Pool Charge Setpoint", 58,
                    0.01f, true, ThermiaGenesis::READ_HOLDING_REGISTERS),
            percent(42, "cooling_mix_valve_setpoint", "Cooling Mix Valve Setpoint",
                    30, 0.01f, true, ThermiaGenesis::READ_HOLDING_REGISTERS),
        });
        return catalog;
    }

    static ParserRegistry buildParserRegistry() {
        ParserRegistry registry;
        registry.registerParser(SensorType::TEMPERATURE, ModbusSensorParser::parse16);
        registry.registerParser(SensorType::PRESSURE, ModbusSensorParser::parse16);
        registry.registerParser(SensorType::POWER, ModbusSensorParser::parse16);
        registry.registerParser(SensorType::SPEED, ModbusSensorParser::parse16);
        registry.registerParser(SensorType::PERCENT, ModbusSensorParser::parse16);
        registry.registerParser(SensorType::INTEGER, ModbusSensorParser::parse16);
        registry.registerParser(SensorType::RUNTIME, ModbusSensorParser::parse32);
        registry.registerParser(SensorType::ENERGY, ModbusSensorParser::parse32);
        return registry;
    }

    bool m_scanActive;
    uint8_t m_lastFunction;
};
