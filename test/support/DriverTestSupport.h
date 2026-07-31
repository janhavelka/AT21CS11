#pragma once

#include <cstddef>
#include <cstdint>

#include <unity.h>

#include "AT21CS/AT21CS.h"
#include "support/ScriptedTransport.h"

namespace AT21CS::test {

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

inline TransferScript manufacturerIdOk(uint32_t manufacturerId) {
  const uint8_t bytes[3] = {
      static_cast<uint8_t>((manufacturerId >> 16u) & 0xFFu),
      static_cast<uint8_t>((manufacturerId >> 8u) & 0xFFu),
      static_cast<uint8_t>(manufacturerId & 0xFFu)};
  return directReadOk(bytes, sizeof(bytes));
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
                            bool standardSpeed = false) {
  queueResetOk(fake);
  TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(manufacturerId)));
  if (standardSpeed) {
    TEST_ASSERT_TRUE(fake.queueTransfer(addressOnlyOk()));
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
                  config.startupSpeed == SpeedMode::STANDARD_SPEED);
  TEST_ASSERT_TRUE(driver.begin(bus, config).ok());
}

inline void assertStatus(Err expected, const Status& status) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected),
                          static_cast<uint8_t>(status.code));
  TEST_ASSERT_EQUAL_STRING(toString(expected), status.msg);
}

}  // namespace AT21CS::test
