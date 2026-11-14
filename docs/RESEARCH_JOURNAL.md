# Research journal: hard-coded heat pump commands

Started: 2026-11-04
Status: research notes, one full Modbus protocol implementation, and opt-in
command handlers
Scope: extra register/command references for the protocol families already in
this repository, transport caveats, and a backlog of known and new heat pump
families that are not implemented yet.

## Why this file exists

The library already contains hard-coded command handlers for Rego, NIBE EB100,
Bosch EMS, Thermia and the RC100H CAN emulation. This journal records where the
additional registers came from, why some handlers are opt-in, and what still
needs hardware verification.

Two rules are used throughout:

1. Existing default register maps and wire bytes are not changed. New register
   maps are exposed through separate extended handlers or raw write helpers.
   The one exception is Modbus response validation: the default Thermia handler
   now checks the 8-byte echo and CRC instead of reading a single ACK byte.
2. A register is only claimed as verified when a public source or a capture
   confirms it. Everything else is marked as an assumption.

All external source links, vendor document names, and PDF references in this
repository are intentionally kept in this file. Other files refer to this
journal instead of linking externally.

## Verification status

| Handler | Register IDs | Temperature encoding | Write frame |
| --- | --- | --- | --- |
| `Rego600ExtendedCommandHandler` | Matches the C00 list | Defaults to bridge tenths; pump-raw option is unverified | Bridge/UART abstraction, not direct service port |
| `Rego800ExtendedCommandHandler` | Matches the C35 list | Defaults to bridge tenths; pump-raw option is unverified | Bridge/UART abstraction, not direct service port |
| `Rego1000ExtendedCommandHandler` | Matches the C30 list | Defaults to bridge tenths; pump-raw option is unverified | Bridge/UART abstraction, not direct service port |
| `NibeEb100ExtendedCommandHandler` | Matches the C40 list | Defaults to bridge tenths; pump-raw option is unverified | 16-bit write frame is UNVERIFIED |
| `ThermiaGenesisCommandHandler` | Coils and holding registers match the Thermia map | `tempC * 100` from the map | Modbus RTU function `0x05` / `0x06` |
| `ThermiaGenesisProtocol` | Input/holding subset matches the v17.1 map | `tempC * 100` from the map | Modbus FC04/FC03 read, FC06 write with echo |
| Default handlers | Simplified pump-side maps | Pump-side `+50` heating, plain hot water | Unchanged; Thermia echo validation was corrected |

## Sources reviewed

| Source | What it provides | Link |
| --- | --- | --- |
| Husdata H1 manual | Common H1 registers, `XW` write command, model-to-bus mapping | https://husdata.se/wp-content/uploads/2015/11/H1-Manual-10.184.pdf |
| Husdata custom registers | Rego 600 and Rego 800/1000 register encoding | https://husdata.se/docs/h60-manual/for-advanced-users/custom-registers/ |
| Husdata C00 Rego 600 | Rego 600 sensor, setting and command IDs | https://online.husdata.se/h-docs/C00.pdf |
| Husdata C30 Rego 1000 | Rego 1000 sensor, setting and command IDs | https://online.husdata.se/h-docs/C30.pdf |
| Husdata C35 Rego 800 | Rego 800 sensor and setting IDs | https://online.husdata.se/h-docs/C35.pdf |
| Husdata C40 NIBE EB100 | NIBE EB100 sensor, setting and command IDs | https://online.husdata.se/h-docs/C40.pdf |
| openHAB RegoHeatPump binding | Rego 6xx serial framing and Husdata bridge support; Rego 800/1000 CAN appears in the supported-model table but bitrate is not specified | https://www.openhab.org/addons/bindings/regoheatpump/ |
| openHAB Rego 6xx source | 9-byte `0x81` serial frames and register handling | https://github.com/openhab/openhab-addons/tree/main/bundles/org.openhab.binding.regoheatpump |
| openHAB NibeHeatpump binding | NIBE register model and UDP/serial gateway behaviour | https://www.openhab.org/addons/bindings/nibeheatpump/ |
| Home Assistant nibe_heatpump integration | Modbus and NibeGW entity mappings, writable coils | https://github.com/home-assistant/core/tree/dev/homeassistant/components/nibe_heatpump |
| `nibe` Python library | Model-specific coil database, Modbus/NibeGW transports, word swap | https://github.com/yozik04/nibe |
| `nibe` coil groups | Common coil addresses for F-series and S-series | https://github.com/yozik04/nibe/blob/master/nibe/coil_groups.py |
| `nibe` model data | Per-model register metadata (factor, signedness, writability) | https://github.com/yozik04/nibe/tree/master/nibe/data |
| EMS-ESP heat pump device | EMS telegram types and payload layouts | https://github.com/emsesp/EMS-ESP32/blob/dev/src/devices/heatpump.cpp |
| EMS-ESP thermostat device | RC100H EMS telegrams `0x42B` and `0x47B` | https://github.com/emsesp/EMS-ESP32/blob/dev/src/devices/thermostat.cpp |
| Thermia Modbus protocol | Official Mega/Genesis coil and holding-register map | https://www.thermia.fi/media/70526/protocol_doc-00079973.pdf |

## Transport findings

These findings matter because this repository currently exposes UART-style
`ICommunicationBus` implementations. A UART handler may therefore be talking to
an inverter or bridge PCB rather than directly to the heat pump controller.

- **Rego 6xx direct service port:** 19200 bps, 8 data bits, no parity, 1 stop
  bit. The openHAB binding uses a 9-byte frame beginning with `0x81`, not the
  `0B 90 FF ...` frame used by the default handler here. The default handler is
  therefore best treated as a bridge/UART abstraction.
- **Rego 800 / Rego 1000 direct service bus:** CAN, not UART. The public
  compatibility table lists "Can bus" for both families. The specific bitrate
  and 29-bit identifier detail is not confirmed by the cited source and remains
  unverified; capture the bus before implementing a direct CAN handler. The
  extended Rego handlers in this repository assume the H66/H1 register map, not
  direct CAN frames.
- **NIBE EB100:** RS-485. Husdata exposes 4-digit H1/H66 IDs. The existing
  `NibeCommandHandler` writes one-byte registers; the extended handler writes
  16-bit registers through `writeRegister16()`.
