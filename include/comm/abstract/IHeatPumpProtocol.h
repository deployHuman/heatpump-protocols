#pragma once

#include <vector>
#include <string>
#include <memory>
#include "comm/abstract/ICommunicationBus.h"
#include "types/SensorValue.h"
#include "types/HeatPumpModel.h"
#include "types/CommunicationError.h"

struct ProtocolCapabilities {
    bool supportsRead;
    bool supportsWrite;
    bool supportsAutoDetect;
    bool supportsLivePolling;
    size_t maxSensorCount;
    size_t maxCommandSize;
};

class IHeatPumpReader {
public:
    virtual ~IHeatPumpReader() = default;

    virtual CommunicationError pollSensors(std::vector<SensorValue>& sensors) = 0;
    virtual CommunicationError readSensor(uint16_t sensorId, SensorValue& value) = 0;
    virtual CommunicationError readSensorByName(const std::string& name, SensorValue& value) = 0;

    [[nodiscard]] virtual HeatPumpProtocol getProtocolType() const = 0;
    [[nodiscard]] virtual ProtocolCapabilities getCapabilities() const = 0;
    [[nodiscard]] virtual const HeatPumpModelInfo& getModelInfo() const = 0;
    [[nodiscard]] virtual bool isReady() const = 0;
};

class IHeatPumpWriter {
public:
    virtual ~IHeatPumpWriter() = default;
    virtual CommunicationError writeValue(uint16_t sensorId, const SensorValue& value) = 0;
};

class IHeatPumpCommander {
public:
    virtual ~IHeatPumpCommander() = default;
    virtual CommunicationError sendCommand(const std::string& command, std::string& response) = 0;
};

class IHeatPumpProtocol : public IHeatPumpReader,
                          public IHeatPumpWriter,
                          public IHeatPumpCommander {
public:
    virtual CommunicationError initialize(ICommunicationBus* bus) = 0;
    virtual CommunicationError shutdown() = 0;
    virtual CommunicationError detectModel(HeatPumpModelInfo& modelInfo) = 0;
};
