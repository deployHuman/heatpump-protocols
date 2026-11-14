#pragma once

#include <memory>
#include <vector>
#include <string>
#include "comm/abstract/ICommunicationBus.h"
#include "comm/abstract/IHeatPumpProtocol.h"
#include "comm/frame/Frame.h"
#include "comm/frame/FrameParser.h"
#include "comm/sensor/SensorCatalog.h"
#include "types/SensorValue.h"
#include "types/HeatPumpModel.h"
#include "types/CommunicationError.h"

class HeatPumpDevice {
public:
    HeatPumpDevice(std::unique_ptr<ICommunicationBus> bus,
                   std::unique_ptr<IHeatPumpProtocol> protocol)
        : m_bus(std::move(bus))
        , m_protocol(std::move(protocol))
        , m_modelInfo(m_protocol ? m_protocol->getModelInfo() : HeatPumpModelInfo::unknown()) {}

    HeatPumpDevice(HeatPumpDevice&&) = default;
    HeatPumpDevice& operator=(HeatPumpDevice&&) = default;

    HeatPumpDevice(const HeatPumpDevice&) = delete;
    HeatPumpDevice& operator=(const HeatPumpDevice&) = delete;

    CommunicationError init(const BusConfig& config) {
        auto err = m_bus->init(config);
        if (!err.isOk()) return err;

        err = m_protocol->initialize(m_bus.get());
        if (!err.isOk()) return err;

        err = m_protocol->detectModel(m_modelInfo);
        return err;
    }

    CommunicationError shutdown() {
        if (m_protocol) {
            auto err = m_protocol->shutdown();
            if (!err.isOk()) return err;
        }
        if (m_bus) {
            return m_bus->deinit();
        }
        return CommunicationError::ok();
    }

    CommunicationError poll(std::vector<SensorValue>& sensors) {
        if (!m_protocol) return makeNotReadyError();
        return m_protocol->pollSensors(sensors);
    }

    CommunicationError readSensor(uint16_t id, SensorValue& value) {
        if (!m_protocol) return makeNotReadyError();
        return m_protocol->readSensor(id, value);
    }

    CommunicationError readSensorByName(const std::string& name, SensorValue& value) {
        if (!m_protocol) return makeNotReadyError();
        return m_protocol->readSensorByName(name, value);
    }

    CommunicationError writeSensor(uint16_t id, const SensorValue& value) {
        auto* writer = dynamic_cast<IHeatPumpWriter*>(m_protocol.get());
        if (!writer) return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                                "Protocol does not support writes", 0};
        return writer->writeValue(id, value);
    }

    CommunicationError writeSensorByName(const std::string& name, const SensorValue& value) {
        auto* writer = dynamic_cast<IHeatPumpWriter*>(m_protocol.get());
        if (!writer) return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                                "Protocol does not support writes", 0};
        return writer->writeValueByName(name, value);
    }

    CommunicationError sendCommand(const std::string& cmd, std::string& response) {
        auto* commander = dynamic_cast<IHeatPumpCommander*>(m_protocol.get());
        if (!commander) return CommunicationError{CommErrorCode::WRITE_REJECTED,
                                                   "Protocol does not support commands", 0};
        return commander->sendCommand(cmd, response);
    }

    [[nodiscard]] const HeatPumpModelInfo& getModelInfo() const { return m_modelInfo; }
    [[nodiscard]] ICommunicationBus* getBus() { return m_bus.get(); }
    [[nodiscard]] IHeatPumpProtocol* getProtocol() { return m_protocol.get(); }
    [[nodiscard]] bool isReady() const { return m_protocol && m_protocol->isReady(); }

private:
    [[nodiscard]] static CommunicationError makeNotReadyError() {
        return CommunicationError{CommErrorCode::BUS_NOT_READY, "Protocol not initialized", 0};
    }

    std::unique_ptr<ICommunicationBus> m_bus;
    std::unique_ptr<IHeatPumpProtocol> m_protocol;
    HeatPumpModelInfo m_modelInfo;
};
