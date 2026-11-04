#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <optional>

enum class SensorType : uint8_t {
    TEMPERATURE,
    PRESSURE,
    POWER,
    ENERGY,
    FLOW,
    RUNTIME,
    SPEED,
    PERCENT,
    BOOLEAN,
    INTEGER,
    FLOAT,
    STRING,
};

enum class SensorUnit : uint8_t {
    NONE,
    CELSIUS,
    FAHRENHEIT,
    BAR,
    PSI,
    KILOWATT,
    WATT,
    KILOWATT_HOUR,
    LITERS_PER_MINUTE,
    HOURS,
    RPM,
    HERTZ,
    PERCENT,
};

struct SensorValue {
    SensorType type;
    SensorUnit unit;
    std::string name;
    std::string displayName;
    uint16_t id;

    std::variant<
        std::monostate,
        int32_t,
        uint32_t,
        float,
        bool,
        std::string
    > rawValue;

    [[nodiscard]] bool isEmpty() const {
        return std::holds_alternative<std::monostate>(rawValue);
    }

    [[nodiscard]] float asFloat() const {
        if (std::holds_alternative<float>(rawValue)) return std::get<float>(rawValue);
        if (std::holds_alternative<int32_t>(rawValue)) return static_cast<float>(std::get<int32_t>(rawValue));
        if (std::holds_alternative<uint32_t>(rawValue)) return static_cast<float>(std::get<uint32_t>(rawValue));
        return 0.0f;
    }

    [[nodiscard]] int32_t asInt() const {
        if (std::holds_alternative<int32_t>(rawValue)) return std::get<int32_t>(rawValue);
        if (std::holds_alternative<uint32_t>(rawValue)) return static_cast<int32_t>(std::get<uint32_t>(rawValue));
        if (std::holds_alternative<float>(rawValue)) return static_cast<int32_t>(std::get<float>(rawValue));
        return 0;
    }

    [[nodiscard]] bool asBool() const {
        if (std::holds_alternative<bool>(rawValue)) return std::get<bool>(rawValue);
        if (std::holds_alternative<int32_t>(rawValue)) return std::get<int32_t>(rawValue) != 0;
        if (std::holds_alternative<uint32_t>(rawValue)) return std::get<uint32_t>(rawValue) != 0;
        return false;
    }
};
