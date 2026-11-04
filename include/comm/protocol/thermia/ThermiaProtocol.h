#pragma once

#include "comm/protocol/HeatPumpProtocolBase.h"
#include "comm/sensor/SensorCatalog.h"
#include "comm/sensor/SensorParser.h"
#include "comm/frame/FrameValidator.h"
#include "utils/CrcCalculator.h"
#include "types/Constants.h"

using namespace Constants;

class ThermiaProtocol : public HeatPumpProtocolBase {
public:
    ThermiaProtocol()
        : HeatPumpProtocolBase(HeatPumpProtocol::THERMIA_DIPLOMAT,
                                buildSensorCatalog(),
                                buildParserRegistry())
        , m_scanActive(false)
        , m_cycleDelayCounter(Thermia::SCAN_CYCLE_DELAY)
        , m_failureCount(0)
        , m_consecutiveErrors(0)
        , m_modeEnabled(false) {}

    CommunicationError detectModel(HeatPumpModelInfo& modelInfo) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        std::vector<uint8_t> identCmd = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
        uint16_t crc = Utils::crc16Modbus(identCmd);
        identCmd.push_back(static_cast<uint8_t>(crc & ByteMasks::LOW_BYTE));
        identCmd.push_back(static_cast<uint8_t>(crc >> 8));

        auto err = m_bus->writeBytes(identCmd);
        if (!err.isOk()) return err;

        std::vector<uint8_t> response(Thermia::RESPONSE_BUFFER_RESERVE);
        size_t bytesRead = 0;
        err = m_bus->readBytes(response, bytesRead, Thermia::READ_TIMEOUT);
        if (!err.isOk()) return err;

        std::string respStr(response.begin(), response.begin() + bytesRead);
        if (respStr.find("Thermia") != std::string::npos ||
            respStr.find("Diplomat") != std::string::npos ||
            respStr.find("Villa Classic") != std::string::npos ||
            respStr.find("Genesis") != std::string::npos ||
            respStr.find("Fighter") != std::string::npos) {
            modelInfo.protocol = HeatPumpProtocol::THERMIA_DIPLOMAT;
            modelInfo.brand = HeatPumpBrand::THERMIA;
            modelInfo.hpType = HeatPumpType::LW;

            if (respStr.find("Diplomat") != std::string::npos) {
                modelInfo.modelName = "Thermia Diplomat";
            } else if (respStr.find("Villa Classic") != std::string::npos) {
                modelInfo.modelName = "Thermia Villa Classic";
            } else if (respStr.find("Genesis") != std::string::npos) {
                modelInfo.modelName = "Thermia Genesis";
            } else {
                modelInfo.modelName = "Thermia";
            }
            modelInfo.writeEnabled = true;
            modelInfo.readOnly = false;
            m_modelInfo = modelInfo;
            m_modeEnabled = true;
            return CommunicationError::ok();
        }

        return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                   "Not a Thermia heat pump", 0};
    }

    [[nodiscard]] ProtocolCapabilities getCapabilities() const override {
        return {true, true, true, true, m_sensorCatalog.count(), Frame::MAX_DATA_SIZE};
    }

