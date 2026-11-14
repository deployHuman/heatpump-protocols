#pragma once

#include <cstdint>
#include <climits>
#include <chrono>

namespace Constants {

namespace FrameLayout {
    constexpr size_t ADDRESS_POS   = 0;
    constexpr size_t COMMAND_POS   = 1;
    constexpr size_t LENGTH_POS    = 2;
    constexpr size_t HEADER_SIZE   = 3;     // address + command + length
    constexpr size_t CHECKSUM_SIZE = 2;     // default size for 16-bit checksums
    constexpr size_t MAX_DATA_SIZE = 256;
    constexpr size_t MAX_FRAME_SIZE = HEADER_SIZE + MAX_DATA_SIZE + CHECKSUM_SIZE;
    constexpr size_t MIN_RESPONSE_SIZE = 4; // addr + cmd + len + at least 1 byte
}

namespace Checksums {
    namespace Xor8 {
        constexpr uint8_t  INITIAL  = 0x00;
        constexpr size_t   SIZE     = 1;
    }
    namespace Crc8 {
        constexpr uint8_t  INITIAL  = 0x00;
        constexpr uint8_t  POLY     = 0x07;
        constexpr uint8_t  MSB_MASK = 0x80;
        constexpr size_t   SIZE     = 1;
    }
    namespace Crc16Modbus {
        constexpr uint16_t INITIAL  = 0xFFFF;
        constexpr uint16_t POLY     = 0xA001;
        constexpr uint8_t  LSB_MASK = 0x01;
        constexpr size_t   SIZE     = 2;
    }
    constexpr int BITS_PER_BYTE = CHAR_BIT;
}

namespace Rego800 {
    constexpr uint8_t  MODBUS_ADDRESS   = 0x02;
    constexpr uint8_t  MODBUS_FUNC_READ = 0x06;
    constexpr uint8_t  REGISTER_COUNT   = 0x03;
    constexpr uint8_t  IDENT_CMD_BYTE   = 0x03;

    constexpr size_t   IDENT_RESPONSE_BUFFER = 64;
    constexpr size_t   RESPONSE_BUFFER_RESERVE = 128;
    constexpr size_t   MIN_RESPONSE_SIZE = 4;

    using namespace std::chrono_literals;
    constexpr auto     READ_TIMEOUT = std::chrono::milliseconds(500);

    constexpr float    TEMP_SCALE   = 0.1f;
    constexpr float    TEMP_OFFSET  = -50.0f;
    constexpr float    TEMP_OFFSET_ZERO = 0.0f;
    constexpr float    SPEED_SCALE  = 1.0f;
    constexpr float    POWER_SCALE  = 0.1f;
    constexpr float    NO_SCALE     = 1.0f;
    constexpr float    NO_OFFSET    = 0.0f;
}

namespace Rego600 {
    constexpr uint8_t  MODBUS_ADDRESS   = 0x02;
    constexpr uint8_t  MODBUS_FUNC_READ = 0x06;
    constexpr size_t   RESPONSE_BUFFER_RESERVE = 100;
    constexpr size_t   MAX_REGISTERS    = 60;
    constexpr size_t   ENTRY_STRIDE     = 16;

    using namespace std::chrono_literals;
    constexpr auto     READ_TIMEOUT = std::chrono::milliseconds(500);

    constexpr float    TEMP_SCALE   = 0.1f;
    constexpr float    TEMP_OFFSET  = -50.0f;
}

namespace Rego1000 {
    constexpr size_t   REGISTER_COUNT   = 58;
    constexpr size_t   RESPONSE_BUFFER_RESERVE = 256;

    using namespace std::chrono_literals;
    constexpr auto     READ_TIMEOUT = std::chrono::milliseconds(500);

    constexpr float    TEMP_SCALE   = 0.1f;
    constexpr float    TEMP_OFFSET  = -50.0f;
}

namespace Nibe {
    constexpr size_t   RESPONSE_BUFFER_RESERVE = 512;
    constexpr size_t   MAX_REGISTERS = 160;
    constexpr size_t   ENTRY_STRIDE  = 16;

    using namespace std::chrono_literals;
    constexpr auto     READ_TIMEOUT = std::chrono::milliseconds(3000);
    constexpr auto     WRITE_TIMEOUT = std::chrono::milliseconds(3000);

    constexpr float    TEMP_SCALE   = 0.1f;
    constexpr float    TEMP_OFFSET  = -50.0f;
}

namespace Thermia {
    constexpr size_t   MAX_REGISTERS          = 200;
    constexpr size_t   RESPONSE_BUFFER_RESERVE = 256;
    constexpr size_t   SCAN_CYCLE_DELAY       = 5;

