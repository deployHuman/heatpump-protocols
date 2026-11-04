#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include "types/Constants.h"

using namespace Constants;

namespace Utils {

inline uint8_t crc8(std::span<const uint8_t> data) {
    uint8_t crc = Checksums::Crc8::INITIAL;
    for (auto b : data) {
        crc ^= b;
        for (int i = 0; i < Checksums::BITS_PER_BYTE; i++) {
            if (crc & Checksums::Crc8::MSB_MASK) {
                crc = (crc << 1) ^ Checksums::Crc8::POLY;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

inline uint16_t crc16Modbus(std::span<const uint8_t> data) {
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

inline uint8_t xorChecksum(std::span<const uint8_t> data) {
    uint8_t result = Checksums::Xor8::INITIAL;
    for (auto b : data) result ^= b;
    return result;
}

} // namespace Utils
