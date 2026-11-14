#pragma once

#include <vector>
#include <memory>
#include <functional>
#include "HeatPumpDevice.h"
#include "HeatPumpDetector.h"
#include "comm/abstract/ICommunicationBus.h"
#include "types/SensorValue.h"
#include "types/CommunicationError.h"

class HeatPumpManager {
public:
    using BusFactory = std::function<std::unique_ptr<ICommunicationBus>(BusType)>;

    explicit HeatPumpManager(BusFactory busFactory)
        : m_busFactory(std::move(busFactory)) {}

    HeatPumpManager(HeatPumpManager&&) = default;
    HeatPumpManager& operator=(HeatPumpManager&&) = default;

    HeatPumpManager(const HeatPumpManager&) = delete;
    HeatPumpManager& operator=(const HeatPumpManager&) = delete;

    CommunicationError addDevice(BusType busType, const BusConfig& config) {
        return addDeviceInternal(busType, config, nullptr);
    }

    CommunicationError addDeviceByProtocol(HeatPumpProtocol protocol,
                                            BusType busType, const BusConfig& config) {
        return addDeviceInternal(busType, config, &protocol);
    }

    CommunicationError pollAll(std::vector<SensorValue>& allSensors) {
        allSensors.clear();
        CommunicationError lastErr = CommunicationError::ok();

        for (auto& device : m_devices) {
            if (!device.isReady()) continue;
            std::vector<SensorValue> deviceSensors;
            auto err = device.poll(deviceSensors);
            if (!err.isOk()) lastErr = err;
            allSensors.insert(allSensors.end(), deviceSensors.begin(), deviceSensors.end());
        }

        return lastErr;
    }

    CommunicationError pollDevice(size_t index, std::vector<SensorValue>& sensors) {
        if (index >= m_devices.size()) {
            return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                       "Device index out of range", 0};
        }
        return m_devices[index].poll(sensors);
    }

    CommunicationError writeSensor(size_t deviceIndex, uint16_t sensorId,
                                    const SensorValue& value) {
        if (deviceIndex >= m_devices.size()) {
            return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                       "Device index out of range", 0};
        }
        return m_devices[deviceIndex].writeSensor(sensorId, value);
    }

    CommunicationError writeSensorByName(size_t deviceIndex, const std::string& name,
                                          const SensorValue& value) {
        if (deviceIndex >= m_devices.size()) {
            return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                       "Device index out of range", 0};
        }
        return m_devices[deviceIndex].writeSensorByName(name, value);
    }

    CommunicationError shutdownAll() {
        CommunicationError lastErr = CommunicationError::ok();
        for (auto& device : m_devices) {
            auto err = device.shutdown();
            if (!err.isOk()) lastErr = err;
        }
        m_devices.clear();
        return lastErr;
    }

    [[nodiscard]] size_t deviceCount() const { return m_devices.size(); }

    [[nodiscard]] HeatPumpDevice* getDevice(size_t index) {
        if (index >= m_devices.size()) return nullptr;
        return &m_devices[index];
    }

    [[nodiscard]] const std::vector<HeatPumpDevice>& getDevices() const { return m_devices; }

    HeatPumpDetector& getDetector() { return m_detector; }
    const HeatPumpDetector& getDetector() const { return m_detector; }

    [[nodiscard]] std::vector<HeatPumpModelInfo> getAllModelInfos() const {
        std::vector<HeatPumpModelInfo> infos;
        infos.reserve(m_devices.size());
        for (const auto& device : m_devices) {
            infos.push_back(device.getModelInfo());
        }
        return infos;
    }

private:
    CommunicationError addDeviceInternal(BusType busType, const BusConfig& config,
                                          const HeatPumpProtocol* protocol) {
        auto bus = m_busFactory(busType);
        if (!bus) {
            return CommunicationError{CommErrorCode::BUS_NOT_READY,
                                       "Failed to create communication bus", 0};
        }

        HeatPumpModelInfo modelInfo;
        std::unique_ptr<IHeatPumpProtocol> proto;
        CommunicationError err = bus->init(config);
        if (!err.isOk()) return err;

        if (protocol) {
            err = m_detector.detectByProtocol(*protocol, bus.get(), modelInfo, proto);
        } else {
            err = m_detector.detect(bus.get(), modelInfo, proto);
        }

        if (!err.isOk()) return err;

        err = proto->initialize(bus.get());
        if (!err.isOk()) return err;

        m_devices.emplace_back(std::move(bus), std::move(proto));
        return CommunicationError::ok();
    }

    BusFactory m_busFactory;
    HeatPumpDetector m_detector;
    std::vector<HeatPumpDevice> m_devices;
};
