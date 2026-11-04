#pragma once

#include "comm/protocol/HeatPumpProtocolBase.h"
#include "comm/protocol/nibe/NibeProtocolBase.h"
#include "types/Constants.h"
#include <sstream>

using namespace Constants;

class NibeEB100Protocol : public HeatPumpProtocolBase {
public:
    NibeEB100Protocol()
        : HeatPumpProtocolBase(HeatPumpProtocol::NIBE_EB100,
                                buildNibeSensorCatalog(),
                                buildNibeParserRegistry()) {}

    CommunicationError detectModel(HeatPumpModelInfo& modelInfo) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        std::vector<uint8_t> identCmd = buildIdentCommand();
        auto err = m_bus->writeBytes(identCmd);
        if (!err.isOk()) return err;

        std::vector<uint8_t> response(Nibe::RESPONSE_BUFFER_RESERVE);
        size_t bytesRead = 0;
        err = m_bus->readBytes(response, bytesRead, Nibe::READ_TIMEOUT);
        if (!err.isOk()) return err;

        std::string respStr(response.begin(), response.begin() + bytesRead);
        if (respStr.find("EB100") != std::string::npos ||
            respStr.find("NIBE") != std::string::npos) {
            modelInfo.protocol = HeatPumpProtocol::NIBE_EB100;
            modelInfo.brand = HeatPumpBrand::NIBE;
            modelInfo.hpType = HeatPumpType::LW;
            modelInfo.modelName = "NIBE EB100";
            modelInfo.writeEnabled = true;
            modelInfo.readOnly = false;
            m_modelInfo = modelInfo;
            return CommunicationError::ok();
        }

        return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                   "Not a NIBE EB100", 0};
    }

    [[nodiscard]] ProtocolCapabilities getCapabilities() const override {
        return {true, true, true, true, m_sensorCatalog.count(), Frame::MAX_DATA_SIZE};
    }

    CommunicationError clearAlarmAndRestart() {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        std::vector<uint8_t> alarmClearCmd = {
            0x38, 0x10, 0xFF, 0x00, 0x03, 0x2B, 0xF2, 0x01
        };
        auto err = m_bus->writeBytes(alarmClearCmd);
        if (!err.isOk()) return err;

        uint8_t ack;
        err = m_bus->readByte(ack, Nibe::READ_TIMEOUT);
        if (!err.isOk()) return err;

        std::vector<uint8_t> restartCmd = buildRestartCommand();
        err = m_bus->writeBytes(restartCmd);
        if (!err.isOk()) return err;

        for (int countdown = m_restartCountdown; countdown > 0; countdown--) {
            uint8_t status;
            err = m_bus->readByte(status, std::chrono::milliseconds(1000));
            if (err.isOk() && status == 0xAA) {
                break;
            }
        }

        flushAndDrain();
        return onInit();
    }

    [[nodiscard]] int getRestartCountdown() const { return m_restartCountdown; }
    void setRestartCountdown(int count) { m_restartCountdown = count; }

protected:
    CommunicationError sendQuery(const SensorMapping& mapping) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};
        auto cmd = buildReadCommand(mapping.registerAddress);
        return m_bus->writeBytes(cmd);
    }

    Frame readResponse(CommunicationError& outErr) override {
        Frame frame;
        auto start = std::chrono::steady_clock::now();
        std::vector<uint8_t> response;
        response.reserve(Nibe::RESPONSE_BUFFER_RESERVE);

        while (true) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > Nibe::READ_TIMEOUT) {
                outErr = CommunicationError::timeout();
                return frame;
            }

            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                Nibe::READ_TIMEOUT - elapsed);

            uint8_t byte;
            auto err = m_bus->readByte(byte, remaining);
            if (!err.isOk()) {
                outErr = err;
                return frame;
            }
            response.push_back(byte);

            if (response.size() >= FrameLayout::MIN_RESPONSE_SIZE &&
                isNibeResponseComplete(response)) {
                break;
            }
        }

        frame = parseNibeResponse(response, outErr);
        return frame;
    }

private:
    static std::vector<uint8_t> buildIdentCommand() {
        return {0x38, 0x10, 0xFF, 0x00, 0x03, 0x2B, 0x00, 0x00};
    }

    static std::vector<uint8_t> buildReadCommand(uint16_t regAddr) {
        return {
            0x38, 0x10, 0xFF, 0x00, 0x03, 0x2B,
            static_cast<uint8_t>(regAddr & ByteMasks::LOW_BYTE),
            static_cast<uint8_t>(regAddr >> 8)
        };
    }

    static bool isNibeResponseComplete(const std::vector<uint8_t>& response) {
        if (response.size() < 4) return false;
        size_t expectedLen = response[FrameLayout::LENGTH_POS] + FrameLayout::HEADER_SIZE;
        return response.size() >= expectedLen;
    }

    static Frame parseNibeResponse(const std::vector<uint8_t>& response,
                                    CommunicationError& outErr) {
        Frame frame;
        if (response.size() < 4) {
            outErr = CommunicationError::invalidFrame("Response too short");
            return frame;
        }

        frame.address = response[FrameLayout::ADDRESS_POS];
        frame.command = response[FrameLayout::COMMAND_POS];
        frame.length = response[FrameLayout::LENGTH_POS];

        size_t dataEnd = FrameLayout::HEADER_SIZE + frame.length;
        if (response.size() >= dataEnd + Checksums::Xor8::SIZE) {
            frame.data.assign(response.begin() + FrameLayout::HEADER_SIZE,
                              response.begin() + dataEnd);

            Xor8Validator validator;
            frame.checksum = response[dataEnd];
            outErr = validator.validate(frame);
        } else {
            frame.data.assign(response.begin() + FrameLayout::HEADER_SIZE,
                              response.begin() + response.size());
            outErr = CommunicationError::invalidFrame("Frame too short for checksum");
        }
        frame.valid = outErr.isOk();
        return frame;
    }

    static std::vector<uint8_t> buildRestartCommand() {
        return {0x38, 0x10, 0xFF, 0x00, 0x03, 0x7B, 0x00, 0x00};
    }

    int m_restartCountdown = 5;
};