- **Thermia Diplomat/Villa:** Husdata documents an i2c service interface
  (`901510` / `901353`). Thermia Mega/Genesis has an official Modbus RTU map.
  The existing `ThermiaCommandHandler` uses Rego-style registers; the new
  `ThermiaGenesisCommandHandler` uses the official Thermia map instead.
- **Bosch EMS:** EMS-ESP documents heat-pump telegram types such as `0x998`,
  `0x999`, `0x4AE`, `0x4AF` and `0x99A`, and room-controller telegrams `0x42B`
  and `0x47B`. The `0x32xx` registers used by the default EMS handler are not
  the same numbering and should be treated as bridge-specific until captured.
- **RC100H:** EMS-ESP identifies RC100H as an EMS room thermostat (telegrams
  `0x42B`/`0x47B`). The CAN emulation in this repository is therefore not the
  Bosch EMS RC100H protocol; it is closer to a NIBE/IVT room-controller
  emulation. The public API name is kept for compatibility.

## Temperature encoding caveat

The public bridge documentation expresses temperature registers as plain
tenths: `raw = tempC * 10` (for example a 26.2 C reading is 262 / `0x0106`).
The pump-side catalogs and test vectors in this repository use
`raw = (tempC + 50) * 10` for heating circuits and `raw = tempC * 10` for hot
water.

The extended bridge handlers therefore default to `BridgeDegrees`
(`raw = tempC * 10`). If a bridge variant exposes pump-raw values, construct
the handler with `BridgeTemperatureEncoding::PumpRawOffset50`.

Before writing a setpoint, check one read from the bridge:

- If a 20 C room shows `200` (or `0x00C8`), keep `BridgeDegrees`.
- If the same 20 C room shows `700` (or `0x02BC`), use `PumpRawOffset50`.

Record the observed convention and firmware version in this journal. The
encoder default is an assumption until that read is captured; the
`PumpRawOffset50` mode is also unverified for the bridge variants.

## Rego 600 commands added

Source: Husdata C00, cross-checked against the H1 common register list.

| Register | Meaning | Encoding / value | Handler method |
| --- | --- | --- | --- |
| `0x0107` | Heating setpoint | `tempC * 10` (BridgeDegrees default) | `setHeatingSetpoint()` override |
| `0x0111` | Hot water setpoint | `tempC * 10` (BridgeDegrees default) | `setWaterSetpoint()` override |
| `0x0203` | Room temperature setpoint | `tempC * 10` (BridgeDegrees default) | `setRoomSetpoint()` |
| `0x0207` | Heat curve parallel shift | integer | `setHeatCurveParallel()` |
| `0x0208` | Hot water stop temperature | `tempC * 10` (BridgeDegrees default) | `setWarmWaterStopTemp()` |
| `0x020A` | Summer mode temperature | `tempC * 10` (BridgeDegrees default) | `setSummerMode()` |
| `0x020B` | Hot water differential | `deltaC * 10` | `setWarmWaterDiff()` |
| `0x1215` | Electric heater on/off | `0` or `1` | `setElectricHeaterEnabled()` |
| `0x1233` | External control | write-only `0` or `1` | `setExternalControl()` |
| `0x12F2` | Alarm reset | write `0` | `resetAlarm()` override |
| `0x2204` | Room sensor influence | integer | `setRoomSensorInfluence()` |
| `0x2205` | Heat curve | integer | `setHeatCurve()` |
| `0x2210` | Holiday mode / days | `0..30` days | `setHolidayDays()`, `setHolidayMode()` |
| `0x7209` | Extra warm water | minutes | `setExtraWarmWater()` |
| `0xB21B` | Electric heater maximum power | `1`, `2` or `4` steps | `setElectricHeaterMaxPower()` |

The base handler's `0x0209` boost and `0x0210` holiday registers do not match
this map. `Rego600ExtendedCommandHandler` therefore overrides boost, holiday
and alarm reset, and it refuses commands whose documented register is unknown.
`setHolidayMode(true)` writes 30 days because the C00 register is a duration,
not a boolean; use `setHolidayDays()` for a precise duration. `boostHeating()`
maps to extra warm water because C00 does not expose a heating-boost register.

Husdata notes that `0x1233` is momentary on Rego 600: the block is released
after roughly a minute up to about 20 minutes, so a long block must re-send the
value. Because it is write-only, a bridge may also fail to acknowledge it even
when the block was applied; verify the pump state instead of relying only on the
return code. `0x1215` only takes effect the next time the additional heater
would start; it does not interrupt an active heater step immediately.
`0x12F2 = 0` cycles power to the controller for about 10 seconds.

## Rego 800 commands added

Source: Husdata C35. Rego 800 is read-only on older firmware (before 3.2), so
check the controller version before enabling writes.

| Register | Meaning | Encoding / value | Handler method |
| --- | --- | --- | --- |
| `0x0107` | Heating setpoint | `tempC * 10` (BridgeDegrees default) | `setHeatingSetpoint()` override |
| `0x0203` | Room temperature setpoint | `tempC * 10` (BridgeDegrees default) | `setRoomSetpoint()` |
| `0x2204` | Room sensor influence | integer | `setRoomSensorInfluence()` |
| `0x0205` | Heat curve 1, left | integer | `setHeatCurve()` |
| `0x0206` | Heat curve 1, right | integer | `setHeatCurveRight()` |
| `0x0208` | Warm water stop temperature | `tempC * 10` (BridgeDegrees default) | `setWarmWaterStopTemp()` |
| `0x0212` | Warm water start temperature | `tempC * 10` (BridgeDegrees default) | `setWarmWaterStartTemp()` |
| `0x921B` | Electric heater maximum power | kW | `setElectricHeaterMaxPowerKw()` |

The C35 list does not expose boost, stop, holiday, hot-water setpoint, alarm
reset or external-port commands. Those methods report unsupported instead of
falling back to the Rego 600 register map.

## Rego 1000 commands added

Source: Husdata C30. Temperature values default to `tempC * 10` (BridgeDegrees)
unless noted. Use `PumpRawOffset50` only after confirming one read.

