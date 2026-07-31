#include <cstddef>
#include <cstdint>
#include <limits>

#include <unity.h>

#include "AT21CS/AT21CS.h"
#include "support/DriverTestSupport.h"

using namespace AT21CS;
using namespace AT21CS::test;

namespace {

constexpr uint32_t CS11_ID = 0x00D384u;
constexpr size_t EXPECTED_PAGE_SIZE = 8;
constexpr uint8_t EXPECTED_EEPROM_OPCODE = 0x0A;
constexpr uint8_t EXPECTED_SECURITY_OPCODE = 0x0B;

TransferScript writeOk(size_t length, bool hasMemoryAddress = true) {
  TransferScript script{};
  script.result.code = TransportCode::OK;
  script.result.phase = TransferPhase::STOP;
  script.result.dataBytesTransferred = length;
  script.result.firstDeviceAddressAcked = true;
  script.result.memoryAddressAcked = hasMemoryAddress;
  script.result.stopCompleted = true;
  return script;
}

TransferScript writeFailure(TransportCode code,
                            TransferPhase phase,
                            int32_t detail,
                            size_t accepted = 0,
                            bool currentMayBeAccepted = false) {
  TransferScript script{};
  script.result.code = code;
  script.result.phase = phase;
  script.result.detail = detail;
  script.result.dataBytesTransferred = accepted;
  script.result.currentWriteByteMayBeAccepted = currentMayBeAccepted;
  if (phase == TransferPhase::MEMORY_ADDRESS ||
      phase == TransferPhase::DATA_WRITE || phase == TransferPhase::STOP) {
    script.result.firstDeviceAddressAcked = true;
  }
  if (phase == TransferPhase::DATA_WRITE || phase == TransferPhase::STOP) {
    script.result.memoryAddressAcked = true;
  }
  return script;
}

WaitScript waitOk() {
  WaitScript script{};
  script.result = auxiliaryOk(TransferPhase::WAIT_HIGH);
  script.advanceToDeadline = true;
  return script;
}

WaitScript waitFailure(TransportCode code, int32_t detail) {
  WaitScript script{};
  script.result.code = code;
  script.result.phase = TransferPhase::WAIT_HIGH;
  script.result.detail = detail;
  return script;
}

void queuePageOk(ScriptedTransport& fake, size_t length) {
  TEST_ASSERT_TRUE(fake.queueTransfer(writeOk(length)));
  TEST_ASSERT_TRUE(fake.queueWait(waitOk()));
}

size_t pageCount(uint8_t address, size_t length) {
  size_t count = 0;
  size_t offset = 0;
  while (offset < length) {
    const size_t absolute = static_cast<size_t>(address) + offset;
    const size_t pageRemaining =
        EXPECTED_PAGE_SIZE - (absolute % EXPECTED_PAGE_SIZE);
    const size_t remaining = length - offset;
    offset += remaining < pageRemaining ? remaining : pageRemaining;
    ++count;
  }
  return count;
}

void queueBulkOk(ScriptedTransport& fake, uint8_t address, size_t length) {
  size_t offset = 0;
  while (offset < length) {
    const size_t absolute = static_cast<size_t>(address) + offset;
    const size_t pageRemaining =
        EXPECTED_PAGE_SIZE - (absolute % EXPECTED_PAGE_SIZE);
    const size_t remaining = length - offset;
    const size_t chunk = remaining < pageRemaining ? remaining : pageRemaining;
    queuePageOk(fake, chunk);
    offset += chunk;
  }
}

void assertBulkCaptures(const ScriptedTransport& fake,
                        size_t firstCapture,
                        uint8_t address,
                        size_t length,
                        uint8_t opcode) {
  size_t offset = 0;
  size_t captureIndex = firstCapture;
  while (offset < length) {
    const size_t absolute = static_cast<size_t>(address) + offset;
    const size_t pageRemaining =
        EXPECTED_PAGE_SIZE - (absolute % EXPECTED_PAGE_SIZE);
    const size_t remaining = length - offset;
    const size_t chunk = remaining < pageRemaining ? remaining : pageRemaining;
    const CapturedTransfer& capture = fake.captured[captureIndex++];
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(opcode << 4u),
                           capture.deviceAddress);
    TEST_ASSERT_TRUE(capture.hasMemoryAddress);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(absolute),
                           capture.memoryAddress);
    TEST_ASSERT_EQUAL_UINT32(chunk, capture.txLength);
    TEST_ASSERT_FALSE(capture.hasRepeatedStart);
    TEST_ASSERT_EQUAL_UINT32(0, capture.rxLength);
    offset += chunk;
  }
  TEST_ASSERT_EQUAL_UINT32(firstCapture + pageCount(address, length),
                           captureIndex);
}

}  // namespace

