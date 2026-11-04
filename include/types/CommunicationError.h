#pragma once

#include <cstdint>
#include <string>

enum class CommErrorCode : uint8_t {
    OK = 0,
    TIMEOUT,
    OVERFLOW,
    CHECKSUM_MISMATCH,
    NAK_RECEIVED,
    INVALID_FRAME,
    BUS_NOT_READY,
    DEVICE_NOT_FOUND,
    PROTOCOL_MISMATCH,
    WRITE_REJECTED,
    UNKNOWN,
};

struct CommunicationError {
    CommErrorCode code;
    std::string message;
    int32_t detail;

    [[nodiscard]] bool isOk() const { return code == CommErrorCode::OK; }

    static CommunicationError ok() {
        return {CommErrorCode::OK, "", 0};
    }

    static CommunicationError timeout(const std::string& msg = "Timeout") {
        return {CommErrorCode::TIMEOUT, msg, 0};
    }

    static CommunicationError checksum(uint32_t expected, uint32_t actual) {
        return {CommErrorCode::CHECKSUM_MISMATCH, "Checksum mismatch",
                static_cast<int32_t>(actual - expected)};
    }

    static CommunicationError invalidFrame(const std::string& msg = "Invalid frame") {
        return {CommErrorCode::INVALID_FRAME, msg, 0};
    }
};
