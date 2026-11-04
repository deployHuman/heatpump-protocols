#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <functional>
#include "types/CommunicationError.h"

enum class FileMode : uint8_t {
    READ,
    WRITE,
    APPEND,
};

class IFileStorage {
public:
    virtual ~IFileStorage() = default;

    virtual CommunicationError init() = 0;
    virtual CommunicationError deinit() = 0;

    virtual CommunicationError writeFile(const std::string& path,
                                          std::span<const uint8_t> data) = 0;
    virtual CommunicationError writeText(const std::string& path,
                                          const std::string& text) = 0;

    virtual CommunicationError readFile(const std::string& path,
                                         std::vector<uint8_t>& data) = 0;
    virtual CommunicationError readText(const std::string& path,
                                         std::string& text) = 0;

    virtual CommunicationError appendText(const std::string& path,
                                           const std::string& text) = 0;

    virtual CommunicationError removeFile(const std::string& path) = 0;
    virtual CommunicationError fileExists(const std::string& path, bool& exists) = 0;
    virtual CommunicationError fileSize(const std::string& path, size_t& size) = 0;

    [[nodiscard]] virtual bool isReady() const = 0;
};

class InMemoryFileStorage : public IFileStorage {
public:
    CommunicationError init() override { return CommunicationError::ok(); }
    CommunicationError deinit() override { m_files.clear(); return CommunicationError::ok(); }

    CommunicationError writeFile(const std::string& path,
                                  std::span<const uint8_t> data) override {
        m_files[path] = std::vector<uint8_t>(data.begin(), data.end());
        return CommunicationError::ok();
    }

    CommunicationError writeText(const std::string& path,
                                  const std::string& text) override {
        m_files[path] = std::vector<uint8_t>(text.begin(), text.end());
        return CommunicationError::ok();
    }

    CommunicationError readFile(const std::string& path,
                                 std::vector<uint8_t>& data) override {
        auto it = m_files.find(path);
        if (it == m_files.end()) {
            return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                       "File not found: " + path, 0};
        }
        data = it->second;
        return CommunicationError::ok();
    }

    CommunicationError readText(const std::string& path,
                                 std::string& text) override {
        std::vector<uint8_t> data;
        auto err = readFile(path, data);
        if (!err.isOk()) return err;
        text.assign(data.begin(), data.end());
        return CommunicationError::ok();
    }

    CommunicationError appendText(const std::string& path,
                                   const std::string& text) override {
        auto& file = m_files[path];
        file.insert(file.end(), text.begin(), text.end());
        return CommunicationError::ok();
    }

    CommunicationError removeFile(const std::string& path) override {
        m_files.erase(path);
        return CommunicationError::ok();
    }

    CommunicationError fileExists(const std::string& path, bool& exists) override {
        exists = m_files.find(path) != m_files.end();
        return CommunicationError::ok();
    }

    CommunicationError fileSize(const std::string& path, size_t& size) override {
        auto it = m_files.find(path);
        if (it == m_files.end()) {
            return CommunicationError{CommErrorCode::DEVICE_NOT_FOUND,
                                       "File not found: " + path, 0};
        }
        size = it->second.size();
        return CommunicationError::ok();
    }

    [[nodiscard]] bool isReady() const override { return true; }

    [[nodiscard]] const std::unordered_map<std::string, std::vector<uint8_t>>&
    getFiles() const { return m_files; }

private:
    std::unordered_map<std::string, std::vector<uint8_t>> m_files;
};
