#include <cstddef>
#include <cstdint>

#include <unity.h>

#include "AT21CS/AT21CS.h"
#include "support/TestAccess.h"
#include "support/TestBuilders.h"

using namespace AT21CS;
using namespace AT21CS::test;

namespace {

struct StateCase {
  DriverState state;
  bool initialized;
  Err normalIo;
  Err probe;
  bool online;
};

constexpr StateCase STATE_CASES[] = {
    {DriverState::UNINIT, false, Err::NOT_INITIALIZED,
     Err::NOT_INITIALIZED, false},
    {DriverState::PROBING, false, Err::INVALID_STATE, Err::INVALID_STATE,
     false},
    {DriverState::INIT_CONFIG, false, Err::INVALID_STATE,
     Err::INVALID_STATE, false},
    {DriverState::READY, true, Err::OK, Err::OK, true},
    {DriverState::BUSY, true, Err::BUSY, Err::BUSY, false},
    {DriverState::DEGRADED, true, Err::OK, Err::OK, true},
    {DriverState::OFFLINE, true, Err::INVALID_STATE, Err::OK, false},
    {DriverState::RECOVERING, false, Err::INVALID_STATE,
     Err::INVALID_STATE, false},
    {DriverState::SLEEPING, false, Err::INVALID_STATE, Err::INVALID_STATE,
     false},
    {DriverState::FAULT, false, Err::INVALID_STATE, Err::INVALID_STATE,
     false}};

TransferScript startFailure(TransportCode code, int32_t detail) {
  TransferScript script{};
  script.expected = expected::randomRead(
      0xA0u, 0u, 0xA1u, 1u, SpeedMode::HIGH_SPEED,
      expected::HIGH_SPEED_POST_HIGH_US);
  script.result.code = code;
  script.result.phase = TransferPhase::START;
  script.result.detail = detail;
  return script;
}

}  // namespace

void test_all_driver_states_have_exact_online_and_normal_io_admission() {
  for (const StateCase& testCase : STATE_CASES) {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, 0x00D385u);
    TestAccess::seedDriverState(driver, testCase.state,
                                testCase.initialized);
    const SettingsSnapshot before = driver.snapshot();
    const size_t transfers = fake.transferCalls;
    if (testCase.normalIo == Err::OK) {
      const uint8_t expectedByte = 0x4Cu;
      TEST_ASSERT_TRUE(
          fake.queueTransfer(withExpected(
              randomReadOk(&expectedByte, 1u),
              expected::randomRead(
                  0xA0u, 0u, 0xA1u, 1u,
                  SpeedMode::HIGH_SPEED,
                  expected::HIGH_SPEED_POST_HIGH_US))));
    }
    uint8_t value = 0xA5u;
    const Status status = driver.readEeprom(0u, &value, 1u);
    assertStatus(testCase.normalIo, status);
    TEST_ASSERT_EQUAL(testCase.online, before.bound && driver.isOnline());
    if (testCase.normalIo == Err::OK) {
      TEST_ASSERT_EQUAL_HEX8(0x4Cu, value);
      TEST_ASSERT_EQUAL_UINT32(before.totalSuccess + 1u,
                               driver.snapshot().totalSuccess);
    } else {
      TEST_ASSERT_EQUAL_HEX8(0xA5u, value);
      TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);
      TEST_ASSERT_EQUAL_UINT32(before.totalSuccess,
                               driver.snapshot().totalSuccess);
      TEST_ASSERT_EQUAL_UINT32(before.totalFailures,
                               driver.snapshot().totalFailures);
    }
  }
}

void test_all_driver_states_have_exact_probe_admission() {
  for (const StateCase& testCase : STATE_CASES) {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, 0x00D385u);
    TestAccess::seedDriverState(driver, testCase.state,
                                testCase.initialized);
    const SettingsSnapshot before = driver.snapshot();
    const size_t transfers = fake.transferCalls;
    if (testCase.probe == Err::OK) {
      TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(0x00D385u)));
    }
    const Status status = driver.probe();
    assertStatus(testCase.probe, status);
    if (testCase.probe == Err::OK) {
      TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                              static_cast<uint8_t>(driver.state()));
      TEST_ASSERT_EQUAL_UINT32(before.totalSuccess + 1u,
                               driver.snapshot().totalSuccess);
    } else {
      TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);
      TEST_ASSERT_EQUAL_UINT32(before.totalSuccess,
                               driver.snapshot().totalSuccess);
      TEST_ASSERT_EQUAL_UINT32(before.totalFailures,
                               driver.snapshot().totalFailures);
    }
  }
}

