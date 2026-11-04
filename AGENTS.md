# AGENTS.md

Guidance for coding agents and new contributors working in this repository.

## What this repository is

A header-only C++20 library for heat pump communication. It contains:

- Protocol framing, checksums, detection, and sensor polling.
- Sensor catalogs and parsers for the supported protocol families.
- Command handlers for boost, stop, holiday, electric heater, setpoints,
  operating mode, fan speed, power limit, external ports, alarm reset, and
  timed writes.
- An RC100H CAN thermostat emulation.
- An in-memory example and Python/C++ tests.

It is deliberately platform-independent. There is **no ESP32, Arduino, Linux,
or vendor-specific bus implementation in this repository yet**. That is the
main gap for a new user who wants to build a controller.

## Read these first

1. `README.md` — supported systems, usage, and build/test overview.
2. `docs/HARDWARE.md` — ESP32-WROOM-32 wiring, pins, and electrical notes.
3. `docs/COMMANDS.md` — command matrix, wire formats, and register mappings.
4. `docs/PROTOCOL_NOTES.md` — value encoding, write behavior, detection,
   polling, and electrical gotchas.
5. `examples/command_example.cpp` — how every command handler is used.
6. `include/heatpump/ProtocolRegistry.h` — registers all protocol factories.

## Current state and known gaps

Working today:

- All protocol classes and command handlers compile under C++20.
- `HeatPumpManager` can create a device, detect a protocol, and poll sensors.
- `registerAllProtocols()` registers the IVT Rego, NIBE, Bosch EMS, Thermia,
  and generic probe protocols.
- Python unit tests and C++ integration tests pass.

Missing for a real controller:

- Concrete `ICommunicationBus` implementations for UART, RS485, and CAN.
- A minimal ESP32 application that initializes a bus, registers protocols,
  polls sensors, and sends commands.
- A sensor reference document (the sensor catalogs are currently in headers).
- Wiring an actual command handler to `HeatPumpDevice`/`HeatPumpManager`
  convenience APIs. `HeatPumpDevice::sendCommand()` only works for protocols
  that implement `IHeatPumpCommander`; the current protocol classes do not.
  Use the handlers in `include/heatpump/commands/` directly.

## Repository layout

```
include/
  comm/
    abstract/       ICommunicationBus, IHeatPumpProtocol
    frame/          frame, parser, XOR8/CRC16 validators
    sensor/         sensor catalog, registry, parsers
    protocol/       Rego, NIBE, EMS, Thermia, generic protocols
    emulation/      RC100H CAN thermostat emulation
    storage/        IFileStorage
  heatpump/         manager, device, detector, command handlers
  types/            sensor values, errors, constants, model info
  utils/            checksums and data converters
examples/           command example with an in-memory bus
tests/              Python unit tests, C++ integration tests, mock bus
docs/               command and hardware documentation
```

## Build and test

Fastest check:

```bash
python3 tests/run_tests.py
```

This runs the Python unit tests and the C++ integration tests. If `pytest` is
not installed, the runner falls back to `tests/run_unit_tests.py`.

Individual layers:

```bash
python3 tests/run_tests.py --unit-only
python3 tests/run_tests.py --cpp-only
```

Compile the library and example directly:

```bash
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror \
  -I include -o /tmp/command_example examples/command_example.cpp
/tmp/command_example
```

CMake build:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Core API

`ICommunicationBus` is the only platform-specific interface. A bus must
implement:

```cpp
CommunicationError init(const BusConfig& config);
CommunicationError deinit();
CommunicationError writeByte(uint8_t byte);
CommunicationError writeBytes(std::span<const uint8_t> data);
CommunicationError readByte(uint8_t& out, std::chrono::milliseconds timeout);
CommunicationError readBytes(std::span<uint8_t> buffer, size_t& bytesRead,
                             std::chrono::milliseconds timeout);
size_t available();
CommunicationError flush();
CommunicationError drain();
bool isReady() const;
BusType getType() const;
const BusConfig& getConfig() const;
```

`HeatPumpManager` owns one or more devices. The normal flow is:

1. Create a bus factory.
2. Call `registerAllProtocols(manager.getDetector())`.
3. Fill a `BusConfig` and call `manager.addDevice(...)`.
4. Call `manager.pollAll(sensors)` in a loop.
5. Use a command handler with `device->getBus()` and `device->getProtocol()`.

`GenericProtocol` must stay last in the detector order because its
`detectModel()` always succeeds.

## Implementing an ESP32 bus

Suggested new layout:

```text
platform/esp32/
  EspUartBus.h / EspUartBus.cpp
  EspModbusBus.h
  EspCanBus.h / EspCanBus.cpp
examples/esp32_uart/
  platformio.ini
  src/main.cpp
```