| Register | Meaning | Encoding / value | Handler method |
| --- | --- | --- | --- |
| `0x0107` | Heating setpoint | `tempC * 10` (BridgeDegrees default) | `setHeatingSetpoint()` override |
| `0x0111` | Hot water setpoint | `tempC * 10` (BridgeDegrees default) | `setWaterSetpoint()` override |
| `0x0120` | Heating setpoint 2 | `tempC * 10` (BridgeDegrees default) | `setHeatingSetpoint2()` |
| `0x0217` | Outdoor temperature offset | signed `deltaC * 10` | `setOutdoorTempOffset()` |
| `0x0219` | Pool setpoint | `tempC * 10` (BridgeDegrees default) | `setPoolSetpoint()` |
| `0x0220` | Room setpoint 2 | `tempC * 10` (BridgeDegrees default) | `setRoomSetpoint2()` |
| `0x0205` | Heat curve 1, left | integer | `setHeatCurve()` |
| `0x0206` | Heat curve 1, right | integer | `setHeatCurveRight()` |
| `0x0222` | Heat curve 2 | integer | `setHeatCurve2()` |
| `0x0223` | Heat curve 2 parallel | integer | `setHeatCurveParallel2()` |
| `0x02F1` | Room controller value | `tempC * 10` (BridgeDegrees default) | `setRoomController()` |
| `0x1210` | Holiday mode | `0` or `1` | `setHolidayMode()` override |
| `0x2213` | Hot water program | `0` comfort, `1` economy | `setWarmWaterProgram()` |
| `0x2216` | Hot water state (v3.7+) | `0` comfort, `1` economy, `2` super saver | `setWarmWaterState()` |
| `0x221C` | Hot water time program | `0` always, `1` program 1, `2` program 2 | `setWarmWaterTimeProgram()` |
| `0x2233` | External control port 1 | `0` or `1` | `setExternalControl(1, ...)` |
| `0x2234` | External control port 2 | `0` or `1` | `setExternalControl(2, ...)` |
| `0x22F2` | Alarm reset | write `1` | `resetAlarm()` override |
| `0x22F3` | Room controller dial range | integer | `setRoomControllerDialRange()` |
| `0x6209` | Extra warm water | hours | `setExtraWarmWater()` |
| `0xB20A` | Summer mode | `0` off, `1` auto, `2` on | `setSummerMode()` |

The Husdata C30 list does not contain a cooling setpoint register. Cooling
setpoints exist on other Rego families but are not exposed by this handler.
Confirm the controller version before using the second-circuit, pool, or
super-saver registers.

## NIBE EB100 commands added

Source: Husdata C40. The extended handler writes 16-bit register IDs.

| Register | Meaning | Encoding / value | Handler method |
| --- | --- | --- | --- |
| `0x0107` | Heating setpoint | `tempC * 10` (BridgeDegrees default) | `setHeatingSetpoint()` override |
| `0x0203` | Room setpoint | `tempC * 10` (BridgeDegrees default) | `setRoomSetpoint()` |
| `0x0220` | Room setpoint 2 | `tempC * 10` (BridgeDegrees default) | `setRoomSetpoint2()` |
| `0x02F1` | Room controller value | `tempC * 10` (BridgeDegrees default) | `setRoomController()` |
| `0x2201` | Operating mode | `0` auto, `1` manual, `2` only additional heater | `setOperatingMode()` |
| `0x2204` | Room sensor influence | integer | `setRoomSensorInfluence()` |
| `0x2205` | Heat curve | integer | `setHeatCurve()` |
| `0x2207` | Heat curve parallel | integer | `setHeatCurveParallel()` |
| `0x2213` | Hot water program | `0` eco, `1` normal, `2` luxury, `4` smart | `setWarmWaterProgram()` |
| `0x2218` | Pool mode | `0` or `1` | `setPoolMode()` |
| `0x2222` | Heat curve 2 | integer | `setHeatCurve2()` |
| `0x2224` | Heat curve 2 parallel | integer | `setHeatCurveParallel2()` |
| `0x22F2` | Alarm reset | write `1` | `resetAlarm()` override |
| `0x8255` | Degree minutes compressor | signed integer | `setDegreeMinutes()` |
| `0x9226` | Maximum additional heat | kW | `setMaxAddHeat()` |

The C40 list does not expose a hot-water setpoint, extra-warm-water boost, or
an "off" operating mode. `boostHeating()` therefore keeps the legacy
`0x09`/`0xD1` path from `NibeCommandHandler`; that path is UNVERIFIED on an
EB100 bridge and is kept only because it is the closest available behavior.
`stopHeating()` is reported as unsupported instead of being mapped to mode `2`
(only additional heater).

### NIBE 16-bit write frame (UNVERIFIED)

Reads use:

```text
38 10 FF 00 03 2B <regLo> <regHi>
```

The extended handler adds:

```text
38 10 FF 00 03 2B <regLo> <regHi> <valueLo> <valueHi>
```

This follows the little-endian value convention used by the sensor parsers and
the register order used by `NibeEB100Protocol::buildReadCommand()`. It has not
been verified against an EB100 capture. Use `writeRegister16Raw()` if a bridge
expects a different payload length, and record the capture in this journal.

## NIBE Modbus / NibeGW (Home Assistant integration)

The Home Assistant `nibe_heatpump` integration supports two local transports:

- **Modbus RTU/TCP** using the `nibe` Python library and a model-specific coil
  database.
- **NibeGW** over UDP ports 9999/10000.

The library and model data are the most useful public reference because they
include the factor, signedness and writability for each register, and they
cover both F-series and S-series pumps.

### Coil address encoding

NIBE Modbus addresses in the `nibe` library are five digits:

```text
<entity type><1-based register number>
```

| First digit | Modbus entity | Read function | Write function |
| --- | --- | --- | --- |
| `0` | coil | `0x01` | `0x05` |
| `1` | discrete input | `0x02` | - |
| `3` | input register | `0x04` | - |
| `4` | holding register | `0x03` | `0x10` |

The wire address is `(coilAddress % 10000) - 1`. For example, `40207` is
holding register `206` (`0x00CE`), and `31029` is input register `1028`
(`0x0404`).

### Value encoding

