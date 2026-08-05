/// @file CommandTable.h
/// @brief Protocol constants for AT21CS01/AT21CS11.
#pragma once

#include <cstddef>
#include <cstdint>

namespace AT21CS {
namespace cmd {

// Device command opcodes (bits 7:4 of device address byte).
static constexpr uint8_t OPCODE_EEPROM = 0x0A;
static constexpr uint8_t OPCODE_SECURITY = 0x0B;
static constexpr uint8_t OPCODE_MANUFACTURER_ID = 0x0C;
static constexpr uint8_t OPCODE_STANDARD_SPEED = 0x0D;
static constexpr uint8_t OPCODE_HIGH_SPEED = 0x0E;
static constexpr uint8_t OPCODE_ROM_ZONE = 0x07;
static constexpr uint8_t OPCODE_FREEZE_ROM = 0x01;
static constexpr uint8_t OPCODE_LOCK_SECURITY = 0x02;

// Memory sizes.
static constexpr size_t EEPROM_SIZE = 128;
static constexpr size_t SECURITY_SIZE = 32;
static constexpr size_t PAGE_SIZE = 8;

// Security register layout.
static constexpr uint8_t SECURITY_USER_MIN = 0x10;
static constexpr uint8_t SECURITY_USER_MAX = 0x1F;
static constexpr uint8_t SECURITY_SERIAL_START = 0x00;
static constexpr size_t SECURITY_SERIAL_SIZE = 8;
static constexpr uint8_t SECURITY_PRODUCT_ID = 0xA0;

// Manufacturer ID part/revision fields (24-bit values).
static constexpr uint32_t MANUFACTURER_ID_PART_MASK = 0x00FFFFF8u;
static constexpr uint32_t MANUFACTURER_ID_AT21CS01_BASE = 0x00D200u;
static constexpr uint32_t MANUFACTURER_ID_AT21CS11_BASE = 0x00D380u;
static constexpr uint32_t MANUFACTURER_ID_REVISION_MASK = 0x00000007u;

// ROM-zone configuration. Applications should use Driver's guarded permanent
// methods instead of constructing these raw frames.
static constexpr uint8_t ROM_ZONE_REGISTER_COUNT = 4;
static constexpr uint8_t ROM_ZONE_REGISTERS[ROM_ZONE_REGISTER_COUNT] = {0x01, 0x02, 0x04, 0x08};
static constexpr uint8_t ROM_ZONE_ROM_VALUE = 0xFF;

// Datasheet-mandated irreversible Freeze payload. Freeze locks the four
// ROM-zone configuration bits; it does not directly freeze EEPROM data.
static constexpr uint8_t FREEZE_ROM_ADDR = 0x55;
static constexpr uint8_t FREEZE_ROM_DATA = 0xAA;

// Datasheet-mandated irreversible Security Lock command address format.
static constexpr uint8_t LOCK_SECURITY_ADDRESS = 0x60;

}  // namespace cmd
}  // namespace AT21CS
