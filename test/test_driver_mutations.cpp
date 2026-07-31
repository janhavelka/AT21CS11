#include <cstddef>
#include <cstdint>

#include <unity.h>

#include "AT21CS/AT21CS.h"
#include "support/DriverTestSupport.h"

using namespace AT21CS;
using namespace AT21CS::test;

namespace {

constexpr uint32_t CS01_ID = 0x00D202u;
constexpr uint32_t CS11_ID = 0x00D384u;
constexpr uint8_t EXPECTED_ROM_REGISTERS[4] = {0x01u, 0x02u, 0x04u, 0x08u};

TransferScript memoryAddressOnlyOk() {
  TransferScript script{};
  script.result.code = TransportCode::OK;
  script.result.phase = TransferPhase::STOP;
  script.result.firstDeviceAddressAcked = true;
  script.result.memoryAddressAcked = true;
  script.result.stopCompleted = true;
  return script;
}

TransferScript deviceAddressNack() {
  TransferScript script{};
  script.result.code = TransportCode::NACK;
  script.result.phase = TransferPhase::DEVICE_ADDRESS_WRITE;
  script.result.stopCompleted = true;
  return script;
}

TransferScript memoryAddressNack() {
  TransferScript script{};
  script.result.code = TransportCode::NACK;
  script.result.phase = TransferPhase::MEMORY_ADDRESS;
  script.result.firstDeviceAddressAcked = true;
  script.result.stopCompleted = true;
  return script;
}

TransferScript mutationWriteOk() {
  TransferScript script{};
  script.result.code = TransportCode::OK;
  script.result.phase = TransferPhase::STOP;
  script.result.dataBytesTransferred = 1;
  script.result.firstDeviceAddressAcked = true;
  script.result.memoryAddressAcked = true;
  script.result.stopCompleted = true;
  return script;
}

TransferScript mutationDataFailure(TransportCode code,
                                   int32_t detail,
                                   bool uncertain) {
  TransferScript script{};
  script.result.code = code;
  script.result.phase = TransferPhase::DATA_WRITE;
  script.result.detail = detail;
  script.result.currentWriteByteMayBeAccepted = uncertain;
  script.result.firstDeviceAddressAcked = true;
  script.result.memoryAddressAcked = true;
  return script;
}

TransferScript directReadFailure(TransportCode code, int32_t detail) {
  TransferScript script{};
  script.result.code = code;
  script.result.phase = TransferPhase::DEVICE_ADDRESS_READ;
  script.result.detail = detail;
  return script;
}

WaitScript mutationWaitOk() {
  WaitScript script{};
  script.result = auxiliaryOk(TransferPhase::WAIT_HIGH);
  script.advanceToDeadline = true;
  return script;
}

WaitScript mutationWaitFailure(TransportCode code, int32_t detail) {
  WaitScript script{};
  script.result.code = code;
  script.result.phase = TransferPhase::WAIT_HIGH;
  script.result.detail = detail;
  return script;
}

void queueMutationOk(ScriptedTransport& fake) {
  TEST_ASSERT_TRUE(fake.queueTransfer(mutationWriteOk()));
  TEST_ASSERT_TRUE(fake.queueWait(mutationWaitOk()));
}

void assertMutationResult(const MutationResult& result,
                          MutationEffect effect,
                          bool alreadyApplied) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(effect),
                          static_cast<uint8_t>(result.effect));
  TEST_ASSERT_EQUAL(alreadyApplied, result.alreadyApplied);
}

}  // namespace

