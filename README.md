# Heat Pump Protocols

Header-only C++20 library for reading sensors and sending control commands to
heat pumps over serial UART, Modbus RTU, and CAN.

The library provides protocol framing, checksum validation, sensor catalogs,
and command handlers. It does not depend on Arduino, ESP-IDF, or any specific
operating system: implement `ICommunicationBus` for your platform and inject it
into the protocol layer.

> Independent implementation. Not affiliated with or endorsed by any heat pump
> manufacturer. Writing registers can change how a heat pump operates; verify
> behavior on your own equipment.

## Supported systems

| Brand | Models | Bus | Detection |
|---|---|---|---|
| IVT | Rego 800 | UART | `IVT_REGO800` |
| IVT | Rego 600 / 2-3000 | UART | `IVT_REGO600` |
| IVT | Rego 1000 / Pro Control 500 | UART | `IVT_REGO1000` |
| IVT / Regin | Rego 5200 | UART | `IVT_REGO5200` |
| NIBE | EB100 | UART | `NIBE_EB100` |
| NIBE | Styr2002 | UART | `NIBE_STYR2002` |
| Bosch / IVT | EMS / ProControl | UART | `BOSCH_EMS_PROCONTROL` |
| Thermia | Atlas / Calibra / Calibra E / Calibra Cool / Calibra RXT / Diplomat Inverter | Modbus RTU | `THERMIA_GENESIS` |
| Thermia | Diplomat / Villa Classic (Rego-style map) | Modbus RTU | `THERMIA_DIPLOMAT` |
| NIBE (rebranded Thermia) | Fighter 1130/1135/1140/1230/1235/1220 | Modbus RTU | `THERMIA_DIPLOMAT` |
| NIBE | RC100H thermostat emulation | CAN | `RC100H` emulation |

Write support depends on the heat pump model and configuration. Check
`HeatPumpModelInfo::writeEnabled` before sending commands.

## Features

- Automatic protocol detection through ordered detector factories.
- Sensor catalogs with lookup by numeric ID or name.
- Parsers for temperature, power, energy, runtime, speed, percentage, flow,
  boolean, integer, and float values.
- XOR8, CRC8, and CRC16 Modbus checksum implementations.
- Command handlers for boost, stop, holiday mode, electric heater, heating and
  hot-water setpoints, operating mode, fan speed, power limit, external ports,
  alarm reset, and timed register writes.
- RC100H CAN thermostat emulation with pairing, room temperature, humidity, and
  dial range control.
- Opt-in extended command handlers for bridge register maps (Rego 600,
  Rego 800, Rego 1000, NIBE EB100) and the official Thermia Mega/Genesis
  Modbus map.
- Full Thermia Atlas/Calibra Genesis Modbus protocol with a focused sensor
  catalog and writable holding-register setpoints.
- Header-only: no compiled library artifact is required.

## Research notes

`docs/RESEARCH_JOURNAL.md` is the single place for external source links,
register tables, transport caveats and verification status. It also tracks
known and new heat pump families that are still missing from the library,
including their current model names and bus types.

## Requirements

- C++20 or later
- `<span>`, `<variant>`, `<concepts>`, `<chrono>`, `<bit>`

## Using the library

Add `include/` to your include path, or use CMake:

```cmake
add_subdirectory(heatpump-protocols)
target_link_libraries(your_target PRIVATE heatpump::protocols)
```

### Read sensors

```cpp
#include <memory>
#include <vector>

#include "heatpump/HeatPumpManager.h"
#include "heatpump/ProtocolRegistry.h"

class MyUartBus : public ICommunicationBus {
    // Implement init/deinit/read/write/flush/drain for your platform.
};

int main() {
    HeatPumpManager manager([](BusType type) -> std::unique_ptr<ICommunicationBus> {
        if (type == BusType::UART) return std::make_unique<MyUartBus>();
        return nullptr;
    });

    registerAllProtocols(manager.getDetector());

    BusConfig config{};
    config.type = BusType::UART;
    config.baudRate = 9600;
    config.dataBits = 8;
    config.stopBits = 1;
    config.parity = 0;
    config.rxPin = 16;
    config.txPin = 17;
    config.inverted = false;
    config.timeoutMs = 1000;

    auto err = manager.addDevice(BusType::UART, config);
    if (!err.isOk()) return 1;

    std::vector<SensorValue> sensors;
    manager.pollAll(sensors);

    for (const auto& sensor : sensors) {
        // sensor.name, sensor.displayName, sensor.asFloat(), sensor.asInt()
    }

    manager.shutdownAll();
}
```

