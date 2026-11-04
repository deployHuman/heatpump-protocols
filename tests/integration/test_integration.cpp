#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include "mocks/MockCommunicationBus.h"
#include "comm/frame/FrameValidator.h"
#include "comm/frame/FrameParser.h"
#include "comm/protocol/thermia/ThermiaGenesisProtocol.h"
#include "comm/sensor/SensorRegistry.h"
#include "comm/sensor/SensorParser.h"
#include "heatpump/commands/BridgeTemperatureEncoding.h"
#include "heatpump/commands/NibeEb100ExtendedCommandHandler.h"
#include "heatpump/commands/Rego600ExtendedCommandHandler.h"
#include "heatpump/commands/Rego800ExtendedCommandHandler.h"
#include "heatpump/commands/Rego1000ExtendedCommandHandler.h"
#include "heatpump/commands/ThermiaCommandHandler.h"
#include "heatpump/commands/ThermiaGenesisCommandHandler.h"
#include "heatpump/HeatPumpManager.h"
#include "heatpump/ProtocolRegistry.h"
#include "types/SensorValue.h"
#include "utils/CrcCalculator.h"
#include "utils/ModbusRtu.h"

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

static std::vector<uint8_t> regoWriteFrame(uint16_t regAddr, uint16_t value) {
    std::vector<uint8_t> frame = {
        0x0B, 0x90, 0xFF,
        static_cast<uint8_t>(regAddr >> 8),
        static_cast<uint8_t>(regAddr & 0xFF),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value & 0xFF),
    };
    frame.push_back(Utils::xorChecksum(frame));
    return frame;
}

static std::vector<uint8_t> modbusFrame(uint8_t slave, uint8_t function,
                                        uint16_t address, uint16_t value) {
    std::vector<uint8_t> frame = {
        slave, function,
        static_cast<uint8_t>(address >> 8),
        static_cast<uint8_t>(address & 0xFF),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value & 0xFF),
    };
    uint16_t crc = Utils::crc16Modbus(frame);
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    return frame;
}

static std::vector<uint8_t> modbusReadRequest(uint8_t slave, uint8_t function,
                                              uint16_t address, uint16_t count) {
    return ModbusRtu::buildReadRequestFrame(slave, function, address, count);
}

static std::vector<uint8_t> modbusReadResponse(uint8_t slave, uint8_t function,
                                               const std::vector<uint8_t>& data) {
    std::vector<uint8_t> frame = {
        slave, function, static_cast<uint8_t>(data.size())
    };
    frame.insert(frame.end(), data.begin(), data.end());
    uint16_t crc = Utils::crc16Modbus(frame);
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    return frame;
}

static bool initMockBus(MockCommunicationBus& bus) {
    BusConfig config{};
    config.type = BusType::UART;
    config.baudRate = 9600;
    config.timeoutMs = 500;
    return bus.init(config).isOk();
}

void test_rego600_extended_frames() {
    TEST("Rego 600 extended command frames");

    MockCommunicationBus bus;
    ASSERT_TRUE(initMockBus(bus), "Mock bus init");
    Rego600ExtendedCommandHandler handler(&bus, nullptr);

    auto setpoint = regoWriteFrame(0x0107, 215);
    bus.loadScenario({{setpoint, {0x06}, "heating setpoint"}});
    auto err = handler.setHeatingSetpoint(21.5f);
    ASSERT_TRUE(err.isOk(), "Heating setpoint write");
    ASSERT_TRUE(bus.getTxLog() == setpoint, "Heating setpoint frame");

    auto extraWw = regoWriteFrame(0x7209, 90);
    bus.loadScenario({{extraWw, {0x06}, "extra warm water"}});
    err = handler.setExtraWarmWater(std::chrono::minutes(90));
    ASSERT_TRUE(err.isOk(), "Extra warm water write");
    ASSERT_TRUE(bus.getTxLog() == extraWw, "Extra warm water frame");

    auto heater = regoWriteFrame(0x1215, 1);
    bus.loadScenario({{heater, {0x06}, "electric heater"}});
    err = handler.setElectricHeaterEnabled(true);
    ASSERT_TRUE(err.isOk(), "Electric heater write");
    ASSERT_TRUE(bus.getTxLog() == heater, "Electric heater frame");

    auto reset = regoWriteFrame(0x12F2, 0);
    bus.loadScenario({{reset, {0x06}, "alarm reset"}});
    err = handler.resetAlarm();
    ASSERT_TRUE(err.isOk(), "Alarm reset write");
    ASSERT_TRUE(bus.getTxLog() == reset, "Alarm reset frame");

    auto holiday = regoWriteFrame(0x2210, 30);
    bus.loadScenario({{holiday, {0x06}, "holiday mode"}});
    err = handler.setHolidayMode(true);
    ASSERT_TRUE(err.isOk(), "Holiday mode write");
    ASSERT_TRUE(bus.getTxLog() == holiday, "Holiday mode frame");

    PASS();
}