void test_check_lock_frame_and_memory_nack_semantics_are_exact() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  Config config{};
  config.addressBits = 3;
  initializeDriver(driver, bus, fake, config, CS11_ID);

  TEST_ASSERT_TRUE(fake.queueTransfer(memoryAddressOnlyOk()));
  bool locked = true;
  TEST_ASSERT_TRUE(driver.readSecurityLockState(locked).ok());
  TEST_ASSERT_FALSE(locked);
  const CapturedTransfer& unlocked = fake.captured[fake.capturedCount - 1u];
  TEST_ASSERT_EQUAL_HEX8(0x26u, unlocked.deviceAddress);
  TEST_ASSERT_TRUE(unlocked.hasMemoryAddress);
  TEST_ASSERT_EQUAL_HEX8(0x60u, unlocked.memoryAddress);
  TEST_ASSERT_EQUAL_UINT32(0, unlocked.txLength);
  TEST_ASSERT_FALSE(unlocked.hasRepeatedStart);
  TEST_ASSERT_EQUAL_UINT32(0, unlocked.rxLength);

  TEST_ASSERT_TRUE(fake.queueTransfer(memoryAddressNack()));
  TEST_ASSERT_TRUE(driver.readSecurityLockState(locked).ok());
  TEST_ASSERT_TRUE(locked);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(TransportCode::NACK),
      static_cast<uint8_t>(bus.snapshot().lastTransfer.code));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(TransferPhase::MEMORY_ADDRESS),
      static_cast<uint8_t>(bus.snapshot().lastTransfer.phase));

  TEST_ASSERT_TRUE(fake.queueTransfer(deviceAddressNack()));
  locked = true;
  const Status status = driver.readSecurityLockState(locked);
  assertStatus(Err::NACK_DEVICE_ADDRESS, status);
  TEST_ASSERT_FALSE(locked);

  Driver unbound;
  locked = true;
  assertStatus(Err::NOT_BOUND, unbound.readSecurityLockState(locked));
  TEST_ASSERT_FALSE(locked);
}

void test_lock_mutation_already_verified_mismatch_and_hold_ambiguity() {
  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    TEST_ASSERT_TRUE(fake.queueTransfer(memoryAddressNack()));
    const size_t firstCapture = fake.capturedCount;
    const size_t firstTransferCalls = fake.transferCalls;
    const SettingsSnapshot before = driver.snapshot();
    MutationResult result{};
    TEST_ASSERT_TRUE(driver.permanentlyLockSecurity(result).ok());
    assertMutationResult(result, MutationEffect::VERIFIED, true);
    TEST_ASSERT_EQUAL_UINT32(firstCapture + 1u, fake.capturedCount);
    TEST_ASSERT_EQUAL_UINT32(firstTransferCalls + 1u, fake.transferCalls);
    TEST_ASSERT_EQUAL_UINT32(before.totalSuccess + 1u,
                             driver.snapshot().totalSuccess);
    TEST_ASSERT_FALSE(fake.overflow);
  }

  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    TEST_ASSERT_TRUE(fake.queueTransfer(memoryAddressOnlyOk()));
    queueMutationOk(fake);
    TEST_ASSERT_TRUE(fake.queueTransfer(memoryAddressNack()));
    const size_t firstCapture = fake.capturedCount;
    MutationResult result{};
    TEST_ASSERT_TRUE(driver.permanentlyLockSecurity(result).ok());
    assertMutationResult(result, MutationEffect::VERIFIED, false);
    TEST_ASSERT_EQUAL_UINT32(firstCapture + 3u, fake.capturedCount);
    const CapturedTransfer& mutation = fake.captured[firstCapture + 1u];
    TEST_ASSERT_EQUAL_HEX8(0x20u, mutation.deviceAddress);
    TEST_ASSERT_TRUE(mutation.hasMemoryAddress);
    TEST_ASSERT_EQUAL_HEX8(0x60u, mutation.memoryAddress);
    TEST_ASSERT_EQUAL_UINT32(1, mutation.txLength);
    TEST_ASSERT_EQUAL_HEX8(0x00u, mutation.txData[0]);
    TEST_ASSERT_EQUAL_UINT32(1, fake.waitCalls);
    TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
    TEST_ASSERT_FALSE(fake.overflow);
  }

  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    TEST_ASSERT_TRUE(fake.queueTransfer(memoryAddressOnlyOk()));
    queueMutationOk(fake);
    TEST_ASSERT_TRUE(fake.queueTransfer(memoryAddressOnlyOk()));
    MutationResult result{};
    const Status status = driver.permanentlyLockSecurity(result);
    assertStatus(Err::VERIFY_MISMATCH, status);
    assertMutationResult(result, MutationEffect::ACCEPTED, false);
    TEST_ASSERT_EQUAL_UINT32(1, fake.waitCalls);
    TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
    TEST_ASSERT_FALSE(fake.overflow);
  }

  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    TEST_ASSERT_TRUE(fake.queueTransfer(memoryAddressOnlyOk()));
    TEST_ASSERT_TRUE(fake.queueTransfer(mutationWriteOk()));
    TEST_ASSERT_TRUE(fake.queueWait(
        mutationWaitFailure(TransportCode::LINE_STUCK, 4101)));
    const size_t firstCapture = fake.capturedCount;
    const size_t firstTransferCalls = fake.transferCalls;
    MutationResult result{};
    const Status status = driver.permanentlyLockSecurity(result);
    assertStatus(Err::LINE_STUCK, status);
    assertMutationResult(result, MutationEffect::MAY_HAVE_COMMITTED, false);
    TEST_ASSERT_EQUAL_UINT32(firstCapture + 2u, fake.capturedCount);
    TEST_ASSERT_EQUAL_UINT32(firstTransferCalls + 2u, fake.transferCalls);
    TEST_ASSERT_EQUAL_UINT32(fake.transferWrite, fake.transferRead);
    TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
    TEST_ASSERT_FALSE(fake.overflow);
  }
}