void test_all_driver_states_have_exact_initialize_and_recover_admission() {
  for (const StateCase& testCase : STATE_CASES) {
    {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      Driver driver;
      initializeDriver(driver, bus, fake, Config{}, 0x00D385u);
      TestAccess::seedDriverState(driver, testCase.state,
                                  testCase.initialized);
      const SettingsSnapshot before = driver.snapshot();
      const size_t events = fake.eventCount;
      const bool allowed = testCase.state == DriverState::UNINIT;
      if (allowed) {
        queueInitialize(fake, 0x00D385u);
      }
      const Status status = driver.initialize();
      assertStatus(allowed ? Err::OK : Err::INVALID_STATE, status);
      if (allowed) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                                static_cast<uint8_t>(driver.state()));
        TEST_ASSERT_EQUAL_UINT32(before.totalSuccess + 1u,
                                 driver.snapshot().totalSuccess);
      } else {
        TEST_ASSERT_EQUAL_UINT32(events, fake.eventCount);
        TEST_ASSERT_EQUAL_UINT32(before.totalSuccess,
                                 driver.snapshot().totalSuccess);
        TEST_ASSERT_EQUAL_UINT32(before.totalFailures,
                                 driver.snapshot().totalFailures);
      }
      assertOracleClean(fake);
    }
    {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      Driver driver;
      initializeDriver(driver, bus, fake, Config{}, 0x00D385u);
      TestAccess::seedDriverState(driver, testCase.state,
                                  testCase.initialized);
      const SettingsSnapshot before = driver.snapshot();
      const size_t events = fake.eventCount;
      const bool allowed =
          testCase.state == DriverState::UNINIT ||
          testCase.state == DriverState::READY ||
          testCase.state == DriverState::DEGRADED ||
          testCase.state == DriverState::OFFLINE;
      if (allowed) {
        queueInitialize(fake, 0x00D385u);
      }
      const Status status = driver.recover();
      assertStatus(allowed ? Err::OK : Err::INVALID_STATE, status);
      if (allowed) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                                static_cast<uint8_t>(driver.state()));
        TEST_ASSERT_EQUAL_UINT32(before.totalSuccess + 1u,
                                 driver.snapshot().totalSuccess);
      } else {
        TEST_ASSERT_EQUAL_UINT32(events, fake.eventCount);
        TEST_ASSERT_EQUAL_UINT32(before.totalSuccess,
                                 driver.snapshot().totalSuccess);
        TEST_ASSERT_EQUAL_UINT32(before.totalFailures,
                                 driver.snapshot().totalFailures);
      }
      assertOracleClean(fake);
    }
  }
}

void test_fault_requires_rebind_then_initialize() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  queueInitialize(fake, 0x00D385u);
  Driver driver;
  Config wrongPart{};
  wrongPart.expectedPart = PartType::AT21CS01;
  assertStatus(Err::PART_MISMATCH, driver.begin(bus, wrongPart));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(driver.state()));
  const size_t events = fake.eventCount;
  assertStatus(Err::INVALID_STATE, driver.recover());
  TEST_ASSERT_EQUAL_UINT32(events, fake.eventCount);

  Config correctPart{};
  correctPart.expectedPart = PartType::AT21CS11;
  TEST_ASSERT_TRUE(driver.bind(bus, correctPart).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(driver.state()));
  queueInitialize(fake, 0x00D385u);
  TEST_ASSERT_TRUE(driver.initialize().ok());
  TEST_ASSERT_TRUE(driver.isOnline());
}

void test_health_threshold_and_success_recovery_are_exact() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  Config config{};
  config.offlineThreshold = 2u;
  initializeDriver(driver, bus, fake, config, 0x00D385u);

  uint8_t value = 0xA5u;
  TEST_ASSERT_TRUE(
      fake.queueTransfer(startFailure(TransportCode::TIMEOUT, 7101)));
  assertStatus(Err::TRANSPORT_TIMEOUT,
               driver.readEeprom(0u, &value, 1u));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(driver.state()));
  TEST_ASSERT_EQUAL_UINT8(1u, driver.snapshot().consecutiveFailures);

  TEST_ASSERT_TRUE(
      fake.queueTransfer(startFailure(TransportCode::LINE_STUCK, 7102)));
  assertStatus(Err::LINE_STUCK, driver.readEeprom(0u, &value, 1u));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(driver.state()));
  TEST_ASSERT_EQUAL_UINT8(2u, driver.snapshot().consecutiveFailures);

  TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(0x00D385u)));
  TEST_ASSERT_TRUE(driver.probe().ok());
  const SettingsSnapshot after = driver.snapshot();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(after.state));
  TEST_ASSERT_EQUAL_UINT8(0u, after.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::LINE_STUCK),
                          static_cast<uint8_t>(after.lastErrorCode));
  TEST_ASSERT_EQUAL_INT32(7102, after.lastErrorDetail);
}
