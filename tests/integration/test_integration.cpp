#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include "mocks/MockCommunicationBus.h"
#include "comm/frame/FrameValidator.h"
#include "comm/frame/FrameParser.h"
#include "comm/sensor/SensorRegistry.h"
#include "comm/sensor/SensorParser.h"
#include "types/SensorValue.h"
#include "utils/CrcCalculator.h"

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    do { \
        std::cout << "  " << name << "... "; \
        std::cout.flush(); \
    } while(0)

#define PASS() \
    do { \
        std::cout << "PASS" << std::endl; \
        testsPassed++; \
    } while(0)

#define FAIL(msg) \
    do { \
        std::cout << "FAIL: " << msg << std::endl; \
        testsFailed++; \
    } while(0)

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { FAIL(msg); return; } \
    } while(0)

#define ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            char buf[256]; \
            snprintf(buf, sizeof(buf), "%s (got %d, expected %d)", msg, (int)(a), (int)(b)); \
            FAIL(buf); return; \
        } \
    } while(0)

static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

void test_xor8_checksum() {
    TEST("XOR8 checksum matches protocol vectors");

    uint8_t result = Utils::xorChecksum(hexToBytes("0206000003"));
    ASSERT_EQ(result, 0x07, "XOR8(0206000003)");

    result = Utils::xorChecksum(hexToBytes("0206000203"));
    ASSERT_EQ(result, 0x05, "XOR8(0206000203)");

    result = Utils::xorChecksum(hexToBytes("0206000A03"));
    ASSERT_EQ(result, 0x0D, "XOR8(0206000A03)");

    result = Utils::xorChecksum({});
    ASSERT_EQ(result, 0x00, "XOR8(empty)");

    PASS();
}

void test_crc16_modbus() {
    TEST("CRC16 Modbus matches standard vectors");

    uint16_t result = Utils::crc16Modbus(hexToBytes("010300000002"));
    ASSERT_EQ(result, 0x0BC4, "CRC16(010300000002)");

    result = Utils::crc16Modbus({});
    ASSERT_EQ(result, 0xFFFF, "CRC16(empty)");

    PASS();
}

void test_xor8_frame_validator() {
    TEST("XOR8 frame validator");

    Xor8Validator validator;
    Frame frame;
    frame.address = 0x02;
    frame.command = 0x06;
    frame.data = {0x00, 0x00, 0x03};
    frame.checksum = 0x07;

    auto err = validator.validate(frame);
    ASSERT_TRUE(err.isOk(), "Valid frame should pass");

    frame.checksum = 0xFF;
    err = validator.validate(frame);
    ASSERT_TRUE(!err.isOk(), "Invalid checksum should fail");
    ASSERT_EQ(err.code, CommErrorCode::CHECKSUM_MISMATCH, "Error code");

    PASS();
}

void test_crc16_frame_validator() {
    TEST("CRC16 Modbus frame validator");

    Crc16ModbusValidator validator;
    Frame frame;
    frame.address = 0x01;
    frame.command = 0x03;
    frame.data = {0x00, 0x00, 0x00, 0x02};

    std::vector<uint8_t> testData = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    uint16_t computed = validator.compute(testData);
    ASSERT_EQ(computed, 0x0BC4, "CRC16 compute");

    frame.checksum = computed;
    auto err = validator.validate(frame);
    ASSERT_TRUE(err.isOk(), "Valid CRC16 frame should pass");

    PASS();
}

void test_frame_parser() {
    TEST("Frame parser state machine");

    auto validator = std::make_unique<Xor8Validator>();
    FrameParser parser(std::move(validator));

    std::vector<uint8_t> frame = hexToBytes("020602EC00EA");
    for (auto b : frame) {
        parser.feedByte(b);
    }

    const auto& state = parser.getState();
    ASSERT_TRUE(state.currentFrame.valid, "Parsed frame should be valid");
    ASSERT_EQ(state.currentFrame.address, 0x02, "Frame address");
    ASSERT_EQ(state.currentFrame.command, 0x06, "Frame command");

    PASS();
}