NIBE Modbus values use `value / 10` for temperature and percent registers.
There is **no +50 degree offset** in this map. A room setpoint of 21.5 C is
raw `215`, not `715`. This is different from the Husdata H1/H66 maps used by
`NibeEb100ExtendedCommandHandler`.

The addresses below are the ones used by the `nibe` library's Modbus
transport. NIBE's official S-series Modbus TCP document describes parameter
IDs instead; do not assume the two numbering schemes are interchangeable.
The Home Assistant integration uses the `nibe` library map.

### S-series registers implemented

Source: `nibe/data/s1155_s1255.json` and `nibe/coil_groups.py`.

| Coil address | Wire address | Meaning | Function | Factor | Writable |
| --- | --- | --- | --- | --- | --- |
| `30038` | `37` | Average outdoor temperature (BT1) | `0x04` | 10 | no |
| `30006` | `5` | Supply line (BT2) | `0x04` | 10 | no |
| `30008` | `7` | Return line (BT3) | `0x04` | 10 | no |
| `30010` | `9` | Hot water charging (BT6) | `0x04` | 10 | no |
| `30027` | `26` | Room temperature S1 | `0x04` | 10 | no |
| `31018` | `1017` | Calculated supply S1 | `0x04` | 10 | no |
| `31029` | `1028` | Priority | `0x04` | 1 | no |
| `31034` | `1033` | Shunt mode S1 | `0x04` | 1 | no |
| `31039` | `1038` | Hot water mode | `0x04` | 1 | no |
| `31976` | `1975` | Alarm number | `0x04` | 1 | no |
| `40207` | `206` | Room setpoint heat S1 | `0x03` | 10 | yes |
| `40989` | `988` | Room setpoint cool S1 | `0x03` | 10 | yes |
| `40203` | `202` | Use room sensor S1 | `0x03` | 1 | yes |
| `40171` | `170` | Cooling with room sensors | `0x03` | 10 | yes |
| `40027` | `26` | Heating curve S1 | `0x03` | 1 | yes |
| `40031` | `30` | Heating offset S1 | `0x03` | 1 | yes |
| `40035` | `34` | Min supply S1 | `0x03` | 10 | yes |
| `40039` | `38` | Max supply S1 | `0x03` | 10 | yes |
| `40211` | `210` | Room sensor factor S1 | `0x03` | 10 | yes |
| `40061` | `60` | Hot water start low | `0x03` | 10 | yes |
| `40060` | `59` | Hot water start normal | `0x03` | 10 | yes |
| `40059` | `58` | Hot water start high | `0x03` | 10 | yes |
| `40065` | `64` | Hot water stop low | `0x03` | 10 | yes |
| `40064` | `63` | Hot water stop normal | `0x03` | 10 | yes |
| `40063` | `62` | Hot water stop high | `0x03` | 10 | yes |
| `40023` | `22` | Reset alarm | `0x03` | 1 | yes |

### F-series registers implemented

Source: `nibe/data/f1155_f1255.json` and `nibe/coil_groups.py`.

| Coil address | Wire address | Meaning | Function | Factor | Writable |
| --- | --- | --- | --- | --- | --- |
| `40067` | `66` | Outdoor temperature | `0x03` | 10 | no |
| `40008` | `7` | Supply line | `0x03` | 10 | no |
| `40012` | `11` | Return line | `0x03` | 10 | no |
| `40033` | `32` | Room temperature S1 | `0x03` | 10 | no |
| `43009` | `3008` | Calculated supply S1 | `0x03` | 10 | no |
| `43086` | `3085` | Priority | `0x03` | 1 | no |
| `43096` | `3095` | Shunt mode S1 | `0x03` | 1 | no |
| `47041` | `7040` | Hot water mode | `0x03` | 1 | no |
| `45001` | `5000` | Alarm number | `0x03` | 1 | no |
| `47398` | `7397` | Room setpoint heat S1 | `0x03` | 10 | yes |
| `48785` | `8784` | Room setpoint cool S1 | `0x03` | 10 | yes |
| `47394` | `7393` | Use room sensor S1 | `0x03` | 1 | yes |
| `47340` | `7339` | Cooling with room sensors | `0x03` | 10 | yes |
| `47007` | `7006` | Heating curve S1 | `0x03` | 1 | yes |
| `47011` | `7010` | Heating offset S1 | `0x03` | 1 | yes |
| `47015` | `7014` | Min supply S1 | `0x03` | 10 | yes |
| `47019` | `7018` | Max supply S1 | `0x03` | 10 | yes |
| `47402` | `7401` | Room sensor factor S1 | `0x03` | 10 | yes |
| `47045` | `7044` | Hot water start economy | `0x03` | 10 | yes |
| `47044` | `7043` | Hot water start normal | `0x03` | 10 | yes |
| `47043` | `7042` | Hot water start luxury | `0x03` | 10 | yes |
| `47049` | `7048` | Hot water stop economy | `0x03` | 10 | yes |
| `47048` | `7047` | Hot water stop normal | `0x03` | 10 | yes |
| `47047` | `7046` | Hot water stop luxury | `0x03` | 10 | yes |
| `48132` | `8131` | Temporary lux | `0x03` | 1 | yes |
| `45171` | `5170` | Reset alarm | `0x03` | 1 | yes |

### Implementation

`NibeSModbusProtocol` and `NibeFModbusProtocol` (in
`include/comm/protocol/nibe/NibeModbusProtocol.h`) implement a focused subset
of the tables above. They use Modbus RTU over the existing byte-oriented
`ICommunicationBus`, with function `0x04` for input registers, `0x03` for
holding registers and `0x10` (write multiple registers) for writes. This
matches the `nibe` library and NIBE's F-series preference for FC10.

Auto-detection reads the priority and supply-temperature registers and checks
the priority value (`10`, `20`, `30`, `40` or `60`) plus a plausible supply
temperature. The full model-specific map still needs the model selected by the
user, as in the Home Assistant config flow.

Writable sensors are available through `writeValue()` and the new
`writeValueByName()` API. Examples: `room_setpoint_heat_s1`,
`room_setpoint_cool_s1`, `use_room_sensor_s1`, `heating_curve_s1`,
`heating_offset_s1`, `min_supply_s1`, `max_supply_s1`, `temporary_lux` and
`alarm_reset`.

