#pragma once

#include <cstddef>
#include <cstdint>

#include <unity.h>

#include "AT21CS/AT21CS.h"
#include "support/ExpectedFrames.h"
#include "support/ScriptedTransport.h"

namespace AT21CS::test {

constexpr const char* expectedErrName(Err value) {
  switch (value) {
    case Err::OK: return "OK";
    case Err::NOT_BOUND: return "NOT_BOUND";
    case Err::NOT_INITIALIZED: return "NOT_INITIALIZED";
    case Err::INVALID_STATE: return "INVALID_STATE";
    case Err::BUSY: return "BUSY";
    case Err::INVALID_CONFIG: return "INVALID_CONFIG";
    case Err::INVALID_PARAM: return "INVALID_PARAM";
    case Err::NOT_PRESENT: return "NOT_PRESENT";
    case Err::NACK_DEVICE_ADDRESS: return "NACK_DEVICE_ADDRESS";
    case Err::NACK_MEMORY_ADDRESS: return "NACK_MEMORY_ADDRESS";
    case Err::NACK_DATA: return "NACK_DATA";
    case Err::TRANSPORT_TIMEOUT: return "TRANSPORT_TIMEOUT";
    case Err::LINE_STUCK: return "LINE_STUCK";
    case Err::IO_ERROR: return "IO_ERROR";
    case Err::CLOCK_STALLED: return "CLOCK_STALLED";
    case Err::UNSUPPORTED_COMMAND: return "UNSUPPORTED_COMMAND";
    case Err::CRC_MISMATCH: return "CRC_MISMATCH";
    case Err::PART_MISMATCH: return "PART_MISMATCH";
    case Err::VERIFY_MISMATCH: return "VERIFY_MISMATCH";
    case Err::INDETERMINATE: return "INDETERMINATE";
  }
  return "UNKNOWN";
}

inline TransferResult auxiliaryOk(TransferPhase phase) {
  TransferResult result{};
  result.code = TransportCode::OK;
  result.phase = phase;
  return result;
}

inline TransferScript directReadOk(const uint8_t* data, size_t length) {
  TransferScript script{};
  script.result.code = TransportCode::OK;
  script.result.phase = TransferPhase::STOP;
  script.result.dataBytesTransferred = length;
  script.result.firstDeviceAddressAcked = true;
  script.result.stopCompleted = true;
  script.rxLength = length;
  for (size_t index = 0; index < length; ++index) {
    script.rxData[index] = data[index];
  }
  return script;
}

inline TransferScript randomReadOk(const uint8_t* data, size_t length) {
  TransferScript script = directReadOk(data, length);
  script.result.memoryAddressAcked = true;
  script.result.repeatedDeviceAddressAcked = true;
  return script;
}

inline TransferScript addressOnlyOk() {
  TransferScript script{};
  script.result.code = TransportCode::OK;
  script.result.phase = TransferPhase::STOP;
  script.result.firstDeviceAddressAcked = true;
  script.result.stopCompleted = true;
  return script;
}

inline TransferScript withExpected(TransferScript script,
                                   const ExpectedTransfer& expectedTransfer) {
  script.expected = expectedTransfer;
  return script;
}

inline TransferScript manufacturerIdOk(uint32_t manufacturerId,
                                       uint8_t addressBits = 0u,
                                       SpeedMode speed =
                                           SpeedMode::HIGH_SPEED) {
  const uint8_t bytes[3] = {
      static_cast<uint8_t>((manufacturerId >> 16u) & 0xFFu),
      static_cast<uint8_t>((manufacturerId >> 8u) & 0xFFu),
      static_cast<uint8_t>(manufacturerId & 0xFFu)};
  TransferScript script = directReadOk(bytes, sizeof(bytes));
  script.expected = expected::directRead(
      expected::rawAddress(expected::MANUFACTURER_ID_OPCODE,
                           addressBits, true),
      sizeof(bytes), speed,
      speed == SpeedMode::HIGH_SPEED
          ? expected::HIGH_SPEED_POST_HIGH_US
          : expected::STANDARD_SPEED_POST_HIGH_US);
  return script;
}

inline void queueResetOk(ScriptedTransport& fake) {
  BooleanScript reset{};
  reset.result = auxiliaryOk(TransferPhase::DISCOVERY_RELEASE);
  reset.value = true;
  TEST_ASSERT_TRUE(fake.queueReset(reset));
}

inline void queuePresence(ScriptedTransport& fake,
                          bool present,
                          TransferResult result =
                              auxiliaryOk(TransferPhase::PRESENCE)) {
  BooleanScript presence{};
  presence.result = result;
  presence.value = present;
  TEST_ASSERT_TRUE(fake.queuePresence(presence));
}

inline void queueInitialize(ScriptedTransport& fake,
                            uint32_t manufacturerId,
                            bool standardSpeed = false,
                            uint8_t addressBits = 0u) {
  queueResetOk(fake);
  TEST_ASSERT_TRUE(fake.queueTransfer(
      manufacturerIdOk(manufacturerId, addressBits)));
  if (standardSpeed) {
    TransferScript speed = addressOnlyOk();
    speed.expected = expected::addressOnly(
        expected::rawAddress(expected::STANDARD_SPEED_OPCODE,
                             addressBits, false),
        SpeedMode::HIGH_SPEED, expected::SPEED_CHANGE_POST_HIGH_US);
    TEST_ASSERT_TRUE(fake.queueTransfer(speed));
  }
}

inline void bindBus(Bus& bus,
                    ScriptedTransport& fake,
                    bool withPresence = false) {
  BusConfig config{};
  config.transport = fake.descriptor(withPresence);
  TEST_ASSERT_TRUE(bus.bind(config).ok());
}

inline void initializeDriver(Driver& driver,
                             Bus& bus,
                             ScriptedTransport& fake,
                             const Config& config,
                             uint32_t manufacturerId) {
  queueInitialize(fake, manufacturerId,
                  config.startupSpeed == SpeedMode::STANDARD_SPEED,
                  config.addressBits);
  TEST_ASSERT_TRUE(driver.begin(bus, config).ok());
}

inline void assertStatus(Err expected, const Status& status) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected),
                          static_cast<uint8_t>(status.code));
  TEST_ASSERT_EQUAL_STRING(expectedErrName(expected), status.msg);
}

inline void assertOracleClean(const ScriptedTransport& fake) {
  TEST_ASSERT_FALSE(fake.overflow);
  TEST_ASSERT_FALSE(fake.mismatch);
  TEST_ASSERT_FALSE(fake.transferDuringWriteHighHold);
  TEST_ASSERT_FALSE(fake.resetDuringWriteHighHold);
  TEST_ASSERT_TRUE(fake.allScriptsConsumed());
}

}  // namespace AT21CS::test
