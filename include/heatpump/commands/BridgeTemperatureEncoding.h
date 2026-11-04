#pragma once

#include <cstdint>

/// Temperature encoding used by the extended bridge-register handlers.
///
/// The public bridge documentation expresses temperatures as plain tenths:
/// `raw = tempC * 10`. Some bridge variants expose the pump-side raw value
/// instead: `raw = (tempC + 50) * 10`. The default is the documented bridge
/// value; switch to `PumpRawOffset50` only after confirming one read.
enum class BridgeTemperatureEncoding {
    BridgeDegrees,
    PumpRawOffset50,
};

inline uint16_t encodeBridgeTemperature(float tempC, BridgeTemperatureEncoding encoding) {
    const float offset = (encoding == BridgeTemperatureEncoding::PumpRawOffset50) ? 50.0f : 0.0f;
    float value = (tempC + offset) * 10.0f;
    if (value <= 0.0f) return 0;
    if (value >= 65535.0f) return 65535;
    return static_cast<uint16_t>(value + 0.5f);
}