void test_rego800_extended_frames() {
    TEST("Rego 800 extended command frames");

    MockCommunicationBus bus;
    ASSERT_TRUE(initMockBus(bus), "Mock bus init");
    Rego800ExtendedCommandHandler handler(&bus, nullptr);

    auto wwStop = regoWriteFrame(0x0208, 600);
    bus.loadScenario({{wwStop, {0x06}, "warm water stop"}});
    auto err = handler.setWarmWaterStopTemp(60.0f);
    ASSERT_TRUE(err.isOk(), "Warm water stop write");
    ASSERT_TRUE(bus.getTxLog() == wwStop, "Warm water stop frame");

    Rego800ExtendedCommandHandler rawHandler(
        &bus, nullptr, BridgeTemperatureEncoding::PumpRawOffset50);
    auto rawWwStop = regoWriteFrame(0x0208, 1100);
    bus.loadScenario({{rawWwStop, {0x06}, "warm water stop raw"}});
    err = rawHandler.setWarmWaterStopTemp(60.0f);
    ASSERT_TRUE(err.isOk(), "Raw warm water stop write");
    ASSERT_TRUE(bus.getTxLog() == rawWwStop, "Raw warm water stop frame");

    auto heater = regoWriteFrame(0x921B, 6);
    bus.loadScenario({{heater, {0x06}, "electric heater limit"}});
    err = handler.setElectricHeaterMaxPowerKw(6);
    ASSERT_TRUE(err.isOk(), "Electric heater limit write");
    ASSERT_TRUE(bus.getTxLog() == heater, "Electric heater limit frame");

    PASS();
}

void test_rego1000_extended_frames() {
    TEST("Rego 1000 extended command frames");

    MockCommunicationBus bus;
    ASSERT_TRUE(initMockBus(bus), "Mock bus init");
    Rego1000ExtendedCommandHandler handler(&bus, nullptr);

    auto extraWw = regoWriteFrame(0x6209, 2);
    bus.loadScenario({{extraWw, {0x06}, "extra warm water"}});
    auto err = handler.setExtraWarmWater(std::chrono::hours(2));
    ASSERT_TRUE(err.isOk(), "Extra warm water write");
    ASSERT_TRUE(bus.getTxLog() == extraWw, "Extra warm water frame");

    auto setpoint = regoWriteFrame(0x0107, 215);
    bus.loadScenario({{setpoint, {0x06}, "heating setpoint"}});
    err = handler.setHeatingSetpoint(21.5f);
    ASSERT_TRUE(err.isOk(), "Heating setpoint write");
    ASSERT_TRUE(bus.getTxLog() == setpoint, "Heating setpoint frame");

    auto extPort = regoWriteFrame(0x2234, 1);
    bus.loadScenario({{extPort, {0x06}, "external control"}});
    err = handler.setExternalControl(2, true);
    ASSERT_TRUE(err.isOk(), "External control write");
    ASSERT_TRUE(bus.getTxLog() == extPort, "External control frame");

    auto reset = regoWriteFrame(0x22F2, 1);
    bus.loadScenario({{reset, {0x06}, "alarm reset"}});
    err = handler.resetAlarm();
    ASSERT_TRUE(err.isOk(), "Alarm reset write");
    ASSERT_TRUE(bus.getTxLog() == reset, "Alarm reset frame");

    PASS();
}

void test_nibe_eb100_extended_frames() {
    TEST("NIBE EB100 extended 16-bit command frames");

    MockCommunicationBus bus;
    ASSERT_TRUE(initMockBus(bus), "Mock bus init");
    NibeEb100ExtendedCommandHandler handler(&bus, nullptr);

    std::vector<uint8_t> heating = {
        0x38, 0x10, 0xFF, 0x00, 0x03, 0x2B, 0x07, 0x01, 0xD7, 0x00
    };
    bus.loadScenario({{heating, {0x06}, "heating setpoint"}});
    auto err = handler.setHeatingSetpoint(21.5f);
    ASSERT_TRUE(err.isOk(), "Heating setpoint write");
    ASSERT_TRUE(bus.getTxLog() == heating, "Heating setpoint frame");

    std::vector<uint8_t> mode = {
        0x38, 0x10, 0xFF, 0x00, 0x03, 0x2B, 0x01, 0x22, 0x02, 0x00
    };
    bus.loadScenario({{mode, {0x06}, "operating mode"}});
    err = handler.setOperatingMode(2);
    ASSERT_TRUE(err.isOk(), "Operating mode write");
    ASSERT_TRUE(bus.getTxLog() == mode, "Operating mode frame");

    std::vector<uint8_t> addHeat = {
        0x38, 0x10, 0xFF, 0x00, 0x03, 0x2B, 0x26, 0x92, 0x09, 0x00
    };
    bus.loadScenario({{addHeat, {0x06}, "max add heat"}});
    err = handler.setMaxAddHeat(9);
    ASSERT_TRUE(err.isOk(), "Max add heat write");
    ASSERT_TRUE(bus.getTxLog() == addHeat, "Max add heat frame");

    PASS();
}

