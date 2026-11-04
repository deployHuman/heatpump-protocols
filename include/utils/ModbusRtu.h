#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include "comm/abstract/ICommunicationBus.h"
#include "utils/CrcCalculator.h"

namespace ModbusRtu {

inline std::vector<uint8_t> buildWriteSingleRegisterFrame(uint8_t slave, uint16_t registerAddress,
                                                          uint16_t value) {
    std::vector<uint8_t> frame = {
        slave, 0x06,
        static_cast<uint8_t>(registerAddress >> 8),
        static_cast<uint8_t>(registerAddress & 0xFF),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value & 0xFF),
    };
    uint16_t crc = Utils::crc16Modbus(frame);
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    return frame;
}

inline std::vector<uint8_t> buildWriteSingleCoilFrame(uint8_t slave, uint16_t coilAddress,
                                                      bool enable) {
    uint16_t value = enable ? 0xFF00 : 0x0000;
    std::vector<uint8_t> frame = {
        slave, 0x05,
        static_cast<uint8_t>(coilAddress >> 8),
        static_cast<uint8_t>(coilAddress & 0xFF),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value & 0xFF),
    };
    uint16_t crc = Utils::crc16Modbus(frame);
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    return frame;
}

inline std::vector<uint8_t> buildReadRequestFrame(uint8_t slave, uint8_t function,
                                                  uint16_t address, uint16_t count) {
    std::vector<uint8_t> frame = {
        slave, function,
        static_cast<uint8_t>(address >> 8),
        static_cast<uint8_t>(address & 0xFF),
        static_cast<uint8_t>(count >> 8),
        static_cast<uint8_t>(count & 0xFF),
    };
    uint16_t crc = Utils::crc16Modbus(frame);
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    return frame;
}

inline CommunicationError readWriteEcho(ICommunicationBus* bus, uint8_t slave, uint8_t function,
                                        uint16_t address, uint16_t value,
                                        std::chrono::milliseconds timeout) {
    if (!bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

    std::vector<uint8_t> response;
    response.reserve(8);
    for (size_t i = 0; i < 8; ++i) {
        uint8_t byte = 0;
        auto err = bus->readByte(byte, timeout);
        if (!err.isOk()) return err;
        response.push_back(byte);

        if (response.size() == 2 && (response[1] & 0x80) != 0) {
            uint8_t exceptionCode = 0;
            auto exceptionErr = bus->readByte(exceptionCode, timeout);
            if (!exceptionErr.isOk()) return exceptionErr;
            bus->drain();
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "Modbus exception " + std::to_string(exceptionCode), 0};
        }
    }

    if (response[0] != slave || response[1] != function ||
        response[2] != static_cast<uint8_t>(address >> 8) ||
        response[3] != static_cast<uint8_t>(address & 0xFF) ||
        response[4] != static_cast<uint8_t>(value >> 8) ||
        response[5] != static_cast<uint8_t>(value & 0xFF)) {
        return CommunicationError{CommErrorCode::PROTOCOL_MISMATCH,
                                   "Modbus echo does not match request", 0};
    }

    uint16_t receivedCrc = static_cast<uint16_t>(response[6]) |
                           (static_cast<uint16_t>(response[7]) << 8);
    std::vector<uint8_t> withoutCrc(response.begin(), response.begin() + 6);
    if (Utils::crc16Modbus(withoutCrc) != receivedCrc) {
        return CommunicationError{CommErrorCode::CHECKSUM_MISMATCH,
                                   "Modbus response CRC mismatch", 0};
    }

    return CommunicationError::ok();
}

} // namespace ModbusRtu
