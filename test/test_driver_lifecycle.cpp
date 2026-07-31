#include <cstdint>
#include <limits>

#include <unity.h>

#include "AT21CS/AT21CS.h"
#include "support/DriverTestSupport.h"
#include "support/TestAccess.h"

using namespace AT21CS;
using namespace AT21CS::test;

namespace {

constexpr uint32_t CS01_ID = 0x00D203u;
constexpr uint32_t CS11_ID = 0x00D385u;

TransferScript transferFailure(TransportCode code,
                               TransferPhase phase,
                               int32_t detail) {
  TransferScript script{};
  script.result.code = code;
  script.result.phase = phase;
  script.result.detail = detail;
  return script;
}

}  // namespace

void test_driver_bind_is_callback_free() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  const size_t eventsBefore = fake.eventCount;

  Driver driver;
  Config config{};
  config.addressBits = 3;
  TEST_ASSERT_TRUE(driver.bind(bus, config).ok());
  TEST_ASSERT_EQUAL_UINT32(eventsBefore, fake.eventCount);
  TEST_ASSERT_EQUAL_UINT32(0, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT32(0, fake.resetCalls);
  TEST_ASSERT_EQUAL_UINT8(0x08u, bus.snapshot().claimedAddressMask);
}

void test_driver_invalid_rebind_preserves_working_binding() {
  ScriptedTransport fakeA;
  ScriptedTransport fakeB;
  Bus busA;
  Bus busB;
  bindBus(busA, fakeA);
  bindBus(busB, fakeB);

  Driver driver;
  Config original{};
  original.addressBits = 2;
  initializeDriver(driver, busA, fakeA, original, CS11_ID);

  Config invalid{};
  invalid.addressBits = 4;
  invalid.startupSpeed = SpeedMode::STANDARD_SPEED;
  const Status status = driver.begin(busB, invalid);
  assertStatus(Err::INVALID_CONFIG, status);
  TEST_ASSERT_EQUAL_UINT8(0x04u, busA.snapshot().claimedAddressMask);
  TEST_ASSERT_EQUAL_UINT8(0x00u, busB.snapshot().claimedAddressMask);

  const uint8_t expected = 0x5Au;
  TEST_ASSERT_TRUE(fakeA.queueTransfer(randomReadOk(&expected, 1)));
  uint8_t value = 0;
  TEST_ASSERT_TRUE(driver.readEeprom(0, &value, 1).ok());
  TEST_ASSERT_EQUAL_HEX8(expected, value);
  TEST_ASSERT_EQUAL_UINT32(0, fakeB.transferCalls);
}

void test_standard_startup_requires_explicit_at21cs01() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);

  Driver driver;
  Config config{};
  config.startupSpeed = SpeedMode::STANDARD_SPEED;
  assertStatus(Err::INVALID_CONFIG, driver.bind(bus, config));
  config.expectedPart = PartType::AT21CS11;
  assertStatus(Err::INVALID_CONFIG, driver.bind(bus, config));
  config.expectedPart = PartType::AT21CS01;
  TEST_ASSERT_TRUE(driver.bind(bus, config).ok());
  driver.end();
  config.addressBits = 8;
  assertStatus(Err::INVALID_CONFIG, driver.bind(bus, config));
  config.addressBits = 0;
  config.expectedPart = static_cast<PartType>(0xFFu);
  assertStatus(Err::INVALID_CONFIG, driver.bind(bus, config));
  config.expectedPart = PartType::AT21CS01;
  config.startupSpeed = static_cast<SpeedMode>(0xFFu);
  assertStatus(Err::INVALID_CONFIG, driver.bind(bus, config));
  TEST_ASSERT_EQUAL_UINT32(0, fake.eventCount);
}

void test_begin_absence_retains_binding_and_exact_status() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake, true);
  queuePresence(fake, false);

  Driver driver;
  Config config{};
  config.addressBits = 6;
  const Status status = driver.begin(bus, config);
  assertStatus(Err::NOT_PRESENT, status);
  TEST_ASSERT_EQUAL_INT32(0, status.detail);
  TEST_ASSERT_TRUE(driver.isBound());
  TEST_ASSERT_FALSE(driver.isInitialized());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(driver.state()));
  TEST_ASSERT_EQUAL_UINT8(0x40u, bus.snapshot().claimedAddressMask);
  TEST_ASSERT_EQUAL_UINT32(0, fake.resetCalls);
  TEST_ASSERT_EQUAL_UINT32(1, driver.snapshot().totalFailures);
}

void test_recover_after_boot_absence_needs_no_config_resupply() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake, true);
  queuePresence(fake, false);

  Driver driver;
  Config config{};
  config.addressBits = 1;
  config.expectedPart = PartType::AT21CS11;
  assertStatus(Err::NOT_PRESENT, driver.begin(bus, config));

  queuePresence(fake, true);
  queueInitialize(fake, CS11_ID);
  TEST_ASSERT_TRUE(driver.recover().ok());
  TEST_ASSERT_TRUE(driver.isInitialized());
  TEST_ASSERT_TRUE(driver.isOnline());
  TEST_ASSERT_EQUAL_UINT8(1, driver.snapshot().addressBits);
  TEST_ASSERT_EQUAL_UINT8(0x02u, bus.snapshot().claimedAddressMask);
}

