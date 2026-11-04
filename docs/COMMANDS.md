# Command reference

The library exposes two command styles:

1. **Typed helpers** such as `setHeatingSetpoint(21.5f)` and
   `boostHeating(std::chrono::hours(2))`. These accept human-friendly units.
2. **Generic `HeatPumpCommand`** passed to `sendCommand()`. This style is
   useful when commands are received from MQTT, a web API, or a CLI.

## Generic command values

| Command type | `value` meaning | `registerAddress` meaning | `duration` meaning |
|---|---|---|---|
| `BOOST_HEATING` | hours (minimum 1) | - | preferred duration |
| `STOP_HEATING` | `1` = stop, `0` = normal | - | - |
| `FORCE_ELECTRIC_HEATER` | `1` = on, `0` = off | - | - |
| `SET_HEATING_SETPOINT` | tenths of °C, e.g. `215` = 21.5 °C | - | - |
| `SET_WATER_SETPOINT` | tenths of °C | - | - |
| `SET_OPERATING_MODE` | protocol mode number | - | - |
| `SET_HOLIDAY_MODE` | `1` = on, `0` = off | - | - |
| `SET_TIMER_OVERRIDE` | raw register value | register address | hold duration |
| `SET_FAN_SPEED` | percent, 0-100 | - | - |
| `SET_POWER_LIMIT` | watts | - | - |
| `SET_EXT_PORT` | `1` = on, `0` = off | port number | - |
| `SET_ROOM_TEMPERATURE` | tenths of °C | RC100H circuit (1 or 2) | - |
| `SET_ROOM_HUMIDITY` | percent | - | - |
| `SET_THERMOSTAT_MODE` | `Rc100H::Mode` value | - | - |
| `RESET_ALARM` | `1` | - | - |

Setpoint conversion is `raw = (tempC + 50) / 0.1` for heating circuits and
`raw = tempC / 0.1` for hot water on the Rego, NIBE, EMS, and Thermia handlers.
The typed helpers apply these conversions for you.

## Rego family

Used by IVT Rego 800/600/1000/5200 and compatible controllers.

Read request:

```
02 06 regHi regLo 03 xor8
```

Write request:

```
0B 90 FF regHi regLo valHi valLo xor8
```

| Command | Register | Value |
|---|---|---|
| Boost heating | `0x0209` | hours |
| Stop heating | `0x020A` | 1/0 |
| Holiday mode | `0x0210` | 1/0 |
| Force electric heater | `0x0215` | 1/0 |
| Heating setpoint | `0x0107` | `(tempC + 50) * 10` |
| Hot water setpoint | `0x010B` | `tempC * 10` |
| Operating mode | `0x0201` | mode |
| Fan speed | `0x021A` | percent |
| Power limit | `0x0226` | watts |
| External port 1 | `0x0FA1` | 1/0 |
| External port 2 | `0x0FA2` | 1/0 |
| Reset alarm | `0x02F2` | 1 |
| Timed override | caller-supplied | caller-supplied |

Acknowledgment: `0x06` = accepted, `0x15` = rejected.

## NIBE EB100 / Styr2002

Write request:

```
38 10 FF 00 03 2B reg value
```

Read request:

```
38 10 FF 00 03 2B regLo regHi
```

| Command | Register | Value |
|---|---|---|
| Boost heating | `0x09`, then `0xD1` | hours, then 1 |
| Stop heating | `0x0A` | 1/0 |
| Holiday mode | `0x10` | 1/0 |
| Force electric heater | `0x15` | 1/0 |
| Heating setpoint | `0x07` | `(tempC + 50) * 10` |
| Hot water setpoint | `0x0B` | `tempC * 10` |
| Operating mode | `0x01` | mode |
| Fan speed | `0x1A` | percent |
| Power limit | `0x1B` | watts / 100 |
| Reset alarm | `0xF2` | 1 |

Alarm reset with restart writes `0xF2 = 1` and then sends:

```
38 10 FF 00 03 7B 00 00
```

The `NibeStyr2002Protocol` class additionally exposes `setMode()` for the
Styr2002 mode value and optional file-backed mode persistence.

NIBE write acknowledgments are read as a single byte. A response byte of `W`
is treated as accepted; the helper otherwise reports success after the write.

## Bosch EMS / ProControl

Write request:

```
DC 02 regHi regLo dataLen valHi valLo xor8
```

| Command | Register | Value |
|---|---|---|
| Boost heating | `0x3209` | hours |
| Stop heating | `0x320A` | 1/0 |
| Force electric heater | `0x3215` | 1/0 |
| Heating setpoint | `0x3211` | `(tempC + 50) * 10` |
| Hot water setpoint | `0x321A` | `tempC * 10` |
| Operating mode | `0x3276` | mode |
| Reset alarm | `0x32F2` | 1 |

Acknowledgment: `0x06` = accepted, `0x15` = rejected.

## Thermia (Modbus RTU)

Thermia uses standard Modbus function `0x06` (write single register):

```
slave 06 regHi regLo valHi valLo crcLo crcHi
```

The default slave address is `0x01`. CRC is CRC16 Modbus, transmitted
low byte first.

| Command | Register | Value |
|---|---|---|
| Boost heating | `0x0209` | hours |
| Stop heating | `0x020A` | 1/0 |
| Force electric heater | `0x0215` | 1/0 |
| Heating setpoint | `0x0107` | `(tempC + 50) * 10` |
| Hot water setpoint | `0x010B` | `tempC * 10` |
| Operating mode | `0x0201` | mode |
| Reset alarm | `0x02F2` | 1 |

## RC100H CAN thermostat emulation

The RC100H emulation writes thermostat data frames and can pair or unpair with
the heat pump.

Thermostat update frame:

```
0B 90 FF 00 FF 01
   temp1Lo temp1Hi
   temp2Lo temp2Hi
   humidityLo humidityHi
   xor8
```

Temperatures use `(tempC + 50) * 10`; humidity is an integer percentage.

Pair / unpair frame:

```
0B 90 FF 00 FF <01|00> 02 F1 xor8
```

`01` pairs the thermostat and `00` unpairs it. The RC100H handler also exposes
`setDialRange()` and a raw `sendXW00Write(variableId, value)` frame for CAN
variables:

```
0B 00 <8-byte little-endian variable ID> <4-byte little-endian value> xor8
```

Acknowledgment: `0x06` = accepted, `0x15` = rejected.

## Confirming writes

`HeatPumpCommandBase::setReadBackCallback()` installs a read-back function. When
set, `writeAndVerify()` re-reads the register after the acknowledgment and
retries up to `DEFAULT_READBACK_RETRIES` times. This is useful for controllers
that acknowledge a write before applying it.

Timed writes via `SET_TIMER_OVERRIDE` store the requested duration and register
in the handler; the caller is responsible for restoring the previous value when
the duration expires.
