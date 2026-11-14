#pragma once

#include <cstdint>

#include "comm/protocol/ModbusProtocolBase.h"
#include "comm/sensor/SensorCatalog.h"
#include "types/Constants.h"
#include "utils/ModbusRtu.h"

using namespace Constants;

/**
 * NIBE S-series / F-series Modbus RTU protocol.
 *
 * Register addresses follow the NIBE "coil" numbering used by the public
 * `nibe` Python library and the Home Assistant nibe_heatpump integration:
 * the first digit selects the Modbus entity type (0 coil, 1 discrete input,
 * 3 input register, 4 holding register) and the last four digits are the
 * 1-based register number. The wire address is therefore
 * `(coilAddress % 10000) - 1`.
 *
 * Examples:
 *   31029 -> input register 1028 (priority)
 *   40207 -> holding register 206 (room setpoint heat, climate system 1)
 *
 * Verification status:
 *   - Addresses, factors, signedness and writability come from the NIBE
 *     model JSON files in https://github.com/yozik04/nibe/tree/master/nibe/data
 *     and the Home Assistant integration in
 *     https://github.com/home-assistant/core/tree/dev/homeassistant/components/nibe_heatpump
 *   - No hardware capture has been recorded yet. See docs/RESEARCH_JOURNAL.md.
 *   - Writes use function 0x10 (write multiple registers), matching the
 *     `nibe` library and NIBE's F-series preference for FC10.
 *   - NIBE Modbus values are plain `raw / 10`, without the 50-degree offset
 *     used by the Husdata H1/H66 register map.
 */
enum class NibeModbusSeries : uint8_t {
    S,
    F,
};

class NibeModbusProtocol : public ModbusProtocolBase {
public:
    NibeModbusProtocol(HeatPumpProtocol type, NibeModbusSeries series,
                       uint8_t slaveAddress = 0x01)
        : ModbusProtocolBase(type, buildSensorCatalog(series),
                             modbusParserRegistry(), slaveAddress,
                             Nibe::READ_TIMEOUT)
        , m_series(series) {}

    CommunicationError detectModel(HeatPumpModelInfo& modelInfo) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        auto profile = profileFor(m_series);

        uint16_t priority = 0;
        auto err = readRawRegister(profile.priorityCoil, priority);
        if (!err.isOk()) return err;
        if (!isPriorityValue(priority)) {
            return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                       "Not a NIBE Modbus device for the selected series", 0};
        }

        uint16_t temperature = 0;
        err = readRawRegister(profile.supplyCoil, temperature);
        if (!err.isOk()) return err;
        if (!isPlausibleTemperature(temperature)) {
            return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                       "NIBE supply temperature out of range", 0};
        }

        modelInfo.protocol = profile.protocol;
        modelInfo.modelName = profile.modelName;
        modelInfo.brand = HeatPumpBrand::NIBE;
        modelInfo.hpType = HeatPumpType::LW;
        modelInfo.version = "";
        modelInfo.writeEnabled = true;
        modelInfo.readOnly = false;
        m_modelInfo = modelInfo;
        return CommunicationError::ok();
    }

protected:
    CommunicationError doWriteValue(const SensorMapping& mapping,
                                     const SensorValue& value) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};
        if (mapping.readFunction != 0x03) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "Sensor is not a holding register", 0};
        }

        float scaled = (value.asFloat() - mapping.offset) / mapping.scale;
        int32_t rounded = scaled >= 0.0f
                              ? static_cast<int32_t>(scaled + 0.5f)
                              : static_cast<int32_t>(scaled - 0.5f);
        uint16_t rawValue = static_cast<uint16_t>(static_cast<int16_t>(rounded));

        const uint16_t values[] = {rawValue};
        auto frame = ModbusRtu::buildWriteMultipleRegistersFrame(
            slaveAddress(), mapping.registerAddress, values);
        auto err = m_bus->writeBytes(frame);
        if (!err.isOk()) return err;

        return ModbusRtu::readWriteMultipleRegistersEcho(
            m_bus, slaveAddress(), mapping.registerAddress, 1,
            Nibe::WRITE_TIMEOUT);
    }

