// Example: build and send control commands for each supported protocol family.
//
// The example uses a small in-memory bus that acknowledges every write. Replace
// AckBus with a real ICommunicationBus implementation (UART, Modbus, or CAN)
// when talking to hardware.

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "comm/abstract/ICommunicationBus.h"
#include "comm/emulation/Rc100HEmulation.h"
#include "heatpump/commands/EmsCommandHandler.h"
#include "heatpump/commands/NibeCommandHandler.h"
#include "heatpump/commands/NibeEb100ExtendedCommandHandler.h"
#include "heatpump/commands/Rc100HCommandHandler.h"
#include "heatpump/commands/Rego600ExtendedCommandHandler.h"
#include "heatpump/commands/Rego800ExtendedCommandHandler.h"
#include "heatpump/commands/Rego1000ExtendedCommandHandler.h"
#include "heatpump/commands/RegoCommandHandler.h"
#include "heatpump/commands/ThermiaCommandHandler.h"
#include "heatpump/commands/ThermiaGenesisCommandHandler.h"

class AckBus : public ICommunicationBus {
public:
    CommunicationError init(const BusConfig& config) override {
        m_config = config;
        m_ready = true;
        return CommunicationError::ok();
    }

    CommunicationError deinit() override {
        m_ready = false;
        return CommunicationError::ok();
    }

    CommunicationError writeByte(uint8_t byte) override {
        if (!m_ready) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};
        m_lastFrame.push_back(byte);
        return CommunicationError::ok();
    }

    CommunicationError writeBytes(std::span<const uint8_t> data) override {
        if (!m_ready) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};
        m_lastFrame.assign(data.begin(), data.end());
        m_frames.push_back(m_lastFrame);
        m_readPos = 0;
        return CommunicationError::ok();
    }

    CommunicationError readByte(uint8_t& out, std::chrono::milliseconds) override {
        if (!m_ready) return CommunicationError{CommErrorCode::BUS_NOT_READY, "Bus not ready", 0};
        if (isModbusWriteFrame() && m_readPos < m_lastFrame.size()) {
            out = m_lastFrame[m_readPos++];
            return CommunicationError::ok();
        }
        out = 0x06;
        return CommunicationError::ok();
    }

    CommunicationError readBytes(std::span<uint8_t>, size_t& bytesRead,
                                 std::chrono::milliseconds) override {
        bytesRead = 0;
        return CommunicationError::timeout("No buffered response");
    }

    size_t available() override { return 0; }
    CommunicationError flush() override { return CommunicationError::ok(); }
    CommunicationError drain() override { return CommunicationError::ok(); }

    [[nodiscard]] bool isReady() const override { return m_ready; }
    [[nodiscard]] BusType getType() const override { return m_config.type; }
    [[nodiscard]] const BusConfig& getConfig() const override { return m_config; }

    [[nodiscard]] const std::vector<std::vector<uint8_t>>& frames() const { return m_frames; }

    void clear() {
        m_lastFrame.clear();
        m_frames.clear();
        m_readPos = 0;
    }

private:
    [[nodiscard]] bool isModbusWriteFrame() const {
        return m_lastFrame.size() == 8 && m_lastFrame[0] == 0x01 &&
               (m_lastFrame[1] == 0x05 || m_lastFrame[1] == 0x06);
    }

    BusConfig m_config{};
    bool m_ready = false;
    std::vector<uint8_t> m_lastFrame;
    std::vector<std::vector<uint8_t>> m_frames;
    size_t m_readPos = 0;
};

static void printFrame(const std::string& label, const std::vector<uint8_t>& frame) {
    std::cout << std::left << std::setw(34) << label << " ";
    for (uint8_t byte : frame) {
        std::cout << std::right << std::hex << std::uppercase << std::setw(2)
                  << std::setfill('0') << static_cast<int>(byte) << " ";
    }
    std::cout << std::dec << std::setfill(' ') << "\n";
}

static void printLast(AckBus& bus, const std::string& label, const CommunicationError& err) {
    if (!err.isOk()) {
        std::cout << std::left << std::setw(34) << label
                  << " ERROR: " << err.message << "\n";
        return;
    }
    printFrame(label, bus.frames().back());
}