### Word swap and 32-bit values

The `nibe` library notes that S-series pumps connected over Modbus TCP return
32-bit registers with the low word first but expect the high word first when
written. Modbus RTU uses the same word order in both directions. The focused
catalog currently uses 16-bit registers only, so this does not affect it. A
future 32-bit extension must implement the TCP word-swap rule.

### NibeGW status

NibeGW is a UDP text protocol on ports 9999/10000. It does not fit the current
byte-oriented `ICommunicationBus`; implementing it needs a datagram transport
and a new device/protocol layer. It remains a backlog item.

## Thermia Mega/Genesis commands added

Source: Thermia Modbus protocol document `ACME01UG0502`. The table has two
address columns: the reference/wire address and the 1-based "De Facto
Address". Coils use the wire address (de-facto minus 1). Holding registers use
the de-facto address minus 40001.

| Coil / register | Meaning | Value | Handler method |
| --- | --- | --- | --- |
| Coil 3 (de-facto 4) | Reset all alarms | on | `resetAlarm()` |
| Coil 5 (de-facto 6) | External additional heater | on/off | `enableExternalAdditionalHeater()` |
| Coil 8 (de-facto 9) | Enable tap water | on/off | `enableTapWater()` |
| Coil 9 (de-facto 10) | Enable heat | on/off | `enableHeating()` |
| Coil 10 (de-facto 11) | Enable active cooling | on/off | `enableActiveCooling()` |
| Coil 28 (de-facto 29) | Enable pool | on/off | `enablePool()` |
| Coil 33 (de-facto 34) | Enable passive cooling | on/off | `enablePassiveCooling()` |
| Coil 41 (de-facto 42) | Enable fixed supply setpoint | on/off | `setFixedSupplySetpoint()` |
| HR 40001 | Operational mode | `1` off, `2` standby, `3` auto | `setOperatingMode()` |
| HR 40004 | Maximum radiator setpoint | `tempC * 100` | `setMaxRadiatorSetpoint()` |
| HR 40005 | Minimum radiator setpoint | `tempC * 100` | `setMinRadiatorSetpoint()` |
| HR 40006 | Comfort wheel | `tempC * 100` | `setComfortWheel()` |
| HR 40007..40013 | Heat curve points 1..7 | `tempC * 100` | `setHeatCurvePoint()` |
| HR 40017 | Heating season stop | `tempC * 100` | `setHeatingSeasonStop()` |
| HR 40023 | Tap water start | `tempC * 100` | `setTapWaterStart()` |
| HR 40024 | Tap water stop | `tempC * 100` | `setTapWaterStop()` |
| HR 40027 / 40028 | Minimum / maximum heating gear | integer | `setMinGearHeating()`, `setMaxGearHeating()` |
| HR 40029 / 40030 | Maximum / minimum tap water gear | integer | `setMaxGearTapWater()`, `setMinGearTapWater()` |
| HR 40031 | Cooling mix valve setpoint | `percent * 100` | `setCoolingMixValvePercent()` |
| HR 40059 | Pool charge setpoint | `percent * 100` | `setPoolChargePercent()` |
| HR 40117 | Fixed supply setpoint | `tempC * 100` | `setFixedSupplySetpoint()` |

`ThermiaGenesisCommandHandler` and the default `ThermiaCommandHandler` both
validate the 8-byte Modbus echo and CRC before reporting success. The default
handler keeps its old Rego-style register addresses; only its response handling
was corrected.

## Thermia Genesis protocol implemented

`ThermiaGenesisProtocol` implements the sensor side of the official map:

- Detection reads input registers `30312`-`30314` (control software major,
  minor, micro) and expects a plausible version.
- Telemetry uses function `0x04` on input registers (`300xx`).
- Setpoints use function `0x03` on holding registers (`400xx`).
- Writes use function `0x06` and validate the 8-byte Modbus echo.
- The sensor catalog is a useful subset: outdoor, discharge, condenser, brine,
  supply/return, tap water, buffer, pump speeds, valve positions, compressor
  speed/hours, power, pressures, expansion valve, inverter temperature, and
  the main writable setpoints.
- The full address list in the v17.1 PDF is larger; add sensors as needed.

The protocol is registered as `THERMIA_GENESIS` before the older
`THERMIA_DIPLOMAT` Rego-style protocol.

## Raw register helpers

These public helpers let a caller apply a newly discovered register without
adding another handler class:

- `RegoCommandHandler::writeRegister()` verifies via the optional read-back
  callback; `writeRegisterRaw()` only waits for the acknowledgement.
- `NibeCommandHandler::writeRegister()` is the existing one-byte path;
  `writeRegister16()`/`writeRegister16Raw()` use the 16-bit frame above.
- `EmsCommandHandler::writeRegister()` accepts a data length so 1-byte EMS
  entities can be written.
- `ThermiaCommandHandler::writeRegister()` exposes the existing Rego-style
  Modbus write path.

## Backlog and new-model research

Investigated 2026-11-04 against the public sources below. This is a research
backlog, not a compatibility promise. "Status" records the best available
evidence, not verification on our own hardware.

### Backlog and new-model sources