void test_initialize_failures_preserve_exact_status_and_identity_nack_state() {
  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake, true);
    TransferResult failure{};
    failure.code = TransportCode::TIMEOUT;
    failure.phase = TransferPhase::PRESENCE;
    failure.detail = 101;
    queuePresence(fake, false, failure);
    Driver driver;
    const Status status = driver.begin(bus, Config{});
    assertStatus(Err::TRANSPORT_TIMEOUT, status);
    TEST_ASSERT_EQUAL_INT32(101, status.detail);
  }
  {
    const TransferPhase resetPhases[] = {
        TransferPhase::NONE, TransferPhase::RESET_LOW,
        TransferPhase::RESET_RECOVERY, TransferPhase::DISCOVERY_REQUEST,
        TransferPhase::DISCOVERY_SAMPLE, TransferPhase::DISCOVERY_RELEASE};
    for (size_t index = 0; index < (sizeof(resetPhases) / sizeof(resetPhases[0]));
         ++index) {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      BooleanScript reset{};
      reset.result.code = TransportCode::TIMEOUT;
      reset.result.phase = resetPhases[index];
      reset.result.detail = static_cast<int32_t>(200u + index);
      TEST_ASSERT_TRUE(fake.queueReset(reset));
      Driver driver;
      const Status status = driver.begin(bus, Config{});
      assertStatus(Err::TRANSPORT_TIMEOUT, status);
      TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(200u + index),
                              status.detail);
      TEST_ASSERT_FALSE(driver.isSpeedKnown());
    }
  }
  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    BooleanScript reset{};
    reset.result = auxiliaryOk(TransferPhase::DISCOVERY_RELEASE);
    reset.value = false;
    TEST_ASSERT_TRUE(fake.queueReset(reset));
    Driver driver;
    const Status status = driver.begin(bus, Config{});
    assertStatus(Err::NOT_PRESENT, status);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                            static_cast<uint8_t>(driver.state()));
  }
  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    queueResetOk(fake);
    TransferScript nack{};
    nack.result.code = TransportCode::NACK;
    nack.result.phase = TransferPhase::DEVICE_ADDRESS_READ;
    TEST_ASSERT_TRUE(fake.queueTransfer(nack));
    Driver driver;
    const Status status = driver.begin(bus, Config{});
    assertStatus(Err::NACK_DEVICE_ADDRESS, status);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ProtocolPhase::DEVICE_ADDRESS_READ),
        static_cast<uint8_t>(protocolDetailPhase(status.detail)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                            static_cast<uint8_t>(driver.state()));
  }
  {
    const TransferPhase phases[] = {
        TransferPhase::START, TransferPhase::DEVICE_ADDRESS_READ,
        TransferPhase::DATA_READ, TransferPhase::STOP};
    for (size_t index = 0; index < (sizeof(phases) / sizeof(phases[0]));
         ++index) {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      queueResetOk(fake);
      TransferScript failure =
          transferFailure(TransportCode::IO_ERROR, phases[index],
                          static_cast<int32_t>(300u + index));
      if (phases[index] == TransferPhase::DATA_READ ||
          phases[index] == TransferPhase::STOP) {
        failure.result.firstDeviceAddressAcked = true;
        failure.result.dataBytesTransferred =
            phases[index] == TransferPhase::STOP ? 3u : 1u;
      }
      TEST_ASSERT_TRUE(fake.queueTransfer(failure));
      Driver driver;
      const Status status = driver.begin(bus, Config{});
      assertStatus(Err::IO_ERROR, status);
      TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(300u + index),
                              status.detail);
    }
  }
  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    queueResetOk(fake);
    TransferScript malformed = manufacturerIdOk(CS11_ID);
    malformed.result.dataBytesTransferred = 2;
    malformed.rxLength = 2;
    TEST_ASSERT_TRUE(fake.queueTransfer(malformed));
    Driver driver;
    assertStatus(Err::IO_ERROR, driver.begin(bus, Config{}));
    TEST_ASSERT_EQUAL_UINT32(1, driver.snapshot().totalFailures);
    TEST_ASSERT_EQUAL_UINT32(0, driver.snapshot().totalSuccess);
  }
  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    queueInitialize(fake, 0x001234u);
    Driver driver;
    const Status status = driver.begin(bus, Config{});
    assertStatus(Err::PART_MISMATCH, status);
    TEST_ASSERT_EQUAL_INT32(0x001234, status.detail);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                            static_cast<uint8_t>(driver.state()));
  }
  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    queueResetOk(fake);
    TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(CS01_ID)));
    TransferScript speedNack{};
    speedNack.result.code = TransportCode::NACK;
    speedNack.result.phase = TransferPhase::DEVICE_ADDRESS_WRITE;
    TEST_ASSERT_TRUE(fake.queueTransfer(speedNack));
    Driver driver;
    Config config{};
    config.expectedPart = PartType::AT21CS01;
    config.startupSpeed = SpeedMode::STANDARD_SPEED;
    const Status status = driver.begin(bus, config);
    assertStatus(Err::NACK_DEVICE_ADDRESS, status);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ProtocolPhase::DEVICE_ADDRESS_WRITE),
        static_cast<uint8_t>(protocolDetailPhase(status.detail)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                            static_cast<uint8_t>(driver.state()));
  }
  {
    const TransferPhase speedFailurePhases[] = {
        TransferPhase::START,
        TransferPhase::DEVICE_ADDRESS_WRITE,
    };
    for (size_t index = 0;
         index < (sizeof(speedFailurePhases) / sizeof(speedFailurePhases[0]));
         ++index) {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      queueResetOk(fake);
      TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(CS01_ID)));
      const int32_t expectedDetail = static_cast<int32_t>(450u + index);
      TEST_ASSERT_TRUE(fake.queueTransfer(transferFailure(
          TransportCode::TIMEOUT, speedFailurePhases[index], expectedDetail)));

      Driver driver;
      Config config{};
      config.expectedPart = PartType::AT21CS01;
      config.startupSpeed = SpeedMode::STANDARD_SPEED;
      const Status status = driver.begin(bus, config);
      assertStatus(Err::TRANSPORT_TIMEOUT, status);
      TEST_ASSERT_EQUAL_INT32(expectedDetail, status.detail);
      TEST_ASSERT_FALSE(driver.isInitialized());
      TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                              static_cast<uint8_t>(driver.state()));
      TEST_ASSERT_EQUAL_UINT32(1, driver.snapshot().totalFailures);
      TEST_ASSERT_EQUAL_UINT32(0, driver.snapshot().totalSuccess);
      TEST_ASSERT_EQUAL_UINT8(
          static_cast<uint8_t>(Err::TRANSPORT_TIMEOUT),
          static_cast<uint8_t>(driver.snapshot().lastStatusCode));
      TEST_ASSERT_EQUAL_INT32(expectedDetail,
                              driver.snapshot().lastStatusDetail);
      TEST_ASSERT_EQUAL(index == 0u, driver.isSpeedKnown());
    }
  }
  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    queueResetOk(fake);
    TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(CS01_ID)));
    TransferScript speedFailure = transferFailure(
        TransportCode::IO_ERROR, TransferPhase::STOP, 401);
    speedFailure.result.firstDeviceAddressAcked = true;
    TEST_ASSERT_TRUE(fake.queueTransfer(speedFailure));
    Driver driver;
    Config config{};
    config.expectedPart = PartType::AT21CS01;
    config.startupSpeed = SpeedMode::STANDARD_SPEED;
    const Status status = driver.begin(bus, config);
    assertStatus(Err::IO_ERROR, status);
    TEST_ASSERT_EQUAL_INT32(401, status.detail);
    TEST_ASSERT_FALSE(driver.isSpeedKnown());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                            static_cast<uint8_t>(driver.state()));
  }
}

