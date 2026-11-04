#pragma once

#include <unordered_map>
#include <functional>
#include "types/SensorValue.h"
#include "SensorCatalog.h"

using ParseFunc = std::function<SensorValue(const std::vector<uint8_t>& raw, size_t offset,
                                            const SensorMapping& mapping)>;

class ParserRegistry {
public:
    void registerParser(SensorType type, ParseFunc func) {
        m_parsers[type] = std::move(func);
    }

    [[nodiscard]] ParseFunc getParser(SensorType type) const {
        auto it = m_parsers.find(type);
        return it != m_parsers.end() ? it->second : nullptr;
    }

private:
    std::unordered_map<SensorType, ParseFunc> m_parsers;
};