| ID | Source | Link |
| --- | --- | --- |
| N1 | NIBE MODBUS 40 FAQ | https://installer.nibe.eu/download/18.47aa975e18a8b43315f342a/1696946128721/FAQ%20Modbus%2040.pdf |
| N2 | NIBE S-series Modbus M12676EN2 | https://headless.nibe.eu/download/18.3bd9d98318c457ffd811678/1703680919076/M12676EN2.pdf |
| N3 | NIBE F1155/F1255 register CSV | https://github.com/yozik04/nibe/blob/master/nibe/data/f1155_f1255.csv |
| N4 | nibe2mqtt nibegw.c | https://github.com/bebben88/nibe2mqtt/blob/master/nibegw.c |
| V1 | VitoWiFi protocol notes | https://github.com/bertmelis/VitoWiFi/blob/cf5c9da18c4cf2631639f7fbd6718eb3ab559f40/PROTOCOL.md |
| V2 | openv address catalogue | https://github.com/openv/openv/wiki/Adressen |
| V3 | Vitotronic 200 WO1C service instructions | https://viessmanndirect.co.uk/files/fd7d5a03-25ec-489b-9b39-adda016c8702/Vitotronic%20200-WO1C%20Service%20Instructions.pdf |
| V4 | Vitocal 250-A service manual | https://www.heizungsdiscount24.de/pdf/Viessmann-Vitocal-250-A-Service-und-Montageanleitung.pdf |
| V5 | Open3E | https://github.com/open3e/open3e |
| V6 | Vitocal 250-A Pro product page | https://www.viessmann.co.uk/en/products/heat-pump/vitocal-250-a-pro.html |
| M1 | Ecodan CN105 protocol notes | https://github.com/gekkekoe/esphome-ecodan-hp/blob/main/protocol.md |
| M2 | Mitsubishi Ecodan R290 transition | https://les.mitsubishielectric.co.uk/latest-news/new-news-pagemitsubishi-electric-announces-transition-from-ecodan-r32-to-the-new-extended-range-of-ecodan-r290-heat-pumps |
| M3 | FTC7 wiring guide | https://view.genially.com/674ebe6054cd482e8e603901 |
| P1 | HeishaMon | https://github.com/HeishaMon/HeishaMon |
| P2 | HeishaMon protocol byte map | https://github.com/HeishaMon/HeishaMon/blob/main/ProtocolByteDecrypt.md |
| P3 | Panasonic Aquarea 2025-2026 catalogue | https://www.panasonicproclub.com/uploads/LT/catalogues/Aquarea_range_2025-2026.pdf |
| P4 | Panasonic CN-CNT protocol v1 | https://github.com/ssjoholm/panasonic-cn-cnt/blob/main/Panasonic-CN-CNT-Protocol-v1.md |
| D1 | ESPAltherma documentation | https://raomin.github.io/ESPAltherma/ |
| D2 | Daikin I protocol | https://github.com/raomin/ESPAltherma/blob/main/doc/Daikin%20I%20protocol.md |
| D3 | Daikin X10A protocol | https://github.com/0Bu/daikin-altherma-esp32/blob/main/docs/X10A_PROTOCOL.md |
| D4 | Daikin Altherma 4 launch | https://www.daikin.eu/en_us/press-releases/daikin-altherma-4-unveiled.html |
| D5 | Faikin S21 protocol | https://codeberg.org/RevK/ESP32-Faikout/wiki/S21-Protocol.md |
| S1 | goelster | https://github.com/andig/goelster |
| S2 | ha-stiebel-control | https://github.com/bullitt186/ha-stiebel-control |
| S3 | Stiebel WPM system service manual | https://www.stiebel-eltron.be/content/dam/ste/be/nl/service/reparatiehandleidingen/Reparatiehandleiding%20WPM%20system.pdf |
| S4 | Stiebel ISG compatibility list | https://www.stiebel-eltron.ch/content/dam/ste/ch/de/downloads/kundenservice/smart-home/Hardware-ISG/kompatibilitaetsliste-isg.pdf |
| SA1 | EHSMonitor | https://github.com/betaphi/EHSMonitor |
| SA2 | pysamsungnasa protocol overview | https://pantherale0.github.io/pysamsungnasa/protocol/overview/ |
| SA3 | Samsung NASA MQTT | https://github.com/70p4z/samsung-nasa-mqtt |
| SA4 | Samsung EHS R290 installation manual | https://stg-images.samsung.com/is/content/samsung/assets/levant/13/EHS_R290_MONO_With_Pump_OD_IM_EU_DB68-12498B-01_EN_.pdf |
| VA1 | eBUS physical/data-link specification | https://adapter.ebusd.eu/Spec_Prot_12_V1_3_1.pdf |
| VA2 | ebusd configuration | https://github.com/john30/ebusd-configuration |
| VA3 | Vaillant aroTHERM plus /8.1 quick guide | https://pimassetsprdst.blob.core.windows.net/assets/apc_Original/51/33/143335133.pdf |
| VA4 | ebusd /8.1 gap report | https://github.com/john30/ebusd/issues/1635 |
| A1 | python-luxtronik | https://github.com/Bouni/python-luxtronik |
| A2 | Alpha LUX 2.1 manual | https://files.ait-group.net/FILES/Alpha-InnoTec/manuals/01%20heat%20pumps/05%20controller/LUX/83055400pUK_Lux_21_Teil_2.pdf |
| W1 | Waterkotte Modbus scan | https://raw.githubusercontent.com/Hoffmann77/home_assistant_waterkotte_card/main/Kommunikationsdaten%20ModBus%20RS485%20Waterkotte%20W%C3%A4rmepumpe.pdf |
| W2 | Waterkotte current lineup | https://www.waterkotte.de/waermepumpen |
| W3 | ioBroker Waterkotte adapter | https://github.com/theknut/ioBroker.waterkotte-easycon |
| O1 | Ochsner OTS Modbus manual | https://library.loxone.com/downloader/file/13142/ZBH-Modbus%20OTS-EN03.pdf |
| O2 | OTS-HomeAssistant | https://github.com/permissionBRICK/OTS-HomeAssistant |
| I1 | IDM Navigator Modbus TCP manual | https://library.loxone.com/downloader/file/14534/iDM%20Mobus%20TCP%20Navigator%202.0%20EN.pdf |
| I2 | Xerolux IDM integration | https://github.com/Xerolux/idm-heatpump-hass |
| H1 | Heliotherm RCG X Modbus manual | https://forum.iobroker.net/assets/uploads/files/1657798802648-20191213_fachmannebene_rcgx_1.0.3.2_de.pdf |
| H2 | ha_heliotherm | https://github.com/mbuchber/ha_heliotherm |
| HI1 | Hitachi ATW-MBS documentation | https://documentation.hitachiaircon.com/glb/en/controls/atw-mbs |
| HI2 | Hitachi ATW-MBS-02 register map | https://github.com/alepee/hass-hitachi_yutaki/blob/main/docs/gateway/atw-mbs-02.md |
| F1 | BSB-LAN supported models | https://raw.githubusercontent.com/fredlcore/BSB-LAN/master/docs/en/supported_models.md |
| F2 | BSB-LAN bus systems | https://raw.githubusercontent.com/fredlcore/BSB-LAN/master/docs/en/bus_systems.md |
| T1 | Thermia Genesis Modbus v17.1 | https://www.geotherma.be/wp-content/uploads/2026/02/Modbus-protocol-for-Genesis-platform-17.1-Calibra.pdf |
| B1 | EMS-ESP EMS bus | https://docs.emsesp.org/EMS-bus |
| B2 | Buderus EMS plus planning document | https://www.buderus.de/resource/blob/79466/f054e33baafdc741b63fdf12098ca123/planungsunterlagen-ems-plus-data.pdf |
| L1 | LG Therma V application guide 2025 | https://www.irishellas.com/files/LG-ThermaV-Application-Guide.pdf |
| MH1 | MHI MODBUS 40M manual | https://condex.bg/wp-content/uploads/2024/02/modobus40m_installation.pdf |
| G1 | Gree Versati Modbus V1.5 | https://man.kievclimate.com/man/Gree/Versati/Versati%20III/R32_Versati%20Modbus%20V1.5.pdf |
| MD1 | Midea M-Thermal Arctic R290 project | https://github.com/mkroiss/midea-thermal-artic-r290-modbus |
| AW1 | Airwell Wellea Mono Split A R32 manual | https://lh.airwell-res.com/sites/default/files/product_uploads/21.AW_.WELLEA%20MONO.SPLIT_.A.USER_.ML_.12.01.pdf |