void test_initialize_is_uninit_only_and_ordinary_calls_never_reset() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);
  const size_t resetCount = fake.resetCalls;
  assertStatus(Err::INVALID_STATE, driver.initialize());

  uint8_t eeprom = 0x11;
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&eeprom, 1)));
  uint8_t output = 0;
  TEST_ASSERT_TRUE(driver.readEeprom(0, &output, 1).ok());
  const uint8_t securityByte = 0x22u;
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&securityByte, 1)));
  TEST_ASSERT_TRUE(driver.readSecurity(0, &output, 1).ok());
  TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(CS11_ID)));
  uint32_t manufacturerId = 0;
  TEST_ASSERT_TRUE(driver.readManufacturerId(manufacturerId).ok());
  const uint8_t serialBytes[8] = {0xA0u, 1u, 2u, 3u,
                                  4u,    5u, 6u, 0xF8u};
  TEST_ASSERT_TRUE(
      fake.queueTransfer(randomReadOk(serialBytes, sizeof(serialBytes))));
  SerialNumberInfo serial{};
  TEST_ASSERT_TRUE(driver.readSerialNumber(serial).ok());
  TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(CS11_ID)));
  TEST_ASSERT_TRUE(driver.probe().ok());
  TEST_ASSERT_TRUE(driver.setSpeedMode(SpeedMode::HIGH_SPEED).ok());
  TEST_ASSERT_EQUAL_UINT32(resetCount, fake.resetCalls);
  TEST_ASSERT_EQUAL_UINT32(1, fake.eventCountFor(FakeEventKind::RESET_DISCOVER));

  ScriptedTransport boundaryFake;
  Bus boundaryBus;
  bindBus(boundaryBus, boundaryFake);
  Driver boundaryDriver;
  TEST_ASSERT_TRUE(boundaryDriver.bind(boundaryBus, Config{}).ok());
  TestAccess::seedGeneration(boundaryBus,
                             std::numeric_limits<uint64_t>::max());
  assertStatus(Err::INVALID_STATE, boundaryDriver.initialize());
  TEST_ASSERT_EQUAL_UINT32(0, boundaryFake.resetCalls);
  TEST_ASSERT_EQUAL_UINT32(0, boundaryDriver.snapshot().totalFailures);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(boundaryDriver.state()));
}

void test_failed_offline_recovery_remains_offline_and_uninitialized() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake, true);
  queuePresence(fake, false);
  Driver driver;
  assertStatus(Err::NOT_PRESENT, driver.begin(bus, Config{}));

  queuePresence(fake, true);
  BooleanScript reset{};
  reset.result.code = TransportCode::LINE_STUCK;
  reset.result.phase = TransferPhase::DISCOVERY_RELEASE;
  reset.result.detail = 717;
  TEST_ASSERT_TRUE(fake.queueReset(reset));
  const Status status = driver.recover();
  assertStatus(Err::LINE_STUCK, status);
  TEST_ASSERT_EQUAL_INT32(717, status.detail);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(driver.state()));
  TEST_ASSERT_FALSE(driver.isInitialized());
}

