#pragma once

#include <span>
#include "SensorCatalog.h"
#include "ParserRegistry.h"

class SensorRegistry : public SensorCatalog {
public:
    void registerSensor(const SensorMapping& mapping) { add(mapping); }
    void registerSensors(std::span<const SensorMapping> mappings) { addAll(mappings); }

    void registerParser(SensorType type, ParseFunc func) {
        m_parsers.registerParser(type, std::move(func));
    }

    [[nodiscard]] ParseFunc getParser(SensorType type) const {
        return m_parsers.getParser(type);
    }

private:
    ParserRegistry m_parsers;
};