    using namespace std::chrono_literals;
    constexpr auto     READ_TIMEOUT = std::chrono::milliseconds(2000);

    constexpr float    TEMP_SCALE   = 0.1f;
    constexpr float    TEMP_OFFSET  = -50.0f;
}

namespace ThermiaGenesis {
    constexpr uint8_t  SLAVE_ADDRESS         = 0x01;
    constexpr uint8_t  READ_INPUT_REGISTERS  = 0x04;
    constexpr uint8_t  READ_HOLDING_REGISTERS = 0x03;
    constexpr uint8_t  WRITE_HOLDING_REGISTER = 0x06;
    constexpr size_t   RESPONSE_BUFFER_RESERVE = 256;
    constexpr size_t   MAX_REGISTERS         = 200;
    constexpr uint16_t VERSION_MAJOR_REG     = 311; // 30312
    constexpr uint16_t VERSION_MINOR_REG     = 312; // 30313
    constexpr uint16_t VERSION_MICRO_REG     = 313; // 30314

    using namespace std::chrono_literals;
    constexpr auto     READ_TIMEOUT = std::chrono::milliseconds(2000);
    constexpr auto     WRITE_TIMEOUT = std::chrono::milliseconds(2000);

    constexpr float    TEMP_SCALE   = 0.01f;
    constexpr float    TEMP_OFFSET  = 0.0f;
}

namespace EmsBosch {
    constexpr size_t   RESPONSE_BUFFER_RESERVE = 256;
    constexpr size_t   MAX_REGISTERS = 90;
    constexpr size_t   EMS_POLL_INTERVAL = 5;

    using namespace std::chrono_literals;
    constexpr auto     READ_TIMEOUT = std::chrono::milliseconds(2000);

    constexpr float    TEMP_SCALE   = 0.1f;
    constexpr float    TEMP_OFFSET  = -50.0f;
}

namespace Rego5200 {
    constexpr uint32_t BAUDRATE     = 115200;
    constexpr size_t   RESPONSE_BUFFER_RESERVE = 256;
    constexpr size_t   MAX_REGISTERS = 80;

    using namespace std::chrono_literals;
    constexpr auto     READ_TIMEOUT = std::chrono::milliseconds(1000);

    constexpr float    TEMP_SCALE   = 0.1f;
    constexpr float    TEMP_OFFSET  = -50.0f;
}

namespace Rc100H {
    namespace Registers {
        constexpr uint16_t ROOM_TEMP_CTRL_1   = 0x2F1;
        constexpr uint16_t ALARM_RESET        = 0x2F2;
        constexpr uint16_t DIAL_RANGE         = 0x2F3;
        constexpr uint16_t ROOM_TEMP_CTRL_2   = 0x2F4;
        constexpr uint16_t ROOM_HUMID_CTRL    = 0x2F5;
        constexpr uint16_t EXT_PORT_3         = 0x2FA;
        constexpr uint16_t EXT_PORT_4         = 0x2FB;
        constexpr uint16_t HUMIDITY_VALUE     = 0x32F5;
    }

    enum class Mode : uint8_t {
        OFF    = 0,
        TYP1   = 1,
        TYP2   = 2,
        RC100H = 3,
    };

    constexpr const char* MODE_NAMES[] = {"OFF", "TYP1", "TYP2", "RC100H"};

    using namespace std::chrono_literals;
    constexpr auto     EMULATION_INTERVAL = std::chrono::milliseconds(10000);
    constexpr auto     CAN_TIMEOUT        = std::chrono::milliseconds(2000);
}

namespace ByteMasks {
    constexpr uint8_t  LOW_BYTE    = 0xFF;
    constexpr uint16_t U16_LOW     = 0x00FF;
    constexpr uint16_t U16_HIGH    = 0xFF00;
    constexpr uint32_t U32_BYTE0   = 0x000000FF;
    constexpr uint32_t U32_BYTE1   = 0x0000FF00;
    constexpr uint32_t U32_BYTE2   = 0x00FF0000;
    constexpr uint32_t U32_BYTE3   = 0xFF000000;
}

namespace Endian {
    constexpr int U16_LOW_SHIFT  = 8;
    constexpr int U32_B0_SHIFT   = 24;
    constexpr int U32_B1_SHIFT   = 16;
    constexpr int U32_B2_SHIFT   = 8;
}

} // namespace Constants
