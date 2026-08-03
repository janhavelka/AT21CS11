#include <cstdint>

#include <unity.h>

#include "AT21CS/AT21CS.h"
#include "support/TestBuilders.h"

using namespace AT21CS;
using namespace AT21CS::test;

namespace {

constexpr uint32_t CS01_ID = 0x00D203u;
constexpr uint32_t CS11_ID = 0x00D385u;

TransferScript expectedRead(uint8_t addressBits, uint8_t value) {
  TransferScript script = randomReadOk(&value, 1u);
  script.expected = expected::randomRead(
      expected::rawAddress(expected::EEPROM_OPCODE, addressBits, false),
      0u,
      expected::rawAddress(expected::EEPROM_OPCODE, addressBits, true),
      1u,
      SpeedMode::HIGH_SPEED,
      expected::HIGH_SPEED_POST_HIGH_US);
  return script;
}

TransferScript failedRead(uint8_t addressBits, int32_t detail) {
  TransferScript script{};
  script.expected = expected::randomRead(
      expected::rawAddress(expected::EEPROM_OPCODE, addressBits, false),
      0u,
      expected::rawAddress(expected::EEPROM_OPCODE, addressBits, true),
      1u,
      SpeedMode::HIGH_SPEED,
      expected::HIGH_SPEED_POST_HIGH_US);
  script.result.code = TransportCode::TIMEOUT;
  script.result.phase = TransferPhase::START;
  script.result.detail = detail;
  return script;
}

TransferScript expectedWrite(uint8_t addressBits, uint8_t value) {
  TransferScript script{};
  script.expected = expected::pageWrite(
      expected::rawAddress(expected::EEPROM_OPCODE, addressBits, false),
      0u, &value, 1u, SpeedMode::HIGH_SPEED,
      expected::HIGH_SPEED_POST_HIGH_US);
  script.result.code = TransportCode::OK;
  script.result.phase = TransferPhase::STOP;
  script.result.dataBytesTransferred = 1u;
  script.result.firstDeviceAddressAcked = true;
  script.result.memoryAddressAcked = true;
  script.result.stopCompleted = true;
  return script;
}

WaitScript failedHold(int32_t detail) {
  WaitScript script{};
  script.result.code = TransportCode::TIMEOUT;
  script.result.phase = TransferPhase::WAIT_HIGH;
  script.result.detail = detail;
  return script;
}

}  // namespace

void test_one_bus_routes_unique_addresses_and_isolates_driver_health() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);

  Driver first;
  Driver second;
  Config firstConfig{};
  firstConfig.expectedPart = PartType::AT21CS11;
  firstConfig.offlineThreshold = 1u;
  Config secondConfig = firstConfig;
  secondConfig.addressBits = 1u;
  initializeDriver(first, bus, fake, firstConfig, CS11_ID);
  initializeDriver(second, bus, fake, secondConfig, CS11_ID);

  const SettingsSnapshot secondBefore = second.snapshot();
  TEST_ASSERT_TRUE(fake.queueTransfer(failedRead(0u, 2101)));
  uint8_t value = 0u;
  assertStatus(Err::TRANSPORT_TIMEOUT, first.readEeprom(0u, &value, 1u));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(first.state()));
  const SettingsSnapshot secondAfterFailure = second.snapshot();
  TEST_ASSERT_EQUAL_UINT32(secondBefore.totalSuccess,
                           secondAfterFailure.totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(secondBefore.totalFailures,
                           secondAfterFailure.totalFailures);
  TEST_ASSERT_EQUAL_UINT8(secondBefore.consecutiveFailures,
                          secondAfterFailure.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(secondBefore.state),
                          static_cast<uint8_t>(secondAfterFailure.state));

  TEST_ASSERT_TRUE(fake.queueTransfer(expectedRead(1u, 0x62u)));
  TEST_ASSERT_TRUE(second.readEeprom(0u, &value, 1u).ok());
  TEST_ASSERT_EQUAL_HEX8(0x62u, value);
  TEST_ASSERT_EQUAL_UINT32(secondBefore.totalSuccess + 1u,
                           second.snapshot().totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(secondBefore.totalFailures,
                           second.snapshot().totalFailures);
  TEST_ASSERT_EQUAL_HEX8(0xA0u, fake.captured[fake.capturedCount - 2u]
                                    .deviceAddress);
  TEST_ASSERT_EQUAL_HEX8(0xA2u, fake.captured[fake.capturedCount - 1u]
                                    .deviceAddress);
  assertOracleClean(fake);
}

void test_shared_reset_resynchronizes_each_device_without_cross_health() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);

  Driver high;
  Driver standard;
  Config highConfig{};
  highConfig.expectedPart = PartType::AT21CS01;
  Config standardConfig = highConfig;
  standardConfig.addressBits = 1u;
  standardConfig.startupSpeed = SpeedMode::STANDARD_SPEED;
  initializeDriver(high, bus, fake, highConfig, CS01_ID);
  initializeDriver(standard, bus, fake, standardConfig, CS01_ID);
  const SettingsSnapshot highBefore = high.snapshot();

  queueInitialize(fake, CS01_ID, true, 1u);
  TEST_ASSERT_TRUE(standard.recover().ok());
  TEST_ASSERT_FALSE(high.isSpeedKnown());
  TEST_ASSERT_EQUAL_UINT32(highBefore.totalSuccess,
                           high.snapshot().totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(highBefore.totalFailures,
                           high.snapshot().totalFailures);

  TEST_ASSERT_TRUE(fake.queueTransfer(expectedRead(0u, 0x31u)));
  uint8_t value = 0u;
  TEST_ASSERT_TRUE(high.readEeprom(0u, &value, 1u).ok());
  TEST_ASSERT_EQUAL_HEX8(0x31u, value);
  TEST_ASSERT_TRUE(high.isSpeedKnown());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SpeedMode::HIGH_SPEED),
                          static_cast<uint8_t>(high.speedMode()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SpeedMode::STANDARD_SPEED),
                          static_cast<uint8_t>(standard.speedMode()));
  assertOracleClean(fake);
}