void test_eeprom_page_positions_lengths_and_frames_are_exact() {
  const uint8_t payload[8] = {0x10u, 0x11u, 0x12u, 0x13u,
                              0x14u, 0x15u, 0x16u, 0x17u};
  for (uint8_t pageOffset = 0; pageOffset < EXPECTED_PAGE_SIZE; ++pageOffset) {
    const size_t maximum = EXPECTED_PAGE_SIZE - pageOffset;
    for (size_t length = 1; length <= EXPECTED_PAGE_SIZE; ++length) {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      Driver driver;
      initializeDriver(driver, bus, fake, Config{}, CS11_ID);

      WriteResult result{};
      const uint8_t address = static_cast<uint8_t>(0x40u + pageOffset);
      if (length > maximum) {
        const size_t transfers = fake.transferCalls;
        assertStatus(Err::INVALID_PARAM,
                     driver.writeEepromPage(address, payload, length, result));
        TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(WriteEffect::NOT_ATTEMPTED),
            static_cast<uint8_t>(result.lastPageEffect));
        continue;
      }
      queuePageOk(fake, length);
      TEST_ASSERT_TRUE(
          driver.writeEepromPage(address, payload, length, result).ok());
      TEST_ASSERT_EQUAL_UINT32(length, result.bytesCommitted);
      TEST_ASSERT_EQUAL_UINT32(length, result.lastPageBytesAccepted);
      TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteEffect::COMMITTED),
                              static_cast<uint8_t>(result.lastPageEffect));
      const CapturedTransfer& capture = fake.captured[fake.capturedCount - 1u];
      TEST_ASSERT_EQUAL_HEX8(0xA0u, capture.deviceAddress);
      TEST_ASSERT_TRUE(capture.hasMemoryAddress);
      TEST_ASSERT_EQUAL_HEX8(address, capture.memoryAddress);
      TEST_ASSERT_EQUAL_UINT32(length, capture.txLength);
      TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, capture.txData, length);
      TEST_ASSERT_EQUAL_UINT32(160, capture.minimumPostTransferHighUs);
      TEST_ASSERT_EQUAL_UINT32(1, fake.waitCalls);
      TEST_ASSERT_EQUAL_UINT32(fake.transferWrite, fake.transferRead);
      TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
    }
  }
}

void test_eeprom_bulk_edges_and_page_splits_are_exact() {
  struct Case {
    uint8_t address;
    size_t length;
  };
  static constexpr Case cases[] = {{0, 1},   {1, 7},   {1, 8},
                                   {7, 2},   {8, 9},   {120, 8},
                                   {127, 1}, {1, 127}, {0, 128}};
  uint8_t payload[128] = {};
  for (size_t index = 0; index < sizeof(payload); ++index) {
    payload[index] = static_cast<uint8_t>(index);
  }

  for (const Case& testCase : cases) {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    const size_t firstCapture = fake.capturedCount;
    queueBulkOk(fake, testCase.address, testCase.length);

    WriteResult result{};
    TEST_ASSERT_TRUE(driver
                         .writeEeprom(testCase.address, payload,
                                      testCase.length, result)
                         .ok());
    TEST_ASSERT_EQUAL_UINT32(testCase.length, result.bytesCommitted);
    TEST_ASSERT_EQUAL_UINT32(pageCount(testCase.address, testCase.length),
                             fake.waitCalls);
    assertBulkCaptures(fake, firstCapture, testCase.address, testCase.length,
                       EXPECTED_EEPROM_OPCODE);
    TEST_ASSERT_FALSE(fake.overflow);
    TEST_ASSERT_EQUAL_UINT32(fake.transferWrite, fake.transferRead);
    TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
  }
}