private:
    struct SeriesProfile {
        HeatPumpProtocol protocol;
        uint16_t priorityCoil;
        uint16_t supplyCoil;
        const char* modelName;
    };

    static SeriesProfile profileFor(NibeModbusSeries series) {
        if (series == NibeModbusSeries::S) {
            return {HeatPumpProtocol::NIBE_S_MODBUS, 31029, 30006,
                    "NIBE S-series (Modbus)"};
        }
        return {HeatPumpProtocol::NIBE_F_MODBUS, 43086, 40008,
                "NIBE F-series (Modbus)"};
    }

    static constexpr uint16_t wireAddress(uint16_t coilAddress) {
        return static_cast<uint16_t>((coilAddress % 10000) - 1);
    }

    static constexpr uint8_t functionFor(uint16_t coilAddress) {
        return (coilAddress / 10000) == 3 ? 0x04 : 0x03;
    }

    static bool isPriorityValue(uint16_t value) {
        return value == 10 || value == 20 || value == 30 ||
               value == 40 || value == 60;
    }

    static bool isPlausibleTemperature(uint16_t raw) {
        float celsius = static_cast<float>(static_cast<int16_t>(raw)) * 0.1f;
        return celsius >= -50.0f && celsius <= 150.0f;
    }

    CommunicationError readRawRegister(uint16_t coilAddress, uint16_t& out) {
        SensorMapping probe{
            0xFFFF, "probe", "Probe", SensorType::INTEGER, SensorUnit::NONE,
            wireAddress(coilAddress), 2, 1.0f, 0.0f, false, functionFor(coilAddress)};

        auto err = sendQuery(probe);
        if (!err.isOk()) return err;

        CommunicationError readErr;
        auto frame = readResponse(readErr);
        if (!readErr.isOk()) return readErr;
        if (frame.data.size() < 2) {
            return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                       "NIBE Modbus probe response too short", 0};
        }

        out = static_cast<uint16_t>((frame.data[0] << 8) | frame.data[1]);
        return CommunicationError::ok();
    }

    static SensorCatalog buildSensorCatalog(NibeModbusSeries series) {
        SensorCatalog catalog;

        auto temp = [](uint16_t id, const char* name, const char* display,
                       uint16_t coilAddress, bool writable = false,
                       uint8_t function = 0xFF) {
            return SensorMapping{id, name, display, SensorType::TEMPERATURE,
                                 SensorUnit::CELSIUS, wireAddress(coilAddress),
                                 2, 0.1f, 0.0f, writable,
                                 function == 0xFF ? functionFor(coilAddress) : function};
        };
        auto integer = [](uint16_t id, const char* name, const char* display,
                          uint16_t coilAddress, bool writable = false,
                          uint8_t function = 0xFF) {
            return SensorMapping{id, name, display, SensorType::INTEGER,
                                 SensorUnit::NONE, wireAddress(coilAddress),
                                 2, 1.0f, 0.0f, writable,
                                 function == 0xFF ? functionFor(coilAddress) : function};
        };
        auto percent = [](uint16_t id, const char* name, const char* display,
                          uint16_t coilAddress, bool writable = false,
                          uint8_t function = 0xFF) {
            return SensorMapping{id, name, display, SensorType::PERCENT,
                                 SensorUnit::PERCENT, wireAddress(coilAddress),
                                 2, 0.1f, 0.0f, writable,
                                 function == 0xFF ? functionFor(coilAddress) : function};
        };
        auto scaledInteger = [](uint16_t id, const char* name, const char* display,
                                uint16_t coilAddress, bool writable = false,
                                uint8_t function = 0xFF) {
            return SensorMapping{id, name, display, SensorType::INTEGER,
                                 SensorUnit::NONE, wireAddress(coilAddress),
                                 2, 0.1f, 0.0f, writable,
                                 function == 0xFF ? functionFor(coilAddress) : function};
        };

        if (series == NibeModbusSeries::S) {
            catalog.addAll({
                temp(0, "outdoor_temp_avg", "Average Outdoor Temperature (BT1)", 30038),
                temp(1, "supply_line_temp", "Supply Line (BT2)", 30006),
                temp(2, "return_line_temp", "Return Line (BT3)", 30008),
                temp(3, "hot_water_charging", "Hot Water Charging (BT6)", 30010),
                temp(4, "room_temp_s1", "Room Temperature S1", 30027),
                temp(5, "calculated_supply_s1", "Calculated Supply S1", 31018),
                integer(6, "priority", "Priority", 31029),
                integer(7, "shunt_mode_s1", "Shunt Mode S1", 31034),
                integer(8, "hot_water_mode", "Hot Water Mode", 31039),
                integer(9, "alarm_number", "Alarm Number", 31976),
                temp(10, "room_setpoint_heat_s1", "Room Setpoint Heat S1", 40207, true),
                temp(11, "room_setpoint_cool_s1", "Room Setpoint Cool S1", 40989, true),
                integer(12, "use_room_sensor_s1", "Use Room Sensor S1", 40203, true),
                scaledInteger(13, "cooling_with_room_sensors", "Cooling With Room Sensors", 40171, true),
                integer(14, "heating_curve_s1", "Heating Curve S1", 40027, true),
                integer(15, "heating_offset_s1", "Heating Offset S1", 40031, true),
                temp(16, "min_supply_s1", "Min Supply S1", 40035, true),
                temp(17, "max_supply_s1", "Max Supply S1", 40039, true),
                percent(18, "room_sensor_factor_s1", "Room Sensor Factor S1", 40211, true),
                temp(19, "hot_water_start_low", "Hot Water Start Low", 40061, true),
                temp(20, "hot_water_start_normal", "Hot Water Start Normal", 40060, true),
                temp(21, "hot_water_start_high", "Hot Water Start High", 40059, true),
                temp(22, "hot_water_stop_low", "Hot Water Stop Low", 40065, true),
                temp(23, "hot_water_stop_normal", "Hot Water Stop Normal", 40064, true),
                temp(24, "hot_water_stop_high", "Hot Water Stop High", 40063, true),
                integer(25, "alarm_reset", "Alarm Reset", 40023, true),
            });
        } else {
            catalog.addAll({
                temp(0, "outdoor_temp", "Outdoor Temperature", 40067),
                temp(1, "supply_line_temp", "Supply Line", 40008),
                temp(2, "return_line_temp", "Return Line", 40012),
                temp(3, "room_temp_s1", "Room Temperature S1", 40033),
                temp(4, "calculated_supply_s1", "Calculated Supply S1", 43009),
                integer(5, "priority", "Priority", 43086),
                integer(6, "shunt_mode_s1", "Shunt Mode S1", 43096),
                integer(7, "hot_water_mode", "Hot Water Mode", 47041),
                integer(8, "alarm_number", "Alarm Number", 45001),
                temp(9, "room_setpoint_heat_s1", "Room Setpoint Heat S1", 47398, true),
                temp(10, "room_setpoint_cool_s1", "Room Setpoint Cool S1", 48785, true),
                integer(11, "use_room_sensor_s1", "Use Room Sensor S1", 47394, true),
                integer(12, "cooling_with_room_sensors", "Cooling With Room Sensors", 47340, true),
                integer(13, "heating_curve_s1", "Heating Curve S1", 47007, true),
                integer(14, "heating_offset_s1", "Heating Offset S1", 47011, true),
                temp(15, "min_supply_s1", "Min Supply S1", 47015, true),
                temp(16, "max_supply_s1", "Max Supply S1", 47019, true),
                percent(17, "room_sensor_factor_s1", "Room Sensor Factor S1", 47402, true),
                temp(18, "hot_water_start_eco", "Hot Water Start Economy", 47045, true),
                temp(19, "hot_water_start_normal", "Hot Water Start Normal", 47044, true),
                temp(20, "hot_water_start_lux", "Hot Water Start Luxury", 47043, true),
                temp(21, "hot_water_stop_eco", "Hot Water Stop Economy", 47049, true),
                temp(22, "hot_water_stop_normal", "Hot Water Stop Normal", 47048, true),
                temp(23, "hot_water_stop_lux", "Hot Water Stop Luxury", 47047, true),
                integer(24, "temporary_lux", "Temporary Lux", 48132, true),
                integer(25, "alarm_reset", "Alarm Reset", 45171, true),
            });
        }

        return catalog;
    }

    NibeModbusSeries m_series;
};

class NibeSModbusProtocol : public NibeModbusProtocol {
public:
    explicit NibeSModbusProtocol(uint8_t slaveAddress = 0x01)
        : NibeModbusProtocol(HeatPumpProtocol::NIBE_S_MODBUS,
                             NibeModbusSeries::S, slaveAddress) {}
};

class NibeFModbusProtocol : public NibeModbusProtocol {
public:
    explicit NibeFModbusProtocol(uint8_t slaveAddress = 0x01)
        : NibeModbusProtocol(HeatPumpProtocol::NIBE_F_MODBUS,
                             NibeModbusSeries::F, slaveAddress) {}
};
