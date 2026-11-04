#pragma once

#include <cstdint>
#include <bit>
#include <concepts>
#include "types/Constants.h"

using namespace Constants;

namespace Utils {

template<std::integral T>
inline T swapEndian(T val) {
    if constexpr (sizeof(T) == 2) {
        return static_cast<T>(
            ((val & ByteMasks::U16_LOW)  << Endian::U16_LOW_SHIFT) |
            ((val & ByteMasks::U16_HIGH) >> Endian::U16_LOW_SHIFT));
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(
            ((val & ByteMasks::U32_BYTE0)  << Endian::U32_B0_SHIFT) |
            ((val & ByteMasks::U32_BYTE1)  << Endian::U32_B1_SHIFT) |
            ((val & ByteMasks::U32_BYTE2)  >> Endian::U32_B1_SHIFT) |
            ((val & ByteMasks::U32_BYTE3)  >> Endian::U32_B0_SHIFT));
    }
    return val;
}

template<std::integral T>
inline T bigEndianToNative(T val) {
    if constexpr (std::endian::native == std::endian::little) {
        return swapEndian(val);
    }
    return val;
}

template<std::integral T>
inline T nativeToBigEndian(T val) {
    return bigEndianToNative(val);
}

inline uint16_t readUint16Big(const uint8_t* data) {
    return (static_cast<uint16_t>(data[0]) << Endian::U16_LOW_SHIFT) | data[1];
}

inline int16_t readInt16Big(const uint8_t* data) {
    return static_cast<int16_t>(readUint16Big(data));
}

inline uint32_t readUint32Big(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << Endian::U32_B0_SHIFT) |
           (static_cast<uint32_t>(data[1]) << Endian::U32_B1_SHIFT) |
           (static_cast<uint32_t>(data[2]) << Endian::U32_B2_SHIFT) |
           data[3];
}

inline void writeUint16Big(uint8_t* data, uint16_t val) {
    data[0] = static_cast<uint8_t>(val >> Endian::U16_LOW_SHIFT);
    data[1] = static_cast<uint8_t>(val & ByteMasks::LOW_BYTE);
}

inline void writeUint32Big(uint8_t* data, uint32_t val) {
    data[0] = static_cast<uint8_t>(val >> Endian::U32_B0_SHIFT);
    data[1] = static_cast<uint8_t>((val >> Endian::U32_B1_SHIFT) & ByteMasks::LOW_BYTE);
    data[2] = static_cast<uint8_t>((val >> Endian::U32_B2_SHIFT) & ByteMasks::LOW_BYTE);
    data[3] = static_cast<uint8_t>(val & ByteMasks::LOW_BYTE);
}

} // namespace Utils
