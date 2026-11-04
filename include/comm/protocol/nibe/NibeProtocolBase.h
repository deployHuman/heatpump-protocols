#pragma once

#include "comm/protocol/HeatPumpProtocolBase.h"
#include "comm/sensor/SensorCatalog.h"
#include "comm/sensor/SensorParser.h"
#include "types/Constants.h"
#include <algorithm>
#include <sstream>

using namespace Constants;

static SensorCatalog buildNibeSensorCatalog() {
    SensorCatalog catalog;
    catalog.addAll({
        SensorMapping{0x001, "radiator_return",      "Radiator Return",       SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x001, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x002, "radiator_forward",     "Radiator Forward",      SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x002, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x003, "heat_carrier_return",   "Heat Carrier Return",   SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x003, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x004, "heat_carrier_forward",  "Heat Carrier Forward",  SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x004, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x005, "brine_in",             "Brine In / Evaporator", SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x005, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x006, "brine_out",            "Brine Out / Condenser", SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x006, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x007, "outdoor_temp",         "Outdoor Temperature",   SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x007, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x008, "indoor_temp",          "Indoor Temperature",    SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x008, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x009, "warm_water_top",       "Warm Water Top",        SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x009, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x00A, "warm_water_mid",       "Warm Water Mid",        SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x00A, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x00B, "hot_gas",              "Hot Gas",               SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x00B, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x00C, "suction_gas",          "Suction Gas",           SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x00C, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x00D, "liquid_flow_temp",     "Liquid Flow Temp",      SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x00D, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x00E, "air_intake",           "Air Intake",            SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x00E, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x00F, "exhaust_air",          "Exhaust Air",           SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x00F, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x010, "air_outlet",           "Air Outlet",            SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x010, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x011, "pool_temp",            "Pool Temperature",      SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x011, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x012, "pressure_tube",        "Pressure Tube",         SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x012, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x013, "cooling_temp",         "Cooling Temperature",   SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x013, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x014, "compressor_state",     "Compressor State",      SensorType::BOOLEAN,     SensorUnit::NONE,             0x014, 1, 1.0f, 0.0f,   false},
        SensorMapping{0x020, "radiator_forward_2",   "Radiator Forward 2",    SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x020, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x021, "indoor_temp_2",        "Indoor Temperature 2",  SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x021, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x022, "radiator_return_2",    "Radiator Return 2",     SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x022, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x024, "humidity",             "Humidity",              SensorType::PERCENT,     SensorUnit::PERCENT,          0x024, 2, 1.0f, 0.0f,   false},
        SensorMapping{0x101, "load_l1",              "Load L1",               SensorType::POWER,       SensorUnit::WATT,             0x101, 2, 1.0f, 0.0f,   false},
        SensorMapping{0x102, "load_l2",              "Load L2",               SensorType::POWER,       SensorUnit::WATT,             0x102, 2, 1.0f, 0.0f,   false},
        SensorMapping{0x103, "load_l3",              "Load L3",               SensorType::POWER,       SensorUnit::WATT,             0x103, 2, 1.0f, 0.0f,   false},
        SensorMapping{0x104, "add_heat_status",      "Add Heat Status",       SensorType::INTEGER,     SensorUnit::NONE,             0x104, 2, 1.0f, 0.0f,   false},
        SensorMapping{0x105, "degree_min_integral",  "Degree Min Integral",   SensorType::INTEGER,     SensorUnit::NONE,             0x105, 2, 1.0f, 0.0f,   false},
        SensorMapping{0x106, "room_temp_setpoint",   "Room Temp Setpoint",    SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x106, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, true},
        SensorMapping{0x107, "heating_setpoint",     "Heating Setpoint",      SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x107, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x108, "compressor_speed",     "Compressor Speed",      SensorType::SPEED,       SensorUnit::HERTZ,            0x108, 2, 1.0f, 0.0f,   false},
        SensorMapping{0x109, "circ_pump_speed",      "Circ Pump Speed",       SensorType::PERCENT,     SensorUnit::PERCENT,          0x109, 2, 1.0f, 0.0f,   false},
        SensorMapping{0x10A, "brine_pump_speed",     "Brine Pump Speed",      SensorType::PERCENT,     SensorUnit::PERCENT,          0x10A, 2, 1.0f, 0.0f,   false},
        SensorMapping{0x10B, "warm_water_setpoint",  "Warm Water Setpoint",   SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x10B, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x10C, "compressor_power",     "Compressor Power",      SensorType::POWER,       SensorUnit::KILOWATT,         0x10C, 2, 0.1f, 0.0f,   false},
        SensorMapping{0x201, "operating_mode",       "Operating Mode",         SensorType::INTEGER,     SensorUnit::NONE,             0x201, 2, 1.0f, 0.0f,   false},
        SensorMapping{0x203, "room_setpoint",        "Room Setpoint",          SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x203, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, true},
        SensorMapping{0x204, "room_sensor_influence", "Room Sensor Influence",  SensorType::INTEGER,     SensorUnit::NONE,             0x204, 2, 1.0f, 0.0f,   false},
        SensorMapping{0x205, "heat_curve_l1",        "Heat Curve L1",          SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x205, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x206, "heat_curve_r1",        "Heat Curve R1",          SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x206, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, false},
        SensorMapping{0x208, "warm_water_stop_temp",  "Warm Water Stop Temp",   SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x208, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, true},
        SensorMapping{0x209, "extra_warm_water",     "Extra Warm Water",       SensorType::BOOLEAN,     SensorUnit::NONE,             0x209, 1, 1.0f, 0.0f,   false},
        SensorMapping{0x20A, "summer_mode",          "Summer Mode",            SensorType::BOOLEAN,     SensorUnit::NONE,             0x20A, 1, 1.0f, 0.0f,   false},
        SensorMapping{0x210, "holiday_mode",         "Holiday Mode",           SensorType::BOOLEAN,     SensorUnit::NONE,             0x210, 1, 1.0f, 0.0f,   false},
        SensorMapping{0x217, "outdoor_temp_offset",  "Outdoor Temp Offset",    SensorType::TEMPERATURE, SensorUnit::CELSIUS,         0x217, 2, Nibe::TEMP_SCALE, 0.0f, true},
        SensorMapping{0x2F1, "room_controller",      "Room Controller",        SensorType::FLOAT,       SensorUnit::CELSIUS,          0x2F1, 2, Nibe::TEMP_SCALE, Nibe::TEMP_OFFSET, true},
        SensorMapping{0x2F2, "alarm_reset",          "Alarm Reset",            SensorType::BOOLEAN,     SensorUnit::NONE,             0x2F2, 1, 1.0f, 0.0f,   true},
        SensorMapping{0xA01, "compressor_output",    "Compressor Output",      SensorType::BOOLEAN,     SensorUnit::NONE,             0xA01, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA02, "add_heat_step_1",      "Add Heat Step 1",        SensorType::BOOLEAN,     SensorUnit::NONE,             0xA02, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA03, "add_heat_step_2",      "Add Heat Step 2",        SensorType::BOOLEAN,     SensorUnit::NONE,             0xA03, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA04, "pump_cold_circuit",    "Pump Cold Circuit",      SensorType::BOOLEAN,     SensorUnit::NONE,             0xA04, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA05, "pump_heat_circuit",    "Pump Heat Circuit",      SensorType::BOOLEAN,     SensorUnit::NONE,             0xA05, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA06, "pump_radiator",        "Pump Radiator",          SensorType::BOOLEAN,     SensorUnit::NONE,             0xA06, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA07, "switch_valve_1",       "Switch Valve 1",         SensorType::BOOLEAN,     SensorUnit::NONE,             0xA07, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA08, "switch_valve_2",       "Switch Valve 2",         SensorType::BOOLEAN,     SensorUnit::NONE,             0xA08, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA0A, "high_pressostat",      "High Pressostat",        SensorType::BOOLEAN,     SensorUnit::NONE,             0xA0A, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA0B, "low_pressostat",       "Low Pressostat",         SensorType::BOOLEAN,     SensorUnit::NONE,             0xA0B, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA0C, "heating_cable",        "Heating Cable",          SensorType::BOOLEAN,     SensorUnit::NONE,             0xA0C, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA0D, "crank_case_heater",    "Crank Case Heater",      SensorType::BOOLEAN,     SensorUnit::NONE,             0xA0D, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA0E, "shunt_open",           "Shunt Open",             SensorType::BOOLEAN,     SensorUnit::NONE,             0xA0E, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA0F, "shunt_close",          "Shunt Close",            SensorType::BOOLEAN,     SensorUnit::NONE,             0xA0F, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA20, "alarm",                "Alarm",                  SensorType::BOOLEAN,     SensorUnit::NONE,             0xA20, 1, 1.0f, 0.0f,   false},
        SensorMapping{0xA91, "alarm_code",           "Alarm Code",             SensorType::INTEGER,     SensorUnit::NONE,             0xA91, 2, 1.0f, 0.0f,   false},
        SensorMapping{0xC50, "total_op_time",        "Total Op Time",          SensorType::RUNTIME,     SensorUnit::HOURS,            0xC50, 4, 1.0f, 0.0f,   false},
        SensorMapping{0xC51, "supply_energy_total",  "Supply Energy Total",    SensorType::ENERGY,      SensorUnit::KILOWATT_HOUR,    0xC51, 4, 0.001f, 0.0f, false},
        SensorMapping{0xC52, "supply_energy_heat",   "Supply Energy Heating",  SensorType::ENERGY,      SensorUnit::KILOWATT_HOUR,    0xC52, 4, 0.001f, 0.0f, false},
        SensorMapping{0xC53, "supply_energy_hotwat", "Supply Energy Hot Water", SensorType::ENERGY,      SensorUnit::KILOWATT_HOUR,    0xC53, 4, 0.001f, 0.0f, false},
        SensorMapping{0xC54, "compr_energy_total",   "Compr Energy Total",     SensorType::ENERGY,      SensorUnit::KILOWATT_HOUR,    0xC54, 4, 0.001f, 0.0f, false},
        SensorMapping{0xC55, "compr_energy_heat",    "Compr Energy Heating",   SensorType::ENERGY,      SensorUnit::KILOWATT_HOUR,    0xC55, 4, 0.001f, 0.0f, false},
        SensorMapping{0xC56, "compr_energy_hotwat",  "Compr Energy Hot Water", SensorType::ENERGY,      SensorUnit::KILOWATT_HOUR,    0xC56, 4, 0.001f, 0.0f, false},
        SensorMapping{0xC57, "aux_energy_total",     "Aux Energy Total",       SensorType::ENERGY,      SensorUnit::KILOWATT_HOUR,    0xC57, 4, 0.001f, 0.0f, false},
        SensorMapping{0xC58, "aux_energy_heat",      "Aux Energy Heating",     SensorType::ENERGY,      SensorUnit::KILOWATT_HOUR,    0xC58, 4, 0.001f, 0.0f, false},
        SensorMapping{0xC59, "aux_energy_hotwat",    "Aux Energy Hot Water",   SensorType::ENERGY,      SensorUnit::KILOWATT_HOUR,    0xC59, 4, 0.001f, 0.0f, false},
    });
    return catalog;
}

static ParserRegistry buildNibeParserRegistry() {
    ParserRegistry registry;
    registry.registerParser(SensorType::TEMPERATURE, SensorParser::parseTemperature);
    registry.registerParser(SensorType::POWER,       SensorParser::parsePower);
    registry.registerParser(SensorType::ENERGY,      SensorParser::parseEnergy);
    registry.registerParser(SensorType::SPEED,       SensorParser::parseSpeed);
    registry.registerParser(SensorType::PERCENT,     SensorParser::parsePercent);
    registry.registerParser(SensorType::FLOW,        SensorParser::parseFlow);
    registry.registerParser(SensorType::RUNTIME,     SensorParser::parseRuntime);
    registry.registerParser(SensorType::INTEGER,     SensorParser::parseInteger);
    registry.registerParser(SensorType::BOOLEAN,     SensorParser::parseInteger);
    registry.registerParser(SensorType::FLOAT,       SensorParser::parseInteger);
    return registry;
}
