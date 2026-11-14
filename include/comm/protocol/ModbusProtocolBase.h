#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "comm/protocol/HeatPumpProtocolBase.h"
#include "comm/sensor/ModbusSensorParser.h"
#include "comm/sensor/ParserRegistry.h"
#include "comm/sensor/SensorCatalog.h"
#include "types/Constants.h"
#include "utils/ModbusRtu.h"

using namespace Constants;

/**
 * Shared Modbus RTU polling layer.
 *
 * Protocols provide a sensor catalog and a parser registry, then implement
 * their own detectModel() and doWriteValue(). This base class owns the
 * request/response state machine and the CRC/echo handling.
 *
 * The write path is deliberately enabled only through doWriteValue(); the
 * single-byte acknowledgement wait is a no-op because Modbus writes consume
 * the full echo response.
 */
class ModbusProtocolBase : public HeatPumpProtocolBase {
public:
    [[nodiscard]] ProtocolCapabilities getCapabilities() const override {
        return {true, true, true, true, m_sensorCatalog.count(), Frame::MAX_DATA_SIZE};
    }

protected:
    ModbusProtocolBase(HeatPumpProtocol protocol, SensorCatalog catalog,
                       ParserRegistry parserRegistry, uint8_t slaveAddress,
                       std::chrono::milliseconds readTimeout)
        : HeatPumpProtocolBase(protocol, std::move(catalog), std::move(parserRegistry))
        , m_slaveAddress(slaveAddress)
        , m_readTimeout(readTimeout)
        , m_scanActive(false)
        , m_lastFunction(0x03) {}

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
            m_slaveAddress, mapping.readFunction, mapping.registerAddress, count);
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
        response.reserve(16);

        while (true) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > m_readTimeout) {
                outErr = CommunicationError::timeout();
                m_scanActive = false;
                return frame;
            }

            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                m_readTimeout - elapsed);

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

        if (frame.address != m_slaveAddress || frame.command != m_lastFunction) {
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

    // Modbus write helpers consume the full echo, so the base-class single
    // ACK wait must not run again.
    CommunicationError waitForWriteAck(std::chrono::milliseconds) override {
        return CommunicationError::ok();
    }

    [[nodiscard]] uint8_t slaveAddress() const { return m_slaveAddress; }

    static ParserRegistry modbusParserRegistry() {
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

private:
    uint8_t m_slaveAddress;
    std::chrono::milliseconds m_readTimeout;
    bool m_scanActive;
    uint8_t m_lastFunction;
};