void test_failed_shared_reset_invalidates_both_device_speed_views() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver first;
  Driver second;
  Config secondConfig{};
  secondConfig.addressBits = 1u;
  initializeDriver(first, bus, fake, Config{}, CS11_ID);
  initializeDriver(second, bus, fake, secondConfig, CS11_ID);
  const uint64_t generationBefore = bus.generation();
  const SettingsSnapshot firstBefore = first.snapshot();

  BooleanScript failedReset{};
  failedReset.result.code = TransportCode::LINE_STUCK;
  failedReset.result.phase = TransferPhase::DISCOVERY_RELEASE;
  failedReset.result.detail = 2151;
  TEST_ASSERT_TRUE(fake.queueReset(failedReset));
  assertStatus(Err::LINE_STUCK, second.recover());
  TEST_ASSERT_EQUAL_UINT64(generationBefore + 1u, bus.generation());
  TEST_ASSERT_FALSE(bus.snapshot().resetEstablishedHighSpeed);
  TEST_ASSERT_FALSE(first.isSpeedKnown());
  TEST_ASSERT_FALSE(second.isSpeedKnown());
  TEST_ASSERT_EQUAL_UINT32(firstBefore.totalSuccess,
                           first.snapshot().totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(firstBefore.totalFailures,
                           first.snapshot().totalFailures);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(first.state()));
  const size_t callbacks = fake.transferCalls;
  uint8_t value = 0u;
  assertStatus(Err::INVALID_STATE,
               first.readEeprom(0u, &value, 1u));
  TEST_ASSERT_EQUAL_UINT32(callbacks, fake.transferCalls);
  assertOracleClean(fake);
}