void test_write_validation_is_complete_transactional_and_callback_free() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);
  const size_t transfers = fake.transferCalls;
  const size_t waits = fake.waitCalls;
  uint8_t data[16] = {};
  WriteResult result{99, 88, WriteEffect::COMMITTED};

  const auto expectInvalid = [&](const Status& status) {
    assertStatus(Err::INVALID_PARAM, status);
    TEST_ASSERT_EQUAL_UINT32(0, result.bytesCommitted);
    TEST_ASSERT_EQUAL_UINT32(0, result.lastPageBytesAccepted);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteEffect::NOT_ATTEMPTED),
                            static_cast<uint8_t>(result.lastPageEffect));
    result = WriteResult{99, 88, WriteEffect::COMMITTED};
  };

  expectInvalid(driver.writeEepromPage(0, nullptr, 1, result));
  expectInvalid(driver.writeEepromPage(0, data, 0, result));
  expectInvalid(driver.writeEepromPage(0, data, 9, result));
  expectInvalid(driver.writeEepromPage(7, data, 2, result));
  expectInvalid(driver.writeEepromPage(128, data, 1, result));
  expectInvalid(driver.writeEeprom(0, nullptr, 1, result));
  expectInvalid(driver.writeEeprom(0, data, 0, result));
  expectInvalid(driver.writeEeprom(127, data, 2, result));
  expectInvalid(driver.writeEeprom(
      0, data, std::numeric_limits<size_t>::max(), result));

  expectInvalid(driver.writeSecurityUserPage(0x10, nullptr, 1, result));
  expectInvalid(driver.writeSecurityUserPage(0x10, data, 0, result));
  expectInvalid(driver.writeSecurityUserPage(0x10, data, 9, result));
  expectInvalid(driver.writeSecurityUserPage(0x0F, data, 1, result));
  expectInvalid(driver.writeSecurityUserPage(0x20, data, 1, result));
  expectInvalid(driver.writeSecurityUserPage(0x17, data, 2, result));
  expectInvalid(driver.writeSecurityUser(0x1F, data, 2, result));
  expectInvalid(driver.writeSecurityUser(0x10, data, 0x10000u, result));
  expectInvalid(driver.writeSecurityUser(
      0x10, data, std::numeric_limits<size_t>::max(), result));
  expectInvalid(driver.writeSecurityUser(0x10, nullptr, 1, result));

  TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT32(waits, fake.waitCalls);

  Driver unbound;
  result = WriteResult{99, 88, WriteEffect::COMMITTED};
  assertStatus(Err::NOT_BOUND,
               unbound.writeEepromPage(0, data, 1, result));
  TEST_ASSERT_EQUAL_UINT32(0, result.bytesCommitted);
  TEST_ASSERT_EQUAL_UINT32(0, result.lastPageBytesAccepted);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteEffect::NOT_ATTEMPTED),
                          static_cast<uint8_t>(result.lastPageEffect));
}

