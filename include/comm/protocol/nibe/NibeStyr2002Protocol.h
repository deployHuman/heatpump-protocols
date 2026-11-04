#pragma once

#include "comm/protocol/nibe/NibeProtocolBase.h"
#include "comm/storage/IFileStorage.h"
#include "types/Constants.h"
#include <sstream>
#include <algorithm>
#include <cctype>

using namespace Constants;

class NibeStyr2002Protocol : public HeatPumpProtocolBase {
public:
    NibeStyr2002Protocol()
        : HeatPumpProtocolBase(HeatPumpProtocol::NIBE_STYR2002,
                                buildNibeSensorCatalog(),
                                buildNibeParserRegistry())
        , m_mode(0)
        , m_storage(nullptr) {}

    void setFileStorage(IFileStorage* storage) {
        m_storage = storage;
    }

    CommunicationError loadModeFromFile() {
        if (!m_storage) return CommunicationError{CommErrorCode::BUS_NOT_READY, "No file storage configured", 0};
        std::string text;
        auto err = m_storage->readText("/STYR-mode.txt", text);
        if (!err.isOk()) return err;

        auto pos = text.find("STYR-mode ");
        if (pos != std::string::npos) {
            std::string modeStr = text.substr(pos + 10);
            modeStr.erase(std::remove_if(modeStr.begin(), modeStr.end(),
                [](char c) { return !std::isdigit(c); }), modeStr.end());
            if (!modeStr.empty()) {
                m_mode = std::stoi(modeStr);
            }
        }
        return CommunicationError::ok();
    }

    CommunicationError saveModeToFile(int mode) {
        if (!m_storage) return CommunicationError{CommErrorCode::BUS_NOT_READY, "No file storage configured", 0};
        m_mode = mode;

        switch (mode) {
        case 1245: m_modeStr = "STYR-mode 1245\n"; break;
        case 360:  m_modeStr = "STYR-mode 360P\n"; break;
        case 1330: m_modeStr = "STYR-mode 1330\n"; break;
        case 1250: m_modeStr = "STYR-mode 1250\n"; break;
        default:   m_modeStr = "STYR-mode " + std::to_string(mode) + "\n"; break;
        }

        auto err = m_storage->writeText("/STYR-mode.txt", m_modeStr);
        if (!err.isOk()) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                       "STYR-mode write failed", 0};
        }
        return CommunicationError::ok();
    }

    CommunicationError detectModel(HeatPumpModelInfo& modelInfo) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        auto cmd = buildStyrIdentCommand();
        auto err = m_bus->writeBytes(cmd);
        if (!err.isOk()) return err;

        std::vector<uint8_t> response(Nibe::RESPONSE_BUFFER_RESERVE);
        size_t bytesRead = 0;
        err = m_bus->readBytes(response, bytesRead, Nibe::READ_TIMEOUT);
        if (!err.isOk()) return err;

        std::string respStr(response.begin(), response.begin() + bytesRead);
        if (respStr.find("STYR2002") != std::string::npos ||
            respStr.find("Fighter") != std::string::npos) {
            modelInfo.protocol = HeatPumpProtocol::NIBE_STYR2002;
            modelInfo.brand = HeatPumpBrand::NIBE;
            modelInfo.hpType = HeatPumpType::LW;
            modelInfo.modelName = "NIBE Styr2002";
            modelInfo.writeEnabled = true;
            modelInfo.readOnly = false;
            m_modelInfo = modelInfo;
            return CommunicationError::ok();
        }

        return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                   "Not a NIBE Styr2002", 0};
    }

    [[nodiscard]] ProtocolCapabilities getCapabilities() const override {
        return {true, true, true, true, m_sensorCatalog.count(), Frame::MAX_DATA_SIZE};
    }

    CommunicationError setMode(int mode) {
        m_mode = mode;
        return doSetMode(mode);
    }

    [[nodiscard]] int getMode() const { return m_mode; }
    [[nodiscard]] const std::string& getModeString() const { return m_modeStr; }

private:

protected:
    CommunicationError sendQuery(const SensorMapping& mapping) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};
        auto cmd = buildStyrReadCommand(mapping.registerAddress);
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
            if (!err.isOk()) { outErr = err; return frame; }
            response.push_back(byte);

            if (response.size() >= 4 && isStyrResponseComplete(response)) break;
        }

        frame = parseStyrResponse(response, outErr);
        return frame;
    }

private:
    static std::vector<uint8_t> buildStyrIdentCommand() {
        return {0x38, 0x10, 0xFF, 0x00, 0x03, 0x2B, 0xFF, 0x00};
    }

    static std::vector<uint8_t> buildStyrReadCommand(uint16_t regAddr) {
        return {
            0x38, 0x10,
            static_cast<uint8_t>(regAddr >> 8),
            static_cast<uint8_t>(regAddr & ByteMasks::LOW_BYTE),
            0x03, 0x2B,
            0x00, 0x00
        };
    }

    CommunicationError doSetMode(int mode) {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};
        std::vector<uint8_t> modeCmd = {
            0x38, 0x10,
            static_cast<uint8_t>(mode >> 8),
            static_cast<uint8_t>(mode & ByteMasks::LOW_BYTE),
            0x03, 0x2B, 0x01, 0x00
        };
        return m_bus->writeBytes(modeCmd);
    }

    static bool isStyrResponseComplete(const std::vector<uint8_t>& response) {
        if (response.size() < 4) return false;
        size_t expected = response[FrameLayout::LENGTH_POS] + FrameLayout::HEADER_SIZE;
        return response.size() >= expected;
    }

    static Frame parseStyrResponse(const std::vector<uint8_t>& response,
                                    CommunicationError& outErr) {
        Frame frame;
        if (response.size() < 4) {
            outErr = CommunicationError::invalidFrame("Response too short");
            return frame;
        }
        frame.address = response[0];
        frame.command = response[1];
        frame.length = response[2];
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
            outErr = CommunicationError::invalidFrame("Checksum missing");
        }
        frame.valid = outErr.isOk();
        return frame;
    }

private:
    int m_mode;
    std::string m_modeStr;
    IFileStorage* m_storage;
};