void test_separate_buses_reuse_addresses_and_isolate_hold_and_lifecycle() {
  ScriptedTransport fakeA;
  ScriptedTransport fakeB;
  Bus busA;
  Bus busB;
  bindBus(busA, fakeA);
  bindBus(busB, fakeB);

  Driver driverA;
  Driver driverB;
  Config config{};
  config.expectedPart = PartType::AT21CS11;
  config.offlineThreshold = 1u;
  initializeDriver(driverA, busA, fakeA, config, CS11_ID);
  initializeDriver(driverB, busB, fakeB, config, CS11_ID);
  TEST_ASSERT_EQUAL_HEX8(0x01u, busA.snapshot().claimedAddressMask);
  TEST_ASSERT_EQUAL_HEX8(0x01u, busB.snapshot().claimedAddressMask);

  const SettingsSnapshot beforeB = driverB.snapshot();
  const uint8_t written = 0xA7u;
  TEST_ASSERT_TRUE(fakeA.queueTransfer(expectedWrite(0u, written)));
  TEST_ASSERT_TRUE(fakeA.queueWait(failedHold(2201)));
  WriteResult writeResult{};
  assertStatus(Err::TRANSPORT_TIMEOUT,
               driverA.writeEepromPage(0u, &written, 1u, writeResult));
  TEST_ASSERT_EQUAL_UINT8(
                          static_cast<uint8_t>(WriteEffect::MAY_HAVE_COMMITTED),
                          static_cast<uint8_t>(writeResult.lastPageEffect));
  TEST_ASSERT_NOT_EQUAL(0u, busA.snapshot().writeHighUntilUs);
  TEST_ASSERT_EQUAL_UINT64(0u, busB.snapshot().writeHighUntilUs);

  TEST_ASSERT_TRUE(fakeB.queueTransfer(expectedRead(0u, 0xB4u)));
  uint8_t value = 0u;
  TEST_ASSERT_TRUE(driverB.readEeprom(0u, &value, 1u).ok());
  TEST_ASSERT_EQUAL_HEX8(0xB4u, value);
  TEST_ASSERT_EQUAL_UINT32(beforeB.totalSuccess + 1u,
                           driverB.snapshot().totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(beforeB.totalFailures,
                           driverB.snapshot().totalFailures);
  TEST_ASSERT_EQUAL_UINT32(0u, fakeB.waitCalls);

  driverA.end();
  WaitScript clear{};
  clear.result = auxiliaryOk(TransferPhase::WAIT_HIGH);
  clear.advanceToDeadline = true;
  TEST_ASSERT_TRUE(fakeA.queueWait(clear));
  TEST_ASSERT_TRUE(busA.end().ok());
  ScriptedTransport replacementA;
  BusConfig replacementConfig{};
  replacementConfig.transport = replacementA.descriptor(false);
  TEST_ASSERT_TRUE(busA.bind(replacementConfig).ok());

  const BusSnapshot busBBeforeARecovery = busB.snapshot();
  const SettingsSnapshot driverBBeforeARecovery = driverB.snapshot();
  const size_t callbacksBBeforeARecovery = fakeB.eventCount;
  TEST_ASSERT_TRUE(driverA.bind(busA, config).ok());
  queueInitialize(replacementA, CS01_ID);
  const Status mismatchStatus = driverA.initialize();
  TEST_ASSERT_EQUAL_HEX32(0u, replacementA.mismatchFields);
  assertStatus(Err::PART_MISMATCH, mismatchStatus);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(driverA.state()));
  assertStatus(Err::INVALID_STATE, driverA.recover());
  TEST_ASSERT_TRUE(driverA.bind(busA, config).ok());
  queueInitialize(replacementA, CS11_ID);
  TEST_ASSERT_TRUE(driverA.initialize().ok());
  TEST_ASSERT_TRUE(replacementA.queueTransfer(failedRead(0u, 2202)));
  assertStatus(Err::TRANSPORT_TIMEOUT,
               driverA.readEeprom(0u, &value, 1u));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(driverA.state()));
  queueInitialize(replacementA, CS11_ID);
  TEST_ASSERT_TRUE(driverA.recover().ok());
  TEST_ASSERT_TRUE(driverA.isOnline());
  TEST_ASSERT_EQUAL_UINT64(busBBeforeARecovery.bindingEpoch,
                           busB.snapshot().bindingEpoch);
  TEST_ASSERT_EQUAL_UINT64(busBBeforeARecovery.generation,
                           busB.snapshot().generation);
  TEST_ASSERT_EQUAL_HEX8(busBBeforeARecovery.claimedAddressMask,
                         busB.snapshot().claimedAddressMask);
  TEST_ASSERT_EQUAL_UINT32(driverBBeforeARecovery.totalSuccess,
                           driverB.snapshot().totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(driverBBeforeARecovery.totalFailures,
                           driverB.snapshot().totalFailures);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(driverBBeforeARecovery.state),
                          static_cast<uint8_t>(driverB.state()));
  TEST_ASSERT_EQUAL_UINT32(callbacksBBeforeARecovery, fakeB.eventCount);

  TEST_ASSERT_TRUE(fakeB.queueTransfer(expectedRead(0u, 0xC5u)));
  TEST_ASSERT_TRUE(driverB.readEeprom(0u, &value, 1u).ok());
  TEST_ASSERT_EQUAL_HEX8(0xC5u, value);
  TEST_ASSERT_TRUE(driverB.isOnline());
  TEST_ASSERT_EQUAL_UINT32(0u, fakeB.resetCalls - 1u);
  TEST_ASSERT_EQUAL_UINT32(0u, fakeB.waitCalls);
  driverA.end();
  assertOracleClean(fakeA);
  assertOracleClean(replacementA);
  assertOracleClean(fakeB);
}