void test_write_nacks_map_every_address_and_data_phase_without_replay() {
  const uint8_t payload[8] = {};
  for (uint8_t scenario = 0; scenario < 10; ++scenario) {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);

    TransferScript failure{};
    Err expected = Err::NACK_DEVICE_ADDRESS;
    size_t accepted = 0;
    if (scenario == 0) {
      failure = writeFailure(TransportCode::NACK,
                             TransferPhase::DEVICE_ADDRESS_WRITE, 0);
    } else if (scenario == 1) {
      failure = writeFailure(TransportCode::NACK,
                             TransferPhase::MEMORY_ADDRESS, 0);
      expected = Err::NACK_MEMORY_ADDRESS;
    } else {
      accepted = scenario - 2u;
      failure = writeFailure(TransportCode::NACK,
                             TransferPhase::DATA_WRITE, 0, accepted);
      expected = Err::NACK_DATA;
      if (accepted != 0) {
        TEST_ASSERT_TRUE(fake.queueWait(waitOk()));
      }
    }
    TEST_ASSERT_TRUE(fake.queueTransfer(failure));

    WriteResult result{};
    const size_t firstCapture = fake.capturedCount;
    const size_t firstTransferCalls = fake.transferCalls;
    const Status status = driver.writeEepromPage(0, payload, 8, result);
    assertStatus(expected, status);
    TEST_ASSERT_EQUAL_UINT32(accepted, result.lastPageBytesAccepted);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(accepted == 0
                                 ? WriteEffect::NOT_ATTEMPTED
                                 : WriteEffect::MAY_HAVE_COMMITTED),
        static_cast<uint8_t>(result.lastPageEffect));
    TEST_ASSERT_EQUAL_UINT32(0, result.bytesCommitted);
    TEST_ASSERT_EQUAL_UINT32(firstCapture + 1u, fake.capturedCount);
    TEST_ASSERT_EQUAL_UINT32(firstTransferCalls + 1u, fake.transferCalls);
    TEST_ASSERT_EQUAL_UINT32(accepted == 0 ? 0 : 1, fake.waitCalls);
    if (expected == Err::NACK_DATA) {
      TEST_ASSERT_EQUAL_UINT16(accepted,
                               protocolDetailIndex(status.detail));
    }
    TEST_ASSERT_EQUAL_UINT32(fake.transferWrite, fake.transferRead);
    TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
    TEST_ASSERT_FALSE(fake.overflow);
  }
}

void test_write_transport_failures_preserve_phase_prefix_and_exact_error() {
  struct Scenario {
    TransferPhase phase;
    size_t accepted;
  };
  static constexpr Scenario scenarios[] = {
      {TransferPhase::START, 0},
      {TransferPhase::DEVICE_ADDRESS_WRITE, 0},
      {TransferPhase::MEMORY_ADDRESS, 0},
      {TransferPhase::DATA_WRITE, 3},
      {TransferPhase::STOP, 8}};
  const uint8_t payload[8] = {};

  for (const Scenario& scenario : scenarios) {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    TEST_ASSERT_TRUE(fake.queueTransfer(writeFailure(
        TransportCode::TIMEOUT, scenario.phase, 3101, scenario.accepted)));
    if (scenario.accepted != 0) {
      TEST_ASSERT_TRUE(fake.queueWait(waitOk()));
    }

    WriteResult result{};
    const size_t firstTransferCalls = fake.transferCalls;
    const Status status = driver.writeEepromPage(0, payload, 8, result);
    assertStatus(Err::TRANSPORT_TIMEOUT, status);
    TEST_ASSERT_EQUAL_INT32(3101, status.detail);
    TEST_ASSERT_EQUAL_UINT32(scenario.accepted,
                             result.lastPageBytesAccepted);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(scenario.accepted == 0
                                 ? WriteEffect::NOT_ATTEMPTED
                                 : WriteEffect::MAY_HAVE_COMMITTED),
        static_cast<uint8_t>(result.lastPageEffect));
    TEST_ASSERT_EQUAL_UINT32(scenario.accepted == 0 ? 0 : 1,
                             fake.waitCalls);
    TEST_ASSERT_EQUAL_UINT32(firstTransferCalls + 1u, fake.transferCalls);
    TEST_ASSERT_FALSE(fake.overflow);
  }
}

