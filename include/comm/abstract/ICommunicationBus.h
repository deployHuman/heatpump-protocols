#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <functional>
#include <chrono>
#include "types/CommunicationError.h"

enum class BusType : uint8_t {
    UART,
    MODBUS_RTU,
    MODBUS_TCP,
    CAN,
};

struct BusConfig {
    BusType type;
    uint32_t baudRate;
    uint8_t dataBits;
    uint8_t stopBits;
    uint8_t parity; // 0=none, 1=odd, 2=even
    int8_t rxPin;
    int8_t txPin;
    bool inverted;
    uint32_t timeoutMs;
};

class ICommunicationBus {
public:
    virtual ~ICommunicationBus() = default;

    virtual CommunicationError init(const BusConfig& config) = 0;
    virtual CommunicationError deinit() = 0;

    virtual CommunicationError writeByte(uint8_t byte) = 0;
    virtual CommunicationError writeBytes(std::span<const uint8_t> data) = 0;

    virtual CommunicationError readByte(uint8_t& out, std::chrono::milliseconds timeout) = 0;
    virtual CommunicationError readBytes(std::span<uint8_t> buffer, size_t& bytesRead,
                                         std::chrono::milliseconds timeout) = 0;

    virtual size_t available() = 0;
    virtual CommunicationError flush() = 0;
    virtual CommunicationError drain() = 0;

    [[nodiscard]] virtual bool isReady() const = 0;
    [[nodiscard]] virtual BusType getType() const = 0;
    [[nodiscard]] virtual const BusConfig& getConfig() const = 0;
};
