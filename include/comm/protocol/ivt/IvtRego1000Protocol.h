#pragma once

#include "comm/protocol/ivt/IvtRegoProtocolBase.h"
#include "comm/sensor/SensorCatalog.h"
#include "comm/sensor/SensorParser.h"
#include "types/Constants.h"

using namespace Constants;

class IvtRego1000Protocol : public IvtRegoProtocolBase {
public:
    IvtRego1000Protocol()
        : IvtRegoProtocolBase(HeatPumpProtocol::IVT_REGO1000,
                               buildSensorCatalog(),
                               buildParserRegistry()) {}

    CommunicationError detectModel(HeatPumpModelInfo& modelInfo) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        std::vector<uint8_t> identCmd = {Rego800::MODBUS_ADDRESS, Rego800::MODBUS_FUNC_READ,
                                          0x00, 0x00, Rego800::IDENT_CMD_BYTE};
        auto err = m_bus->writeBytes(identCmd);
        if (!err.isOk()) return err;

        std::vector<uint8_t> response(Rego800::IDENT_RESPONSE_BUFFER);
        size_t bytesRead = 0;
        err = m_bus->readBytes(response, bytesRead, Rego800::READ_TIMEOUT);
        if (!err.isOk()) return err;

        std::string respStr(response.begin(), response.begin() + bytesRead);
        if (respStr.find("Rego 1000") != std::string::npos ||
            respStr.find("Pro Control 500") != std::string::npos) {
            modelInfo.protocol = HeatPumpProtocol::IVT_REGO1000;
            modelInfo.brand = HeatPumpBrand::IVT;
            modelInfo.hpType = HeatPumpType::LW;
            modelInfo.modelName = "Rego 1000";
            modelInfo.version = parseVersion(respStr);
            modelInfo.writeEnabled = true;
            modelInfo.readOnly = false;
            m_modelInfo = modelInfo;
            return CommunicationError::ok();
        }

        return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND, "Not an IVT Rego 1000", 0};
    }

    [[nodiscard]] ProtocolCapabilities getCapabilities() const override {
        return {true, true, true, true, m_sensorCatalog.count(), Frame::MAX_DATA_SIZE};
    }

protected:
    void buildReadCommand(uint16_t registerAddress, std::vector<uint8_t>& cmd) override {
        cmd.clear();
        cmd.push_back(Rego800::MODBUS_ADDRESS);
        cmd.push_back(Rego800::MODBUS_FUNC_READ);
        cmd.push_back(static_cast<uint8_t>(registerAddress >> 8));
        cmd.push_back(static_cast<uint8_t>(registerAddress & ByteMasks::LOW_BYTE));
        cmd.push_back(Rego800::REGISTER_COUNT);

        uint8_t checksum = Checksums::Xor8::INITIAL;
        for (auto b : cmd) checksum ^= b;
        cmd.push_back(checksum);
    }

    bool isResponseComplete(const std::vector<uint8_t>& response) override {
        if (response.size() < 4) return false;
        uint8_t len = response[FrameLayout::LENGTH_POS];
        return response.size() >= static_cast<size_t>(len) + FrameLayout::HEADER_SIZE;
    }

    Frame parseRegoResponse(const std::vector<uint8_t>& response,
                             CommunicationError& outErr) override {
        Frame frame;
        if (response.size() < 4) {
            outErr = CommunicationError::invalidFrame("Response too short");
            return frame;
        }

        frame.address = response[FrameLayout::ADDRESS_POS];
        frame.command = response[FrameLayout::COMMAND_POS];
        uint8_t len = response[FrameLayout::LENGTH_POS];
        frame.length = len;

        size_t dataEnd = FrameLayout::HEADER_SIZE + len;
        if (response.size() >= dataEnd) {
            frame.data.assign(response.begin() + FrameLayout::HEADER_SIZE,
                              response.begin() + dataEnd);
        }

        Xor8Validator validator;
        frame.checksum = response.size() > dataEnd ? response[dataEnd] : 0;
        outErr = validator.validate(frame);
        frame.valid = outErr.isOk();
        return frame;
    }