### Send commands

Use the command handler that matches the detected protocol, or pass a
`HeatPumpCommand` to `sendCommand()`.

```cpp
#include "heatpump/commands/RegoCommandHandler.h"
#include "heatpump/commands/NibeCommandHandler.h"
#include "heatpump/commands/EmsCommandHandler.h"
#include "heatpump/commands/ThermiaCommandHandler.h"

HeatPumpDevice* device = manager.getDevice(0);
RegoCommandHandler handler(device->getBus(), device->getProtocol());

handler.boostHeating(std::chrono::hours(2));
handler.setHeatingSetpoint(21.5f);   // degrees Celsius
handler.setWaterSetpoint(55.0f);     // degrees Celsius
handler.resetAlarm();
```

The generic `sendCommand()` form uses integer tenths of a degree for setpoints:

```cpp
HeatPumpCommand cmd{};
cmd.type = HeatPumpCommandType::SET_HEATING_SETPOINT;
cmd.value = 215; // 21.5 C
cmd.duration = std::chrono::seconds(0);
handler.sendCommand(cmd);
```

See [docs/COMMANDS.md](docs/COMMANDS.md) for the command matrix, wire formats,
and register mappings. For common protocol and API pitfalls, see
[docs/PROTOCOL_NOTES.md](docs/PROTOCOL_NOTES.md).

## Providing a bus implementation

For a cheap ESP32-WROOM-32 setup, see [docs/HARDWARE.md](docs/HARDWARE.md).

`ICommunicationBus` is the only platform-specific interface:

```cpp
class ICommunicationBus {
public:
    virtual CommunicationError init(const BusConfig& config) = 0;
    virtual CommunicationError deinit() = 0;
    virtual CommunicationError writeByte(uint8_t byte) = 0;
    virtual CommunicationError writeBytes(std::span<const uint8_t> data) = 0;
    virtual CommunicationError readByte(uint8_t& out,
                                        std::chrono::milliseconds timeout) = 0;
    virtual CommunicationError readBytes(std::span<uint8_t> buffer,
                                         size_t& bytesRead,
                                         std::chrono::milliseconds timeout) = 0;
    virtual size_t available() = 0;
    virtual CommunicationError flush() = 0;
    virtual CommunicationError drain() = 0;
    virtual bool isReady() const = 0;
    virtual BusType getType() const = 0;
    virtual const BusConfig& getConfig() const = 0;
};
```

A complete in-memory example is in
[examples/command_example.cpp](examples/command_example.cpp).

## Tests

Python unit tests cover checksums, frames, sensor mappings, and detection
strings. C++ integration tests cover the frame parser, validators, sensor
registry, and mock bus.

```bash
python3 tests/run_tests.py
```

If pytest is not installed, `run_tests.py` uses the built-in unit test runner.
pytest is used automatically when available.

Or run each layer separately:

```bash
python3 tests/run_tests.py --unit-only
python3 tests/run_tests.py --cpp-only
```

With CMake:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Project layout

```
AGENTS.md        start-here notes for coding agents and contributors
include/
  comm/          protocol framing, buses, sensors, protocols, RC100H emulation
  heatpump/      device management, detector, command handlers
  types/         sensor values, errors, model information, constants
  utils/         checksums and data converters
examples/        runnable command example
tests/           Python unit tests, C++ integration tests, mock bus
docs/            command, register, hardware, and protocol notes
```

## License

MIT. See [LICENSE](LICENSE).
