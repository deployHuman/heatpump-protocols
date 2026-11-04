#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include "comm/sensor/SensorRegistry.h"
#include "types/SensorValue.h"
#include "types/Constants.h"

using namespace Constants;

namespace SensorParser {

namespace detail {
    template<typename T>
    inline T readLittleEndian(const std::vector<uint8_t>& raw, size_t offset, size_t len) {
        T result = 0;
        for (size_t i = 0; i < len && i < sizeof(T); i++) {
            result |= static_cast<T>(raw[offset + i]) << (i * Checksums::BITS_PER_BYTE);
        }
        return result;
    }
}

template<SensorType Type, SensorUnit Unit, typename TRaw>
inline SensorValue parseNumeric(const std::vector<uint8_t>& raw, size_t offset,
                                 const SensorMapping& mapping) {
    SensorValue sv;
    sv.type = Type;
    sv.unit = Unit;
    sv.name = mapping.name;
    sv.displayName = mapping.displayName;
    sv.id = mapping.sensorId;

    if (offset + mapping.dataLength <= raw.size()) {
        auto rawVal = detail::readLittleEndian<TRaw>(raw, offset, mapping.dataLength);
        sv.rawValue = static_cast<float>(rawVal) * mapping.scale + mapping.offset;
    }
    return sv;
}

inline constexpr auto parseTemperature = parseNumeric<SensorType::TEMPERATURE, SensorUnit::CELSIUS, int32_t>;
inline constexpr auto parsePower       = parseNumeric<SensorType::POWER, SensorUnit::KILOWATT, uint32_t>;
inline constexpr auto parseEnergy      = parseNumeric<SensorType::ENERGY, SensorUnit::KILOWATT_HOUR, uint32_t>;
inline constexpr auto parsePercent     = parseNumeric<SensorType::PERCENT, SensorUnit::PERCENT, uint32_t>;
inline constexpr auto parseSpeed       = parseNumeric<SensorType::SPEED, SensorUnit::HERTZ, uint32_t>;
inline constexpr auto parseFlow        = parseNumeric<SensorType::FLOW, SensorUnit::LITERS_PER_MINUTE, uint32_t>;
inline constexpr auto parseRuntime     = parseNumeric<SensorType::RUNTIME, SensorUnit::HOURS, uint32_t>;
inline constexpr auto parseInteger     = parseNumeric<SensorType::INTEGER, SensorUnit::NONE, int32_t>;

} // namespace SensorParser
