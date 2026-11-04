# Protocol notes and gotchas

Practical details that are easy to get wrong when building a controller on top
of this library. They follow the protocol implementations in `include/` and the
command handlers in `include/heatpump/commands/`.

## Values and units

- Temperature values are 16-bit little-endian with scale `0.1`. Most mappings
  use offset `-50.0`, so raw `0` means `-50.0 C`. Hot water and some room
  sensors use offset `0.0`. Always read the `SensorMapping` instead of
  assuming one formula for every temperature.
- Heating setpoints convert as `raw = (tempC + 50) * 10`. Hot water setpoints
  convert as `raw = tempC * 10`. Using the wrong formula writes a value that
  is tens of degrees off.
- Power values use scale `0.1` (kW). Energy values use scale `0.001` (kWh) and
  are 32-bit little-endian. Runtime values are 32-bit hours. Do not read
  4-byte values as 16-bit.
- Some protocols return sentinel or out-of-range values for unconnected
  sensors (for example `-853.1 C`). Filter impossible values before publishing
  them.
- `SensorValue::isEmpty()` can be true when a poll cycle was skipped or a frame
  was empty. Check it before using the value.
- `SensorValue::asFloat()` returns `0` for boolean, string, and empty values.
  It is not a validity check.

## Commands and writes

- Write support is model-specific. Check `HeatPumpModelInfo::writeEnabled`.
  In the current mappings, Rego 800/600 are read-only; Rego 1000/5200, NIBE,
  EMS, and Thermia are writable.
- `STOP_HEATING` with value `1` means "stop enabled". It is not "heating on".
- Boost duration is converted to whole hours with a minimum of 1. A 30-minute
  request becomes 1 hour on the wire.
- `HeatPumpCommand::value` is `uint16_t`. The generic setpoint form uses tenths
  of a degree and cannot represent negative temperatures. Use the typed
  helpers for negative or fractional values.
- `SET_EXT_PORT` uses `registerAddress` as the port number.
- `SET_ROOM_TEMPERATURE` uses `registerAddress` as the RC100H circuit.
- `SET_POWER_LIMIT` units differ by protocol: NIBE writes watts / 100 as a
  `uint8_t`; Rego writes watts directly as a `uint16_t`.
- Check `supportsCommand()` before `sendCommand()`. Unsupported commands
  return an error instead of being ignored.
- `SET_TIMER_OVERRIDE` records the requested duration but does not
  automatically restore the previous value. The caller must restore it.
- For critical writes, install `setReadBackCallback()` so `writeAndVerify()`
  re-reads the register after the acknowledgment. Some pumps acknowledge a
  write before applying it.

## Rego family

- Read request: `02 06 regHi regLo 03 xor8`.
- Write request: `0B 90 FF regHi regLo valHi valLo xor8`.
- Rego 800/600 are read-only in the current mappings; Rego 1000/5200 support
  writes.
- Rego 5200 runs at 115200 baud. Rego 800/600/1000 default to 9600.
- Rego 600 responses can be textual (`key=value,...`) rather than binary.
  Do not assume every Rego model returns the same frame layout.
- Some IVT Rego service ports use inverted TTL. Set `BusConfig::inverted` and
  make sure the bus adapter applies `uart_set_line_inverse()`.

## NIBE EB100 / Styr2002

- Writes use `38 10 FF 00 03 2B reg value` and do not carry an XOR byte.
- Boost is two writes: register `0x09` gets the hours, then register `0xD1`
  gets `1`.
- Reset with restart writes `0xF2 = 1` and then sends
  `38 10 FF 00 03 7B 00 00`.
- NIBE uses a longer read timeout (3 s). Do not reuse the 500 ms Rego timeout.
- STYR2002 mode persistence needs an `IFileStorage` via `setFileStorage()`.

## Bosch EMS / ProControl

- Read is `DC 01 regHi regLo len xor8`; write is `DC 02 ...`.
- Do not use the write form for reads.
- EMS has its own poll cadence and a smaller register set than NIBE.

## Thermia / Modbus RTU

- Read function is `0x03`; write function is `0x06`.
- CRC is CRC16 Modbus, transmitted low byte first.
- Default slave address is `0x01`.
- The implementation inserts a 5-poll cycle delay and disables the protocol
  after 3 consecutive errors. Do not poll it like a Rego device.
- A Modbus write response is an 8-byte echo. If your command path reads only a
  single ACK byte, drain the rest before the next transaction.

## RC100H

- RC100H is an active CAN emulation, not a normal polled protocol.
- Thermostat frame: `0B 90 FF 00 FF 01` followed by two temperatures and
  humidity, then XOR8.
- Pairing uses register `0x2F1`; value `0x01` pairs and `0x00` unpairs.
- The emulation interval is 10 s. Do not send frames continuously.
- The ESP32 needs a 3.3 V CAN transceiver and correct bus termination.

## Detection and polling

- Register `GenericProtocol` last. Its `detectModel()` always succeeds, so it
  will mask every protocol registered after it.
- Prefer an explicit protocol when the model is known. Auto-detection sends
  probe frames to the bus.
- Rebranded pumps exist: a NIBE-labelled unit can speak the Thermia protocol.
  Detect by response, not by the label on the case.
- Read timeouts differ: Rego 500 ms, Rego 5200 1 s, Thermia/EMS 2 s, NIBE 3 s.
- The base class retries timeouts and checksum errors up to 3 times and
  flushes/drains between attempts. A bus implementation must not keep stale
  bytes.
- A partial read is not a complete frame. Keep reading until the expected
  length is reached or the timeout expires.
- `pollSensors()` stops at the first sensor error. For a more resilient
  controller, call `readSensor()` per sensor or add per-sensor recovery.
- Run one transaction at a time per bus. `HeatPumpManager` and
  `HeatPumpDevice` are not thread-safe.

## Platform and electrical

- ESP32 GPIOs are 3.3 V and are not 5 V tolerant.
- RS485 needs direction control. The reference UART adapter does not toggle
  DE/RE; use an auto-direction module or add a DE/RE GPIO.
- CAN needs a physical transceiver (for example SN65HVD230), not just the
  ESP32 TWAI pins.
- Long sensor polls can trigger the task watchdog. Reset/feed the watchdog or
  yield between sensors.
- Avoid Arduino `String` churn and large temporary buffers. Preallocate and
  monitor free heap and minimum free heap.
- Do not write logs or configuration to flash on every poll; rate-limit flash
  writes to avoid wear.
- Use reconnect backoff for WiFi and MQTT loops instead of retrying with zero
  delay.
- Keep large buffers off the task stack and check stack high-water marks.
- If WiFi/MQTT/web run on the same controller, keep heat pump transactions in
  one task and guard the shared sensor cache.

## Scope and security

- Keep scheduling, pricing, and cloud telemetry in Home Assistant or another
  upstream system. A bridge should have one job.
- Do not add hardcoded credentials, remote support backdoors, or plaintext
  password fields to a web UI.
- Require authentication for write endpoints and validate/clamp command values.
- Prefer local MQTT with authentication and TLS when the controller is
  reachable from outside the local network.

## Safety

- Start read-only. Verify the model, register map, and electrical levels
  before enabling writes.
- Clamp setpoints to safe ranges before sending them.
- Do not expose unauthenticated write endpoints. A heat pump controller is
  safety-relevant equipment.
