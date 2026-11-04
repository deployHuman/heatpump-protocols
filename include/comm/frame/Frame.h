#pragma once

#include <cstdint>
#include <vector>
#include <span>
#include "types/Constants.h"

using namespace Constants;

struct Frame {
    uint8_t address = 0;
    uint8_t command = 0;
    uint8_t length = 0;
    std::vector<uint8_t> data;
    uint16_t checksum = 0;
    bool valid = false;

    static constexpr size_t MAX_DATA_SIZE = FrameLayout::MAX_DATA_SIZE;
    static constexpr size_t MAX_FRAME_SIZE = FrameLayout::MAX_FRAME_SIZE;

    [[nodiscard]] size_t totalSize() const {
        return FrameLayout::HEADER_SIZE + data.size() + FrameLayout::CHECKSUM_SIZE;
    }

    void clear() {
        address = 0;
        command = 0;
        length = 0;
        data.clear();
        checksum = 0;
        valid = false;
    }
};

enum class FrameState {
    IDLE,
    RECEIVING,
    COMPLETE,
    ERROR,
};

struct FrameParserState {
    FrameState state = FrameState::IDLE;
    Frame currentFrame{};
    size_t bytesReceived = 0;
    size_t dataBytesExpected = 0;
};
