#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include "comm/abstract/ICommunicationBus.h"
#include "types/CommunicationError.h"
#include "types/Constants.h"

using namespace Constants;

struct Rc100HThermostatState {
    Rc100H::Mode mode = Rc100H::Mode::OFF;
    float roomTemp1 = 20.0f;
    float roomTemp2 = 20.0f;
    float humidity = 50.0f;
    uint8_t dialRange = 5;
    bool circuit1Active = false;
    bool circuit2Active = false;
    bool paired = false;
};

class IRc100HEmulation {
public:
    virtual ~IRc100HEmulation() = default;

    virtual CommunicationError init(ICommunicationBus* canBus) = 0;
    virtual CommunicationError shutdown() = 0;

    virtual CommunicationError setTemperature(uint8_t circuit, float temp) = 0;
    virtual CommunicationError setHumidity(float humidity) = 0;
    virtual CommunicationError setMode(Rc100H::Mode mode) = 0;
    virtual CommunicationError setDialRange(uint8_t range) = 0;

    virtual CommunicationError update() = 0;

    [[nodiscard]] virtual Rc100HThermostatState getState() const = 0;
    [[nodiscard]] virtual bool isPaired() const = 0;
};

class Rc100HEmulation : public IRc100HEmulation {
public:
    Rc100HEmulation() = default;

    CommunicationError init(ICommunicationBus* canBus) override {
        m_canBus = canBus;
        m_state.paired = false;
        m_state.mode = Rc100H::Mode::OFF;
        return CommunicationError::ok();
    }

    CommunicationError shutdown() override {
        m_canBus = nullptr;
        m_state.paired = false;
        return CommunicationError::ok();
    }

    CommunicationError setTemperature(uint8_t circuit, float temp) override {
        if (circuit == 1) {
            m_state.roomTemp1 = temp;
            m_state.circuit1Active = true;
        } else if (circuit == 2) {
            m_state.roomTemp2 = temp;
            m_state.circuit2Active = true;
        } else {
            return CommunicationError{CommErrorCode::UNKNOWN, "Invalid circuit: " + std::to_string(circuit), 0};
        }
        return CommunicationError::ok();
    }

    CommunicationError setHumidity(float humidity) override {
        m_state.humidity = std::max(0.0f, std::min(100.0f, humidity));
        return CommunicationError::ok();
    }

    CommunicationError setMode(Rc100H::Mode mode) override {
        if (mode == Rc100H::Mode::OFF && m_state.paired) {
            sendUnpairCommand();
        }
        m_state.mode = mode;
        if (mode != Rc100H::Mode::OFF && !m_state.paired) {
            return sendPairCommand();
        }
        return CommunicationError::ok();
    }

    CommunicationError setDialRange(uint8_t range) override {
        m_state.dialRange = range;
        return CommunicationError::ok();
    }

    CommunicationError update() override {
        if (!m_canBus || m_state.mode == Rc100H::Mode::OFF) {
            return CommunicationError::ok();
        }

        if (!m_state.paired) {
            auto err = sendPairCommand();
            if (!err.isOk()) return err;
        }

        std::vector<uint8_t> frame = buildThermostatFrame();
        auto err = m_canBus->writeBytes(frame);
        if (err.isOk()) {
            m_lastUpdate = std::chrono::steady_clock::now();
        }
        return err;
    }

    [[nodiscard]] Rc100HThermostatState getState() const override {
        return m_state;
    }

    [[nodiscard]] bool isPaired() const override {
        return m_state.paired;
    }

