# Cheap hardware setup

The library is transport-agnostic: it only needs an `ICommunicationBus`
implementation. The reference hardware is an **ESP32-WROOM-32 dev board**.
For UART protocols that is three wires plus USB power. RS485 and CAN need one
small transceiver.

## Bill of materials

| Part | Needed for |
|---|---|
| ESP32-WROOM-32 dev board | WiFi, MQTT, UART |
| USB cable | power and flashing |
| Dupont wires | UART/RS485/CAN wiring |
| USB-TTL adapter (optional) | sniffing/debugging |
| MAX3485/MAX485 module | Thermia / Modbus RTU |
| SN65HVD230 module | RC100H / CAN |
| Isolated DC-DC (permanent install) | galvanic isolation |

The heat pump service port is usually the unknown part, not the electronics.

## Default ESP32 pins

The reference setup uses these defaults:

| Function | ESP32 pin | Notes |
|---|---|---|
| UART RX | GPIO16 | from heat pump TX |
| UART TX | GPIO17 | to heat pump RX |
| UART GND | GND | common ground |
| CAN TX | GPIO21 | to CAN transceiver TXD |
| CAN RX | GPIO22 | from CAN transceiver RXD |
| Status LED | GPIO2 | optional |

Avoid GPIO1/GPIO3 (USB serial console) and GPIO9/GPIO10 (flash).

## TTL UART wiring

```
Heat pump service port          ESP32-WROOM-32
TX  --------------------------> GPIO16 (RX)
RX  <-------------------------- GPIO17 (TX)
GND --------------------------- GND
```

Cross TX and RX. Do not connect the heat pump's VCC to the ESP32 unless you
have verified the port can supply the board; power the ESP32 from USB or a
separate 5 V supply.

Some IVT Rego ports use inverted TTL. The reference adapter can invert the
signals in software (`inverted = true`, implemented with
`uart_set_line_inverse`). Check the signal before assuming it is standard
non-inverted UART.

## RS485 / Modbus RTU

Thermia uses Modbus RTU over RS485. Add a 3.3 V MAX3485/MAX485 module:

```
ESP32 GPIO17 (TX) -> DI
ESP32 GPIO16 (RX) <- RO
                  -> DE/RE (see note)
A/B               -> heat pump A/B
GND               -> GND (if required by the installation)
```

The reference `EspModbusBus` adapter wraps the UART only; it does not control
a DE/RE pin. Use an RS485 module with automatic direction control, or add a
DE/RE GPIO and toggle it around each write. Use 120 ohm termination only when
the bus requires it.

## CAN / RC100H

The ESP32 has a CAN/TWAI controller but no physical CAN transceiver. Add a
3.3 V transceiver such as SN65HVD230:

```
ESP32 GPIO21 (TX) -> transceiver TXD
ESP32 GPIO22 (RX) <- transceiver RXD
3.3 V             -> transceiver VCC
GND               -> transceiver GND
CANH/CANL         -> heat pump CANH/CANL
```

Use 120 ohm termination if required by the bus. Do not use a 5 V-only
transceiver without level shifting.

## Power and safety

- ESP32 GPIOs are 3.3 V and are not 5 V tolerant. Check the heat pump side
  before connecting anything.
- Power the ESP32 from USB or a separate 5 V supply. Do not assume the service
  port can power it.
- TTL UART needs a common ground. For a permanent installation, use an
  isolated supply and keep mains wiring separated.
- Start read-only. Enable writes only after confirming the protocol, register
  map, and wiring.
- Use a fuse, enclosure, and strain relief for anything permanently installed.

## What you implement

The library supplies framing, checksums, protocol detection, sensor parsing,
and command handlers. You supply a small `ICommunicationBus` adapter:

- `writeBytes()` -> UART/RS485/CAN transmit
- `readByte()` / `readBytes()` -> receive with timeout
- `flush()` / `drain()` -> discard stale bytes
- `isReady()` / `getConfig()` -> status

The reference ESP32 UART adapter is about 150 lines. The public example
`examples/command_example.cpp` shows the interface with an in-memory bus; on
the ESP32 the same calls map to `Serial2`.

## Effort

- UART: an evening including flashing and WiFi/MQTT setup.
- RS485 or CAN: a few hours more for the transceiver and termination.
- The hard part is usually identifying the heat pump service port and
  confirming its electrical levels, not writing the ESP32 code.

The reference application (WiFi portal, MQTT, web UI) is separate.
This repository is the protocol/command library; the minimum application is a
loop that initializes the bus, registers protocols, detects the heat pump,
polls sensors, and calls a command handler.
