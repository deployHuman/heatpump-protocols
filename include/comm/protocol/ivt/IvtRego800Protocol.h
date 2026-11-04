#pragma once

#include "IvtRegoProtocolBase.h"
#include "comm/sensor/SensorCatalog.h"
#include "comm/sensor/SensorParser.h"
#include "types/Constants.h"

using namespace Constants;

class IvtRego800Protocol : public IvtRegoProtocolBase {
public:
    IvtRego800Protocol()
        : IvtRegoProtocolBase(HeatPumpProtocol::IVT_REGO800,
                               buildSensorCatalog(),
                               buildParserRegistry()) {}

    CommunicationError detectModel(HeatPumpModelInfo& modelInfo) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        std::vector<uint8_t> identCmd = {
            Rego800::MODBUS_ADDRESS,
            Rego800::MODBUS_FUNC_READ,
            0x00, 0x00,
            Rego800::IDENT_CMD_BYTE
        };
        auto err = m_bus->writeBytes(identCmd);
        if (!err.isOk()) return err;

        std::vector<uint8_t> response(Rego800::IDENT_RESPONSE_BUFFER);
        size_t bytesRead = 0;
        err = m_bus->readBytes(response, bytesRead, Rego800::READ_TIMEOUT);
        if (!err.isOk()) return err;

        std::string respStr(response.begin(), response.begin() + bytesRead);
        if (respStr.find("REGO") != std::string::npos || respStr.find("800") != std::string::npos) {
            modelInfo.protocol = HeatPumpProtocol::IVT_REGO800;
            modelInfo.brand = HeatPumpBrand::IVT;
            modelInfo.hpType = HeatPumpType::LW;
            modelInfo.modelName = "Rego 800";
            modelInfo.writeEnabled = false;
            modelInfo.readOnly = true;
            m_modelInfo = modelInfo;
            return CommunicationError::ok();
        }

        return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND, "Not an IVT Rego 800", 0};
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
        if (response.size() < Rego800::MIN_RESPONSE_SIZE) return false;
        uint8_t len = response[FrameLayout::LENGTH_POS];
        return response.size() >= static_cast<size_t>(len) + FrameLayout::HEADER_SIZE;
    }

    Frame parseRegoResponse(const std::vector<uint8_t>& response,
                             CommunicationError& outErr) override {
        Frame frame;

        if (response.size() < Rego800::MIN_RESPONSE_SIZE) {
            outErr = CommunicationError::invalidFrame("Response too short");
            return frame;
        }

        uint8_t len = response[FrameLayout::LENGTH_POS];
        frame.address = response[FrameLayout::ADDRESS_POS];
        frame.command = response[FrameLayout::COMMAND_POS];

        size_t dataEnd = FrameLayout::HEADER_SIZE + len;
        if (response.size() >= dataEnd) {
            frame.data.assign(response.begin() + FrameLayout::HEADER_SIZE,
                              response.begin() + dataEnd);
        }

        Xor8Validator validator;
        frame.length = len;
        frame.checksum = response.size() > dataEnd ? response[dataEnd] : 0;
        outErr = validator.validate(frame);
        frame.valid = outErr.isOk();
        return frame;
    }

private:
    static SensorCatalog buildSensorCatalog() {
        SensorCatalog catalog;
        catalog.addAll({
            SensorMapping{0, "outdoor_temp", "Outdoor Temperature", SensorType::TEMPERATURE,
                          SensorUnit::CELSIUS, 0x0000, 2, Rego800::TEMP_SCALE, Rego800::TEMP_OFFSET, false},
            SensorMapping{1, "supply_line_temp", "Supply Line Temperature", SensorType::TEMPERATURE,
                          SensorUnit::CELSIUS, 0x0002, 2, Rego800::TEMP_SCALE, Rego800::TEMP_OFFSET, false},
            SensorMapping{2, "return_line_temp", "Return Line Temperature", SensorType::TEMPERATURE,
                          SensorUnit::CELSIUS, 0x0004, 2, Rego800::TEMP_SCALE, Rego800::TEMP_OFFSET, false},
            SensorMapping{3, "hot_water_temp", "Hot Water Temperature", SensorType::TEMPERATURE,
                          SensorUnit::CELSIUS, 0x0006, 2, Rego800::TEMP_SCALE, Rego800::TEMP_OFFSET_ZERO, false},
            SensorMapping{4, "brine_in_temp", "Brine In Temperature", SensorType::TEMPERATURE,
                          SensorUnit::CELSIUS, 0x0008, 2, Rego800::TEMP_SCALE, Rego800::TEMP_OFFSET, false},
            SensorMapping{5, "brine_out_temp", "Brine Out Temperature", SensorType::TEMPERATURE,
                          SensorUnit::CELSIUS, 0x000A, 2, Rego800::TEMP_SCALE, Rego800::TEMP_OFFSET, false},
            SensorMapping{6, "compressor_speed", "Compressor Speed", SensorType::SPEED,
                          SensorUnit::HERTZ, 0x0010, 2, Rego800::SPEED_SCALE, Rego800::NO_OFFSET, false},
            SensorMapping{7, "heat_power", "Heat Power", SensorType::POWER,
                          SensorUnit::KILOWATT, 0x0012, 2, Rego800::POWER_SCALE, Rego800::NO_OFFSET, false},
        });
        return catalog;
    }

    static ParserRegistry buildParserRegistry() {
        ParserRegistry registry;
        registry.registerParser(SensorType::TEMPERATURE, SensorParser::parseTemperature);
        registry.registerParser(SensorType::POWER, SensorParser::parsePower);
        registry.registerParser(SensorType::SPEED, SensorParser::parseSpeed);
        registry.registerParser(SensorType::PERCENT, SensorParser::parsePercent);
        registry.registerParser(SensorType::ENERGY, SensorParser::parseEnergy);
        registry.registerParser(SensorType::FLOW, SensorParser::parseFlow);
        registry.registerParser(SensorType::RUNTIME, SensorParser::parseRuntime);
        registry.registerParser(SensorType::INTEGER, SensorParser::parseInteger);
        return registry;
    }
};