void test_thermia_genesis_frames() {
    TEST("Thermia Genesis Modbus command frames");

    MockCommunicationBus bus;
    ASSERT_TRUE(initMockBus(bus), "Mock bus init");
    ThermiaGenesisCommandHandler handler(&bus, nullptr);

    auto reset = modbusFrame(0x01, 0x05, 3, 0xFF00);
    bus.loadScenario({{reset, reset, "reset alarms coil"}});
    auto err = handler.resetAlarm();
    ASSERT_TRUE(err.isOk(), "Reset alarm write");
    ASSERT_TRUE(bus.getTxLog() == reset, "Reset alarm frame");

    auto externalHeater = modbusFrame(0x01, 0x05, 5, 0xFF00);
    bus.loadScenario({{externalHeater, externalHeater, "external heater coil"}});
    err = handler.forceElectricHeater(true);
    ASSERT_TRUE(err.isOk(), "External heater write");
    ASSERT_TRUE(bus.getTxLog() == externalHeater, "External heater frame");

    auto maxRadiator = modbusFrame(0x01, 0x06, 3, 5500);
    bus.loadScenario({{maxRadiator, maxRadiator, "max radiator setpoint"}});
    err = handler.setMaxRadiatorSetpoint(55.0f);
    ASSERT_TRUE(err.isOk(), "Max radiator setpoint write");
    ASSERT_TRUE(bus.getTxLog() == maxRadiator, "Max radiator setpoint frame");

    auto fixedCoil = modbusFrame(0x01, 0x05, 41, 0xFF00);
    auto fixedValue = modbusFrame(0x01, 0x06, 116, 4500);
    bus.loadScenario({
        {fixedValue, fixedValue, "fixed supply setpoint"},
        {fixedCoil, fixedCoil, "fixed supply enable"},
    });
    err = handler.setFixedSupplySetpoint(45.0f);
    ASSERT_TRUE(err.isOk(), "Fixed supply write");
    std::vector<uint8_t> expected = fixedValue;
    expected.insert(expected.end(), fixedCoil.begin(), fixedCoil.end());
    ASSERT_TRUE(bus.getTxLog() == expected, "Fixed supply frames");

    PASS();
}

void test_thermia_default_modbus_echo() {
    TEST("Thermia default handler validates Modbus echo");

    MockCommunicationBus bus;
    ASSERT_TRUE(initMockBus(bus), "Mock bus init");
    ThermiaCommandHandler handler(&bus, nullptr);

    auto frame = modbusFrame(0x01, 0x06, 0x0107, 715);
    bus.loadScenario({{frame, frame, "heating setpoint"}});
    auto err = handler.setHeatingSetpoint(21.5f);
    ASSERT_TRUE(err.isOk(), "Heating setpoint write");
    ASSERT_TRUE(bus.getTxLog() == frame, "Heating setpoint frame");

    PASS();
}

