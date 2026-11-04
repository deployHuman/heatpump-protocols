#pragma once

#include "comm/protocol/HeatPumpProtocolBase.h"
#include "types/Constants.h"
#include <span>
#include <chrono>

using namespace Constants;

class IvtRegoProtocolBase : public HeatPumpProtocolBase {
public:
    explicit IvtRegoProtocolBase(HeatPumpProtocol type,
                                  SensorCatalog sensorCatalog = {},
                                  ParserRegistry parserRegistry = {})
        : HeatPumpProtocolBase(type,
                                std::move(sensorCatalog),
                                std::move(parserRegistry)) {}

protected:
    CommunicationError sendQuery(const SensorMapping& mapping) override {
        if (!m_bus) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};

        std::vector<uint8_t> query;
        buildReadCommand(mapping.registerAddress, query);

        return m_bus->writeBytes(query);
    }

    Frame readResponse(CommunicationError& outErr) override {
        Frame frame;

        auto start = std::chrono::steady_clock::now();

        std::vector<uint8_t> response;
        response.reserve(Rego800::RESPONSE_BUFFER_RESERVE);

        while (true) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > Rego800::READ_TIMEOUT) {
                outErr = CommunicationError::timeout();
                return frame;
            }

            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                Rego800::READ_TIMEOUT - elapsed);

            uint8_t byte;
            auto err = m_bus->readByte(byte, remaining);
            if (!err.isOk()) {
                outErr = err;
                return frame;
            }

            response.push_back(byte);

            if (isResponseComplete(response)) {
                break;
            }
        }

        frame = parseRegoResponse(response, outErr);
        return frame;
    }

    virtual void buildReadCommand(uint16_t registerAddress, std::vector<uint8_t>& cmd) = 0;
    virtual bool isResponseComplete(const std::vector<uint8_t>& response) = 0;
    virtual Frame parseRegoResponse(const std::vector<uint8_t>& response,
                                     CommunicationError& outErr) = 0;
};
