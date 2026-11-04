#pragma once

#include "comm/protocol/HeatPumpProtocolBase.h"
#include "comm/sensor/SensorCatalog.h"
#include "comm/sensor/SensorParser.h"
#include "comm/frame/FrameValidator.h"
#include "utils/CrcCalculator.h"
#include "types/Constants.h"

using namespace Constants;

class EmsBoschProtocol : public HeatPumpProtocolBase {
public:
    EmsBoschProtocol()
        : HeatPumpProtocolBase(HeatPumpProtocol::BOSCH_EMS_PROCONTROL,
                                buildSensorCatalog(),
                                buildParserRegistry()) {}

    CommunicationError detectModel(HeatPumpModelInfo& modelInfo) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        std::vector<uint8_t> identCmd = {0xDC, 0x01, 0x00, 0x00, 0x00};
        auto err = m_bus->writeBytes(identCmd);
        if (!err.isOk()) return err;

        std::vector<uint8_t> response(EmsBosch::RESPONSE_BUFFER_RESERVE);
        size_t bytesRead = 0;
        err = m_bus->readBytes(response, bytesRead, EmsBosch::READ_TIMEOUT);
        if (!err.isOk()) return err;

        std::string respStr(response.begin(), response.begin() + bytesRead);
        if (respStr.find("Bosch") != std::string::npos ||
            respStr.find("ProControl") != std::string::npos ||
            respStr.find("BOSCH") != std::string::npos) {
            modelInfo.protocol = HeatPumpProtocol::BOSCH_EMS_PROCONTROL;
            modelInfo.brand = HeatPumpBrand::BOSCH;
            modelInfo.hpType = HeatPumpType::LW;
            modelInfo.modelName = "Bosch ProControl";
            modelInfo.writeEnabled = true;
            modelInfo.readOnly = false;
            m_modelInfo = modelInfo;
            return CommunicationError::ok();
        }

        return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                   "Not a Bosch EMS heat pump", 0};
    }

    [[nodiscard]] ProtocolCapabilities getCapabilities() const override {
        return {true, true, true, true, m_sensorCatalog.count(), Frame::MAX_DATA_SIZE};
    }

protected:
    CommunicationError sendQuery(const SensorMapping& mapping) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        std::vector<uint8_t> cmd = {
            0xDC, 0x01,
            static_cast<uint8_t>(mapping.registerAddress >> 8),
            static_cast<uint8_t>(mapping.registerAddress & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>(mapping.dataLength)
        };

        uint8_t checksum = Checksums::Xor8::INITIAL;
        for (auto b : cmd) checksum ^= b;
        cmd.push_back(checksum);

        return m_bus->writeBytes(cmd);
    }

    Frame readResponse(CommunicationError& outErr) override {
        Frame frame;
        auto start = std::chrono::steady_clock::now();
        std::vector<uint8_t> response;
        response.reserve(EmsBosch::RESPONSE_BUFFER_RESERVE);

        while (true) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > EmsBosch::READ_TIMEOUT) {
                outErr = CommunicationError::timeout();
                return frame;
            }

            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                EmsBosch::READ_TIMEOUT - elapsed);

            uint8_t byte;
            auto err = m_bus->readByte(byte, remaining);
            if (!err.isOk()) { outErr = err; return frame; }
            response.push_back(byte);

            if (response.size() >= 3 && isEmsResponseComplete(response)) break;
        }

        frame = parseEmsResponse(response, outErr);
        return frame;
    }

    CommunicationError doWriteValue(const SensorMapping& mapping,
                                     const SensorValue& value) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        uint16_t rawVal = static_cast<uint16_t>(
            (value.asFloat() - mapping.offset) / mapping.scale);

        std::vector<uint8_t> cmd = {
            0xDC, 0x02,
            static_cast<uint8_t>(mapping.registerAddress >> 8),
            static_cast<uint8_t>(mapping.registerAddress & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>(mapping.dataLength),
            static_cast<uint8_t>(rawVal >> 8),
            static_cast<uint8_t>(rawVal & ByteMasks::LOW_BYTE)
        };

        uint8_t checksum = Checksums::Xor8::INITIAL;
        for (auto b : cmd) checksum ^= b;
        cmd.push_back(checksum);

        auto err = m_bus->writeBytes(cmd);
        if (!err.isOk()) return err;

        return waitForWriteAck(EmsBosch::READ_TIMEOUT);
    }

