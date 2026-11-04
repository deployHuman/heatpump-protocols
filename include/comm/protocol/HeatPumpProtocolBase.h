#pragma once

#include "comm/abstract/ICommunicationBus.h"
#include "comm/abstract/IHeatPumpProtocol.h"
#include "comm/frame/Frame.h"
#include "comm/frame/FrameParser.h"
#include "comm/frame/FrameValidator.h"
#include "comm/sensor/SensorCatalog.h"
#include "comm/sensor/ParserRegistry.h"
#include "comm/sensor/SensorParser.h"
#include "types/SensorValue.h"
#include "types/HeatPumpModel.h"
#include "types/CommunicationError.h"
#include "types/Constants.h"

#include <vector>
#include <memory>
#include <chrono>
#include <span>

using namespace Constants;

class HeatPumpProtocolBase : public IHeatPumpProtocol {
public:
    explicit HeatPumpProtocolBase(HeatPumpProtocol type,
                                  SensorCatalog sensorCatalog = {},
                                  ParserRegistry parserRegistry = {})
        : m_protocolType(type)
        , m_modelInfo(HeatPumpModelInfo::unknown())
        , m_bus(nullptr)
        , m_sensorCatalog(std::move(sensorCatalog))
        , m_parserRegistry(std::move(parserRegistry))
        , m_state(ProtocolState::UNINITIALIZED)
        , m_errorCount(0)
        , m_retryCount(0)
        , m_dataBuffer(Frame::MAX_DATA_SIZE) {}

    ~HeatPumpProtocolBase() override = default;

    CommunicationError initialize(ICommunicationBus* bus) override {
        m_bus = bus;
        m_state = ProtocolState::INITIALIZING;

        auto err = checkDeviceReady();
        if (!err.isOk()) {
            m_state = ProtocolState::ERROR;
            return err;
        }

        err = onInit();
        if (!err.isOk()) {
            m_state = ProtocolState::ERROR;
            return err;
        }

        m_state = ProtocolState::READY;
        m_errorCount = 0;
        m_retryCount = 0;
        return CommunicationError::ok();
    }

    CommunicationError shutdown() override {
        onShutdown();
        m_bus = nullptr;
        m_state = ProtocolState::UNINITIALIZED;
        return CommunicationError::ok();
    }

    CommunicationError pollSensors(std::vector<SensorValue>& sensors) override {
        sensors.clear();
        if (!isOperational()) return makeNotReadyError();

        auto ids = m_sensorCatalog.getAllIds();
        for (auto id : ids) {
            const auto* mapping = m_sensorCatalog.getById(id);
            if (!mapping) continue;

            auto err = sendQueryWithRetry(*mapping);
            if (!err.isOk()) {
                handleError(err);
                return err;
            }

            CommunicationError readErr;
            auto frame = readResponseWithRetry(readErr);
            if (!readErr.isOk()) {
                handleError(readErr);
                return readErr;
            }

            auto parser = m_parserRegistry.getParser(mapping->type);
            if (parser) {
                sensors.push_back(parser(frame.data, 0, *mapping));
            }
        }

        m_errorCount = 0;
        return CommunicationError::ok();
    }

    CommunicationError readSensor(uint16_t sensorId, SensorValue& value) override {
        if (!isOperational()) return makeNotReadyError();

        const auto* mapping = m_sensorCatalog.getById(sensorId);
        if (!mapping) {
            return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                       "Unknown sensor ID " + std::to_string(sensorId), 0};
        }
        auto err = sendQueryWithRetry(*mapping);
        if (!err.isOk()) { handleError(err); return err; }

        CommunicationError readErr;
        auto frame = readResponseWithRetry(readErr);
        if (!readErr.isOk()) { handleError(readErr); return readErr; }

        auto parser = m_parserRegistry.getParser(mapping->type);
        if (parser) value = parser(frame.data, 0, *mapping);