void test_state_admission_and_centralized_finish_are_exact() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);
  uint8_t output = 0;
  const size_t transfersBefore = fake.transferCalls;

  const DriverState rejected[] = {
      DriverState::UNINIT,       DriverState::PROBING,
      DriverState::INIT_CONFIG, DriverState::OFFLINE,
      DriverState::RECOVERING,  DriverState::SLEEPING,
      DriverState::FAULT};
  for (DriverState state : rejected) {
    TestAccess::seedDriverState(driver, state, state != DriverState::UNINIT);
    const Status status = driver.readEeprom(0, &output, 1);
    assertStatus(state == DriverState::UNINIT ? Err::NOT_INITIALIZED
                                             : Err::INVALID_STATE,
                 status);
  }
  TestAccess::seedDriverState(driver, DriverState::BUSY, true);
  assertStatus(Err::BUSY, driver.readEeprom(0, &output, 1));
  TEST_ASSERT_EQUAL_UINT32(transfersBefore, fake.transferCalls);

  TestAccess::seedDriverState(driver, DriverState::DEGRADED, true);
  const uint8_t recoveredValue = 0x31u;
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&recoveredValue, 1)));
  TEST_ASSERT_TRUE(driver.readEeprom(0, &output, 1).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(driver.state()));

  TestAccess::seedDriverState(driver, DriverState::READY, true);
  TransferScript failure = transferFailure(TransportCode::TIMEOUT,
                                           TransferPhase::START, 818);
  TEST_ASSERT_TRUE(fake.queueTransfer(failure));
  const uint32_t failuresBefore = driver.snapshot().totalFailures;
  assertStatus(Err::TRANSPORT_TIMEOUT, driver.readEeprom(0, &output, 1));
  TEST_ASSERT_EQUAL_UINT32(failuresBefore + 1u,
                           driver.snapshot().totalFailures);

  const DriverState illegalRecoveryStates[] = {
      DriverState::PROBING, DriverState::INIT_CONFIG, DriverState::BUSY,
      DriverState::RECOVERING, DriverState::SLEEPING, DriverState::FAULT};
  const size_t resetsBeforeIllegal = fake.resetCalls;
  for (DriverState state : illegalRecoveryStates) {
    TestAccess::seedDriverState(driver, state, true);
    assertStatus(Err::INVALID_STATE, driver.recover());
  }
  TEST_ASSERT_EQUAL_UINT32(resetsBeforeIllegal, fake.resetCalls);

  const DriverState legalRecoveryStates[] = {
      DriverState::UNINIT, DriverState::READY, DriverState::DEGRADED,
      DriverState::OFFLINE};
  for (DriverState state : legalRecoveryStates) {
    TestAccess::seedDriverState(driver, state,
                                state != DriverState::UNINIT);
    queueInitialize(fake, CS11_ID);
    TEST_ASSERT_TRUE(driver.recover().ok());
    TEST_ASSERT_TRUE(driver.isInitialized());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                            static_cast<uint8_t>(driver.state()));
  }
}

void test_binding_epoch_blocks_stale_traffic_until_recover() {
  ScriptedTransport original;
  ScriptedTransport replacement;
  Bus bus;
  bindBus(bus, original);
  Driver driver;
  Config config{};
  config.expectedPart = PartType::AT21CS11;
  initializeDriver(driver, bus, original, config, CS11_ID);

  BusConfig replacementConfig{};
  replacementConfig.transport = replacement.descriptor(false);
  TEST_ASSERT_TRUE(bus.bind(replacementConfig).ok());
  uint8_t value = 0;
  assertStatus(Err::INVALID_STATE, driver.readEeprom(0, &value, 1));
  TEST_ASSERT_EQUAL_UINT32(0, replacement.transferCalls);
  TEST_ASSERT_FALSE(driver.isSpeedKnown());

  queueInitialize(replacement, CS11_ID);
  TEST_ASSERT_TRUE(driver.recover().ok());
  const uint8_t expected = 0xA6u;
  TEST_ASSERT_TRUE(replacement.queueTransfer(randomReadOk(&expected, 1)));
  TEST_ASSERT_TRUE(driver.readEeprom(0, &value, 1).ok());
  TEST_ASSERT_EQUAL_HEX8(expected, value);
}

void test_shared_reset_generation_resynchronizes_without_reset_loop() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver first;
  Driver second;
  Config firstConfig{};
  firstConfig.expectedPart = PartType::AT21CS01;
  Config secondConfig = firstConfig;
  secondConfig.addressBits = 1;
  initializeDriver(first, bus, fake, firstConfig, CS01_ID);
  initializeDriver(second, bus, fake, secondConfig, CS01_ID);
  TEST_ASSERT_FALSE(first.isSpeedKnown());
  TEST_ASSERT_FALSE(first.isOnline());
  const size_t resetsBefore = fake.resetCalls;

  const uint8_t expected = 0x29u;
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&expected, 1)));
  uint8_t value = 0;
  TEST_ASSERT_TRUE(first.readEeprom(0, &value, 1).ok());
  TEST_ASSERT_EQUAL_UINT32(resetsBefore, fake.resetCalls);
  TEST_ASSERT_TRUE(first.isSpeedKnown());
}