protected:
    CommunicationError onInit() override {
        m_scanActive = false;
        m_cycleDelayCounter = Thermia::SCAN_CYCLE_DELAY;
        m_failureCount = 0;
        m_consecutiveErrors = 0;
        return CommunicationError::ok();
    }

    CommunicationError sendQuery(const SensorMapping& mapping) override {
        if (m_scanActive) {
            return CommunicationError{CommErrorCode::UNKNOWN, "Scan in progress", 0};
        }

        if (m_cycleDelayCounter > 0) {
            m_cycleDelayCounter--;
            return CommunicationError{CommErrorCode::OK, "", 0};
        }

        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        m_scanActive = true;
        auto cmd = buildModbusReadCommand(0x01, mapping.registerAddress, 1);
        return m_bus->writeBytes(cmd);
    }

    Frame readResponse(CommunicationError& outErr) override {
        Frame frame;
        if (!m_scanActive) {
            outErr = CommunicationError::ok();
            return frame;
        }

        auto start = std::chrono::steady_clock::now();
        std::vector<uint8_t> response;
        response.reserve(Thermia::RESPONSE_BUFFER_RESERVE);

        while (true) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > Thermia::READ_TIMEOUT) {
                outErr = CommunicationError::timeout();
                m_scanActive = false;
                m_failureCount++;
                m_consecutiveErrors++;
                return frame;
            }

            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                Thermia::READ_TIMEOUT - elapsed);

            uint8_t byte;
            auto err = m_bus->readByte(byte, remaining);
            if (!err.isOk()) {
                outErr = err;
                m_scanActive = false;
                m_failureCount++;
                m_consecutiveErrors++;
                return frame;
            }
            response.push_back(byte);

            if (response.size() >= 5 && isModbusResponseComplete(response)) break;
        }

        frame = parseModbusResponse(response, outErr);
        m_scanActive = false;

        if (outErr.isOk()) {
            m_cycleDelayCounter = Thermia::SCAN_CYCLE_DELAY;
            m_consecutiveErrors = 0;
        } else {
            m_failureCount++;
            m_consecutiveErrors++;
        }

        if (m_consecutiveErrors >= 3) {
            m_modeEnabled = false;
        }

        return frame;
    }

    CommunicationError doWriteValue(const SensorMapping& mapping,
                                     const SensorValue& value) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        uint16_t rawVal = static_cast<uint16_t>(
            (value.asFloat() - mapping.offset) / mapping.scale);

        std::vector<uint8_t> cmd = {0x01, 0x06,
                                     static_cast<uint8_t>(mapping.registerAddress >> 8),
                                     static_cast<uint8_t>(mapping.registerAddress & ByteMasks::LOW_BYTE),
                                     static_cast<uint8_t>(rawVal >> 8),
                                     static_cast<uint8_t>(rawVal & ByteMasks::LOW_BYTE)};

        uint16_t crc = Utils::crc16Modbus(cmd);
        cmd.push_back(static_cast<uint8_t>(crc & ByteMasks::LOW_BYTE));
        cmd.push_back(static_cast<uint8_t>(crc >> 8));

        auto err = m_bus->writeBytes(cmd);
        if (!err.isOk()) return err;

        return waitForWriteAck(Thermia::READ_TIMEOUT);
    }

