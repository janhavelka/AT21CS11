#pragma once

#include <cstddef>
#include <cstdint>

#include "support/ScriptedTransport.h"

namespace AT21CS::test::expected {

inline constexpr uint8_t EEPROM_OPCODE = 0x0Au;
inline constexpr uint8_t SECURITY_OPCODE = 0x0Bu;
inline constexpr uint8_t MANUFACTURER_ID_OPCODE = 0x0Cu;
inline constexpr uint8_t STANDARD_SPEED_OPCODE = 0x0Du;
inline constexpr uint8_t HIGH_SPEED_OPCODE = 0x0Eu;
inline constexpr uint8_t ROM_ZONE_OPCODE = 0x07u;
inline constexpr uint8_t FREEZE_ROM_OPCODE = 0x01u;
inline constexpr uint8_t LOCK_SECURITY_OPCODE = 0x02u;

inline constexpr uint32_t HIGH_SPEED_POST_HIGH_US = 160u;
inline constexpr uint32_t STANDARD_SPEED_POST_HIGH_US = 650u;
inline constexpr uint32_t SPEED_CHANGE_POST_HIGH_US = 650u;
inline constexpr uint32_t TRANSFER_TIMEOUT_US = 9000u;
inline constexpr uint32_t RESET_TIMEOUT_US = 5000u;
inline constexpr uint32_t WRITE_HIGH_HOLD_US = 10000u;

constexpr uint8_t rawAddress(uint8_t opcode,
                             uint8_t addressBits,
                             bool read) {
  return static_cast<uint8_t>((static_cast<uint32_t>(opcode) << 4u) |
                              (static_cast<uint32_t>(addressBits) << 1u) |
                              (read ? 1u : 0u));
}

inline ExpectedTransfer addressOnly(uint8_t rawDeviceAddress,
                                    SpeedMode speed,
                                    uint32_t postHighUs) {
  ExpectedTransfer expected{};
  expected.enabled = true;
  expected.speed = speed;
  expected.deviceAddress = rawDeviceAddress;
  expected.minimumPostTransferHighUs = postHighUs;
  return expected;
}

inline ExpectedTransfer directRead(uint8_t rawDeviceAddress,
                                   size_t length,
                                   SpeedMode speed,
                                   uint32_t postHighUs) {
  ExpectedTransfer expected =
      addressOnly(rawDeviceAddress, speed, postHighUs);
  expected.rxLength = length;
  return expected;
}

inline ExpectedTransfer randomRead(uint8_t rawWriteAddress,
                                   uint8_t memoryAddress,
                                   uint8_t rawReadAddress,
                                   size_t length,
                                   SpeedMode speed,
                                   uint32_t postHighUs) {
  ExpectedTransfer expected =
      addressOnly(rawWriteAddress, speed, postHighUs);
  expected.hasMemoryAddress = true;
  expected.memoryAddress = memoryAddress;
  expected.hasRepeatedStart = true;
  expected.repeatedDeviceAddress = rawReadAddress;
  expected.rxLength = length;
  return expected;
}

inline ExpectedTransfer pageWrite(uint8_t rawWriteAddress,
                                  uint8_t memoryAddress,
                                  const uint8_t* data,
                                  size_t length,
                                  SpeedMode speed,
                                  uint32_t postHighUs) {
  ExpectedTransfer expected =
      addressOnly(rawWriteAddress, speed, postHighUs);
  expected.hasMemoryAddress = true;
  expected.memoryAddress = memoryAddress;
  expected.txLength = length;
  if (data != nullptr) {
    const size_t copyLength = length < 8u ? length : 8u;
    for (size_t index = 0; index < copyLength; ++index) {
      expected.txData[index] = data[index];
    }
  }
  return expected;
}

inline ExpectedTransfer withDeadline(ExpectedTransfer expected,
                                     uint64_t deadlineUs) {
  expected.verifyDeadline = true;
  expected.deadlineUs = deadlineUs;
  return expected;
}

}  // namespace AT21CS::test::expected