private:
    static std::string parseVersion(const std::string& response) {
        auto pos = response.find("ver:");
        if (pos == std::string::npos) return "";
        return response.substr(pos);
    }

    static SensorCatalog buildSensorCatalog() {
        SensorCatalog catalog;
        catalog.addAll({
            SensorMapping{0, "outdoor_temp", "Outdoor Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x007, 2, Rego1000::TEMP_SCALE, Rego1000::TEMP_OFFSET, false},
            SensorMapping{1, "radiator_forward", "Radiator Forward", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x002, 2, Rego1000::TEMP_SCALE, Rego1000::TEMP_OFFSET, false},
            SensorMapping{2, "heat_carrier_return", "Heat Carrier Return", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x003, 2, Rego1000::TEMP_SCALE, Rego1000::TEMP_OFFSET, false},
            SensorMapping{3, "heat_carrier_forward", "Heat Carrier Forward", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x004, 2, Rego1000::TEMP_SCALE, Rego1000::TEMP_OFFSET, false},
            SensorMapping{4, "brine_in", "Brine In / Evaporator", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x005, 2, Rego1000::TEMP_SCALE, Rego1000::TEMP_OFFSET, false},
            SensorMapping{5, "brine_out", "Brine Out / Condenser", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x006, 2, Rego1000::TEMP_SCALE, Rego1000::TEMP_OFFSET, false},
            SensorMapping{6, "hot_water_top", "Hot Water Top", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x009, 2, Rego1000::TEMP_SCALE, 0.0f, false},
            SensorMapping{7, "indoor_temp", "Indoor Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x008, 2, Rego1000::TEMP_SCALE, Rego1000::TEMP_OFFSET, false},
            SensorMapping{8, "hot_gas", "Hot Gas", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x00B, 2, Rego1000::TEMP_SCALE, Rego1000::TEMP_OFFSET, false},
            SensorMapping{9, "compressor_speed", "Compressor Speed", SensorType::SPEED,
             SensorUnit::HERTZ, 0x108, 2, 1.0f, 0.0f, false},
            SensorMapping{10, "add_heat_status", "Add Heat Status", SensorType::INTEGER,
             SensorUnit::NONE, 0x104, 2, 1.0f, 0.0f, false},
            SensorMapping{11, "operating_mode", "Operating Mode", SensorType::INTEGER,
             SensorUnit::NONE, 0x201, 2, 1.0f, 0.0f, false},
            SensorMapping{12, "room_setpoint", "Room Setpoint", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x203, 2, Rego1000::TEMP_SCALE, Rego1000::TEMP_OFFSET, true},
            SensorMapping{13, "heating_setpoint", "Heating Setpoint", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x107, 2, Rego1000::TEMP_SCALE, Rego1000::TEMP_OFFSET, true},
            SensorMapping{14, "warm_water_setpoint", "Warm Water Setpoint", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x10B, 2, Rego1000::TEMP_SCALE, Rego1000::TEMP_OFFSET, true},
            SensorMapping{15, "compressor_power", "Compressor Power", SensorType::POWER,
             SensorUnit::KILOWATT, 0x10C, 2, 0.1f, 0.0f, false},
            SensorMapping{16, "total_op_time", "Total Operating Time", SensorType::RUNTIME,
             SensorUnit::HOURS, 0xC50, 4, 1.0f, 0.0f, false},
            SensorMapping{17, "total_energy", "Total Energy", SensorType::ENERGY,
             SensorUnit::KILOWATT_HOUR, 0xC51, 4, 0.001f, 0.0f, false},
            SensorMapping{18, "compressor_energy", "Compressor Energy", SensorType::ENERGY,
             SensorUnit::KILOWATT_HOUR, 0xC54, 4, 0.001f, 0.0f, false},
            SensorMapping{19, "aux_energy", "Auxiliary Energy", SensorType::ENERGY,
             SensorUnit::KILOWATT_HOUR, 0xC57, 4, 0.001f, 0.0f, false},
        });
        return catalog;
    }

    static ParserRegistry buildParserRegistry() {
        ParserRegistry registry;
        registry.registerParser(SensorType::TEMPERATURE, SensorParser::parseTemperature);
        registry.registerParser(SensorType::POWER,       SensorParser::parsePower);
        registry.registerParser(SensorType::ENERGY,      SensorParser::parseEnergy);
        registry.registerParser(SensorType::SPEED,       SensorParser::parseSpeed);
        registry.registerParser(SensorType::PERCENT,     SensorParser::parsePercent);
        registry.registerParser(SensorType::FLOW,        SensorParser::parseFlow);
        registry.registerParser(SensorType::RUNTIME,     SensorParser::parseRuntime);
        registry.registerParser(SensorType::INTEGER,     SensorParser::parseInteger);
        return registry;
    }
};