void test_lock_postcheck_failure_preserves_accepted_and_exact_status() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);
  TEST_ASSERT_TRUE(fake.queueTransfer(memoryAddressOnlyOk()));
  queueMutationOk(fake);
  TransferScript failure{};
  failure.result.code = TransportCode::TIMEOUT;
  failure.result.phase = TransferPhase::MEMORY_ADDRESS;
  failure.result.detail = 4201;
  failure.result.firstDeviceAddressAcked = true;
  TEST_ASSERT_TRUE(fake.queueTransfer(failure));

  MutationResult result{};
  const Status status = driver.permanentlyLockSecurity(result);
  assertStatus(Err::TRANSPORT_TIMEOUT, status);
  TEST_ASSERT_EQUAL_INT32(4201, status.detail);
  assertMutationResult(result, MutationEffect::ACCEPTED, false);
  TEST_ASSERT_EQUAL_UINT32(1, fake.waitCalls);
  TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
  TEST_ASSERT_FALSE(fake.overflow);
}

void test_rom_zone_read_mappings_and_invalid_values_are_exact() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);

  for (uint8_t zone = 0; zone < 4; ++zone) {
    const uint8_t disabled = 0;
    TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&disabled, 1)));
    bool enabled = true;
    TEST_ASSERT_TRUE(driver.readRomZoneState(zone, enabled).ok());
    TEST_ASSERT_FALSE(enabled);
    const CapturedTransfer& first = fake.captured[fake.capturedCount - 1u];
    TEST_ASSERT_EQUAL_HEX8(0x70u, first.deviceAddress);
    TEST_ASSERT_EQUAL_HEX8(EXPECTED_ROM_REGISTERS[zone],
                           first.memoryAddress);
    TEST_ASSERT_EQUAL_HEX8(0x71u, first.repeatedDeviceAddress);

    const uint8_t enabledValue = 0xFFu;
    TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&enabledValue, 1)));
    TEST_ASSERT_TRUE(driver.readRomZoneState(zone, enabled).ok());
    TEST_ASSERT_TRUE(enabled);
  }

  const uint8_t invalid = 0x7Eu;
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&invalid, 1)));
  bool enabled = true;
  const Status status = driver.readRomZoneState(0, enabled);
  assertStatus(Err::VERIFY_MISMATCH, status);
  TEST_ASSERT_EQUAL_INT32(0x7E, status.detail);
  TEST_ASSERT_FALSE(enabled);

  const size_t transfers = fake.transferCalls;
  enabled = true;
  assertStatus(Err::INVALID_PARAM, driver.readRomZoneState(4, enabled));
  TEST_ASSERT_FALSE(enabled);
  TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);
}