    CommunicationError sendXW00Write(uint64_t variableId, uint32_t value) {
        if (!m_canBus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "CAN bus not ready", 0};

        std::vector<uint8_t> frame = {
            0x0B, 0x00,
            static_cast<uint8_t>(variableId & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>((variableId >> 8) & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>((variableId >> 16) & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>((variableId >> 24) & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>((variableId >> 32) & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>((variableId >> 40) & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>((variableId >> 48) & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>((variableId >> 56) & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>(value & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>((value >> 8) & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>((value >> 16) & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>((value >> 24) & ByteMasks::LOW_BYTE),
        };

        uint8_t checksum = Checksums::Xor8::INITIAL;
        for (auto b : frame) checksum ^= b;
        frame.push_back(checksum);

        auto err = m_canBus->writeBytes(frame);
        if (!err.isOk()) return err;

        uint8_t response;
        err = m_canBus->readByte(response, Rc100H::CAN_TIMEOUT);
        if (!err.isOk()) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "XW00 CAN update failed", 0};
        }

        if (response == 0x15) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "XW00 ID not found", 0};
        }

        if (response == 0x06) {
            m_lastUpdate = std::chrono::steady_clock::now();
            return CommunicationError::ok();
        }

        return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                   "XW00 unexpected response: 0x" +
                                   std::to_string(response), 0};
    }

private:
    CommunicationError sendPairCommand() {
        if (!m_canBus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "CAN bus not ready", 0};

        std::vector<uint8_t> pairCmd = {
            0x0B, 0x90, 0xFF, 0x00, 0xFF, 0x01,
            static_cast<uint8_t>(Rc100H::Registers::ROOM_TEMP_CTRL_1 >> 8),
            static_cast<uint8_t>(Rc100H::Registers::ROOM_TEMP_CTRL_1 & ByteMasks::LOW_BYTE),
        };

        uint8_t checksum = Checksums::Xor8::INITIAL;
        for (auto b : pairCmd) checksum ^= b;
        pairCmd.push_back(checksum);

        auto err = m_canBus->writeBytes(pairCmd);
        if (!err.isOk()) return err;

        uint8_t ack;
        err = m_canBus->readByte(ack, Rc100H::CAN_TIMEOUT);
        if (err.isOk() && ack == 0x06) {
            m_state.paired = true;
            m_lastUpdate = std::chrono::steady_clock::now();
        }

        return err;
    }

    void sendUnpairCommand() {
        if (!m_canBus) return;

        std::vector<uint8_t> unpairCmd = {
            0x0B, 0x90, 0xFF, 0x00, 0xFF, 0x00,
            static_cast<uint8_t>(Rc100H::Registers::ROOM_TEMP_CTRL_1 >> 8),
            static_cast<uint8_t>(Rc100H::Registers::ROOM_TEMP_CTRL_1 & ByteMasks::LOW_BYTE),
        };

        uint8_t checksum = Checksums::Xor8::INITIAL;
        for (auto b : unpairCmd) checksum ^= b;
        unpairCmd.push_back(checksum);

        m_canBus->writeBytes(unpairCmd);
        m_state.paired = false;
    }

    std::vector<uint8_t> buildThermostatFrame() {
        uint16_t tempRaw1 = static_cast<uint16_t>(
            (m_state.roomTemp1 - Rego800::TEMP_OFFSET) / Rego800::TEMP_SCALE);
        uint16_t tempRaw2 = static_cast<uint16_t>(
            (m_state.roomTemp2 - Rego800::TEMP_OFFSET) / Rego800::TEMP_SCALE);
        uint16_t humidRaw = static_cast<uint16_t>(m_state.humidity);

        std::vector<uint8_t> frame = {
            0x0B, 0x90, 0xFF, 0x00, 0xFF, 0x01,
            static_cast<uint8_t>(tempRaw1 & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>(tempRaw1 >> 8),
            static_cast<uint8_t>(tempRaw2 & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>(tempRaw2 >> 8),
            static_cast<uint8_t>(humidRaw & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>(humidRaw >> 8),
        };

        uint8_t checksum = Checksums::Xor8::INITIAL;
        for (auto b : frame) checksum ^= b;
        frame.push_back(checksum);

        return frame;
    }

    ICommunicationBus* m_canBus = nullptr;
    Rc100HThermostatState m_state;
    std::chrono::steady_clock::time_point m_lastUpdate;
};