private:
    static std::vector<uint8_t> buildModbusReadCommand(uint8_t addr, uint16_t reg, uint16_t count) {
        std::vector<uint8_t> cmd = {addr, 0x03,
                                     static_cast<uint8_t>(reg >> 8),
                                     static_cast<uint8_t>(reg & ByteMasks::LOW_BYTE),
                                     static_cast<uint8_t>(count >> 8),
                                     static_cast<uint8_t>(count & ByteMasks::LOW_BYTE)};
        uint16_t crc = Utils::crc16Modbus(cmd);
        cmd.push_back(static_cast<uint8_t>(crc & ByteMasks::LOW_BYTE));
        cmd.push_back(static_cast<uint8_t>(crc >> 8));
        return cmd;
    }

    static bool isModbusResponseComplete(const std::vector<uint8_t>& response) {
        if (response.size() < 3) return false;
        uint8_t byteCount = response[2];
        return response.size() >= static_cast<size_t>(byteCount + 5);
    }

    static Frame parseModbusResponse(const std::vector<uint8_t>& response,
                                      CommunicationError& outErr) {
        Frame frame;
        if (response.size() < 5) {
            outErr = CommunicationError::invalidFrame("Modbus response too short");
            return frame;
        }

        frame.address = response[0];
        frame.command = response[1];
        uint8_t byteCount = response[2];

        size_t dataEnd = 3 + byteCount;
        if (response.size() >= dataEnd + Checksums::Crc16Modbus::SIZE) {
            frame.data.assign(response.begin() + 3, response.begin() + dataEnd);
            frame.length = byteCount;

            uint16_t receivedCrc = static_cast<uint16_t>(response[dataEnd]) |
                                  (static_cast<uint16_t>(response[dataEnd + 1]) << 8);

            uint16_t computedCrc = Utils::crc16Modbus(
                std::span<const uint8_t>(response.data(), dataEnd));

            frame.checksum = receivedCrc;
            if (computedCrc != receivedCrc) {
                outErr = CommunicationError::checksum(receivedCrc, computedCrc);
                frame.valid = false;
            } else {
                outErr = CommunicationError::ok();
                frame.valid = true;
            }
        } else {
            frame.data.assign(response.begin() + 3, response.end());
            outErr = CommunicationError::invalidFrame("Modbus CRC missing");
            frame.valid = false;
        }
        return frame;
    }

    static SensorCatalog buildSensorCatalog() {
        SensorCatalog catalog;
        catalog.addAll({
            SensorMapping{0, "outdoor_temp", "Outdoor Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0000, 2, Thermia::TEMP_SCALE, Thermia::TEMP_OFFSET, false},
            SensorMapping{1, "supply_line_temp", "Supply Line Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0002, 2, Thermia::TEMP_SCALE, Thermia::TEMP_OFFSET, false},
            SensorMapping{2, "return_line_temp", "Return Line Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0004, 2, Thermia::TEMP_SCALE, Thermia::TEMP_OFFSET, false},
            SensorMapping{3, "hot_water_temp", "Hot Water Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0006, 2, Thermia::TEMP_SCALE, 0.0f, false},
            SensorMapping{4, "brine_in_temp", "Brine In Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0008, 2, Thermia::TEMP_SCALE, Thermia::TEMP_OFFSET, false},
            SensorMapping{5, "brine_out_temp", "Brine Out Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x000A, 2, Thermia::TEMP_SCALE, Thermia::TEMP_OFFSET, false},
            SensorMapping{6, "compressor_speed", "Compressor Speed", SensorType::SPEED,
             SensorUnit::HERTZ, 0x0010, 2, 1.0f, 0.0f, false},
            SensorMapping{7, "heat_power", "Heat Power", SensorType::POWER,
             SensorUnit::KILOWATT, 0x0012, 2, 0.1f, 0.0f, false},
            SensorMapping{8, "room_temp", "Room Temperature", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0014, 2, Thermia::TEMP_SCALE, Thermia::TEMP_OFFSET, false},
            SensorMapping{9, "heating_setpoint", "Heating Setpoint", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x0107, 2, Thermia::TEMP_SCALE, Thermia::TEMP_OFFSET, true},
            SensorMapping{10, "warm_water_setpoint", "Warm Water Setpoint", SensorType::TEMPERATURE,
             SensorUnit::CELSIUS, 0x010B, 2, Thermia::TEMP_SCALE, Thermia::TEMP_OFFSET, true},
            SensorMapping{11, "total_op_time", "Total Operating Time", SensorType::RUNTIME,
             SensorUnit::HOURS, 0xC50, 4, 1.0f, 0.0f, false},
            SensorMapping{12, "total_energy", "Total Energy", SensorType::ENERGY,
             SensorUnit::KILOWATT_HOUR, 0xC51, 4, 0.001f, 0.0f, false},
            SensorMapping{13, "compressor_energy", "Compressor Energy", SensorType::ENERGY,
             SensorUnit::KILOWATT_HOUR, 0xC54, 4, 0.001f, 0.0f, false},
            SensorMapping{14, "aux_energy", "Auxiliary Energy", SensorType::ENERGY,
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

    bool m_scanActive;
    int m_cycleDelayCounter;
    int m_failureCount;
    int m_consecutiveErrors;
    bool m_modeEnabled;
};