void test_rom_zone_mutation_outcomes_and_one_health_update_are_exact() {
  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    const uint8_t enabled = 0xFFu;
    TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&enabled, 1)));
    const size_t firstTransferCalls = fake.transferCalls;
    MutationResult result{};
    TEST_ASSERT_TRUE(driver.permanentlyEnableRomZone(2, result).ok());
    assertMutationResult(result, MutationEffect::VERIFIED, true);
    TEST_ASSERT_EQUAL_UINT32(2, fake.capturedCount);
    TEST_ASSERT_EQUAL_UINT32(firstTransferCalls + 1u, fake.transferCalls);
    TEST_ASSERT_FALSE(fake.overflow);
  }

  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    const uint8_t disabled = 0;
    const uint8_t enabled = 0xFFu;
    TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&disabled, 1)));
    queueMutationOk(fake);
    TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&enabled, 1)));
    const size_t firstCapture = fake.capturedCount;
    const SettingsSnapshot before = driver.snapshot();
    MutationResult result{};
    TEST_ASSERT_TRUE(driver.permanentlyEnableRomZone(3, result).ok());
    assertMutationResult(result, MutationEffect::VERIFIED, false);
    TEST_ASSERT_EQUAL_UINT32(before.totalSuccess + 1u,
                             driver.snapshot().totalSuccess);
    const CapturedTransfer& mutation = fake.captured[firstCapture + 1u];
    TEST_ASSERT_EQUAL_HEX8(0x70u, mutation.deviceAddress);
    TEST_ASSERT_EQUAL_HEX8(0x08u, mutation.memoryAddress);
    TEST_ASSERT_EQUAL_UINT32(1, mutation.txLength);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, mutation.txData[0]);
    TEST_ASSERT_EQUAL_UINT32(1, fake.waitCalls);
    TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
    TEST_ASSERT_FALSE(fake.overflow);
  }

  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    const uint8_t disabled = 0;
    TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&disabled, 1)));
    TEST_ASSERT_TRUE(fake.queueTransfer(mutationDataFailure(
        TransportCode::IO_ERROR, 4301, true)));
    TEST_ASSERT_TRUE(fake.queueWait(mutationWaitOk()));
    const size_t firstTransferCalls = fake.transferCalls;
    MutationResult result{};
    const Status status = driver.permanentlyEnableRomZone(0, result);
    assertStatus(Err::IO_ERROR, status);
    assertMutationResult(result, MutationEffect::MAY_HAVE_COMMITTED, false);
    TEST_ASSERT_EQUAL_UINT32(firstTransferCalls + 2u, fake.transferCalls);
    TEST_ASSERT_EQUAL_UINT32(fake.transferWrite, fake.transferRead);
    TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
    TEST_ASSERT_FALSE(fake.overflow);
  }

  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    const size_t transfers = fake.transferCalls;
    MutationResult result{MutationEffect::VERIFIED, true};
    assertStatus(Err::INVALID_PARAM,
                 driver.permanentlyEnableRomZone(4, result));
    assertMutationResult(result, MutationEffect::NOT_ATTEMPTED, false);
    TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);
  }
}

void test_rom_postcheck_mismatch_and_failure_preserve_accepted() {
  const uint8_t disabled = 0;
  for (uint8_t scenario = 0; scenario < 2; ++scenario) {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&disabled, 1)));
    queueMutationOk(fake);
    if (scenario == 0) {
      TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&disabled, 1)));
    } else {
      TransferScript failure{};
      failure.result.code = TransportCode::LINE_STUCK;
      failure.result.phase = TransferPhase::DATA_READ;
      failure.result.detail = 4401;
      failure.result.dataBytesTransferred = 0;
      failure.result.firstDeviceAddressAcked = true;
      failure.result.memoryAddressAcked = true;
      failure.result.repeatedDeviceAddressAcked = true;
      TEST_ASSERT_TRUE(fake.queueTransfer(failure));
    }

    MutationResult result{};
    const Status status = driver.permanentlyEnableRomZone(1, result);
    assertStatus(scenario == 0 ? Err::VERIFY_MISMATCH : Err::LINE_STUCK,
                 status);
    TEST_ASSERT_EQUAL_INT32(scenario == 0 ? 0 : 4401, status.detail);
    assertMutationResult(result, MutationEffect::ACCEPTED, false);
    TEST_ASSERT_EQUAL_UINT32(1, fake.waitCalls);
    TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
    TEST_ASSERT_FALSE(fake.overflow);
  }
}