void test_standard_speed_is_restored_lazily_after_shared_reset() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver standard;
  Driver resetter;
  Config standardConfig{};
  standardConfig.expectedPart = PartType::AT21CS01;
  standardConfig.startupSpeed = SpeedMode::STANDARD_SPEED;
  Config resetterConfig{};
  resetterConfig.addressBits = 1;
  resetterConfig.expectedPart = PartType::AT21CS01;
  initializeDriver(standard, bus, fake, standardConfig, CS01_ID);
  initializeDriver(resetter, bus, fake, resetterConfig, CS01_ID);
  const size_t captureBefore = fake.capturedCount;
  const size_t resetsBefore = fake.resetCalls;

  TEST_ASSERT_TRUE(fake.queueTransfer(addressOnlyOk()));
  const uint8_t expected = 0x7Cu;
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&expected, 1)));
  uint8_t value = 0;
  TEST_ASSERT_TRUE(standard.readEeprom(0, &value, 1).ok());
  TEST_ASSERT_EQUAL_UINT32(resetsBefore, fake.resetCalls);
  TEST_ASSERT_EQUAL_UINT32(captureBefore + 2u, fake.capturedCount);
  TEST_ASSERT_EQUAL_HEX8(0xD0u, fake.captured[captureBefore].deviceAddress);
  TEST_ASSERT_EQUAL_UINT32(650u,
                           fake.captured[captureBefore]
                               .minimumPostTransferHighUs);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SpeedMode::HIGH_SPEED),
                          static_cast<uint8_t>(fake.captured[captureBefore].speed));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(SpeedMode::STANDARD_SPEED),
      static_cast<uint8_t>(fake.captured[captureBefore + 1u].speed));

  ScriptedTransport ambiguousFake;
  Bus ambiguousBus;
  bindBus(ambiguousBus, ambiguousFake);
  Driver ambiguousStandard;
  Driver ambiguousResetter;
  initializeDriver(ambiguousStandard, ambiguousBus, ambiguousFake,
                   standardConfig, CS01_ID);
  initializeDriver(ambiguousResetter, ambiguousBus, ambiguousFake,
                   resetterConfig, CS01_ID);
  TransferScript ambiguousRestore = transferFailure(
      TransportCode::IO_ERROR, TransferPhase::STOP, 1201);
  ambiguousRestore.result.firstDeviceAddressAcked = true;
  TEST_ASSERT_TRUE(ambiguousFake.queueTransfer(ambiguousRestore));
  uint8_t blockedValue = 0;
  assertStatus(Err::IO_ERROR,
               ambiguousStandard.readEeprom(0, &blockedValue, 1));
  TEST_ASSERT_FALSE(ambiguousStandard.isSpeedKnown());
  const size_t ambiguousTransfers = ambiguousFake.transferCalls;
  assertStatus(Err::INVALID_STATE,
               ambiguousStandard.readEeprom(0, &blockedValue, 1));
  TEST_ASSERT_EQUAL_UINT32(ambiguousTransfers, ambiguousFake.transferCalls);

  ScriptedTransport retryFake;
  Bus retryBus;
  bindBus(retryBus, retryFake);
  Driver retryStandard;
  Driver retryResetter;
  initializeDriver(retryStandard, retryBus, retryFake, standardConfig,
                   CS01_ID);
  initializeDriver(retryResetter, retryBus, retryFake, resetterConfig,
                   CS01_ID);
  const size_t retryResets = retryFake.resetCalls;
  const size_t retryTransfers = retryFake.transferCalls;
  const uint32_t retryFailures = retryStandard.snapshot().totalFailures;
  TransferScript restoreNack{};
  restoreNack.result.code = TransportCode::NACK;
  restoreNack.result.phase = TransferPhase::DEVICE_ADDRESS_WRITE;
  TEST_ASSERT_TRUE(retryFake.queueTransfer(restoreNack));
  assertStatus(Err::NACK_DEVICE_ADDRESS,
               retryStandard.readEeprom(0, &blockedValue, 1));
  TEST_ASSERT_TRUE(retryStandard.isSpeedKnown());
  TEST_ASSERT_EQUAL_UINT64(retryBus.generation(),
                           retryStandard.snapshot().seenBusGeneration);
  TEST_ASSERT_EQUAL_UINT32(retryFailures + 1u,
                           retryStandard.snapshot().totalFailures);
  TEST_ASSERT_EQUAL_UINT32(retryTransfers + 1u, retryFake.transferCalls);

  TransferScript restoreStartFailure = transferFailure(
      TransportCode::TIMEOUT, TransferPhase::START, 1202);
  restoreStartFailure.result.stopCompleted = true;
  TEST_ASSERT_TRUE(retryFake.queueTransfer(restoreStartFailure));
  assertStatus(Err::TRANSPORT_TIMEOUT,
               retryStandard.readEeprom(0, &blockedValue, 1));
  TEST_ASSERT_TRUE(retryStandard.isSpeedKnown());
  TEST_ASSERT_EQUAL_UINT32(retryTransfers + 2u, retryFake.transferCalls);

  TEST_ASSERT_TRUE(retryFake.queueTransfer(addressOnlyOk()));
  const uint8_t retryValue = 0x63u;
  TEST_ASSERT_TRUE(retryFake.queueTransfer(randomReadOk(&retryValue, 1)));
  TEST_ASSERT_TRUE(retryStandard.readEeprom(0, &blockedValue, 1).ok());
  TEST_ASSERT_EQUAL_HEX8(retryValue, blockedValue);
  TEST_ASSERT_EQUAL_UINT32(retryTransfers + 4u, retryFake.transferCalls);
  TEST_ASSERT_EQUAL_UINT32(retryResets, retryFake.resetCalls);
}