        m_errorCount = 0;
        return CommunicationError::ok();
    }

    CommunicationError readSensorByName(const std::string& name, SensorValue& value) override {
        const auto* mapping = m_sensorCatalog.getByName(name);
        if (!mapping) {
            return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                       "Unknown sensor name: " + name, 0};
        }
        return readSensor(mapping->sensorId, value);
    }

    CommunicationError writeValue(uint16_t sensorId, const SensorValue& value) {
        if (!isOperational()) return makeNotReadyError();

        const auto* mapping = m_sensorCatalog.getById(sensorId);
        if (!mapping) {
            return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND, "Unknown sensor ID", 0};
        }
        if (!mapping->writable) {
            return CommunicationError{CommErrorCode::WRITE_REJECTED, "Sensor is read-only", 0};
        }

        auto err = doWriteValue(*mapping, value);
        if (!err.isOk()) return err;

        return waitForWriteAck(m_writeTimeout);
    }

    CommunicationError sendCommand(const std::string& command, std::string& response) {
        if (!isOperational()) return makeNotReadyError();
        return doSendCommand(command, response);
    }

    [[nodiscard]] HeatPumpProtocol getProtocolType() const override { return m_protocolType; }
    [[nodiscard]] const HeatPumpModelInfo& getModelInfo() const override { return m_modelInfo; }
    [[nodiscard]] bool isReady() const override { return m_state == ProtocolState::READY; }

protected:
    virtual CommunicationError sendQuery(const SensorMapping& mapping) = 0;
    virtual Frame readResponse(CommunicationError& outErr) = 0;
    virtual CommunicationError doWriteValue(const SensorMapping&, const SensorValue&) {
        return CommunicationError{CommErrorCode::WRITE_REJECTED, "Write not supported", 0};
    }
    virtual CommunicationError doSendCommand(const std::string&, std::string&) {
        return CommunicationError{CommErrorCode::UNKNOWN, "Commands not supported", 0};
    }
    virtual CommunicationError onInit() { return CommunicationError::ok(); }
    virtual void onShutdown() {}

    virtual CommunicationError waitForWriteAck(std::chrono::milliseconds timeout) {
        return waitForAck(timeout);
    }

    [[nodiscard]] CommunicationError waitForAck(std::chrono::milliseconds timeout) {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};
        uint8_t byte;
        return m_bus->readByte(byte, timeout);
    }

    CommunicationError flushAndDrain() {
        if (!m_bus) return CommunicationError::ok();
        auto err = m_bus->flush();
        if (!err.isOk()) return err;
        return m_bus->drain();
    }

    [[nodiscard]] CommunicationError sendQueryWithRetry(const SensorMapping& mapping) {
        for (int attempt = 0; attempt <= m_maxRetries; attempt++) {
            auto err = sendQuery(mapping);
            if (err.isOk()) return err;
            if (!isRecoverable(err) || attempt >= m_maxRetries) return err;
            onRetry(attempt);
        }
        return CommunicationError{CommErrorCode::UNKNOWN, "Max retries exceeded", 0};
    }

    Frame readResponseWithRetry(CommunicationError& outErr) {
        for (int attempt = 0; attempt <= m_maxRetries; attempt++) {
            auto frame = readResponse(outErr);
            if (outErr.isOk()) return frame;
            if (!isRecoverable(outErr) || attempt >= m_maxRetries) return frame;
            onRetry(attempt);
        }
        return Frame{};
    }

    [[nodiscard]] bool isRecoverable(const CommunicationError& err) const {
        return err.code == CommErrorCode::TIMEOUT ||
               err.code == CommErrorCode::CHECKSUM_MISMATCH;
    }

    void onRetry(int /*attempt*/) {
        flushAndDrain();
        m_retryCount++;
    }

    void handleError(const CommunicationError& /*err*/) {
        m_errorCount++;
        if (m_errorCount >= m_maxErrors) {
            m_state = ProtocolState::ERROR;
        }
    }

    [[nodiscard]] CommunicationError checkDeviceReady() {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};
        if (!m_bus->isReady()) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus hardware not ready", 0};
        return CommunicationError::ok();
    }

    [[nodiscard]] bool isOperational() const {
        return m_state == ProtocolState::READY && m_bus != nullptr;
    }

    [[nodiscard]] CommunicationError makeNotReadyError() const {
        return CommunicationError{CommErrorCode::BUS_NOT_READY, "Protocol not ready (state: " +
                                   std::to_string(static_cast<int>(m_state)) + ")", 0};
    }

    enum class ProtocolState : uint8_t {
        UNINITIALIZED,
        INITIALIZING,
        READY,
        BUSY,
        ERROR,
    };

    HeatPumpProtocol m_protocolType;
    HeatPumpModelInfo m_modelInfo;
    ICommunicationBus* m_bus;
    SensorCatalog m_sensorCatalog;
    ParserRegistry m_parserRegistry;
    ProtocolState m_state;
    int m_errorCount;
    int m_retryCount;
    std::vector<uint8_t> m_dataBuffer;

    static constexpr int m_maxRetries = 3;
    static constexpr int m_maxErrors = 5;
    std::chrono::milliseconds m_writeTimeout{3000};
};