void test_freeze_observation_ack_is_early_stop_and_mutation_address_nack_is_indeterminate() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);
  TEST_ASSERT_TRUE(fake.queueTransfer(addressOnlyOk()));
  TEST_ASSERT_TRUE(fake.queueTransfer(deviceAddressNack()));
  const size_t firstCapture = fake.capturedCount;
  const size_t firstTransferCalls = fake.transferCalls;

  MutationResult result{};
  const Status status = driver.permanentlyFreezeRomZones(result);
  assertStatus(Err::INDETERMINATE, status);
  assertMutationResult(result, MutationEffect::NOT_ATTEMPTED, false);
  TEST_ASSERT_EQUAL_UINT32(firstCapture + 2u, fake.capturedCount);
  TEST_ASSERT_EQUAL_UINT32(firstTransferCalls + 2u, fake.transferCalls);
  TEST_ASSERT_TRUE(bus.snapshot().previousTransfer.stopCompleted);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(TransportCode::OK),
      static_cast<uint8_t>(bus.snapshot().previousTransfer.code));
  const CapturedTransfer& observation = fake.captured[firstCapture];
  TEST_ASSERT_EQUAL_HEX8(0x10u, observation.deviceAddress);
  TEST_ASSERT_FALSE(observation.hasMemoryAddress);
  TEST_ASSERT_EQUAL_UINT32(0, observation.txLength);
  TEST_ASSERT_EQUAL_UINT32(0, observation.rxLength);
  const CapturedTransfer& mutation = fake.captured[firstCapture + 1u];
  TEST_ASSERT_EQUAL_HEX8(0x10u, mutation.deviceAddress);
  TEST_ASSERT_TRUE(mutation.hasMemoryAddress);
  TEST_ASSERT_EQUAL_HEX8(0x55u, mutation.memoryAddress);
  TEST_ASSERT_EQUAL_UINT32(1, mutation.txLength);
  TEST_ASSERT_EQUAL_HEX8(0xAAu, mutation.txData[0]);
  TEST_ASSERT_FALSE(fake.overflow);
}

void test_freeze_observation_liveness_confirms_only_matching_part() {
  {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    TEST_ASSERT_TRUE(fake.queueTransfer(deviceAddressNack()));
    TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(CS11_ID)));
    const size_t firstCapture = fake.capturedCount;
    const size_t firstTransferCalls = fake.transferCalls;
    MutationResult result{};
    TEST_ASSERT_TRUE(driver.permanentlyFreezeRomZones(result).ok());
    assertMutationResult(result, MutationEffect::VERIFIED, true);
    TEST_ASSERT_EQUAL_UINT32(firstCapture + 2u, fake.capturedCount);
    TEST_ASSERT_EQUAL_UINT32(firstTransferCalls + 2u, fake.transferCalls);
    TEST_ASSERT_EQUAL_HEX8(0xC1u,
                           fake.captured[firstCapture + 1u].deviceAddress);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(TransportCode::NACK),
        static_cast<uint8_t>(bus.snapshot().previousTransfer.code));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(TransportCode::OK),
        static_cast<uint8_t>(bus.snapshot().lastTransfer.code));
    TEST_ASSERT_FALSE(fake.overflow);
  }

  for (uint8_t scenario = 0; scenario < 2; ++scenario) {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    TEST_ASSERT_TRUE(fake.queueTransfer(deviceAddressNack()));
    if (scenario == 0) {
      TEST_ASSERT_TRUE(fake.queueTransfer(
          directReadFailure(TransportCode::TIMEOUT, 4501)));
    } else {
      TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(CS01_ID)));
    }
    const size_t firstTransferCalls = fake.transferCalls;
    MutationResult result{};
    const Status status = driver.permanentlyFreezeRomZones(result);
    assertStatus(Err::INDETERMINATE, status);
    assertMutationResult(result, MutationEffect::NOT_ATTEMPTED, false);
    TEST_ASSERT_EQUAL_UINT32(firstTransferCalls + 2u, fake.transferCalls);
    TEST_ASSERT_EQUAL_UINT32(fake.transferWrite, fake.transferRead);
    TEST_ASSERT_FALSE(fake.overflow);
  }
}

