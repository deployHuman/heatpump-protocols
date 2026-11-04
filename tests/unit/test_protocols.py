"""
Test suite for the heat pump communication library.
Tests checksums, frame parsing, sensor mappings, and protocol behavior.

Usage:
    pytest tests/ -v
    python3 tests/run_tests.py
"""

import json
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parent.parent
DATA_DIR = TEST_DIR / "data"

with open(DATA_DIR / "test_vectors.json") as f:
    VECTORS = json.load(f)


def xor8(data: bytes) -> int:
    result = 0
    for b in data:
        result ^= b
    return result


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def crc8(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = (crc << 1) ^ 0x07
            else:
                crc <<= 1
    return crc & 0xFF


def hex_to_bytes(hex_str: str) -> bytes:
    return bytes.fromhex(hex_str)


class TestChecksums:
    """Verify checksum calculations against protocol vectors."""

    def test_xor8_vectors(self):
        for vec in VECTORS["checksum_vectors"]["xor8"]:
            data = hex_to_bytes(vec["data"])
            expected = int(vec["expected"], 16)
            result = xor8(data)
            assert result == expected, (
                f"{vec['description']}: XOR8({vec['data']}) = 0x{result:02X}, "
                f"expected 0x{expected:02X}"
            )

    def test_xor8_empty(self):
        assert xor8(b"") == 0

    def test_xor8_single_byte(self):
        assert xor8(b"\xFF") == 0xFF
        assert xor8(b"\x00") == 0x00
        assert xor8(b"\x02") == 0x02

    def test_crc16_modbus_vectors(self):
        for vec in VECTORS["checksum_vectors"]["crc16_modbus"]:
            data = hex_to_bytes(vec["data"])
            expected = int(vec["expected"], 16)
            result = crc16_modbus(data)
            assert result == expected, (
                f"{vec['description']}: CRC16({vec['data']}) = 0x{result:04X}, "
                f"expected 0x{expected:04X}"
            )

    def test_crc16_modbus_standard(self):
        assert crc16_modbus(b"\x01\x03\x00\x00\x00\x02") == 0x0BC4

    def test_crc8_vectors(self):
        for vec in VECTORS["checksum_vectors"]["crc8"]:
            data = hex_to_bytes(vec["data"])
            expected = int(vec["expected"], 16)
            result = crc8(data)
            assert result == expected, (
                f"CRC8({vec['data']}) = 0x{result:02X}, expected 0x{expected:02X}"
            )

    def test_crc8_empty(self):
        assert crc8(b"") == 0x00

    def test_xor8_rego800_commands(self):
        """Ensure Rego 800 read commands produce correct XOR checksums."""
        commands = [
            (b"\x02\x06\x00\x00\x03", 0x07),   # Read outdoor temp (reg 0x0000)
            (b"\x02\x06\x00\x02\x03", 0x05),   # Read supply line (reg 0x0002)
            (b"\x02\x06\x00\x04\x03", 0x03),   # Read return line (reg 0x0004)
            (b"\x02\x06\x00\x06\x03", 0x01),   # Read hot water (reg 0x0006)
            (b"\x02\x06\x00\x08\x03", 0x0F),   # Read brine in (reg 0x0008)
            (b"\x02\x06\x00\x0A\x03", 0x0D),   # Read brine out (reg 0x000A)
            (b"\x02\x06\x00\x10\x03", 0x17),   # Read compressor speed (reg 0x0010)
            (b"\x02\x06\x00\x12\x03", 0x15),   # Read heat power (reg 0x0012)
        ]
        for data, expected in commands:
            result = xor8(data)
            assert result == expected, (
                f"XOR8({data.hex(' ').upper()}) = 0x{result:02X}, "
                f"expected 0x{expected:02X}"
            )


class TestFrameStructure:
    """Verify frame format matches the protocol specification."""

    def test_rego800_frame_parsing(self):
        """Parse Rego 800 response: [ADDR] [CMD] [LEN] [DATA...] [XOR]"""
        for name, vec in VECTORS["frame_vectors"].items():
            rx = hex_to_bytes(vec["rx"])
            assert len(rx) >= 4, f"Frame too short: {name}"

            addr = rx[0]
            cmd = rx[1]
            length = rx[2]
            data_end = 3 + length
            data = rx[3:data_end]
            checksum_byte = rx[data_end] if len(rx) > data_end else None

            computed_xor = xor8(rx[:data_end])
            assert not checksum_byte or computed_xor == checksum_byte, (
                f"{name}: XOR checksum mismatch: computed 0x{computed_xor:02X}, "
                f"got 0x{checksum_byte:02X}"
            )

    def test_rego800_read_command_format(self):
        """Verify read command format: 02 06 [REG_HI] [REG_LO] 03 [XOR]"""
        for name, vec in VECTORS["frame_vectors"].items():
            tx = hex_to_bytes(vec["tx"])
            assert len(tx) == 5, f"Command wrong length: {name}"
            assert tx[0] == 0x02, f"Wrong address byte: {name}"
            assert tx[1] == 0x06, f"Wrong command byte: {name}"
            assert tx[4] == 0x03, f"Wrong terminator byte: {name}"

            computed = xor8(tx)
            expected = int(vec["tx_checksum"], 16)
            assert computed == expected, (
                f"{name}: Command checksum: 0x{computed:02X} != 0x{expected:02X}"
            )

    def test_rego800_write_command_pattern(self):
        """Verify write commands match known protocol patterns."""
        for vec in VECTORS["rego800_write_commands"]:
            data = hex_to_bytes(vec["hex"])
            assert len(data) >= 6, f"Write command too short: {vec['description']}"
            assert data[0] == 0x0B, "Write commands must start with 0x0B"
            assert data[1] == 0x90, "Write command second byte must be 0x90"

    def test_data_parsing_temperature(self):
        """Parse a temperature value from Rego 800 response data."""
        vec = VECTORS["frame_vectors"]["rego800_read_temp_register_0x0000"]
        rx = hex_to_bytes(vec["rx"])
        data = rx[3:5]  # First 2 data bytes
        raw = data[0] | (data[1] << 8)  # little-endian
        temp = raw * 0.1 - 50.0
        assert abs(temp - vec["expected_float_value"]) < 0.01, (
            f"Temperature parsing: got {temp}°C, expected {vec['expected_float_value']}°C"
        )


class TestSensorRegistry:
    """Verify sensor registry mappings match the protocol specification."""

    def test_sensor_count(self):
        sensors = VECTORS["sensor_mappings_rego800"]
        assert len(sensors) == 8, f"Expected 8 sensors for Rego 800, got {len(sensors)}"

    def test_sensor_register_addresses_sequential(self):
        sensors = VECTORS["sensor_mappings_rego800"]
        regs = sorted(s["register"] for s in sensors)
        assert regs[0] == "0x0000", "First register must be 0x0000"
        assert regs[-1] == "0x0012", "Last register must be 0x0012"

    def test_sensor_unique_ids(self):
        sensors = VECTORS["sensor_mappings_rego800"]
        ids = [s["id"] for s in sensors]
        assert len(ids) == len(set(ids)), "Sensor IDs must be unique"

    def test_sensor_unique_names(self):
        sensors = VECTORS["sensor_mappings_rego800"]
        names = [s["name"] for s in sensors]
        assert len(names) == len(set(names)), "Sensor names must be unique"

    def test_temperature_sensors_offset(self):
        sensors = VECTORS["sensor_mappings_rego800"]
        temp_sensors = [s for s in sensors if s["type"] == "TEMPERATURE"]
        for s in temp_sensors:
            scale = s["scale"]
            offset = s["offset"]
            raw_value = 500  # 50.0°C before offset+scale
            temp = raw_value * scale + offset
            if s["name"] == "hot_water_temp":
                assert abs(temp - 50.0) < 0.01, (
                    f"{s['name']}: hot water temp offset should be 0, got offset={offset}"
                )
            else:
                assert abs(temp - 0.0) < 0.01, (
                    f"{s['name']}: temp should be 0°C at raw=500 with offset=-50, "
                    f"got {temp}°C"
                )

    def test_compressor_speed_no_offset(self):
        sensors = VECTORS["sensor_mappings_rego800"]
        cs = [s for s in sensors if s["name"] == "compressor_speed"][0]
        assert cs["scale"] == 1.0, "Compressor speed scale should be 1.0"
        assert cs["offset"] == 0.0, "Compressor speed offset should be 0.0"

    def test_all_registers_no_writable(self):
        sensors = VECTORS["sensor_mappings_rego800"]
        writable = [s for s in sensors if s["writable"]]
        assert len(writable) == 0, "No Rego 800 sensors should be writable"

    def test_sensor_values_against_vectors(self):
        """Recalculate all temperature values from known test vectors."""
        for name, vec in VECTORS["frame_vectors"].items():
            if "expected_float_value" not in vec:
                continue
            rx = hex_to_bytes(vec["rx"])
            data = rx[3:5]
            raw = data[0] | (data[1] << 8)

            sensor_name = vec["expected_sensor"]
            sensor = [s for s in VECTORS["sensor_mappings_rego800"]
                      if s["name"] == sensor_name][0]

            value = raw * sensor["scale"] + sensor["offset"]
            assert abs(value - vec["expected_float_value"]) < 0.01, (
                f"{name}: computed {value}, expected {vec['expected_float_value']}"
            )


class TestProtocolDetection:
    """Verify protocol detection string matching."""

    def test_detection_strings_present(self):
        strings = VECTORS["protocol_detection_strings"]
        assert len(strings) >= 8, "Expected at least 8 protocol detection entries"

    def test_rego800_detection(self):
        resp = "IVT Rego 800"
        patterns = VECTORS["protocol_detection_strings"]["rego800"]
        for pattern in patterns:
            assert pattern in resp, f"'{pattern}' should be in '{resp}'"

    def test_rego1000_detection(self):
        patterns = VECTORS["protocol_detection_strings"]["rego1000"]
        assert any(p in "Rego 1000/Pro Control 500" for p in patterns)

    def test_nibe_eb100_detection(self):
        patterns = VECTORS["protocol_detection_strings"]["nibe_eb100"]
        found = any(p in "NIBE EB100" for p in patterns)
        assert found, f"NIBE EB100 patterns {patterns} should match"

    def test_all_detection_patterns_unique(self):
        strings = VECTORS["protocol_detection_strings"]
        all_patterns = []
        for patterns in strings.values():
            all_patterns.extend(patterns)
        assert len(all_patterns) == len(set(all_patterns)), "Detection patterns must be unique"
