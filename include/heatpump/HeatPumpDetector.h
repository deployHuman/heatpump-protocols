#pragma once

#include <vector>
#include <memory>
#include <functional>
#include "comm/abstract/ICommunicationBus.h"
#include "comm/abstract/IHeatPumpProtocol.h"
#include "types/HeatPumpModel.h"
#include "types/CommunicationError.h"

class HeatPumpDetector {
public:
    using ProtocolFactory = std::function<std::unique_ptr<IHeatPumpProtocol>()>;

    struct DetectionEntry {
        HeatPumpProtocol protocol;
        ProtocolFactory factory;
        std::string description;
    };

    void registerProtocol(HeatPumpProtocol protocol, ProtocolFactory factory,
                          const std::string& description) {
        m_entries.push_back({protocol, std::move(factory), description});
    }

    CommunicationError detect(ICommunicationBus* bus, HeatPumpModelInfo& modelInfo,
                               std::unique_ptr<IHeatPumpProtocol>& outProtocol) {
        for (auto& entry : m_entries) {
            auto protocol = entry.factory();
            auto err = protocol->initialize(bus);

            if (!err.isOk()) continue;

            auto detectErr = protocol->detectModel(modelInfo);
            protocol->shutdown();

            if (detectErr.isOk()) {
                outProtocol = std::move(protocol);
                return CommunicationError::ok();
            }
        }

        modelInfo = HeatPumpModelInfo::unknown();
        return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                   "No matching heat pump found", 0};
    }

    CommunicationError detectByProtocol(HeatPumpProtocol protocol, ICommunicationBus* bus,
                                         HeatPumpModelInfo& modelInfo,
                                         std::unique_ptr<IHeatPumpProtocol>& outProtocol) {
        for (auto& entry : m_entries) {
            if (entry.protocol != protocol) continue;

            auto proto = entry.factory();
            auto err = proto->initialize(bus);
            if (!err.isOk()) return err;

            err = proto->detectModel(modelInfo);
            if (!err.isOk()) {
                proto->shutdown();
                return err;
            }

            outProtocol = std::move(proto);
            return CommunicationError::ok();
        }

        return CommunicationError{CommErrorCode::PROTOCOL_MISMATCH,
                                   "Requested protocol not registered", 0};
    }

    [[nodiscard]] const std::vector<DetectionEntry>& getRegisteredProtocols() const {
        return m_entries;
    }

    [[nodiscard]] size_t count() const { return m_entries.size(); }

private:
    std::vector<DetectionEntry> m_entries;
};