void test_set_high_after_shared_reset_skips_standard_restoration() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver standard;
  Driver resetter;
  Config standardConfig{};
  standardConfig.expectedPart = PartType::AT21CS01;
  standardConfig.startupSpeed = SpeedMode::STANDARD_SPEED;
  Config resetterConfig{};
  resetterConfig.addressBits = 1;
  resetterConfig.expectedPart = PartType::AT21CS01;
  initializeDriver(standard, bus, fake, standardConfig, CS01_ID);
  initializeDriver(resetter, bus, fake, resetterConfig, CS01_ID);
  const size_t transfersBefore = fake.transferCalls;
  const uint32_t successesBefore = standard.snapshot().totalSuccess;

  TEST_ASSERT_TRUE(standard.setSpeedMode(SpeedMode::HIGH_SPEED).ok());
  TEST_ASSERT_EQUAL_UINT32(transfersBefore, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT32(successesBefore, standard.snapshot().totalSuccess);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(SpeedMode::HIGH_SPEED),
      static_cast<uint8_t>(standard.snapshot().configuredSpeed));
}

void test_speed_failure_evidence_controls_speed_knowledge() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  Config config{};
  config.expectedPart = PartType::AT21CS01;
  initializeDriver(driver, bus, fake, config, CS01_ID);

  TransferScript nack{};
  nack.result.code = TransportCode::NACK;
  nack.result.phase = TransferPhase::DEVICE_ADDRESS_WRITE;
  TEST_ASSERT_TRUE(fake.queueTransfer(nack));
  assertStatus(Err::NACK_DEVICE_ADDRESS,
               driver.setSpeedMode(SpeedMode::STANDARD_SPEED));
  TEST_ASSERT_TRUE(driver.isSpeedKnown());
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(SpeedMode::HIGH_SPEED),
      static_cast<uint8_t>(driver.snapshot().configuredSpeed));

  TransferScript startFailure = transferFailure(
      TransportCode::TIMEOUT, TransferPhase::START, 1401);
  startFailure.result.stopCompleted = true;
  TEST_ASSERT_TRUE(fake.queueTransfer(startFailure));
  assertStatus(Err::TRANSPORT_TIMEOUT,
               driver.setSpeedMode(SpeedMode::STANDARD_SPEED));
  TEST_ASSERT_TRUE(driver.isSpeedKnown());

  TEST_ASSERT_TRUE(fake.queueTransfer(addressOnlyOk()));
  TEST_ASSERT_TRUE(driver.setSpeedMode(SpeedMode::STANDARD_SPEED).ok());
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(SpeedMode::STANDARD_SPEED),
      static_cast<uint8_t>(driver.speedMode()));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(SpeedMode::STANDARD_SPEED),
      static_cast<uint8_t>(driver.snapshot().configuredSpeed));
  TEST_ASSERT_EQUAL_UINT32(
      650u, fake.captured[fake.capturedCount - 1u].minimumPostTransferHighUs);

  TEST_ASSERT_TRUE(fake.queueTransfer(addressOnlyOk()));
  TEST_ASSERT_TRUE(driver.setSpeedMode(SpeedMode::HIGH_SPEED).ok());
  const CapturedTransfer& highTransition =
      fake.captured[fake.capturedCount - 1u];
  TEST_ASSERT_EQUAL_HEX8(0xE0u, highTransition.deviceAddress);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(SpeedMode::STANDARD_SPEED),
      static_cast<uint8_t>(highTransition.speed));
  TEST_ASSERT_EQUAL_UINT32(650u,
                           highTransition.minimumPostTransferHighUs);

  TEST_ASSERT_TRUE(fake.queueTransfer(transferFailure(
      TransportCode::TIMEOUT, TransferPhase::DEVICE_ADDRESS_WRITE, 1402)));
  assertStatus(Err::TRANSPORT_TIMEOUT,
               driver.setSpeedMode(SpeedMode::STANDARD_SPEED));
  TEST_ASSERT_FALSE(driver.isSpeedKnown());
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(SpeedMode::HIGH_SPEED),
      static_cast<uint8_t>(driver.snapshot().configuredSpeed));
  const size_t transfersBefore = fake.transferCalls;
  uint8_t value = 0;
  assertStatus(Err::INVALID_STATE, driver.readEeprom(0, &value, 1));
  TEST_ASSERT_EQUAL_UINT32(transfersBefore, fake.transferCalls);

  ScriptedTransport malformedFake;
  Bus malformedBus;
  bindBus(malformedBus, malformedFake);
  Driver malformedDriver;
  initializeDriver(malformedDriver, malformedBus, malformedFake, config, CS01_ID);
  TransferScript malformed = nack;
  malformed.result.firstDeviceAddressAcked = true;
  TEST_ASSERT_TRUE(malformedFake.queueTransfer(malformed));
  assertStatus(Err::IO_ERROR,
               malformedDriver.setSpeedMode(SpeedMode::STANDARD_SPEED));
  TEST_ASSERT_FALSE(malformedDriver.isSpeedKnown());

  ScriptedTransport malformedStartFake;
  Bus malformedStartBus;
  bindBus(malformedStartBus, malformedStartFake);
  Driver malformedStartDriver;
  initializeDriver(malformedStartDriver, malformedStartBus,
                   malformedStartFake, config, CS01_ID);
  TransferScript malformedStart{};
  malformedStart.result.code = TransportCode::OK;
  malformedStart.result.phase = TransferPhase::START;
  TEST_ASSERT_TRUE(malformedStartFake.queueTransfer(malformedStart));
  assertStatus(
      Err::IO_ERROR,
      malformedStartDriver.setSpeedMode(SpeedMode::STANDARD_SPEED));
  TEST_ASSERT_FALSE(malformedStartDriver.isSpeedKnown());

  ScriptedTransport stopFake;
  Bus stopBus;
  bindBus(stopBus, stopFake);
  Driver stopDriver;
  initializeDriver(stopDriver, stopBus, stopFake, config, CS01_ID);
  TransferScript stopFailure = transferFailure(
      TransportCode::IO_ERROR, TransferPhase::STOP, 1403);
  stopFailure.result.firstDeviceAddressAcked = true;
  TEST_ASSERT_TRUE(stopFake.queueTransfer(stopFailure));
  assertStatus(Err::IO_ERROR,
               stopDriver.setSpeedMode(SpeedMode::STANDARD_SPEED));
  TEST_ASSERT_FALSE(stopDriver.isSpeedKnown());
}

