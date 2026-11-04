#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <initializer_list>
#include <span>
#include "comm/frame/Frame.h"
#include "types/SensorValue.h"

struct SensorMapping {
    uint16_t sensorId;
    std::string name;
    std::string displayName;
    SensorType type;
    SensorUnit unit;
    uint16_t registerAddress;
    uint8_t dataLength;
    float scale;
    float offset;
    bool writable;
    uint8_t readFunction = 0x03;
};

class SensorCatalog {
public:
    void add(const SensorMapping& mapping) {
        m_sensors[mapping.sensorId] = mapping;
        m_sensorsByName[mapping.name] = mapping.sensorId;
    }

    void addAll(std::span<const SensorMapping> mappings) {
        for (const auto& m : mappings) add(m);
    }

    void addAll(std::initializer_list<SensorMapping> mappings) {
        for (const auto& m : mappings) add(m);
    }

    [[nodiscard]] const SensorMapping* getById(uint16_t id) const {
        auto it = m_sensors.find(id);
        return it != m_sensors.end() ? &it->second : nullptr;
    }

    [[nodiscard]] const SensorMapping* getByName(const std::string& name) const {
        auto it = m_sensorsByName.find(name);
        return it != m_sensorsByName.end() ? getById(it->second) : nullptr;
    }

    [[nodiscard]] std::vector<uint16_t> getAllIds() const {
        std::vector<uint16_t> ids;
        ids.reserve(m_sensors.size());
        for (const auto& [id, _] : m_sensors) ids.push_back(id);
        return ids;
    }

    [[nodiscard]] size_t count() const { return m_sensors.size(); }

private:
    std::unordered_map<uint16_t, SensorMapping> m_sensors;
    std::unordered_map<std::string, uint16_t> m_sensorsByName;
};