void test_thermia_genesis_protocol() {
    TEST("Thermia Genesis protocol detection, read and write");

    MockCommunicationBus bus;
    ASSERT_TRUE(initMockBus(bus), "Mock bus init");

    ThermiaGenesisProtocol protocol;
    auto err = protocol.initialize(&bus);
    ASSERT_TRUE(err.isOk(), "Protocol init");

    auto versionRequest = modbusReadRequest(0x01, 0x04, 311, 3);
    auto versionResponse = modbusReadResponse(
        0x01, 0x04, {0x00, 0x11, 0x00, 0x01, 0x00, 0x00});
    bus.loadScenario({{versionRequest, versionResponse, "version"}});

    HeatPumpModelInfo info;
    err = protocol.detectModel(info);
    ASSERT_TRUE(err.isOk(), "Detection");
    ASSERT_TRUE(info.protocol == HeatPumpProtocol::THERMIA_GENESIS, "Protocol type");
    ASSERT_TRUE(info.version == "17.1.0", "Firmware version");
    ASSERT_TRUE(info.writeEnabled, "Writes enabled");

    auto readRequest = modbusReadRequest(0x01, 0x04, 13, 1);
    auto readResponse = modbusReadResponse(0x01, 0x04, {0x08, 0x66});
    bus.loadScenario({{readRequest, readResponse, "outdoor temperature"}});

    SensorValue value;
    err = protocol.readSensor(0, value);
    ASSERT_TRUE(err.isOk(), "Read sensor");
    ASSERT_TRUE(std::fabs(value.asFloat() - 21.5f) < 0.01f, "Outdoor temperature value");

    auto hoursRequest = modbusReadRequest(0x01, 0x04, 48, 2);
    auto hoursResponse = modbusReadResponse(
        0x01, 0x04, {0x00, 0x01, 0x00, 0x02});
    bus.loadScenario({{hoursRequest, hoursResponse, "compressor hours"}});
    err = protocol.readSensor(28, value);
    ASSERT_TRUE(err.isOk(), "Read 32-bit sensor");
    ASSERT_TRUE(std::fabs(value.asFloat() - 65538.0f) < 0.01f, "Compressor hours value");

    auto writeRequest = ModbusRtu::buildWriteSingleRegisterFrame(0x01, 3, 5500);
    bus.loadScenario({{writeRequest, writeRequest, "max radiator setpoint"}});

    SensorValue setpoint;
    setpoint.type = SensorType::TEMPERATURE;
    setpoint.unit = SensorUnit::CELSIUS;
    setpoint.rawValue = 55.0f;
    err = protocol.writeValue(30, setpoint);
    ASSERT_TRUE(err.isOk(), "Write sensor");
    ASSERT_TRUE(bus.getTxLog() == writeRequest, "Setpoint frame");

    auto poolWrite = ModbusRtu::buildWriteSingleRegisterFrame(0x01, 58, 5000);
    bus.loadScenario({{poolWrite, poolWrite, "pool charge"}});
    SensorValue poolValue;
    poolValue.type = SensorType::PERCENT;
    poolValue.unit = SensorUnit::PERCENT;
    poolValue.rawValue = 50.0f;
    err = protocol.writeValue(41, poolValue);
    ASSERT_TRUE(err.isOk(), "Write percent sensor");
    ASSERT_TRUE(bus.getTxLog() == poolWrite, "Pool charge frame");

    PASS();
}

void test_protocol_registry() {
    TEST("Protocol registry includes all protocol families");

    HeatPumpDetector detector;
    registerAllProtocols(detector);
    ASSERT_EQ(detector.count(), 10, "Registered protocol count");

    PASS();
}

void test_heatpump_manager_thermia_genesis() {
    TEST("HeatPumpManager detects Thermia Genesis");

    MockCommunicationBus* busPtr = nullptr;
    auto factory = [&busPtr](BusType) -> std::unique_ptr<ICommunicationBus> {
        auto bus = std::make_unique<MockCommunicationBus>();
        busPtr = bus.get();
        auto versionRequest = modbusReadRequest(0x01, 0x04, 311, 3);
        auto versionResponse = modbusReadResponse(
            0x01, 0x04, {0x00, 0x11, 0x00, 0x01, 0x00, 0x00});
        bus->loadScenario({{versionRequest, versionResponse, "version"}});
        return bus;
    };

    HeatPumpManager manager(factory);
    manager.getDetector().registerProtocol(
        HeatPumpProtocol::THERMIA_GENESIS,
        [] { return std::make_unique<ThermiaGenesisProtocol>(); },
        "Thermia Genesis");

    BusConfig config{};
    config.type = BusType::UART;
    auto err = manager.addDevice(BusType::UART, config);
    ASSERT_TRUE(err.isOk(), "Add device");
    ASSERT_EQ(manager.deviceCount(), 1, "Device count");
    ASSERT_TRUE(busPtr != nullptr, "Bus created");

    auto infos = manager.getAllModelInfos();
    ASSERT_TRUE(infos.size() == 1, "Model info count");
    ASSERT_TRUE(infos[0].protocol == HeatPumpProtocol::THERMIA_GENESIS, "Detected protocol");
    ASSERT_TRUE(infos[0].version == "17.1.0", "Detected version");

    auto writeRequest = ModbusRtu::buildWriteSingleRegisterFrame(0x01, 3, 5500);
    busPtr->loadScenario({{writeRequest, writeRequest, "manager setpoint"}});
    SensorValue setpoint;
    setpoint.type = SensorType::TEMPERATURE;
    setpoint.unit = SensorUnit::CELSIUS;
    setpoint.rawValue = 55.0f;
    err = manager.writeSensor(0, 30, setpoint);
    ASSERT_TRUE(err.isOk(), "Manager write sensor");
    ASSERT_TRUE(busPtr->getTxLog() == writeRequest, "Manager setpoint frame");

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
    test_rego600_extended_frames();
    test_rego800_extended_frames();
    test_rego1000_extended_frames();
    test_nibe_eb100_extended_frames();
    test_thermia_default_modbus_echo();
    test_protocol_registry();
    test_thermia_genesis_protocol();
    test_heatpump_manager_thermia_genesis();
    test_thermia_genesis_frames();

    std::cout << "\n=== Results: " << testsPassed << " passed, "
              << testsFailed << " failed ===\n" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