private:
    static bool isEmsResponseComplete(const std::vector<uint8_t>& response) {
        if (response.size() < 4) return false;
        uint8_t len = response[FrameLayout::LENGTH_POS];
        return response.size() >= static_cast<size_t>(len) + FrameLayout::MIN_RESPONSE_SIZE;
    }

    static Frame parseEmsResponse(const std::vector<uint8_t>& response,
                                   CommunicationError& outErr) {
        Frame frame;
        if (response.size() < 4) {
            outErr = CommunicationError::invalidFrame("EMS response too short");
            return frame;
        }

        frame.address = response[FrameLayout::ADDRESS_POS];
        frame.command = response[FrameLayout::COMMAND_POS];
        uint8_t len = response[FrameLayout::LENGTH_POS];
        frame.length = len;

        size_t dataEnd = FrameLayout::HEADER_SIZE + len;
        if (response.size() >= dataEnd + Checksums::Xor8::SIZE) {
            frame.data.assign(response.begin() + FrameLayout::HEADER_SIZE,
                              response.begin() + dataEnd);

            Xor8Validator validator;
            frame.checksum = response[dataEnd];
            outErr = validator.validate(frame);
        } else {
            frame.data.assign(response.begin() + FrameLayout::HEADER_SIZE,
                              response.begin() + response.size());
            outErr = CommunicationError::invalidFrame("EMS checksum missing");
        }
        frame.valid = outErr.isOk();
        return frame;
    }

    static SensorCatalog buildSensorCatalog() {
        SensorCatalog catalog;
        catalog.addAll({
            SensorMapping{0x3000, "outdoor_temp",     "Outdoor Temperature",    SensorType::TEMPERATURE, SensorUnit::CELSIUS,   0x3000, 2, EmsBosch::TEMP_SCALE, EmsBosch::TEMP_OFFSET, false},
            SensorMapping{0x3002, "supply_temp",      "Supply Temperature",     SensorType::TEMPERATURE, SensorUnit::CELSIUS,   0x3002, 2, EmsBosch::TEMP_SCALE, EmsBosch::TEMP_OFFSET, false},
            SensorMapping{0x3004, "return_temp",      "Return Temperature",     SensorType::TEMPERATURE, SensorUnit::CELSIUS,   0x3004, 2, EmsBosch::TEMP_SCALE, EmsBosch::TEMP_OFFSET, false},
            SensorMapping{0x3006, "hot_water_temp",   "Hot Water Temperature",  SensorType::TEMPERATURE, SensorUnit::CELSIUS,   0x3006, 2, EmsBosch::TEMP_SCALE, 0.0f,          false},
            SensorMapping{0x3008, "brine_in_temp",    "Brine In Temperature",   SensorType::TEMPERATURE, SensorUnit::CELSIUS,   0x3008, 2, EmsBosch::TEMP_SCALE, EmsBosch::TEMP_OFFSET, false},
            SensorMapping{0x300A, "brine_out_temp",   "Brine Out Temperature",  SensorType::TEMPERATURE, SensorUnit::CELSIUS,   0x300A, 2, EmsBosch::TEMP_SCALE, EmsBosch::TEMP_OFFSET, false},
            SensorMapping{0x3012, "compressor_temp",  "Compressor Temperature", SensorType::TEMPERATURE, SensorUnit::CELSIUS,   0x3012, 2, EmsBosch::TEMP_SCALE, EmsBosch::TEMP_OFFSET, false},
            SensorMapping{0x3071, "compressor_speed", "Compressor Speed",       SensorType::SPEED,       SensorUnit::HERTZ,      0x3071, 2, 1.0f, 0.0f,             false},
            SensorMapping{0x3091, "heat_power",       "Heat Power",             SensorType::POWER,       SensorUnit::KILOWATT,   0x3091, 2, 0.1f, 0.0f,             false},
            SensorMapping{0x3092, "electrical_power", "Electrical Power",       SensorType::POWER,       SensorUnit::KILOWATT,   0x3092, 2, 0.1f, 0.0f,             false},
            SensorMapping{0x3102, "flow_temp",        "Flow Temperature",       SensorType::TEMPERATURE, SensorUnit::CELSIUS,   0x3102, 2, EmsBosch::TEMP_SCALE, EmsBosch::TEMP_OFFSET, false},
            SensorMapping{0x3111, "room_temp_set",    "Room Temp Setpoint",     SensorType::TEMPERATURE, SensorUnit::CELSIUS,   0x3111, 2, EmsBosch::TEMP_SCALE, EmsBosch::TEMP_OFFSET, true},
            SensorMapping{0x3211, "heating_setpoint", "Heating Setpoint",       SensorType::TEMPERATURE, SensorUnit::CELSIUS,   0x3211, 2, EmsBosch::TEMP_SCALE, EmsBosch::TEMP_OFFSET, true},
            SensorMapping{0x321A, "ww_setpoint",      "Warm Water Setpoint",    SensorType::TEMPERATURE, SensorUnit::CELSIUS,   0x321A, 2, EmsBosch::TEMP_SCALE, EmsBosch::TEMP_OFFSET, true},
            SensorMapping{0x3276, "operating_mode",   "Operating Mode",         SensorType::INTEGER,     SensorUnit::NONE,       0x3276, 1, 1.0f, 0.0f,             false},
            SensorMapping{0x32F5, "room_humidity",    "Room Humidity",          SensorType::PERCENT,     SensorUnit::PERCENT,    0x32F5, 2, 1.0f, 0.0f,             false},
        });
        return catalog;
    }

    static ParserRegistry buildParserRegistry() {
        ParserRegistry registry;
        registry.registerParser(SensorType::TEMPERATURE, SensorParser::parseTemperature);
        registry.registerParser(SensorType::POWER,       SensorParser::parsePower);
        registry.registerParser(SensorType::SPEED,       SensorParser::parseSpeed);
        registry.registerParser(SensorType::PERCENT,     SensorParser::parsePercent);
        registry.registerParser(SensorType::INTEGER,     SensorParser::parseInteger);
        return registry;
    }
};