int main() {
    BusConfig config{};
    config.type = BusType::UART;
    config.baudRate = 9600;
    config.timeoutMs = 1000;

    AckBus bus;
    if (!bus.init(config).isOk()) {
        std::cerr << "Failed to initialize bus\n";
        return 1;
    }

    std::cout << "Rego (IVT Rego 800/600/1000/5200)\n";
    RegoCommandHandler rego(&bus, nullptr);
    printLast(bus, "boost heating 2h", rego.boostHeating(std::chrono::hours(2)));
    printLast(bus, "heating setpoint 21.5 C", rego.setHeatingSetpoint(21.5f));
    printLast(bus, "hot water setpoint 55 C", rego.setWaterSetpoint(55.0f));
    printLast(bus, "reset alarm", rego.resetAlarm());

    std::cout << "\nRego 800 extended (bridge register map)\n";
    Rego800ExtendedCommandHandler rego800(&bus, nullptr);
    printLast(bus, "room setpoint 22 C", rego800.setRoomSetpoint(22.0f));
    printLast(bus, "warm water stop 60 C", rego800.setWarmWaterStopTemp(60.0f));
    printLast(bus, "electric heater 6 kW", rego800.setElectricHeaterMaxPowerKw(6));

    std::cout << "\nRego 600 extended (bridge register map)\n";
    Rego600ExtendedCommandHandler rego600(&bus, nullptr);
    printLast(bus, "extra warm water 90 min",
              rego600.setExtraWarmWater(std::chrono::minutes(90)));
    printLast(bus, "electric heater on", rego600.setElectricHeaterEnabled(true));
    printLast(bus, "holiday 14 days", rego600.setHolidayDays(14));

    std::cout << "\nRego 1000 extended (bridge register map)\n";
    Rego1000ExtendedCommandHandler rego1000(&bus, nullptr);
    printLast(bus, "extra warm water 2 h",
              rego1000.setExtraWarmWater(std::chrono::hours(2)));
    printLast(bus, "external control 2 on", rego1000.setExternalControl(2, true));
    printLast(bus, "summer mode auto", rego1000.setSummerMode(1));

    std::cout << "\nNIBE (EB100 / Styr2002)\n";
    NibeCommandHandler nibe(&bus, nullptr);
    printLast(bus, "boost heating 2h", nibe.boostHeating(std::chrono::hours(2)));
    printLast(bus, "heating setpoint 21.5 C", nibe.setHeatingSetpoint(21.5f));
    printLast(bus, "fan speed 60%", nibe.setFanSpeed(60));
    printLast(bus, "reset alarm", nibe.resetAlarm());

    std::cout << "\nNIBE EB100 extended (16-bit register map)\n";
    NibeEb100ExtendedCommandHandler nibeEb100(&bus, nullptr);
    printLast(bus, "heating setpoint 21.5 C", nibeEb100.setHeatingSetpoint(21.5f));
    printLast(bus, "room setpoint 22 C", nibeEb100.setRoomSetpoint(22.0f));
    printLast(bus, "operating mode manual", nibeEb100.setOperatingMode(1));
    printLast(bus, "max add heat 9 kW", nibeEb100.setMaxAddHeat(9));

    std::cout << "\nBosch EMS (ProControl)\n";
    EmsCommandHandler ems(&bus, nullptr);
    printLast(bus, "boost heating 2h", ems.boostHeating(std::chrono::hours(2)));
    printLast(bus, "heating setpoint 21.5 C", ems.setHeatingSetpoint(21.5f));
    printLast(bus, "reset alarm", ems.resetAlarm());

    std::cout << "\nThermia (Modbus RTU)\n";
    ThermiaCommandHandler thermia(&bus, nullptr);
    printLast(bus, "boost heating 2h", thermia.boostHeating(std::chrono::hours(2)));
    printLast(bus, "heating setpoint 21.5 C", thermia.setHeatingSetpoint(21.5f));
    printLast(bus, "reset alarm", thermia.resetAlarm());

    std::cout << "\nThermia Genesis/Mega (official Modbus map)\n";
    ThermiaGenesisCommandHandler thermiaGenesis(&bus, nullptr);
    printLast(bus, "enable heating", thermiaGenesis.enableHeating(true));
    printLast(bus, "max radiator 55 C", thermiaGenesis.setMaxRadiatorSetpoint(55.0f));
    auto fixedErr = thermiaGenesis.setFixedSupplySetpoint(45.0f);
    if (fixedErr.isOk() && bus.frames().size() >= 2) {
        printFrame("fixed supply setpoint 45 C", bus.frames()[bus.frames().size() - 2]);
        printFrame("fixed supply enable", bus.frames().back());
    } else {
        printLast(bus, "fixed supply 45 C", fixedErr);
    }
    printLast(bus, "reset alarms", thermiaGenesis.resetAlarm());

    std::cout << "\nRC100H thermostat emulation (CAN)\n";
    Rc100HEmulation emulation;
    emulation.init(&bus);
    Rc100HCommandHandler rc100h(&emulation);
    printLast(bus, "thermostat mode RC100H",
              rc100h.setThermostatMode(Rc100H::Mode::RC100H));
    printLast(bus, "room temperature 21.5 C",
              rc100h.setRoomTemperature(1, 21.5f));
    printLast(bus, "room humidity 45%", rc100h.setRoomHumidity(45.0f));

    return 0;
}