void test_every_uncertain_data_ack_is_held_reported_and_never_replayed() {
  static constexpr TransportCode codes[] = {
      TransportCode::TIMEOUT, TransportCode::LINE_STUCK,
      TransportCode::IO_ERROR};
  static constexpr Err errors[] = {Err::TRANSPORT_TIMEOUT, Err::LINE_STUCK,
                                   Err::IO_ERROR};
  const uint8_t payload[8] = {};

  for (size_t codeIndex = 0; codeIndex < 3; ++codeIndex) {
    for (size_t accepted = 0; accepted < 8; ++accepted) {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      Driver driver;
      initializeDriver(driver, bus, fake, Config{}, CS11_ID);
      TEST_ASSERT_TRUE(fake.queueTransfer(writeFailure(
          codes[codeIndex], TransferPhase::DATA_WRITE,
          static_cast<int32_t>(3200u + accepted), accepted, true)));
      TEST_ASSERT_TRUE(fake.queueWait(waitOk()));

      WriteResult result{};
      const size_t firstCapture = fake.capturedCount;
      const size_t firstTransferCalls = fake.transferCalls;
      const Status status = driver.writeEepromPage(0, payload, 8, result);
      assertStatus(errors[codeIndex], status);
      TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(3200u + accepted),
                              status.detail);
      TEST_ASSERT_EQUAL_UINT32(accepted, result.lastPageBytesAccepted);
      TEST_ASSERT_EQUAL_UINT8(
          static_cast<uint8_t>(WriteEffect::MAY_HAVE_COMMITTED),
          static_cast<uint8_t>(result.lastPageEffect));
      TEST_ASSERT_EQUAL_UINT32(0, result.bytesCommitted);
      TEST_ASSERT_EQUAL_UINT32(firstCapture + 1u, fake.capturedCount);
      TEST_ASSERT_EQUAL_UINT32(firstTransferCalls + 1u, fake.transferCalls);
      TEST_ASSERT_EQUAL_UINT32(1, fake.waitCalls);
      TEST_ASSERT_TRUE(bus.snapshot()
                           .lastWriteCycle.frame.currentWriteByteMayBeAccepted);
      TEST_ASSERT_EQUAL_UINT32(fake.transferWrite, fake.transferRead);
      TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
      TEST_ASSERT_FALSE(fake.overflow);
    }
  }
}

void test_full_frame_hold_failures_are_ambiguous_and_retain_deadline() {
  const uint8_t payload[8] = {};
  for (uint8_t scenario = 0; scenario < 3; ++scenario) {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    TEST_ASSERT_TRUE(fake.queueTransfer(writeOk(8)));
    const size_t firstCapture = fake.capturedCount;
    const size_t firstTransferCalls = fake.transferCalls;
    Err expected = Err::IO_ERROR;
    if (scenario == 0) {
      TEST_ASSERT_TRUE(fake.queueWait(waitFailure(TransportCode::IO_ERROR,
                                                  3301)));
    } else if (scenario == 1) {
      WaitScript early{};
      early.result = auxiliaryOk(TransferPhase::WAIT_HIGH);
      TEST_ASSERT_TRUE(fake.queueWait(early));
      expected = Err::CLOCK_STALLED;
    } else {
      TEST_ASSERT_TRUE(fake.queueNow(fake.currentUs));
      TEST_ASSERT_TRUE(fake.queueNow(
          std::numeric_limits<uint64_t>::max() - 1u));
      expected = Err::CLOCK_STALLED;
    }

    WriteResult result{};
    const Status status = driver.writeEepromPage(0, payload, 8, result);
    assertStatus(expected, status);
    TEST_ASSERT_EQUAL_UINT32(8, result.lastPageBytesAccepted);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(WriteEffect::MAY_HAVE_COMMITTED),
        static_cast<uint8_t>(result.lastPageEffect));
    TEST_ASSERT_EQUAL_UINT32(0, result.bytesCommitted);
    TEST_ASSERT_NOT_EQUAL(0, bus.snapshot().writeHighUntilUs);
    TEST_ASSERT_EQUAL_UINT32(firstCapture + 1u, fake.capturedCount);
    TEST_ASSERT_EQUAL_UINT32(firstTransferCalls + 1u, fake.transferCalls);
    TEST_ASSERT_FALSE(fake.overflow);
  }
}