All links in this table returned HTTP 200 on 2026-11-04.

### Backlog candidates

| Family / current models | Transport | Protocol / register model | Sources | Status |
| --- | --- | --- | --- | --- |
| NIBE F1155/F1255 with MODBUS 40 | Modbus RTU, 9600 8N1, slave 1–247 | 4xxxx holding registers; FC03 read, FC10 write; FC06 is not reliable on MODBUS 40 | N1, N3 | Official + community, high |
| NIBE S1155/S1255 and S1156/S1256 | Modbus TCP, port 502 | Parameter IDs, not 4xxxx; FC03/FC04/FC06/FC10 | N2 | Official, high |
| NIBE F2120/S2125 outdoor units | Via SMO 40 + MODBUS 40 or SMO S40 | Outdoor unit is a slave behind the controller; no standalone Modbus port | N1, N2 | Official, medium-high |
| NIBE EB100 / Styr2002 | Internal RS-485, proprietary | NIBE/Husdata bridge protocol; existing handler covers EB100 | N4 | Community, medium |
| Viessmann Vitotronic 200 KW2/KW1/WO1C | Optolink, 4800 8E2 | KW/VS1 or P300/VS2 frames; vito.xml address model | V1, V2, V3 | Community + official, high |
| Viessmann Vitocal 150-A/151-A/250-A/252-A | CAN, 250 kbit/s | UDS/DID model; Open3E for MQTT | V4, V5 | Official + community, high CAN |
| Viessmann Vitocal 250-A Pro | Modbus TCP/BACnet | Object model; no public register table | V6 | Official, medium |
| Mitsubishi Ecodan CN105 (FTC4/5/6/7, R32/R290) | UART, 2400 8E1 | `0xFC` frame family; `0xC9` reports FTC version | M1, M2, M3 | Community + official, high |
| Panasonic Aquarea CN-CNT (H/J/K/L/M, T-CAP) | UART, 9600 8E1 | `0x71` poll/response, `0xF1` control; 203-byte block | P1, P2, P3, P4 | Community + official, high |
| Daikin Altherma X10A (Altherma 2/3) | UART, 9600 8E1 | Protocol I: `03 40 reg cksum`; protocol S older framing | D1, D2, D3 | Community, high |
| Daikin Altherma 4 H | Unknown | X10A compatibility is not confirmed | D4 | Official product, low |
| Stiebel Eltron / Tecalor WPL/WPM | CAN, 20 kbit/s (CAN A) or 50 kbit/s (CAN B) | Elster index/value registers; WPL-A .2 map unpublished | S1, S2, S3, S4 | Community + official, medium-high |
| Samsung EHS Mono HT Quiet / R290 Gen7 | RS-485 F1/F2, 9600 8E1 | NASA packet protocol; F3/F4 is a separate bus | SA1, SA2, SA3, SA4 | Community + official, high |
| Vaillant aroTHERM plus /6 | eBUS, 2400 8N1 | eBUS telegram/CRC; Vaillant register CSVs in ebusd | VA1, VA2 | Official standard + community, high physical |
| Vaillant aroTHERM plus /8.1 | Modbus outdoor↔indoor; eBUS system bus | Not covered by upstream ebusd; do not reuse /6 CSVs | VA3, VA4 | Official + community gap, medium |
| Alpha Innotec / Novelan Luxtronik 2.0/2.1 | CFI TCP 8889; SHI Modbus TCP 502 | Proprietary CFI binary; Modbus input/holding registers | A1, A2 | Official + community, high ports |
| Waterkotte EcoTouch | Modbus TCP 502; RTU 9600 8N1 on older/optional cards | FC03 holding; float32 pairs word-swapped | W1, W2, W3 | Official + community, medium |
| Ochsner OTS | Modbus TCP 502; RTU 2400–38400 | Address/data-point list; BMS mode may be needed for writes | O1, O2 | Official + community, medium |
| IDM Navigator 2.0/10/Pro | Modbus TCP 502, unit ID 1 | 0-based decimal addresses; FC03/04/16; float low word first | I1, I2 | Official + community, medium-high |
| Heliotherm RCG X / RCG2+ | Modbus TCP 502; RTU 19200 8N1 | FC03/04/06/16; ≥5 s write cycle | H1, H2 | Official + community, medium-high |
| Hitachi Yutaki S/S Combi/S80/M | Modbus RTU 19200/9600, parity selectable; gateway | ATW-MBS-02 or HC-A map; H-LINK side is 9600 | HI1, HI2 | Official + community, high |
| Fujitsu Waterstage / Atlantic/Thermor | BSB 2-wire, 4800 8O1 | BSB telegrams and parameter IDs; not Modbus | F1, F2 | Community, medium |
| Thermia Atlas/Calibra/Diplomat Inverter | Modbus RTU 19200 8E1; TCP 502 | Coils/discrete/input/holding; 16/32-bit values | T1 | Implemented in `ThermiaGenesisProtocol` (sensor subset) + command handler |
| Bosch Compress 7000i / Buderus Logatherm | EMS/EMS2, 9600 8N1 | EMS telegram bus; not Modbus native | B1, B2 | Official + community, high bus |
| LG Therma V | Modbus RTU 9600 8N1 | Open Modbus coils/registers; model/date-specific map | L1 | Official, high |
| MHI Hydrolution | Modbus RTU 9600 8N1 | Holding 40001+; FC03/10; max 20 registers per read | MH1 | Official, high |
| Gree Versati | Modbus RTU 9600 8N1 | Coils + 16-bit registers; FC01/03/05/06/0F/10 | G1 | Official, high |
| Midea M-Thermal Arctic R290 | Modbus RTU 9600 8N1 | Holding 0–289; FC03/06/16; community map | MD1 | Community, medium |
| Airwell Wellea Mono Split A R32 | Modbus RTU 9600 8N1 | Registers 0+; FC03/06/10 | AW1 | Official, high |
| Hisense / Beko | Unknown | No public register map found | - | Excluded |