void test_at21cs11_standard_rejection_is_silent_and_untracked() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  Config config{};
  config.expectedPart = PartType::AT21CS11;
  initializeDriver(driver, bus, fake, config, CS11_ID);
  const SettingsSnapshot before = driver.snapshot();
  const size_t eventsBefore = fake.eventCount;

  assertStatus(Err::UNSUPPORTED_COMMAND,
               driver.setSpeedMode(SpeedMode::STANDARD_SPEED));
  const SettingsSnapshot after = driver.snapshot();
  TEST_ASSERT_EQUAL_UINT32(eventsBefore, fake.eventCount);
  TEST_ASSERT_EQUAL_UINT32(before.totalSuccess, after.totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(before.totalFailures, after.totalFailures);
  TEST_ASSERT_EQUAL_UINT8(before.consecutiveFailures,
                          after.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(before.lastStatusCode),
                          static_cast<uint8_t>(after.lastStatusCode));
  TEST_ASSERT_EQUAL_INT32(before.lastStatusDetail, after.lastStatusDetail);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(before.lastErrorCode),
                          static_cast<uint8_t>(after.lastErrorCode));
  TEST_ASSERT_EQUAL_INT32(before.lastErrorDetail, after.lastErrorDetail);
  TEST_ASSERT_EQUAL_UINT64(before.lastOkUs, after.lastOkUs);
  TEST_ASSERT_EQUAL_UINT64(before.lastErrorUs, after.lastErrorUs);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(before.state),
                          static_cast<uint8_t>(after.state));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(before.configuredSpeed),
                          static_cast<uint8_t>(after.configuredSpeed));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(before.activeSpeed),
                          static_cast<uint8_t>(after.activeSpeed));
  TEST_ASSERT_EQUAL(before.speedKnown, after.speedKnown);
}

void test_manufacturer_revisions_and_part_mismatch_are_exact() {
  const uint32_t bases[] = {0x00D200u, 0x00D380u};
  const PartType parts[] = {PartType::AT21CS01, PartType::AT21CS11};
  for (size_t partIndex = 0; partIndex < 2; ++partIndex) {
    for (uint8_t revision = 0; revision < 8u; ++revision) {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      Driver driver;
      Config config{};
      config.expectedPart = parts[partIndex];
      const uint32_t rawId = bases[partIndex] | revision;
      initializeDriver(driver, bus, fake, config, rawId);
      TEST_ASSERT_EQUAL_UINT32(rawId, driver.manufacturerId());
      TEST_ASSERT_EQUAL_UINT8(revision, driver.siliconRevision());
      TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(parts[partIndex]),
                              static_cast<uint8_t>(driver.detectedPart()));
    }
  }

  ScriptedTransport unknownFake;
  Bus unknownBus;
  bindBus(unknownBus, unknownFake);
  queueInitialize(unknownFake, 0x00ABCDu);
  Driver unknown;
  const Status unknownStatus = unknown.begin(unknownBus, Config{});
  assertStatus(Err::PART_MISMATCH, unknownStatus);
  TEST_ASSERT_EQUAL_INT32(0x00ABCD, unknownStatus.detail);
  TEST_ASSERT_EQUAL_UINT32(0x00ABCDu, unknown.manufacturerId());
  TEST_ASSERT_EQUAL_UINT8(5u, unknown.siliconRevision());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PartType::UNKNOWN),
                          static_cast<uint8_t>(unknown.detectedPart()));

  ScriptedTransport mismatchFake;
  Bus mismatchBus;
  bindBus(mismatchBus, mismatchFake);
  queueInitialize(mismatchFake, CS11_ID);
  Driver mismatch;
  Config expected01{};
  expected01.expectedPart = PartType::AT21CS01;
  const Status mismatchStatus = mismatch.begin(mismatchBus, expected01);
  assertStatus(Err::PART_MISMATCH, mismatchStatus);
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(CS11_ID),
                          mismatchStatus.detail);
  TEST_ASSERT_EQUAL_UINT32(CS11_ID, mismatch.manufacturerId());
  TEST_ASSERT_EQUAL_UINT8(5u, mismatch.siliconRevision());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PartType::AT21CS11),
                          static_cast<uint8_t>(mismatch.detectedPart()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(mismatch.state()));
}