void test_sensor_registry_lookup() {
    TEST("Sensor registry lookup by ID and name");

    SensorRegistry registry;
    SensorMapping mapping;
    mapping.sensorId = 0;
    mapping.name = "outdoor_temp";
    mapping.displayName = "Outdoor Temperature";
    mapping.type = SensorType::TEMPERATURE;
    mapping.unit = SensorUnit::CELSIUS;
    mapping.registerAddress = 0x0000;
    mapping.dataLength = 2;
    mapping.scale = 0.1f;
    mapping.offset = -50.0f;
    mapping.writable = false;
    registry.registerSensor(mapping);

    const auto* found = registry.getById(0);
    ASSERT_TRUE(found != nullptr, "Sensor lookup by ID 0");
    ASSERT_TRUE(found->name == "outdoor_temp", "Sensor name match");

    found = registry.getByName("outdoor_temp");
    ASSERT_TRUE(found != nullptr, "Sensor lookup by name");
    ASSERT_EQ(found->sensorId, 0, "Sensor ID via name");

    found = registry.getByName("nonexistent");
    ASSERT_TRUE(found == nullptr, "Nonexistent sensor returns null");

    auto ids = registry.getAllIds();
    ASSERT_EQ(ids.size(), 1, "Registry count");

    PASS();
}

void test_sensor_temperature_parsing() {
    TEST("Temperature sensor parsing matches protocol values");

    SensorRegistry registry;
    SensorMapping mapping;
    mapping.sensorId = 0;
    mapping.name = "outdoor_temp";
    mapping.displayName = "Outdoor Temperature";
    mapping.type = SensorType::TEMPERATURE;
    mapping.unit = SensorUnit::CELSIUS;
    mapping.registerAddress = 0x0000;
    mapping.dataLength = 2;
    mapping.scale = 0.1f;
    mapping.offset = -50.0f;
    mapping.writable = false;
    registry.registerSensor(mapping);

    registry.registerParser(SensorType::TEMPERATURE, SensorParser::parseTemperature);

    std::vector<uint8_t> raw = hexToBytes("EC00DC0032000000");
    auto parser = registry.getParser(SensorType::TEMPERATURE);
    ASSERT_TRUE(parser != nullptr, "Parser registered");

    auto sv = parser(raw, 0, mapping);
    ASSERT_TRUE(!sv.isEmpty(), "Value should not be empty");

    float expect = (0xEC | (0x00 << 8)) * 0.1f - 50.0f;
    float got = sv.asFloat();
    ASSERT_TRUE(std::abs(got - expect) < 0.01f, "Temperature value");

    PASS();
}

void test_mock_bus_scenario() {
    TEST("Mock bus replay scenario");

    MockCommunicationBus bus;
    BusConfig config{};
    config.type = BusType::UART;
    config.baudRate = 9600;
    config.timeoutMs = 500;

    auto err = bus.init(config);
    ASSERT_TRUE(err.isOk(), "Mock bus init");

    bus.loadScenario({
        {hexToBytes("AABB"), hexToBytes("02"), "Simple ping-pong"}
    });

    err = bus.writeBytes(hexToBytes("AABB"));
    ASSERT_TRUE(err.isOk(), "TX write");

    uint8_t byte;
    err = bus.readByte(byte, std::chrono::milliseconds(100));
    ASSERT_TRUE(err.isOk(), "RX first byte");
    ASSERT_EQ(byte, 0x02, "First byte should be 0x02");

    ASSERT_TRUE(bus.getPairsConsumed() >= 1, "At least one pair consumed");

    PASS();
}

void test_mock_bus_tx_mismatch_detection() {
    TEST("Mock bus detects TX mismatch");

    MockCommunicationBus bus;
    BusConfig config{};
    bus.init(config);

    bus.loadScenario({
        {hexToBytes("AA"), hexToBytes("BB"), "Expected AA"}
    });

    auto err = bus.writeByte(0xFF);
    ASSERT_TRUE(!err.isOk(), "TX mismatch should fail");
    ASSERT_TRUE(err.code == CommErrorCode::PROTOCOL_MISMATCH, "Error code should be PROTOCOL_MISMATCH");

    PASS();
}

int main() {
    std::cout << "\n=== HP Communication Library - Integration Tests ===\n" << std::endl;

    test_xor8_checksum();
    test_crc16_modbus();
    test_xor8_frame_validator();
    test_crc16_frame_validator();
    test_frame_parser();
    test_sensor_registry_lookup();
    test_sensor_temperature_parsing();
    test_mock_bus_scenario();
    test_mock_bus_tx_mismatch_detection();

    std::cout << "\n=== Results: " << testsPassed << " passed, "
              << testsFailed << " failed ===\n" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
