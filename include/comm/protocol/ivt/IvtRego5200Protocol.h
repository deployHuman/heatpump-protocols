#pragma once

#include "comm/protocol/ivt/IvtRegoProtocolBase.h"
#include "comm/sensor/SensorCatalog.h"
#include "comm/sensor/SensorParser.h"
#include "types/Constants.h"

using namespace Constants;

class IvtRego5200Protocol : public IvtRegoProtocolBase {
public:
    IvtRego5200Protocol()
        : IvtRegoProtocolBase(HeatPumpProtocol::IVT_REGO5200,
                               buildSensorCatalog(),
                               buildParserRegistry())
        , m_errorCount(0)
        , m_errorLimit(5) {}

    CommunicationError detectModel(HeatPumpModelInfo& modelInfo) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        std::vector<uint8_t> identCmd = {0x02, 0x06, 0x00, 0x00, Rego800::IDENT_CMD_BYTE};
        auto err = m_bus->writeBytes(identCmd);
        if (!err.isOk()) return err;

        std::vector<uint8_t> response(Rego800::IDENT_RESPONSE_BUFFER);
        size_t bytesRead = 0;
        err = m_bus->readBytes(response, bytesRead, Rego800::READ_TIMEOUT);
        if (!err.isOk()) return err;

        std::string respStr(response.begin(), response.begin() + bytesRead);
        if (respStr.find("Rego 5200") != std::string::npos ||
            respStr.find("Regin") != std::string::npos) {
            modelInfo.protocol = HeatPumpProtocol::IVT_REGO5200;
            modelInfo.brand = respStr.find("Regin") != std::string::npos
                ? HeatPumpBrand::REGIN : HeatPumpBrand::IVT;
            modelInfo.hpType = HeatPumpType::LW;
            modelInfo.modelName = "Rego 5200 / Regin";
            modelInfo.version = parseVersion(respStr);
            modelInfo.writeEnabled = true;
            modelInfo.readOnly = false;
            m_modelInfo = modelInfo;
            return CommunicationError::ok();
        }

        return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                   "Not an IVT Rego 5200 / Regin", 0};
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
        if (response.size() < Rego800::MIN_RESPONSE_SIZE) return false;
        uint8_t len = response[FrameLayout::LENGTH_POS];
        return response.size() >= static_cast<size_t>(len) + FrameLayout::HEADER_SIZE;
    }

    Frame parseRegoResponse(const std::vector<uint8_t>& response,
                             CommunicationError& outErr) override {
        Frame frame;
        if (response.size() < Rego800::MIN_RESPONSE_SIZE) {
            outErr = CommunicationError::invalidFrame("Rego 5200 response too short");
            m_errorCount++;
            checkErrorLimit();
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

        if (!outErr.isOk()) {
            m_errorCount++;
            checkErrorLimit();
        } else {
            m_errorCount = 0;
        }

        return frame;
    }

private:
    void checkErrorLimit() {
        if (m_errorCount >= m_errorLimit) {
            m_errorCount = 0;
        }
    }

    static std::string parseVersion(const std::string& response) {
        auto pos = response.find("ver:");
        if (pos == std::string::npos) return "";
        return response.substr(pos);
    }

    static SensorCatalog buildSensorCatalog() {
        SensorCatalog catalog;
        catalog.addAll({
            SensorMapping{0, "outdoor_temp", "Outdoor Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0007, 2, Rego5200::TEMP_SCALE, Rego5200::TEMP_OFFSET, false},
            SensorMapping{1, "supply_temp", "Supply Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0002, 2, Rego5200::TEMP_SCALE, Rego5200::TEMP_OFFSET, false},
            SensorMapping{2, "return_temp", "Return Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0003, 2, Rego5200::TEMP_SCALE, Rego5200::TEMP_OFFSET, false},
            SensorMapping{3, "hot_water_temp", "Hot Water Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0009, 2, Rego5200::TEMP_SCALE, 0.0f, false},
            SensorMapping{4, "brine_in_temp", "Brine In Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0005, 2, Rego5200::TEMP_SCALE, Rego5200::TEMP_OFFSET, false},
            SensorMapping{5, "brine_out_temp", "Brine Out Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0006, 2, Rego5200::TEMP_SCALE, Rego5200::TEMP_OFFSET, false},
            SensorMapping{6, "compressor_speed", "Compressor Speed", SensorType::SPEED,
             SensorUnit::HERTZ, 0x3071, 2, 1.0f, 0.0f, false},
            SensorMapping{7, "heat_power", "Heat Power", SensorType::POWER,
             SensorUnit::KILOWATT, 0x3091, 2, 0.1f, 0.0f, false},
            SensorMapping{8, "room_temp_set", "Room Temp Setpoint", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x203, 2, Rego5200::TEMP_SCALE, Rego5200::TEMP_OFFSET, true},
            SensorMapping{9, "heating_setpoint", "Heating Setpoint", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x107, 2, Rego5200::TEMP_SCALE, Rego5200::TEMP_OFFSET, true},
            SensorMapping{10, "ww_setpoint", "Warm Water Setpoint", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x10B, 2, Rego5200::TEMP_SCALE, Rego5200::TEMP_OFFSET, true},
            SensorMapping{11, "humidity", "Humidity", SensorType::PERCENT,
             SensorUnit::PERCENT, 0x32F5, 2, 1.0f, 0.0f, false},
            SensorMapping{12, "total_op_time", "Total Operating Time", SensorType::RUNTIME,
             SensorUnit::HOURS, 0xC50, 4, 1.0f, 0.0f, false},
            SensorMapping{13, "total_energy", "Total Energy", SensorType::ENERGY,
             SensorUnit::KILOWATT_HOUR, 0xC51, 4, 0.001f, 0.0f, false},
            SensorMapping{14, "compressor_energy", "Compressor Energy", SensorType::ENERGY,
             SensorUnit::KILOWATT_HOUR, 0xC54, 4, 0.001f, 0.0f, false},
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
        registry.registerParser(SensorType::RUNTIME,     SensorParser::parseRuntime);
        registry.registerParser(SensorType::INTEGER,     SensorParser::parseInteger);
        return registry;
    }

    int m_errorCount;
    int m_errorLimit;
};
