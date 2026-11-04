#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include "comm/frame/Frame.h"
#include "types/CommunicationError.h"
#include "types/Constants.h"

using namespace Constants;

enum class ChecksumType : uint8_t {
    XOR8,
    CRC8,
    CRC16_MODBUS,
    CRC16_CCITT,
    CUSTOM,
};

class IFrameValidator {
public:
    virtual ~IFrameValidator() = default;

    virtual uint16_t compute(std::span<const uint8_t> data) const = 0;
    virtual CommunicationError validate(const Frame& frame) const = 0;
    [[nodiscard]] virtual ChecksumType getType() const = 0;
    [[nodiscard]] virtual size_t checksumSize() const = 0;

protected:
    [[nodiscard]] CommunicationError validateFrame(const Frame& frame) const {
        std::vector<uint8_t> buf;
        bool hasLength = frame.length > 0;
        size_t headerSize = hasLength ? FrameLayout::HEADER_SIZE : FrameLayout::HEADER_SIZE - 1;
        buf.reserve(headerSize + frame.data.size());
        buf.push_back(frame.address);
        buf.push_back(frame.command);
        if (hasLength) {
            buf.push_back(frame.length);
        }
        buf.insert(buf.end(), frame.data.begin(), frame.data.end());
        auto computed = compute(buf);
        if (computed != frame.checksum) {
            return CommunicationError::checksum(frame.checksum, computed);
        }
        return CommunicationError::ok();
    }
};

class Xor8Validator : public IFrameValidator {
public:
    uint16_t compute(std::span<const uint8_t> data) const override {
        uint8_t result = Checksums::Xor8::INITIAL;
        for (auto b : data) result ^= b;
        return result;
    }

    CommunicationError validate(const Frame& frame) const override {
        return validateFrame(frame);
    }

    [[nodiscard]] ChecksumType getType() const override { return ChecksumType::XOR8; }
    [[nodiscard]] size_t checksumSize() const override { return Checksums::Xor8::SIZE; }
};

class Crc16ModbusValidator : public IFrameValidator {
public:
    uint16_t compute(std::span<const uint8_t> data) const override {
        uint16_t crc = Checksums::Crc16Modbus::INITIAL;
        for (auto b : data) {
            crc ^= b;
            for (int i = 0; i < Checksums::BITS_PER_BYTE; i++) {
                if (crc & Checksums::Crc16Modbus::LSB_MASK) {
                    crc = (crc >> 1) ^ Checksums::Crc16Modbus::POLY;
                } else {
                    crc >>= 1;
                }
            }
        }
        return crc;
    }

    CommunicationError validate(const Frame& frame) const override {
        return validateFrame(frame);
    }

    [[nodiscard]] ChecksumType getType() const override { return ChecksumType::CRC16_MODBUS; }
    [[nodiscard]] size_t checksumSize() const override { return Checksums::Crc16Modbus::SIZE; }
};
