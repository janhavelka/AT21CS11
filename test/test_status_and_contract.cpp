#include <cstddef>
#include <cstdint>

#include <unity.h>

#include "AT21CS/AT21CS.h"
#include "support/ExpectedFrames.h"
#include "support/TestBuilders.h"

using namespace AT21CS;
using namespace AT21CS::test;

void test_status_messages_and_crc_vectors_are_independent_literals() {
  static constexpr Err ERRORS[] = {
      Err::OK, Err::NOT_BOUND, Err::NOT_INITIALIZED, Err::INVALID_STATE,
      Err::BUSY, Err::INVALID_CONFIG, Err::INVALID_PARAM, Err::NOT_PRESENT,
      Err::NACK_DEVICE_ADDRESS, Err::NACK_MEMORY_ADDRESS, Err::NACK_DATA,
      Err::TRANSPORT_TIMEOUT, Err::LINE_STUCK, Err::IO_ERROR,
      Err::CLOCK_STALLED, Err::UNSUPPORTED_COMMAND, Err::CRC_MISMATCH,
      Err::PART_MISMATCH, Err::VERIFY_MISMATCH, Err::INDETERMINATE};
  for (const Err error : ERRORS) {
    const Status status = error == Err::OK ? Status{} : Status::Error(error);
    TEST_ASSERT_EQUAL_STRING(expectedErrName(error), status.msg);
  }

  static constexpr uint8_t DIGITS[] = {'1', '2', '3', '4', '5',
                                       '6', '7', '8', '9'};
  static constexpr uint8_t SHORT_VECTOR[] = {0x01u, 0x02u, 0x03u};
  TEST_ASSERT_EQUAL_HEX8(0x00u, Driver::crc8Maxim(nullptr, 0));
  TEST_ASSERT_EQUAL_HEX8(0xA1u,
                         Driver::crc8Maxim(DIGITS, sizeof(DIGITS)));
  TEST_ASSERT_EQUAL_HEX8(
      0xD8u, Driver::crc8Maxim(SHORT_VECTOR, sizeof(SHORT_VECTOR)));
  TEST_ASSERT_EQUAL_HEX8(
      0x00u, Driver::crc8Maxim(nullptr, sizeof(SHORT_VECTOR)));
}

void test_expected_address_oracle_covers_all_documented_opcodes() {
  struct AddressCase {
    uint8_t opcode;
    uint8_t addressBits;
    bool read;
    uint8_t raw;
  };
  static constexpr AddressCase CASES[] = {
      {0x0Au, 0u, false, 0xA0u}, {0x0Au, 0u, true, 0xA1u},
      {0x0Au, 1u, false, 0xA2u}, {0x0Au, 7u, true, 0xAFu},
      {0x0Bu, 0u, false, 0xB0u}, {0x0Bu, 7u, true, 0xBFu},
      {0x0Cu, 0u, true, 0xC1u},  {0x0Cu, 7u, true, 0xCFu},
      {0x0Du, 0u, false, 0xD0u}, {0x0Eu, 7u, false, 0xEEu},
      {0x02u, 0u, false, 0x20u}, {0x07u, 1u, true, 0x73u},
      {0x01u, 7u, false, 0x1Eu}};
  for (const AddressCase& testCase : CASES) {
    TEST_ASSERT_EQUAL_HEX8(
        testCase.raw,
        expected::rawAddress(testCase.opcode, testCase.addressBits,
                             testCase.read));
  }
  TEST_ASSERT_EQUAL_HEX8(0x10u,
                         expected::rawAddress(0x01u, 0u, false));
}

void test_cached_getters_are_bus_silent() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, 0x00D385u);
  const size_t events = fake.eventCount;
  const size_t transfers = fake.transferCalls;
  const size_t resets = fake.resetCalls;
  const size_t waits = fake.waitCalls;
  const size_t presence = fake.presenceCalls;

  TEST_ASSERT_TRUE(driver.isBound());
  TEST_ASSERT_TRUE(driver.isInitialized());
  TEST_ASSERT_TRUE(driver.isOnline());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(driver.state()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PartType::AT21CS11),
                          static_cast<uint8_t>(driver.detectedPart()));
  TEST_ASSERT_EQUAL_HEX32(0x00D385u, driver.manufacturerId());
  TEST_ASSERT_EQUAL_UINT8(5u, driver.siliconRevision());
  TEST_ASSERT_TRUE(driver.isSpeedKnown());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SpeedMode::HIGH_SPEED),
                          static_cast<uint8_t>(driver.speedMode()));
  TEST_ASSERT_TRUE(driver.lastStatus().ok());
  TEST_ASSERT_TRUE(driver.lastError().ok());
  (void)driver.snapshot();
  (void)bus.isBound();
  (void)bus.hasPresenceIndicator();
  (void)bus.generation();
  (void)bus.snapshot();

  TEST_ASSERT_EQUAL_UINT32(events, fake.eventCount);
  TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT32(resets, fake.resetCalls);
  TEST_ASSERT_EQUAL_UINT32(waits, fake.waitCalls);
  TEST_ASSERT_EQUAL_UINT32(presence, fake.presenceCalls);
}

void test_scripted_transport_rejects_wrong_frames_deadlines_and_hold_traffic() {
  {
    ScriptedTransport fake;
    TransferScript script{};
    script.expected = expected::addressOnly(
        0xA0u, SpeedMode::HIGH_SPEED,
        expected::HIGH_SPEED_POST_HIGH_US);
    script.result = addressOnlyOk().result;
    TEST_ASSERT_TRUE(fake.queueTransfer(script));
    SingleWireTransfer wrong{};
    wrong.deviceAddress = 0xA2u;
    wrong.minimumPostTransferHighUs = expected::HIGH_SPEED_POST_HIGH_US;
    const SingleWireTransport descriptor = fake.descriptor(false);
    const TransferResult result =
        descriptor.transfer(wrong, 10000u, descriptor.user);
    TEST_ASSERT_EQUAL_INT32(ScriptedTransport::SCRIPT_ERROR_DETAIL,
                            result.detail);
    TEST_ASSERT_TRUE(fake.mismatch);
  }
  {
    ScriptedTransport fake;
    WaitScript wait{};
    wait.result = auxiliaryOk(TransferPhase::WAIT_HIGH);
    wait.verifyDeadline = true;
    wait.expectedDeadlineUs = 11000u;
    TEST_ASSERT_TRUE(fake.queueWait(wait));
    const SingleWireTransport descriptor = fake.descriptor(false);
    const TransferResult result =
        descriptor.waitUntilUs(11001u, descriptor.user);
    TEST_ASSERT_EQUAL_INT32(ScriptedTransport::SCRIPT_ERROR_DETAIL,
                            result.detail);
    TEST_ASSERT_TRUE(fake.mismatch);
  }
  {
    ScriptedTransport fake;
    fake.activeWriteHighUntilUs = 2000u;
    TransferScript script{};
    script.expected = expected::addressOnly(
        0xA0u, SpeedMode::HIGH_SPEED,
        expected::HIGH_SPEED_POST_HIGH_US);
    script.result = addressOnlyOk().result;
    TEST_ASSERT_TRUE(fake.queueTransfer(script));
    const SingleWireTransport descriptor = fake.descriptor(false);
    SingleWireTransfer transfer{};
    transfer.deviceAddress = 0xA0u;
    transfer.minimumPostTransferHighUs = 160u;
    (void)descriptor.transfer(transfer, 3000u, descriptor.user);
    TEST_ASSERT_TRUE(fake.transferDuringWriteHighHold);
  }
}
