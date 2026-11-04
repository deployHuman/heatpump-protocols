#pragma once

#include "comm/protocol/HeatPumpProtocolBase.h"
#include "types/Constants.h"

using namespace Constants;

class GenericProtocol : public HeatPumpProtocolBase {
public:
    GenericProtocol()
        : HeatPumpProtocolBase(HeatPumpProtocol::GENERIC)
        , m_probeAttempts(0)
        , m_maxProbeAttempts(6) {}

    CommunicationError detectModel(HeatPumpModelInfo& modelInfo) override {
        modelInfo = HeatPumpModelInfo::unknown();
        m_modelInfo = modelInfo;
        m_probeAttempts = 0;
        return CommunicationError::ok();
    }

    [[nodiscard]] ProtocolCapabilities getCapabilities() const override {
        return {true, false, true, false, 0, 0};
    }

protected:
    CommunicationError onInit() override {
        m_probeAttempts = 0;
        return CommunicationError::ok();
    }

    CommunicationError sendQuery(const SensorMapping&) override {
        if (m_probeAttempts < m_maxProbeAttempts) {
            m_probeAttempts++;
            return CommunicationError::ok();
        }
        return CommunicationError{CommErrorCode::TIMEOUT,
                                   "Generic: max probe attempts reached", 0};
    }

    Frame readResponse(CommunicationError& outErr) override {
        if (m_probeAttempts <= 0) {
            outErr = CommunicationError::ok();
            return Frame{};
        }

        if (m_probeAttempts >= m_maxProbeAttempts) {
            outErr = CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                         "Generic: device not identified after " +
                                         std::to_string(m_maxProbeAttempts) + " probes", 0};
            return Frame{};
        }

        outErr = CommunicationError::ok();
        return Frame{};
    }

private:
    int m_probeAttempts;
    int m_maxProbeAttempts;
};