void test_write_hold_trace_has_no_intervening_frame_events() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);
  fake.eventCount = 0;
  const uint8_t data = 0x5Au;
  queuePageOk(fake, 1);

  WriteResult result{};
  TEST_ASSERT_TRUE(driver.writeEepromPage(0, &data, 1, result).ok());
  size_t endIndex = fake.eventCount;
  size_t waitIndex = fake.eventCount;
  uint64_t stopAtUs = 0;
  for (size_t index = 0; index < fake.eventCount; ++index) {
    if (fake.events[index].kind == FakeEventKind::TRANSFER_END) {
      endIndex = index;
    }
    if (fake.events[index].kind == FakeEventKind::STOP) {
      stopAtUs = fake.events[index].atUs;
    }
    if (fake.events[index].kind == FakeEventKind::WAIT_UNTIL) {
      waitIndex = index;
      break;
    }
  }
  TEST_ASSERT_TRUE(endIndex < waitIndex);
  TEST_ASSERT_EQUAL_UINT64(stopAtUs + 10000u, fake.lastWaitDeadlineUs);
  for (size_t index = endIndex + 1u; index < fake.eventCount; ++index) {
    const FakeEventKind kind = fake.events[index].kind;
    TEST_ASSERT_TRUE(kind == FakeEventKind::NOW_US ||
                     kind == FakeEventKind::WAIT_UNTIL);
  }
  TEST_ASSERT_EQUAL_UINT32(1,
                           fake.eventCountFor(FakeEventKind::TRANSFER_BEGIN));
  TEST_ASSERT_EQUAL_UINT32(1,
                           fake.eventCountFor(FakeEventKind::WAIT_UNTIL));
  TEST_ASSERT_EQUAL_UINT64(fake.lastWaitDeadlineUs, fake.currentUs);
  TEST_ASSERT_FALSE(fake.overflow);
}

void test_retained_hold_blocks_a_second_driver_before_its_frame() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver first;
  Driver second;
  Config secondConfig{};
  secondConfig.addressBits = 1;
  initializeDriver(first, bus, fake, Config{}, CS11_ID);
  initializeDriver(second, bus, fake, secondConfig, CS11_ID);

  const uint8_t value = 0x34u;
  TEST_ASSERT_TRUE(fake.queueTransfer(writeOk(1)));
  TEST_ASSERT_TRUE(
      fake.queueWait(waitFailure(TransportCode::IO_ERROR, 3401)));
  WriteResult writeResult{};
  assertStatus(Err::IO_ERROR,
               first.writeEepromPage(0, &value, 1, writeResult));
  const size_t captures = fake.capturedCount;
  const size_t transferCalls = fake.transferCalls;

  TEST_ASSERT_TRUE(
      fake.queueWait(waitFailure(TransportCode::TIMEOUT, 3402)));
  uint8_t output = 0xFFu;
  const Status status = second.readEeprom(0, &output, 1);
  assertStatus(Err::TRANSPORT_TIMEOUT, status);
  TEST_ASSERT_EQUAL_HEX8(0xFFu, output);
  TEST_ASSERT_EQUAL_UINT32(captures, fake.capturedCount);
  TEST_ASSERT_EQUAL_UINT32(transferCalls, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT32(2, fake.waitCalls);
  TEST_ASSERT_FALSE(fake.overflow);
}

