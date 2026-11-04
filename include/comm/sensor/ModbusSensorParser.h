#pragma once

#include <cstdint>
#include <vector>

#include "comm/sensor/SensorCatalog.h"
#include "types/SensorValue.h"

/// Parsers for Modbus register data. Modbus sends 16-bit registers big-endian;
/// 32-bit values in the Thermia map are two consecutive registers with the
/// most-significant word first.
namespace ModbusSensorParser {

inline bool isSignedType(SensorType type) {
    return type == SensorType::TEMPERATURE ||
           type == SensorType::PRESSURE ||
           type == SensorType::INTEGER;
}

inline SensorValue makeValue(const SensorMapping& mapping) {
    SensorValue value;
    value.type = mapping.type;
    value.unit = mapping.unit;
    value.name = mapping.name;
    value.displayName = mapping.displayName;
    value.id = mapping.sensorId;
    return value;
}

inline SensorValue parse16(const std::vector<uint8_t>& raw, size_t offset,
                           const SensorMapping& mapping) {
    auto value = makeValue(mapping);
    if (offset + 2 <= raw.size()) {
        uint16_t rawValue = static_cast<uint16_t>(
            (static_cast<uint16_t>(raw[offset]) << 8) |
            static_cast<uint16_t>(raw[offset + 1]));
        float numeric = isSignedType(mapping.type)
                            ? static_cast<float>(static_cast<int16_t>(rawValue))
                            : static_cast<float>(rawValue);
        value.rawValue = numeric * mapping.scale + mapping.offset;
    }
    return value;
}

inline SensorValue parse32(const std::vector<uint8_t>& raw, size_t offset,
                           const SensorMapping& mapping) {
    auto value = makeValue(mapping);
    if (offset + 4 <= raw.size()) {
        uint32_t rawValue = (static_cast<uint32_t>(raw[offset]) << 24) |
                            (static_cast<uint32_t>(raw[offset + 1]) << 16) |
                            (static_cast<uint32_t>(raw[offset + 2]) << 8) |
                            static_cast<uint32_t>(raw[offset + 3]);
        float numeric = isSignedType(mapping.type)
                            ? static_cast<float>(static_cast<int32_t>(rawValue))
                            : static_cast<float>(rawValue);
        value.rawValue = numeric * mapping.scale + mapping.offset;
    }
    return value;
}

inline SensorValue parseBool(const std::vector<uint8_t>& raw, size_t offset,
                             const SensorMapping& mapping) {
    auto value = makeValue(mapping);
    if (offset + 2 <= raw.size()) {
        uint16_t rawValue = static_cast<uint16_t>(
            (static_cast<uint16_t>(raw[offset]) << 8) |
            static_cast<uint16_t>(raw[offset + 1]));
        value.rawValue = rawValue != 0;
    }
    return value;
}

} // namespace ModbusSensorParser