void test_probe_is_nondestructive_tracked_and_offline_sticky_on_failure() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  Config config{};
  config.expectedPart = PartType::AT21CS11;
  config.offlineThreshold = 1;
  initializeDriver(driver, bus, fake, config, CS11_ID);
  const size_t resets = fake.resetCalls;
  const uint32_t successes = driver.snapshot().totalSuccess;
  TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(CS11_ID)));
  TEST_ASSERT_TRUE(driver.probe().ok());
  const CapturedTransfer& probeFrame = fake.captured[fake.capturedCount - 1u];
  TEST_ASSERT_EQUAL_HEX8(0xC1u, probeFrame.deviceAddress);
  TEST_ASSERT_FALSE(probeFrame.hasMemoryAddress);
  TEST_ASSERT_FALSE(probeFrame.hasRepeatedStart);
  TEST_ASSERT_EQUAL_UINT32(3, probeFrame.rxLength);
  TEST_ASSERT_EQUAL_UINT32(successes + 1u, driver.snapshot().totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(resets, fake.resetCalls);

  TransferScript readFailure = transferFailure(
      TransportCode::TIMEOUT, TransferPhase::START, 1701);
  TEST_ASSERT_TRUE(fake.queueTransfer(readFailure));
  uint8_t value = 0;
  assertStatus(Err::TRANSPORT_TIMEOUT, driver.readEeprom(0, &value, 1));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(driver.state()));
  TransferScript probeNack{};
  probeNack.result.code = TransportCode::NACK;
  probeNack.result.phase = TransferPhase::DEVICE_ADDRESS_READ;
  TEST_ASSERT_TRUE(fake.queueTransfer(probeNack));
  assertStatus(Err::NACK_DEVICE_ADDRESS, driver.probe());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(driver.state()));
  TEST_ASSERT_TRUE(driver.isInitialized());
  TEST_ASSERT_EQUAL_UINT32(resets, fake.resetCalls);

  TestAccess::seedDriverState(driver, DriverState::OFFLINE, true);
  TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(CS11_ID)));
  TEST_ASSERT_TRUE(driver.probe().ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(driver.state()));

  TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(CS01_ID)));
  assertStatus(Err::PART_MISMATCH, driver.probe());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(driver.state()));
  TEST_ASSERT_FALSE(driver.isInitialized());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PartType::AT21CS11),
                          static_cast<uint8_t>(driver.detectedPart()));
}

void test_driver_end_is_idempotent_silent_and_releases_one_claim() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver first;
  Driver second;
  Config firstConfig{};
  Config secondConfig{};
  secondConfig.addressBits = 2;
  TEST_ASSERT_TRUE(first.bind(bus, firstConfig).ok());
  TEST_ASSERT_TRUE(second.bind(bus, secondConfig).ok());
  const size_t events = fake.eventCount;

  first.end();
  TEST_ASSERT_EQUAL_UINT8(0x04u, bus.snapshot().claimedAddressMask);
  first.end();
  TEST_ASSERT_EQUAL_UINT8(0x04u, bus.snapshot().claimedAddressMask);
  TEST_ASSERT_EQUAL_UINT32(events, fake.eventCount);
}

void test_address_claims_are_per_bus_and_transactional() {
  ScriptedTransport fakeA;
  ScriptedTransport fakeB;
  Bus busA;
  Bus busB;
  bindBus(busA, fakeA);
  bindBus(busB, fakeB);
  Driver first;
  Driver alias;
  Driver independent;
  Config config{};
  config.addressBits = 7;
  TEST_ASSERT_TRUE(first.bind(busA, config).ok());
  const Status duplicate = alias.bind(busA, config);
  assertStatus(Err::INVALID_CONFIG, duplicate);
  TEST_ASSERT_EQUAL_INT32(7, duplicate.detail);
  TEST_ASSERT_TRUE(independent.bind(busB, config).ok());
  TEST_ASSERT_EQUAL_UINT8(0x80u, busA.snapshot().claimedAddressMask);
  TEST_ASSERT_EQUAL_UINT8(0x80u, busB.snapshot().claimedAddressMask);
  queueInitialize(fakeA, CS11_ID);
  queueInitialize(fakeB, CS11_ID);
  TEST_ASSERT_TRUE(first.initialize().ok());
  TEST_ASSERT_TRUE(independent.initialize().ok());
}

void test_replacement_preserves_claim_and_stale_owner_can_recover() {
  ScriptedTransport original;
  ScriptedTransport replacement;
  Bus bus;
  bindBus(bus, original);
  Driver owner;
  Config config{};
  config.addressBits = 4;
  config.expectedPart = PartType::AT21CS11;
  initializeDriver(owner, bus, original, config, CS11_ID);

  BusConfig replacementConfig{};
  replacementConfig.transport = replacement.descriptor(false);
  TEST_ASSERT_TRUE(bus.bind(replacementConfig).ok());
  TEST_ASSERT_EQUAL_UINT8(0x10u, bus.snapshot().claimedAddressMask);
  Driver alias;
  assertStatus(Err::INVALID_CONFIG, alias.bind(bus, config));

  queueInitialize(replacement, CS11_ID);
  TEST_ASSERT_TRUE(owner.recover().ok());
  const uint8_t expected = 0x44u;
  TEST_ASSERT_TRUE(replacement.queueTransfer(randomReadOk(&expected, 1)));
  uint8_t value = 0;
  TEST_ASSERT_TRUE(owner.readEeprom(0, &value, 1).ok());
  TEST_ASSERT_EQUAL_HEX8(expected, value);
}
