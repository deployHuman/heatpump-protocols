#pragma once

#include "comm/protocol/ivt/IvtRegoProtocolBase.h"
#include "comm/sensor/SensorCatalog.h"
#include "comm/sensor/SensorParser.h"
#include "types/Constants.h"

using namespace Constants;

class IvtRego600Protocol : public IvtRegoProtocolBase {
public:
    IvtRego600Protocol()
        : IvtRegoProtocolBase(HeatPumpProtocol::IVT_REGO600,
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
        if (respStr.find("Rego 600") != std::string::npos ||
            respStr.find("Pro Ctrl 600") != std::string::npos ||
            respStr.find("Rego 2-3000") != std::string::npos) {
            modelInfo.protocol = HeatPumpProtocol::IVT_REGO600;
            modelInfo.brand = HeatPumpBrand::IVT;
            modelInfo.hpType = HeatPumpType::LW;
            modelInfo.modelName = "Rego 600";
            modelInfo.writeEnabled = false;
            modelInfo.readOnly = true;
            m_modelInfo = modelInfo;
            return CommunicationError::ok();
        }

        return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND, "Not an IVT Rego 600", 0};
    }

    [[nodiscard]] ProtocolCapabilities getCapabilities() const override {
        return {true, false, true, true, m_sensorCatalog.count(), Frame::MAX_DATA_SIZE};
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

        std::string respStr(response.begin(), response.end());
        auto eqPos = respStr.find('=');
        if (eqPos != std::string::npos) {
            auto commaPos = respStr.find(',');
            std::string dataStr = (commaPos != std::string::npos)
                ? respStr.substr(eqPos + 1, commaPos - eqPos - 1)
                : respStr.substr(eqPos + 1);

            frame.data.assign(dataStr.begin(), dataStr.end());
            frame.length = static_cast<uint8_t>(frame.data.size());

            Xor8Validator validator;
            uint8_t cs = Checksums::Xor8::INITIAL;
            for (auto b : response) cs ^= b;
            frame.checksum = cs;
            outErr = validator.validate(frame);
        } else {
            frame.data = response;
            frame.length = static_cast<uint8_t>(response.size());
            frame.checksum = 0;
            outErr = CommunicationError::ok();
        }
        frame.valid = outErr.isOk();
        return frame;
    }

private:
    static SensorCatalog buildSensorCatalog() {
        SensorCatalog catalog;
        catalog.addAll({
            SensorMapping{0, "outdoor_temp", "Outdoor Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0000, 2, Rego600::TEMP_SCALE, Rego600::TEMP_OFFSET, false},
            SensorMapping{1, "supply_line_temp", "Supply Line Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0002, 2, Rego600::TEMP_SCALE, Rego600::TEMP_OFFSET, false},
            SensorMapping{2, "return_line_temp", "Return Line Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0004, 2, Rego600::TEMP_SCALE, Rego600::TEMP_OFFSET, false},
            SensorMapping{3, "hot_water_temp", "Hot Water Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0006, 2, Rego600::TEMP_SCALE, 0.0f, false},
            SensorMapping{4, "brine_in_temp", "Brine In Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0008, 2, Rego600::TEMP_SCALE, Rego600::TEMP_OFFSET, false},
            SensorMapping{5, "brine_out_temp", "Brine Out Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x000A, 2, Rego600::TEMP_SCALE, Rego600::TEMP_OFFSET, false},
            SensorMapping{6, "compressor_speed", "Compressor Speed", SensorType::SPEED,
             SensorUnit::HERTZ, 0x0010, 2, 1.0f, 0.0f, false},
            SensorMapping{7, "heat_power", "Heat Power", SensorType::POWER,
             SensorUnit::KILOWATT, 0x0012, 2, 0.1f, 0.0f, false},
            SensorMapping{8, "heat_carrier_forward", "Heat Carrier Forward", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x004, 2, Rego600::TEMP_SCALE, Rego600::TEMP_OFFSET, false},
            SensorMapping{9, "heat_carrier_return", "Heat Carrier Return", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x003, 2, Rego600::TEMP_SCALE, Rego600::TEMP_OFFSET, false},
            SensorMapping{10, "room_temp", "Room Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x008, 2, Rego600::TEMP_SCALE, Rego600::TEMP_OFFSET, false},
            SensorMapping{11, "hot_gas", "Hot Gas", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x00B, 2, Rego600::TEMP_SCALE, Rego600::TEMP_OFFSET, false},
            SensorMapping{12, "total_op_hours", "Total Operating Hours", SensorType::RUNTIME,
             SensorUnit::HOURS, 0xC50, 4, 1.0f, 0.0f, false},
            SensorMapping{13, "total_energy", "Total Energy", SensorType::ENERGY,
             SensorUnit::KILOWATT_HOUR, 0xC51, 4, 0.001f, 0.0f, false},
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