void test_freeze_complete_frame_and_confirmed_postcheck_are_exact() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  Config config{};
  config.addressBits = 2;
  initializeDriver(driver, bus, fake, config, CS11_ID);
  TEST_ASSERT_TRUE(fake.queueTransfer(addressOnlyOk()));
  queueMutationOk(fake);
  TEST_ASSERT_TRUE(fake.queueTransfer(deviceAddressNack()));
  TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(CS11_ID)));
  const size_t firstCapture = fake.capturedCount;
  const SettingsSnapshot before = driver.snapshot();

  MutationResult result{};
  TEST_ASSERT_TRUE(driver.permanentlyFreezeRomZones(result).ok());
  assertMutationResult(result, MutationEffect::VERIFIED, false);
  TEST_ASSERT_EQUAL_UINT32(firstCapture + 4u, fake.capturedCount);
  const CapturedTransfer& mutation = fake.captured[firstCapture + 1u];
  TEST_ASSERT_EQUAL_HEX8(0x14u, mutation.deviceAddress);
  TEST_ASSERT_TRUE(mutation.hasMemoryAddress);
  TEST_ASSERT_EQUAL_HEX8(0x55u, mutation.memoryAddress);
  TEST_ASSERT_EQUAL_UINT32(1, mutation.txLength);
  TEST_ASSERT_EQUAL_HEX8(0xAAu, mutation.txData[0]);
  TEST_ASSERT_EQUAL_UINT32(before.totalSuccess + 1u,
                           driver.snapshot().totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(1, fake.waitCalls);
  TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
  TEST_ASSERT_FALSE(fake.overflow);
  for (size_t index = firstCapture; index < fake.capturedCount; ++index) {
    const uint8_t address = fake.captured[index].deviceAddress;
    TEST_ASSERT_FALSE((address & 0xF1u) == 0x11u);
  }
}

void test_freeze_accepted_postcheck_mismatch_and_failures_stay_accepted() {
  for (uint8_t scenario = 0; scenario < 3; ++scenario) {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    TEST_ASSERT_TRUE(fake.queueTransfer(addressOnlyOk()));
    queueMutationOk(fake);
    Err expected = Err::VERIFY_MISMATCH;
    if (scenario == 0) {
      TEST_ASSERT_TRUE(fake.queueTransfer(addressOnlyOk()));
    } else if (scenario == 1) {
      TEST_ASSERT_TRUE(fake.queueTransfer(deviceAddressNack()));
      TEST_ASSERT_TRUE(fake.queueTransfer(
          directReadFailure(TransportCode::IO_ERROR, 4601)));
      expected = Err::INDETERMINATE;
    } else {
      TransferScript failure{};
      failure.result.code = TransportCode::TIMEOUT;
      failure.result.phase = TransferPhase::DEVICE_ADDRESS_WRITE;
      failure.result.detail = 4602;
      TEST_ASSERT_TRUE(fake.queueTransfer(failure));
      expected = Err::TRANSPORT_TIMEOUT;
    }

    MutationResult result{};
    const Status status = driver.permanentlyFreezeRomZones(result);
    assertStatus(expected, status);
    const int32_t expectedDetail =
        scenario == 0 ? 0 : (scenario == 1 ? 4601 : 4602);
    TEST_ASSERT_EQUAL_INT32(expectedDetail, status.detail);
    assertMutationResult(result, MutationEffect::ACCEPTED, false);
    TEST_ASSERT_EQUAL_UINT32(1, fake.waitCalls);
    TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
    TEST_ASSERT_FALSE(fake.overflow);
  }
}