Use the existing `ICommunicationBus` interface and `BusConfig` fields.

### UART adapter checklist

- Use `HardwareSerial` (`Serial2` is the reference default).
- Pins: RX `GPIO16`, TX `GPIO17`, GND common.
- Avoid `GPIO1`/`GPIO3` (USB console) and `GPIO9`/`GPIO10` (flash).
- Call `begin(baudRate, SERIAL_8N1, rxPin, txPin)`.
- Implement `readByte()` and `readBytes()` with a real timeout.
- Implement `flush()` and `drain()` so stale bytes do not corrupt the next
  command.
- Support inverted UART with `uart_set_line_inverse()` when
  `BusConfig::inverted` is true. Some IVT Rego ports require this.
- Default baud is 9600; Rego 5200 uses 115200.
- ESP32 GPIOs are 3.3 V. Do not connect 5 V logic directly.
- `NibeStyr2002Protocol` optionally uses `setFileStorage()` for STYR-mode
  persistence; provide an `IFileStorage` implementation if you need it.

### Modbus RTU / RS485

`EspModbusBus` can inherit the UART adapter, but RS485 needs direction control:

- Use an auto-direction RS485 module, or
- Add a DE/RE GPIO and toggle it around each write.

The current reference adapter does not control DE/RE. Add 120 ohm termination
only when the bus requires it.

### CAN / RC100H

The ESP32 has a TWAI controller but no physical transceiver. Add a 3.3 V
transceiver such as SN65HVD230:

- TX `GPIO21`, RX `GPIO22`.
- CANH/CANL to the heat pump.
- 120 ohm termination if required.
- The RC100H emulation is not a normal protocol: instantiate
  `Rc100HEmulation` with a CAN bus and use `Rc100HCommandHandler`.

## Minimal application flow

A first ESP32 application can stay very small:

```cpp
// Pseudocode: adapt to the concrete bus class.
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

HeatPumpManager manager(busFactory);
registerAllProtocols(manager.getDetector());

if (!manager.addDevice(BusType::UART, config).isOk()) {
    // Print the error and check wiring.
}

std::vector<SensorValue> sensors;
manager.pollAll(sensors);

auto* device = manager.getDevice(0);
RegoCommandHandler handler(device->getBus(), device->getProtocol());
handler.setHeatingSetpoint(21.5f);
```

Use `HeatPumpCommand` when commands come from MQTT, a web API, or a CLI.
Setpoints are tenths of a degree in the generic command form (`215` = 21.5 C).
Typed helpers accept degrees Celsius directly.

## Next tasks in recommended order

1. Add `platform/esp32/EspUartBus.{h,cpp}` and verify it against the mock
   pattern in `tests/mocks/MockCommunicationBus.h`.
2. Add `examples/esp32_uart/` with `platformio.ini` and a minimal `main.cpp`.
3. Add `EspModbusBus` with a documented DE/RE strategy.
4. Add `EspCanBus` using the ESP32 TWAI driver and a 3.3 V transceiver.
5. Add a sensor reference document generated from the sensor catalogs.
6. Optionally add a command-handler factory so `HeatPumpDevice` can expose
   commands without callers constructing each handler manually.

## Conventions and guardrails

- C++20. Keep `include/` platform-independent.
- Keep platform code under `platform/`; keep runnable demos under `examples/`.
- Preserve `#pragma once`, header-only library style, and existing include
  paths.
- Build with `-Wall -Wextra -Wpedantic -Werror`; warnings are errors in the
  provided build targets.
- Do not break the existing Python or C++ tests. Add tests for new bus
  behavior, framing, and command paths.
- Keep the public repository self-contained and product-neutral. Do not add
  private paths, credentials, third-party binaries, or references to
  non-public development material.
- Do not commit build output, `.pio/`, `build/`, `__pycache__/`, or generated
  binaries.
- Logging: no secrets, WiFi passwords, MQTT credentials, or raw user data.

## Troubleshooting

- **Detection fails:** verify RX/TX are crossed, GND is common, baud is correct,
  and the correct pins are configured. Try a read-only protocol first.
- **Garbage bytes / checksum errors:** check inversion, logic level, baud, and
  ground integrity.
- **Writes rejected:** confirm the model reports `writeEnabled`, confirm the
  register, and check that the heat pump accepts writes on that service port.
- **No response after a while:** call `flush()`/`drain()` before the next
  transaction and check for a stuck UART driver.
- **RS485 echoes:** direction control is wrong; add or fix DE/RE handling.
- **CAN errors:** check transceiver power, CANH/CANL polarity, and termination.