void test_security_write_frames_locked_nack_and_bulk_health_are_exact() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);
  uint8_t data[16] = {};
  queueBulkOk(fake, 0x10, sizeof(data));
  const SettingsSnapshot before = driver.snapshot();
  const size_t firstCapture = fake.capturedCount;

  WriteResult result{};
  TEST_ASSERT_TRUE(
      driver.writeSecurityUser(0x10, data, sizeof(data), result).ok());
  TEST_ASSERT_EQUAL_UINT32(16, result.bytesCommitted);
  assertBulkCaptures(fake, firstCapture, 0x10, sizeof(data),
                     EXPECTED_SECURITY_OPCODE);
  const SettingsSnapshot after = driver.snapshot();
  TEST_ASSERT_EQUAL_UINT32(before.totalSuccess + 1u, after.totalSuccess);

  TEST_ASSERT_TRUE(fake.queueTransfer(writeFailure(
      TransportCode::NACK, TransferPhase::DATA_WRITE, 0, 0)));
  const size_t waits = fake.waitCalls;
  const Status status =
      driver.writeSecurityUserPage(0x10, data, 1, result);
  assertStatus(Err::NACK_DATA, status);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteEffect::NOT_ATTEMPTED),
                          static_cast<uint8_t>(result.lastPageEffect));
  TEST_ASSERT_EQUAL_UINT32(0, result.lastPageBytesAccepted);
  TEST_ASSERT_EQUAL_UINT32(waits, fake.waitCalls);
  TEST_ASSERT_FALSE(fake.overflow);
}

void test_multi_page_write_stops_on_ambiguous_page_and_keeps_prefix() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);
  uint8_t data[20] = {};
  queuePageOk(fake, 8);
  TEST_ASSERT_TRUE(fake.queueTransfer(writeFailure(
      TransportCode::LINE_STUCK, TransferPhase::DATA_WRITE, 3501, 2, true)));
  TEST_ASSERT_TRUE(fake.queueWait(waitOk()));
  const SettingsSnapshot before = driver.snapshot();
  const size_t firstCapture = fake.capturedCount;
  const size_t firstTransferCalls = fake.transferCalls;

  WriteResult result{};
  const Status status = driver.writeEeprom(0, data, sizeof(data), result);
  assertStatus(Err::LINE_STUCK, status);
  TEST_ASSERT_EQUAL_UINT32(8, result.bytesCommitted);
  TEST_ASSERT_EQUAL_UINT32(2, result.lastPageBytesAccepted);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(WriteEffect::MAY_HAVE_COMMITTED),
      static_cast<uint8_t>(result.lastPageEffect));
  TEST_ASSERT_EQUAL_UINT32(firstCapture + 2u, fake.capturedCount);
  TEST_ASSERT_EQUAL_UINT32(firstTransferCalls + 2u, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT32(before.totalFailures + 1u,
                           driver.snapshot().totalFailures);
  TEST_ASSERT_EQUAL_UINT32(fake.transferWrite, fake.transferRead);
  TEST_ASSERT_EQUAL_UINT32(fake.waitWrite, fake.waitRead);
  TEST_ASSERT_FALSE(fake.overflow);
}

void test_write_and_mutation_stale_binding_are_callback_free() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);
  const size_t transfers = fake.transferCalls;
  const SettingsSnapshot before = driver.snapshot();
  BusConfig replacement{};
  replacement.transport = fake.descriptor(false);
  TEST_ASSERT_TRUE(bus.bind(replacement).ok());

  const uint8_t data = 0xA5u;
  WriteResult writeResult{3, 2, WriteEffect::COMMITTED};
  assertStatus(Err::INVALID_STATE,
               driver.writeEepromPage(0, &data, 1, writeResult));
  TEST_ASSERT_EQUAL_UINT32(0, writeResult.bytesCommitted);
  TEST_ASSERT_EQUAL_UINT32(0, writeResult.lastPageBytesAccepted);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteEffect::NOT_ATTEMPTED),
                          static_cast<uint8_t>(writeResult.lastPageEffect));

  MutationResult mutationResult{MutationEffect::VERIFIED, true};
  assertStatus(Err::INVALID_STATE,
               driver.permanentlyFreezeRomZones(mutationResult));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(MutationEffect::NOT_ATTEMPTED),
      static_cast<uint8_t>(mutationResult.effect));
  TEST_ASSERT_FALSE(mutationResult.alreadyApplied);
  TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT32(before.totalSuccess,
                           driver.snapshot().totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(before.totalFailures,
                           driver.snapshot().totalFailures);
}
