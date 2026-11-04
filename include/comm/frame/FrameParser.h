#pragma once

#include <vector>
#include <span>
#include <functional>
#include <optional>
#include "comm/frame/Frame.h"
#include "comm/frame/FrameValidator.h"
#include "comm/abstract/ICommunicationBus.h"
#include "types/CommunicationError.h"
#include "types/Constants.h"

using namespace Constants;

class FrameParser {
public:
    using FrameCallback = std::function<void(const Frame&)>;

    explicit FrameParser(std::unique_ptr<IFrameValidator> validator)
        : m_validator(std::move(validator)) {}

    void reset() {
        m_state = FrameParserState{};
        m_buffer.clear();
    }

    CommunicationError feedByte(uint8_t byte) {
        m_buffer.push_back(byte);

        switch (m_state.state) {
        case FrameState::IDLE:
            m_state.currentFrame.address = byte;
            m_state.state = FrameState::RECEIVING;
            m_state.bytesReceived = 1;
            break;

        case FrameState::RECEIVING:
            m_state.bytesReceived++;

            if (m_state.bytesReceived == FrameLayout::COMMAND_POS + 1) {
                m_state.currentFrame.command = byte;
            } else if (m_state.bytesReceived == FrameLayout::LENGTH_POS + 1) {
                m_state.dataBytesExpected = byte;
                m_state.currentFrame.length = byte;
                if (m_state.dataBytesExpected == 0) {
                    m_state.state = FrameState::COMPLETE;
                }
            } else if (m_state.dataBytesExpected > 0 &&
                       m_state.bytesReceived > FrameLayout::HEADER_SIZE + m_state.dataBytesExpected) {
                m_state.state = FrameState::COMPLETE;
            } else if (m_state.bytesReceived > FrameLayout::HEADER_SIZE &&
                       m_state.bytesReceived <= FrameLayout::HEADER_SIZE + m_state.dataBytesExpected) {
                m_state.currentFrame.data.push_back(byte);
            }
            break;

        default:
            break;
        }

        if (m_state.state == FrameState::COMPLETE) {
            return parseCompletedFrame();
        }
        return CommunicationError::ok();
    }

    CommunicationError feedBytes(std::span<const uint8_t> data) {
        for (auto byte : data) {
            auto err = feedByte(byte);
            if (!err.isOk()) return err;
        }
        return CommunicationError::ok();
    }

    CommunicationError readFrame(ICommunicationBus* bus, Frame& frame,
                                  std::chrono::milliseconds timeout) {
        reset();
        auto start = std::chrono::steady_clock::now();

        while (m_state.state != FrameState::COMPLETE) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > timeout) {
                return CommunicationError::timeout();
            }

            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                timeout - elapsed);

            uint8_t byte;
            auto err = bus->readByte(byte, remaining);
            if (!err.isOk()) return err;

            err = feedByte(byte);
            if (!err.isOk()) return err;
        }

        frame = m_state.currentFrame;
        return CommunicationError::ok();
    }

    void setFrameCallback(FrameCallback callback) {
        m_callback = std::move(callback);
    }

    [[nodiscard]] const FrameParserState& getState() const { return m_state; }

private:
    CommunicationError parseCompletedFrame() {
        auto& frame = m_state.currentFrame;

        size_t dataStart = FrameLayout::HEADER_SIZE + m_state.dataBytesExpected;
        size_t csSize = m_validator->checksumSize();

        if (m_buffer.size() >= dataStart + csSize) {
            frame.checksum = static_cast<uint16_t>(m_buffer[dataStart]);
            if (csSize == Checksums::Crc16Modbus::SIZE) {
                frame.checksum |= static_cast<uint16_t>(m_buffer[dataStart + 1]) << 8;
            }

            auto err = m_validator->validate(frame);
            frame.valid = err.isOk();

            if (m_callback) {
                m_callback(frame);
            }

            return err;
        }

        return CommunicationError::invalidFrame("Frame too short for checksum");
    }

    FrameParserState m_state;
    std::vector<uint8_t> m_buffer;
    std::unique_ptr<IFrameValidator> m_validator;
    FrameCallback m_callback;
};