### Corrections to the earlier backlog

- Daikin Altherma X10A is **9600 8E1**, not 2400 8E2. The 2400 8E2 S21 port is for air-to-air units.
- Panasonic Aquarea CN-CNT uses **0x71 poll/response and 0xF1 control** in HeishaMon. The `0x70/0xF0` pair belongs to a different air-to-air CN-CNT variant.
- Viessmann Optolink is **4800 8E2**, not 8N1. Vitocal 250-A/150-A are CAN/Open3E; Vitocal 300-A/WO1C are Optolink.
- NIBE S-series official Modbus TCP uses **parameter IDs**, not 4xxxx offsets; the `nibe` library maps its S-series coils to 3xxxx/4xxxx. F-series MODBUS 40 uses 4xxxx and prefers FC10 for writes.
- NIBE F2120/S2125 are outdoor units; Modbus is exposed by the controller, not the outdoor unit.
- Mitsubishi Ecodan CN105 is **2400 8E1**; 9600 appears only in some MELCloud/CN105 contexts.
- Stiebel/Tecalor is **Elster CAN** at 20/50 kbit/s, not Modbus. WPL-A .2 has no public CAN map.
- Samsung NASA is **9600 8E1** on F1/F2; F3/F4 is a separate remote-controller bus.
- Vaillant /6 uses eBUS 2400 8N1; /8.1 uses Modbus between outdoor and indoor units and is not covered by upstream ebusd.
- Alpha Innotec Luxtronik is CFI TCP 8889 plus Modbus TCP 502; no RTU. Waterkotte's ioBroker adapter is HTTP/CGI, not Modbus.
- Ochsner may require BMS-control mode for writes; IDM uses 0-based addresses and low-word-first floats; Heliotherm RTU is 19200 8N1.
- Fujitsu/Atlantic/Thermor use **BSB 4800 8O1**, not Modbus. Hitachi uses a Modbus gateway. Bosch/Buderus EMS is not Modbus.
- LG, MHI, Gree, and Airwell expose Modbus RTU at 9600 8N1; Midea has a community map only.

### New/current model notes

- NIBE S1156/S1256 are current; 2026 R290 S1157/S1257/S1357 are announced but not in the current public Modbus PDF.
- Mitsubishi Ecodan R290/FTC7 is current; R32 phase-out was announced in 2025.
- Panasonic Aquarea L/M/T-CAP are R290; K is R32 and still current.
- Daikin Altherma 4 H (R290) launched December 2024; public X10A support is not confirmed.
- Viessmann Vitocal 150-A/250-A use CAN/Open3E; Vitocal 250-A Pro uses Modbus TCP/BACnet.
- Vaillant aroTHERM plus /8.1 is new and is an ebusd gap; Stiebel WPL-A .2 Plus/Trend is current with no public CAN map.
- Thermia Atlas/Calibra are covered by the v17.1 Modbus document.
- LG Therma V 2025 models, MHI Hydrolution, Gree Versati, Airwell Wellea, and Midea Arctic R290 all expose Modbus RTU with model-specific maps.

### Suggested next protocol work

1. **Daikin X10A, Mitsubishi CN105, Panasonic CN-CNT**: UART protocols with active public tooling and clear framing. Good candidates for a shared UART test harness and mock-bus tests.
2. **Viessmann Optolink** (4800 8E2) plus **Open3E CAN** for the newer Vitocal generation. Keep the two transports separate; do not assume vito.xml works on 250-A.
3. **Modbus RTU families** (LG Therma V, MHI Hydrolution, Gree Versati, Airwell Wellea, Heliotherm, Waterkotte, Ochsner, Midea): a generic Modbus RTU transport plus per-family register tables would cover many models. Thermia Atlas/Calibra is already implemented and can serve as the reference pattern.
4. **Modbus TCP families** (NIBE S-series, Luxtronik, Waterkotte, Ochsner, IDM, Heliotherm): needs a TCP transport outside the current `ICommunicationBus` scope.
5. **Stiebel Elster CAN**, **Vaillant eBUS**, **Fujitsu/Atlantic/Thermor BSB**, and **Bosch/Buderus EMS**: separate bus protocols; implement only after a concrete capture and a transport.


## Verification checklist before using on hardware

- Confirm the controller model and firmware version.
- Confirm whether the UART port is a direct service port or a bridge/inverter
  PCB. The register numbering is different.
- Sniff the first read transaction and compare the register order and value
  endianness with this document.
- Confirm the temperature encoding with one read: a 20 C room is `200` with
  `BridgeDegrees` or `700` with `PumpRawOffset50`.
- Confirm coil addressing on the Thermia map: the wire address is the
  de-facto address minus 1.
- Confirm the NIBE EB100 16-bit write payload length before using the extended
  handler.
- Test writes on a non-critical setting first and confirm the value in the pump
  display or a read-back.
- Keep a record of the capture, firmware version and model in this journal.
- Do not enable cooling, pool, electric heater or fixed-supply modes on a system
  that is not configured for them.
