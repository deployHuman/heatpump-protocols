#pragma once

#include <cstdint>
#include <string>

enum class HeatPumpBrand : uint8_t {
    NIBE,
    IVT,
    BOSCH,
    REGIN,
    THERMIA,
    GENERIC,
};

enum class HeatPumpProtocol : uint8_t {
    NIBE_EB100,
    NIBE_STYR2002,
    IVT_REGO800,
    IVT_REGO600,
    IVT_REGO1000,
    IVT_REGO5200,
    BOSCH_EMS_PROCONTROL,
    THERMIA_DIPLOMAT,
    THERMIA_GENESIS,
    GENERIC,
};

enum class HeatPumpType : uint8_t {
    LW,   // Liquid/Water
    AW,   // Air/Water
    EW,   // Earth/Water
};

struct HeatPumpModelInfo {
    HeatPumpProtocol protocol;
    HeatPumpBrand brand;
    HeatPumpType hpType;
    std::string modelName;
    std::string version;
    bool writeEnabled;
    bool readOnly;

    [[nodiscard]] static HeatPumpModelInfo unknown() {
        return {HeatPumpProtocol::GENERIC, HeatPumpBrand::GENERIC,
                HeatPumpType::LW, "Unknown", "", false, true};
    }
};
