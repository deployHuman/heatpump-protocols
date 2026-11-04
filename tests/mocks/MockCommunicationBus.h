#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>
#include <queue>
#include <string>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <optional>
#include "comm/abstract/ICommunicationBus.h"

struct RxTxPair {
    std::vector<uint8_t> expectedTx;
    std::vector<uint8_t> simulatedRx;
    std::string description;
};

class MockCommunicationBus : public ICommunicationBus {
public:
    MockCommunicationBus()
        : m_ready(false)
        , m_baudRate(0)
        , m_config{}
        , m_traceEnabled(false)
        , m_currentPairIndex(0)
        , m_rxPos(0) {}

    CommunicationError init(const BusConfig& config) override {
        m_config = config;
        m_ready = true;
        return CommunicationError::ok();
    }

    CommunicationError deinit() override {
        m_ready = false;
        m_txLog.clear();
        m_rxLog.clear();
        m_rxBuffer.reset();
        m_currentPairIndex = 0;
        m_rxPos = 0;
        return CommunicationError::ok();
    }

    CommunicationError writeByte(uint8_t byte) override {
        if (!m_ready) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Mock bus not initialized", 0};
        m_txLog.push_back(byte);

        if (m_currentPairIndex < m_pairs.size()) {
            auto& pair = m_pairs[m_currentPairIndex];
            size_t txIdx = m_txLog.size() - 1;
            if (txIdx < pair.expectedTx.size() && pair.expectedTx[txIdx] != byte) {
                return CommunicationError{CommErrorCode::PROTOCOL_MISMATCH,
                    "TX mismatch at byte " + std::to_string(txIdx) +
                    ": expected 0x" + hexStr(pair.expectedTx[txIdx]) +
                    " got 0x" + hexStr(byte), 0};
            }
        }

        return CommunicationError::ok();
    }

    CommunicationError writeBytes(std::span<const uint8_t> data) override {
        for (auto b : data) {
            auto err = writeByte(b);
            if (!err.isOk()) return err;
        }
        return CommunicationError::ok();
    }

    CommunicationError readByte(uint8_t& out, std::chrono::milliseconds /*timeout*/) override {
        if (!m_ready) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Mock bus not initialized", 0};

        if (!m_rxBuffer.has_value() || m_rxPos >= m_rxBuffer->size()) {
            if (m_currentPairIndex >= m_pairs.size()) {
                return CommunicationError::timeout("No more mock data");
            }

            auto& pair = m_pairs[m_currentPairIndex];

            if (m_txLog.size() < pair.expectedTx.size()) {
                return CommunicationError::timeout("Mock: not all TX bytes sent yet");
            }

            m_rxBuffer = pair.simulatedRx;
            m_rxPos = 0;
            m_currentPairIndex++;
        }

        if (m_rxBuffer.has_value() && m_rxPos < m_rxBuffer->size()) {
            out = (*m_rxBuffer)[m_rxPos++];
            m_rxLog.push_back(out);
            return CommunicationError::ok();
        }

        return CommunicationError::timeout("No more data in mock buffer");
    }

    CommunicationError readBytes(std::span<uint8_t> buffer, size_t& bytesRead,
                                  std::chrono::milliseconds timeout) override {
        bytesRead = 0;
        auto start = std::chrono::steady_clock::now();

        for (size_t i = 0; i < buffer.size(); i++) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > timeout) break;

            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                timeout - elapsed);

            uint8_t byte;
            auto err = readByte(byte, remaining);
            if (!err.isOk()) break;

            buffer[i] = byte;
            bytesRead++;
        }

        if (bytesRead == 0) {
            return CommunicationError::timeout("No bytes read in mock");
        }
        return CommunicationError::ok();
    }

    size_t available() override {
        if (m_currentPairIndex >= m_pairs.size()) return 0;
        if (!m_rxBuffer.has_value()) return 0;
        return m_rxBuffer->size() - m_rxPos;
    }

    CommunicationError flush() override { return CommunicationError::ok(); }
    CommunicationError drain() override { return CommunicationError::ok(); }

    [[nodiscard]] bool isReady() const override { return m_ready; }
    [[nodiscard]] BusType getType() const override { return m_config.type; }
    [[nodiscard]] const BusConfig& getConfig() const override { return m_config; }

    void loadScenario(const std::vector<RxTxPair>& pairs) {
        m_pairs = pairs;
        m_currentPairIndex = 0;
        m_rxPos = 0;
        m_txLog.clear();
        m_rxLog.clear();
        m_rxBuffer.reset();
    }

    void loadScenario(std::initializer_list<RxTxPair> pairs) {
        loadScenario(std::vector<RxTxPair>(pairs));
    }

    void addPair(const RxTxPair& pair) {
        m_pairs.push_back(pair);
    }

    void addPair(std::vector<uint8_t> expectedTx, std::vector<uint8_t> simulatedRx,
                 std::string description = "") {
        m_pairs.push_back({std::move(expectedTx), std::move(simulatedRx), std::move(description)});
    }

    [[nodiscard]] const std::vector<uint8_t>& getTxLog() const { return m_txLog; }
    [[nodiscard]] const std::vector<uint8_t>& getRxLog() const { return m_rxLog; }

    [[nodiscard]] bool allPairsConsumed() const {
        return m_currentPairIndex >= m_pairs.size() &&
               (!m_rxBuffer.has_value() || m_rxPos >= m_rxBuffer->size());
    }

    [[nodiscard]] size_t getPairsConsumed() const { return m_currentPairIndex; }
    [[nodiscard]] size_t getPairsTotal() const { return m_pairs.size(); }

    void resetState() {
        m_currentPairIndex = 0;
        m_rxPos = 0;
        m_txLog.clear();
        m_rxLog.clear();
        m_rxBuffer.reset();
    }

    void enableTrace(bool enabled) { m_traceEnabled = enabled; }

    [[nodiscard]] static std::vector<uint8_t> hexToBytes(const std::string& hex) {
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i < hex.length(); i += 2) {
            if (i + 1 < hex.length()) {
                bytes.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
            }
        }
        return bytes;
    }

    [[nodiscard]] static std::string bytesToHex(std::span<const uint8_t> bytes) {
        std::string result;
        result.reserve(bytes.size() * 3);
        for (size_t i = 0; i < bytes.size(); i++) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X ", bytes[i]);
            result += buf;
        }
        if (!result.empty()) result.pop_back();
        return result;
    }

private:
    [[nodiscard]] static std::string hexStr(uint8_t byte) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X", byte);
        return buf;
    }

    bool m_ready;
    uint32_t m_baudRate;
    BusConfig m_config;
    bool m_traceEnabled;
    std::vector<uint8_t> m_txLog;
    std::vector<uint8_t> m_rxLog;
    std::vector<RxTxPair> m_pairs;
    std::optional<std::vector<uint8_t>> m_rxBuffer;
    size_t m_currentPairIndex;
    size_t m_rxPos;
};