void test_freeze_first_data_nack_has_no_hold_or_effect() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);
  TEST_ASSERT_TRUE(fake.queueTransfer(addressOnlyOk()));
  TEST_ASSERT_TRUE(fake.queueTransfer(
      mutationDataFailure(TransportCode::NACK, 0, false)));
  const size_t waits = fake.waitCalls;
  const size_t transferCalls = fake.transferCalls;

  MutationResult result{};
  const Status status = driver.permanentlyFreezeRomZones(result);
  assertStatus(Err::NACK_DATA, status);
  assertMutationResult(result, MutationEffect::NOT_ATTEMPTED, false);
  TEST_ASSERT_EQUAL_UINT32(waits, fake.waitCalls);
  TEST_ASSERT_EQUAL_UINT32(transferCalls + 2u, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT32(fake.transferWrite, fake.transferRead);
  TEST_ASSERT_FALSE(fake.overflow);
}

void test_freeze_uncertain_payload_is_held_ambiguous_and_not_replayed() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);
  TEST_ASSERT_TRUE(fake.queueTransfer(addressOnlyOk()));
  TEST_ASSERT_TRUE(fake.queueTransfer(
      mutationDataFailure(TransportCode::TIMEOUT, 4701, true)));
  TEST_ASSERT_TRUE(fake.queueWait(mutationWaitOk()));
  const size_t firstCapture = fake.capturedCount;
  const size_t firstTransferCalls = fake.transferCalls;

  MutationResult result{};
  const Status status = driver.permanentlyFreezeRomZones(result);
  assertStatus(Err::TRANSPORT_TIMEOUT, status);
  TEST_ASSERT_EQUAL_INT32(4701, status.detail);
  assertMutationResult(result, MutationEffect::MAY_HAVE_COMMITTED, false);
  TEST_ASSERT_EQUAL_UINT32(firstCapture + 2u, fake.capturedCount);
  TEST_ASSERT_EQUAL_UINT32(firstTransferCalls + 2u, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT32(1, fake.waitCalls);
  TEST_ASSERT_EQUAL_UINT32(fake.transferWrite, fake.transferRead);
  TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
  TEST_ASSERT_FALSE(fake.overflow);
}

void test_freeze_observation_is_not_cached_between_calls() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);
  TEST_ASSERT_TRUE(fake.queueTransfer(deviceAddressNack()));
  TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(CS11_ID)));
  MutationResult result{};
  TEST_ASSERT_TRUE(driver.permanentlyFreezeRomZones(result).ok());
  assertMutationResult(result, MutationEffect::VERIFIED, true);
  const size_t afterFirst = fake.capturedCount;

  TEST_ASSERT_TRUE(fake.queueTransfer(addressOnlyOk()));
  TEST_ASSERT_TRUE(fake.queueTransfer(deviceAddressNack()));
  const Status status = driver.permanentlyFreezeRomZones(result);
  assertStatus(Err::INDETERMINATE, status);
  assertMutationResult(result, MutationEffect::NOT_ATTEMPTED, false);
  TEST_ASSERT_EQUAL_UINT32(afterFirst + 2u, fake.capturedCount);
  TEST_ASSERT_EQUAL_UINT32(fake.transferWrite, fake.transferRead);
  TEST_ASSERT_FALSE(fake.overflow);
}

void test_mutation_results_reset_before_state_failures() {
  Driver driver;
  MutationResult result{MutationEffect::VERIFIED, true};
  assertStatus(Err::NOT_BOUND, driver.permanentlyLockSecurity(result));
  assertMutationResult(result, MutationEffect::NOT_ATTEMPTED, false);
  result = MutationResult{MutationEffect::VERIFIED, true};
  assertStatus(Err::NOT_BOUND,
               driver.permanentlyEnableRomZone(0, result));
  assertMutationResult(result, MutationEffect::NOT_ATTEMPTED, false);
  result = MutationResult{MutationEffect::VERIFIED, true};
  assertStatus(Err::NOT_BOUND, driver.permanentlyFreezeRomZones(result));
  assertMutationResult(result, MutationEffect::NOT_ATTEMPTED, false);
}
