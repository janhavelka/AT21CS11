#include <cstddef>
#include <cstdint>
#include <limits>

#include <unity.h>

#include "AT21CS/platform/esp32/Esp32Transport.h"
#include "support/TestAccess.h"

using namespace AT21CS;
using AT21CS::test::TestAccess;

namespace {

SingleWireTransfer addressOnly(SpeedMode speed = SpeedMode::HIGH_SPEED) {
  SingleWireTransfer transfer{};
  transfer.speed = speed;
  transfer.deviceAddress = 0xA0;
  transfer.minimumPostTransferHighUs =
      speed == SpeedMode::HIGH_SPEED ? 160 : 650;
  return transfer;
}

void queueLevel(Esp32Transport& transport, bool high) {
  TEST_ASSERT_TRUE(TestAccess::queueLineLevel(transport, high));
}

void assertCodePhase(const TransferResult& result,
                     TransportCode code,
                     TransferPhase phase) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(code),
                          static_cast<uint8_t>(result.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(phase),
                          static_cast<uint8_t>(result.phase));
}

uint16_t collectLowEvents(const Esp32Transport& transport,
                          uint16_t* indices,
                          uint16_t capacity) {
  uint16_t count = 0;
  for (uint16_t index = 0; index < TestAccess::lineEventCount(transport);
       ++index) {
    if (!TestAccess::lineEventReleased(transport, index)) {
      TEST_ASSERT_LESS_THAN_UINT16(capacity, count);
      indices[count++] = index;
    }
  }
  return count;
}

}  // namespace

void test_esp32_timing_profiles_and_pin_ranges_are_exact() {
  TEST_ASSERT_EQUAL_UINT32(64000,
                           TestAccess::bitNs(SpeedMode::STANDARD_SPEED));
  TEST_ASSERT_EQUAL_UINT32(32000,
                           TestAccess::low0Ns(SpeedMode::STANDARD_SPEED));
  TEST_ASSERT_EQUAL_UINT32(6000,
                           TestAccess::low1Ns(SpeedMode::STANDARD_SPEED));
  TEST_ASSERT_EQUAL_UINT32(6000,
                           TestAccess::readLowNs(SpeedMode::STANDARD_SPEED));
  TEST_ASSERT_EQUAL_UINT32(7000,
                           TestAccess::readSampleNs(
                               SpeedMode::STANDARD_SPEED));
  TEST_ASSERT_EQUAL_UINT32(650,
                           TestAccess::startHighUs(
                               SpeedMode::STANDARD_SPEED));

  TEST_ASSERT_EQUAL_UINT32(16000,
                           TestAccess::bitNs(SpeedMode::HIGH_SPEED));
  TEST_ASSERT_EQUAL_UINT32(10000,
                           TestAccess::low0Ns(SpeedMode::HIGH_SPEED));
  TEST_ASSERT_EQUAL_UINT32(1500,
                           TestAccess::low1Ns(SpeedMode::HIGH_SPEED));
  TEST_ASSERT_EQUAL_UINT32(1200,
                           TestAccess::readLowNs(SpeedMode::HIGH_SPEED));
  TEST_ASSERT_EQUAL_UINT32(1800,
                           TestAccess::readSampleNs(SpeedMode::HIGH_SPEED));
  TEST_ASSERT_EQUAL_UINT32(160,
                           TestAccess::startHighUs(SpeedMode::HIGH_SPEED));

  TEST_ASSERT_EQUAL_UINT32(120, TestAccess::cyclesForNs(1500, 80));
  TEST_ASSERT_EQUAL_UINT32(240, TestAccess::cyclesForNs(1500, 160));
  TEST_ASSERT_EQUAL_UINT32(360, TestAccess::cyclesForNs(1500, 240));
  TEST_ASSERT_EQUAL_UINT32(144, TestAccess::cyclesForNs(1800, 80));
  TEST_ASSERT_EQUAL_UINT32(288, TestAccess::cyclesForNs(1800, 160));
  TEST_ASSERT_EQUAL_UINT32(432, TestAccess::cyclesForNs(1800, 240));

  Esp32TransportConfig config{};
  config.sioPin = 0;
  TEST_ASSERT_TRUE(TestAccess::pinNumbersInRange(config, 49));
  config.sioPin = 31;
  TEST_ASSERT_TRUE(TestAccess::pinNumbersInRange(config, 47));
  config.sioPin = 32;
  TEST_ASSERT_TRUE(TestAccess::pinNumbersInRange(config, 47));
  config.sioPin = 46;
  TEST_ASSERT_TRUE(TestAccess::pinNumbersInRange(config, 47));
  config.sioPin = 47;
  TEST_ASSERT_FALSE(TestAccess::pinNumbersInRange(config, 47));
  config.sioPin = 48;
  config.presencePin = 47;
  TEST_ASSERT_TRUE(TestAccess::pinNumbersInRange(config, 49));
  config.sioPin = -1;
  TEST_ASSERT_FALSE(TestAccess::pinNumbersInRange(config, 49));
  config.sioPin = 49;
  TEST_ASSERT_FALSE(TestAccess::pinNumbersInRange(config, 49));
  config.sioPin = std::numeric_limits<int>::max();
  TEST_ASSERT_FALSE(TestAccess::pinNumbersInRange(config, 49));
  config.sioPin = 1;
  config.presencePin = -2;
  TEST_ASSERT_FALSE(TestAccess::pinNumbersInRange(config, 49));
  config.presencePin = std::numeric_limits<int>::min();
  TEST_ASSERT_FALSE(TestAccess::pinNumbersInRange(config, 49));
  config.presencePin = 49;
  TEST_ASSERT_FALSE(TestAccess::pinNumbersInRange(config, 49));
  config.presencePin = config.sioPin;
  TEST_ASSERT_FALSE(TestAccess::pinNumbersInRange(config, 49));
}

void test_esp32_transfer_validation_and_wait_guards_are_bounded() {
  Esp32Transport transport;
  TestAccess::activateWithoutHardware(transport, -1);

  uint8_t byte = 0;
  SingleWireTransfer invalid = addressOnly();
  invalid.txData = &byte;
  invalid.txLength = std::numeric_limits<size_t>::max();
  const TransferResult invalidResult =
      TestAccess::transfer(transport, invalid, 1000);
  assertCodePhase(invalidResult, TransportCode::IO_ERROR,
                  TransferPhase::NONE);
  TEST_ASSERT_EQUAL_UINT16(0, TestAccess::lineEventCount(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  const TransferResult insufficientDeadline =
      TestAccess::transfer(transport, addressOnly(), 160);
  assertCodePhase(insufficientDeadline, TransportCode::TIMEOUT,
                  TransferPhase::START);
  TEST_ASSERT_EQUAL_UINT16(0, TestAccess::lineEventCount(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, false);
  queueLevel(transport, true);
  const TransferResult hugeDeadline = TestAccess::transfer(
      transport, addressOnly(), std::numeric_limits<uint64_t>::max());
  assertCodePhase(hugeDeadline, TransportCode::OK,
                  TransferPhase::STOP);
  TEST_ASSERT_TRUE(hugeDeadline.firstDeviceAddressAcked);
  TEST_ASSERT_TRUE(hugeDeadline.stopCompleted);

  TestAccess::activateWithoutHardware(transport, -1);
  TestAccess::setNowUs(transport, 100);
  const TransferResult tooLong = TestAccess::waitUntilUs(transport, 10101);
  assertCodePhase(tooLong, TransportCode::TIMEOUT,
                  TransferPhase::WAIT_HIGH);
  TEST_ASSERT_EQUAL_UINT16(0, TestAccess::lineEventCount(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  TestAccess::setNowUs(transport, 100);
  const TransferResult completed = TestAccess::waitUntilUs(transport, 260);
  assertCodePhase(completed, TransportCode::OK,
                  TransferPhase::WAIT_HIGH);
  TEST_ASSERT_EQUAL_UINT64(260, TestAccess::nowUs(transport));
  TEST_ASSERT_TRUE(TestAccess::lineReleased(transport));
  TEST_ASSERT_EQUAL_UINT16(1,
                           TestAccess::timingLockAcquireCount(transport));
  TEST_ASSERT_EQUAL_UINT16(1,
                           TestAccess::timingLockReleaseCount(transport));
  TEST_ASSERT_EQUAL_UINT16(0, TestAccess::timingLockDepth(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  TestAccess::setNowUs(transport, 100);
  queueLevel(transport, false);
  const TransferResult heldLow = TestAccess::waitUntilUs(transport, 100);
  assertCodePhase(heldLow, TransportCode::LINE_STUCK,
                  TransferPhase::WAIT_HIGH);
  TEST_ASSERT_TRUE(TestAccess::lineReleased(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  TestAccess::setNowUs(transport, 100);
  TestAccess::freezeClock(transport, true);
  const TransferResult frozenBefore =
      TestAccess::waitUntilUs(transport, 260);
  assertCodePhase(frozenBefore, TransportCode::TIMEOUT,
                  TransferPhase::WAIT_HIGH);
  TEST_ASSERT_EQUAL_INT32(TestAccess::frozenClockDetail(),
                          frozenBefore.detail);
  TEST_ASSERT_TRUE(TestAccess::lineReleased(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  TestAccess::setNowUs(transport, 100);
  TestAccess::freezeAfterCoarseDelay(transport, 159);
  const TransferResult frozenAfter =
      TestAccess::waitUntilUs(transport, 260);
  assertCodePhase(frozenAfter, TransportCode::TIMEOUT,
                  TransferPhase::WAIT_HIGH);
  TEST_ASSERT_EQUAL_INT32(TestAccess::frozenClockDetail(),
                          frozenAfter.detail);
  TEST_ASSERT_EQUAL_UINT32(100000,
                           TestAccess::finalWaitPollLimit());
  TEST_ASSERT_EQUAL_UINT32(2000, TestAccess::finalWaitGuardUs());
  TEST_ASSERT_TRUE(TestAccess::lineReleased(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  TestAccess::setNowUs(transport, 100);
  TestAccess::freezeClock(transport, true);
  TestAccess::setNowCallCycles(transport, 2);
  const TransferResult cycleGuard =
      TestAccess::waitUntilUs(transport, 260);
  assertCodePhase(cycleGuard, TransportCode::TIMEOUT,
                  TransferPhase::WAIT_HIGH);
  TEST_ASSERT_EQUAL_INT32(TestAccess::frozenClockDetail(),
                          cycleGuard.detail);
  TEST_ASSERT_LESS_THAN_UINT32(
      160u * 240u + TestAccess::finalWaitPollLimit() * 3u,
      TestAccess::cycle(transport));
  TEST_ASSERT_EQUAL_UINT16(1,
                           TestAccess::timingLockAcquireCount(transport));
  TEST_ASSERT_EQUAL_UINT16(1,
                           TestAccess::timingLockReleaseCount(transport));
  TEST_ASSERT_EQUAL_UINT16(0, TestAccess::timingLockDepth(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  TestAccess::failTimingLock(transport, true);
  const TransferResult lockFailure =
      TestAccess::transfer(transport, addressOnly(), 5000);
  assertCodePhase(lockFailure, TransportCode::IO_ERROR,
                  TransferPhase::START);
  TEST_ASSERT_EQUAL_UINT16(0, TestAccess::lineEventCount(transport));
  TEST_ASSERT_EQUAL_UINT16(1,
                           TestAccess::timingLockAcquireCount(transport));
  TEST_ASSERT_EQUAL_UINT16(0,
                           TestAccess::timingLockReleaseCount(transport));
  TEST_ASSERT_EQUAL_UINT16(0, TestAccess::timingLockDepth(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  TestAccess::setNowUs(transport, 100);
  TestAccess::setNowCallCycles(transport, 240);
  TEST_ASSERT_FALSE(TestAccess::beginSegment(transport, 101));
}

void test_esp32_bit_slots_are_preflighted_before_falling_edges() {
  Esp32Transport writeTransport;
  TestAccess::activateWithoutHardware(writeTransport, -1);
  queueLevel(writeTransport, true);

  const TransferResult writeResult =
      TestAccess::transfer(writeTransport, addressOnly(), 171);
  assertCodePhase(writeResult, TransportCode::TIMEOUT,
                  TransferPhase::DEVICE_ADDRESS_WRITE);
  uint16_t writeLowIndices[2]{};
  TEST_ASSERT_EQUAL_UINT16(
      0, collectLowEvents(writeTransport, writeLowIndices, 2));
  TEST_ASSERT_TRUE(TestAccess::lineReleased(writeTransport));

  Esp32Transport ackTransport;
  TestAccess::activateWithoutHardware(ackTransport, -1);
  queueLevel(ackTransport, true);

  const TransferResult ackResult =
      TestAccess::transfer(ackTransport, addressOnly(), 299);
  assertCodePhase(ackResult, TransportCode::TIMEOUT,
                  TransferPhase::DEVICE_ADDRESS_WRITE);
  uint16_t lowIndices[12]{};
  TEST_ASSERT_EQUAL_UINT16(
      8, collectLowEvents(ackTransport, lowIndices, 12));
  TEST_ASSERT_TRUE(TestAccess::lineReleased(ackTransport));
  TEST_ASSERT_EQUAL_UINT16(1, TestAccess::readCount(ackTransport));
}

void test_esp32_reset_discovery_has_one_exact_request_and_release_check() {
  Esp32Transport transport;
  TestAccess::activateWithoutHardware(transport, -1);
  bool present = true;
  const TransferResult insufficientDeadline =
      TestAccess::resetAndDiscover(transport, present, 795);
  assertCodePhase(insufficientDeadline, TransportCode::TIMEOUT,
                  TransferPhase::RESET_LOW);
  TEST_ASSERT_FALSE(present);
  TEST_ASSERT_EQUAL_UINT16(0, TestAccess::lineEventCount(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  TestAccess::failTimingLock(transport, true);
  present = true;
  const TransferResult lockFailure =
      TestAccess::resetAndDiscover(transport, present, 5000);
  assertCodePhase(lockFailure, TransportCode::IO_ERROR,
                  TransferPhase::RESET_LOW);
  TEST_ASSERT_FALSE(present);
  TEST_ASSERT_EQUAL_UINT16(0, TestAccess::lineEventCount(transport));
  TEST_ASSERT_EQUAL_UINT16(1,
                           TestAccess::timingLockAcquireCount(transport));
  TEST_ASSERT_EQUAL_UINT16(0,
                           TestAccess::timingLockReleaseCount(transport));
  TEST_ASSERT_EQUAL_UINT16(0, TestAccess::timingLockDepth(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, false);
  queueLevel(transport, true);
  queueLevel(transport, true);

  present = false;
  const TransferResult result =
      TestAccess::resetAndDiscover(transport, present, 5000);
  assertCodePhase(result, TransportCode::OK,
                  TransferPhase::DISCOVERY_RELEASE);
  TEST_ASSERT_TRUE(present);
  TEST_ASSERT_TRUE(TestAccess::lineReleased(transport));
  TEST_ASSERT_FALSE(TestAccess::testOverflow(transport));

  uint16_t lowIndices[4]{};
  TEST_ASSERT_EQUAL_UINT16(
      2, collectLowEvents(transport, lowIndices, 4));
  const uint16_t discoveryLow = lowIndices[1];
  TEST_ASSERT_EQUAL_UINT32(146400,
                           TestAccess::lineEventCycle(transport,
                                                      discoveryLow));
  TEST_ASSERT_TRUE(
      TestAccess::lineEventReleased(transport, discoveryLow + 1u));
  TEST_ASSERT_EQUAL_UINT32(
      288,
      TestAccess::lineEventCycle(transport, discoveryLow + 1u) -
          TestAccess::lineEventCycle(transport, discoveryLow));
  TEST_ASSERT_EQUAL_UINT16(3, TestAccess::readCount(transport));
  TEST_ASSERT_EQUAL_UINT32(
      960, TestAccess::readCycle(transport, 0) -
               TestAccess::lineEventCycle(transport, discoveryLow));
  TEST_ASSERT_EQUAL_UINT32(
      6000, TestAccess::readCycle(transport, 1) -
                TestAccess::lineEventCycle(transport, discoveryLow));
  TEST_ASSERT_EQUAL_UINT32(
      38400, TestAccess::readCycle(transport, 2) -
                 TestAccess::readCycle(transport, 1));
  TEST_ASSERT_EQUAL_UINT16(1,
                           TestAccess::timingLockAcquireCount(transport));
  TEST_ASSERT_EQUAL_UINT16(1,
                           TestAccess::timingLockReleaseCount(transport));
  TEST_ASSERT_EQUAL_UINT16(0, TestAccess::timingLockDepth(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, false);
  queueLevel(transport, false);
  present = true;
  const TransferResult stuck =
      TestAccess::resetAndDiscover(transport, present, 5000);
  assertCodePhase(stuck, TransportCode::LINE_STUCK,
                  TransferPhase::DISCOVERY_RELEASE);
  TEST_ASSERT_FALSE(present);
  TEST_ASSERT_TRUE(TestAccess::lineReleased(transport));
  TEST_ASSERT_EQUAL_UINT16(
      2, collectLowEvents(transport, lowIndices, 4));

  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, true);
  queueLevel(transport, true);
  present = true;
  const TransferResult absent =
      TestAccess::resetAndDiscover(transport, present, 5000);
  assertCodePhase(absent, TransportCode::OK,
                  TransferPhase::DISCOVERY_RELEASE);
  TEST_ASSERT_FALSE(present);
  TEST_ASSERT_TRUE(TestAccess::lineReleased(transport));
  TEST_ASSERT_EQUAL_UINT16(
      2, collectLowEvents(transport, lowIndices, 4));
}

void test_esp32_high_speed_frame_is_msb_first_and_samples_ack_absolutely() {
  Esp32Transport transport;
  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, false);
  queueLevel(transport, true);

  const TransferResult result =
      TestAccess::transfer(transport, addressOnly(), 5000);
  assertCodePhase(result, TransportCode::OK, TransferPhase::STOP);
  TEST_ASSERT_TRUE(result.firstDeviceAddressAcked);
  TEST_ASSERT_TRUE(result.stopCompleted);

  uint16_t lowIndices[12]{};
  TEST_ASSERT_EQUAL_UINT16(
      9, collectLowEvents(transport, lowIndices, 12));
  const bool expectedBits[8] = {true, false, true, false,
                                false, false, false, false};
  for (uint16_t bit = 0; bit < 8; ++bit) {
    const uint16_t lowIndex = lowIndices[bit];
    TEST_ASSERT_TRUE(TestAccess::lineEventReleased(transport,
                                                   lowIndex + 1u));
    const uint32_t lowCycles =
        TestAccess::lineEventCycle(transport, lowIndex + 1u) -
        TestAccess::lineEventCycle(transport, lowIndex);
    TEST_ASSERT_EQUAL_UINT32(expectedBits[bit] ? 360 : 2400,
                             lowCycles);
    if (bit != 0) {
      TEST_ASSERT_EQUAL_UINT32(
          3840,
          TestAccess::lineEventCycle(transport, lowIndex) -
              TestAccess::lineEventCycle(transport, lowIndices[bit - 1u]));
    }
  }
  const uint16_t ackLow = lowIndices[8];
  TEST_ASSERT_EQUAL_UINT32(
      288, TestAccess::lineEventCycle(transport, ackLow + 1u) -
               TestAccess::lineEventCycle(transport, ackLow));
  TEST_ASSERT_EQUAL_UINT32(
      432, TestAccess::readCycle(transport, 1) -
               TestAccess::lineEventCycle(transport, ackLow));
  TEST_ASSERT_EQUAL_UINT32(38400,
                           TestAccess::lineEventCycle(transport,
                                                      lowIndices[0]));
  const uint16_t finalHighEvent =
      static_cast<uint16_t>(TestAccess::lineEventCount(transport) - 1u);
  TEST_ASSERT_EQUAL_UINT32(
      38400, TestAccess::readCycle(transport, 2) -
                 TestAccess::lineEventCycle(transport, finalHighEvent));
}

void test_esp32_write_ack_boundaries_preserve_ambiguous_and_definite_evidence() {
  const uint8_t payload = 0x5A;
  SingleWireTransfer transfer = addressOnly();
  transfer.txData = &payload;
  transfer.txLength = 1;

  Esp32Transport transport;
  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, false);
  TransferResult result = TestAccess::transfer(transport, transfer, 320);
  assertCodePhase(result, TransportCode::TIMEOUT,
                  TransferPhase::DATA_WRITE);
  TEST_ASSERT_TRUE(result.firstDeviceAddressAcked);
  TEST_ASSERT_EQUAL_UINT32(0, result.dataBytesTransferred);
  TEST_ASSERT_FALSE(result.currentWriteByteMayBeAccepted);
  TEST_ASSERT_FALSE(result.stopCompleted);
  TEST_ASSERT_TRUE(TestAccess::lineReleased(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, false);
  result = TestAccess::transfer(transport, transfer, 433);
  assertCodePhase(result, TransportCode::TIMEOUT,
                  TransferPhase::DATA_WRITE);
  TEST_ASSERT_TRUE(result.firstDeviceAddressAcked);
  TEST_ASSERT_EQUAL_UINT32(0, result.dataBytesTransferred);
  TEST_ASSERT_TRUE(result.currentWriteByteMayBeAccepted);
  TEST_ASSERT_FALSE(result.stopCompleted);
  TEST_ASSERT_TRUE(TestAccess::lineReleased(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, false);
  queueLevel(transport, true);
  result = TestAccess::transfer(transport, transfer, 449);
  assertCodePhase(result, TransportCode::NACK,
                  TransferPhase::DATA_WRITE);
  TEST_ASSERT_TRUE(result.firstDeviceAddressAcked);
  TEST_ASSERT_EQUAL_UINT32(0, result.dataBytesTransferred);
  TEST_ASSERT_FALSE(result.currentWriteByteMayBeAccepted);
  TEST_ASSERT_FALSE(result.stopCompleted);
  TEST_ASSERT_TRUE(TestAccess::lineReleased(transport));

  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, false);
  queueLevel(transport, false);
  result = TestAccess::transfer(transport, transfer, 449);
  assertCodePhase(result, TransportCode::TIMEOUT, TransferPhase::STOP);
  TEST_ASSERT_TRUE(result.firstDeviceAddressAcked);
  TEST_ASSERT_EQUAL_UINT32(1, result.dataBytesTransferred);
  TEST_ASSERT_FALSE(result.currentWriteByteMayBeAccepted);
  TEST_ASSERT_FALSE(result.stopCompleted);
  TEST_ASSERT_TRUE(TestAccess::lineReleased(transport));
}

void test_esp32_direct_read_uses_host_nack_and_commits_complete_prefix() {
  uint8_t output = 0;
  SingleWireTransfer transfer{};
  transfer.speed = SpeedMode::HIGH_SPEED;
  transfer.deviceAddress = 0xA1;
  transfer.rxData = &output;
  transfer.rxLength = 1;
  transfer.minimumPostTransferHighUs = 160;

  Esp32Transport transport;
  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, false);
  const bool valueBits[8] = {true, false, true, false,
                             false, true, false, true};
  for (bool high : valueBits) {
    queueLevel(transport, high);
  }
  queueLevel(transport, true);

  const TransferResult result =
      TestAccess::transfer(transport, transfer, 5000);
  assertCodePhase(result, TransportCode::OK, TransferPhase::STOP);
  TEST_ASSERT_EQUAL_HEX8(0xA5, output);
  TEST_ASSERT_EQUAL_UINT32(1, result.dataBytesTransferred);
  TEST_ASSERT_TRUE(result.firstDeviceAddressAcked);
  TEST_ASSERT_TRUE(result.stopCompleted);

  uint16_t lowIndices[24]{};
  TEST_ASSERT_EQUAL_UINT16(
      18, collectLowEvents(transport, lowIndices, 24));
  const uint16_t hostNackLow = lowIndices[17];
  TEST_ASSERT_EQUAL_UINT32(
      360, TestAccess::lineEventCycle(transport, hostNackLow + 1u) -
               TestAccess::lineEventCycle(transport, hostNackLow));

  uint8_t partialOutput[2] = {0xCC, 0xDD};
  transfer.rxData = partialOutput;
  transfer.rxLength = 2;
  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, false);
  for (uint8_t bit = 0; bit < 8; ++bit) {
    queueLevel(transport, false);
  }
  queueLevel(transport, true);

  const TransferResult partial =
      TestAccess::transfer(transport, transfer, 465);
  assertCodePhase(partial, TransportCode::TIMEOUT,
                  TransferPhase::DATA_READ);
  TEST_ASSERT_EQUAL_UINT32(1, partial.dataBytesTransferred);
  TEST_ASSERT_EQUAL_HEX8(0x00, partialOutput[0]);
  TEST_ASSERT_EQUAL_HEX8(0xDD, partialOutput[1]);
  TEST_ASSERT_TRUE(TestAccess::lineReleased(transport));
}

void test_esp32_released_high_faults_are_typed_and_silent() {
  Esp32Transport transport;
  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, false);
  const TransferResult result =
      TestAccess::transfer(transport, addressOnly(), 5000);
  assertCodePhase(result, TransportCode::LINE_STUCK,
                  TransferPhase::START);
  TEST_ASSERT_FALSE(result.firstDeviceAddressAcked);
  TEST_ASSERT_FALSE(result.stopCompleted);
  TEST_ASSERT_TRUE(TestAccess::lineReleased(transport));

  uint16_t lowIndices[2]{};
  TEST_ASSERT_EQUAL_UINT16(
      0, collectLowEvents(transport, lowIndices, 2));
}

void test_esp32_standard_frame_and_every_address_nack_phase_are_exact() {
  Esp32Transport transport;
  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, false);
  queueLevel(transport, true);
  TransferResult result = TestAccess::transfer(
      transport, addressOnly(SpeedMode::STANDARD_SPEED), 5000);
  assertCodePhase(result, TransportCode::OK, TransferPhase::STOP);

  uint16_t lowIndices[12]{};
  TEST_ASSERT_EQUAL_UINT16(
      9, collectLowEvents(transport, lowIndices, 12));
  for (uint16_t bit = 1; bit < 8; ++bit) {
    TEST_ASSERT_EQUAL_UINT32(
        15360,
        TestAccess::lineEventCycle(transport, lowIndices[bit]) -
            TestAccess::lineEventCycle(transport, lowIndices[bit - 1u]));
  }
  const uint16_t standardAckLow = lowIndices[8];
  TEST_ASSERT_EQUAL_UINT32(
      1680, TestAccess::readCycle(transport, 1) -
                TestAccess::lineEventCycle(transport, standardAckLow));

  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, true);
  queueLevel(transport, true);
  result = TestAccess::transfer(transport, addressOnly(), 5000);
  assertCodePhase(result, TransportCode::NACK,
                  TransferPhase::DEVICE_ADDRESS_WRITE);
  TEST_ASSERT_FALSE(result.firstDeviceAddressAcked);
  TEST_ASSERT_TRUE(result.stopCompleted);

  uint8_t directOutput = 0xA5;
  SingleWireTransfer directRead{};
  directRead.speed = SpeedMode::HIGH_SPEED;
  directRead.deviceAddress = 0xA1;
  directRead.rxData = &directOutput;
  directRead.rxLength = 1;
  directRead.minimumPostTransferHighUs = 160;
  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, true);
  queueLevel(transport, true);
  result = TestAccess::transfer(transport, directRead, 5000);
  assertCodePhase(result, TransportCode::NACK,
                  TransferPhase::DEVICE_ADDRESS_READ);
  TEST_ASSERT_FALSE(result.firstDeviceAddressAcked);
  TEST_ASSERT_EQUAL_HEX8(0xA5, directOutput);
  TEST_ASSERT_TRUE(result.stopCompleted);

  uint8_t readByte = 0;
  SingleWireTransfer randomRead{};
  randomRead.speed = SpeedMode::HIGH_SPEED;
  randomRead.deviceAddress = 0xA0;
  randomRead.hasMemoryAddress = true;
  randomRead.memoryAddress = 0x12;
  randomRead.hasRepeatedStart = true;
  randomRead.repeatedDeviceAddress = 0xA1;
  randomRead.rxData = &readByte;
  randomRead.rxLength = 1;
  randomRead.minimumPostTransferHighUs = 160;

  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, false);
  queueLevel(transport, true);
  queueLevel(transport, true);
  result = TestAccess::transfer(transport, randomRead, 5000);
  assertCodePhase(result, TransportCode::NACK,
                  TransferPhase::MEMORY_ADDRESS);
  TEST_ASSERT_TRUE(result.firstDeviceAddressAcked);
  TEST_ASSERT_FALSE(result.memoryAddressAcked);
  TEST_ASSERT_FALSE(result.repeatedDeviceAddressAcked);
  TEST_ASSERT_TRUE(result.stopCompleted);

  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, false);
  queueLevel(transport, false);
  queueLevel(transport, true);
  queueLevel(transport, true);
  queueLevel(transport, true);
  result = TestAccess::transfer(transport, randomRead, 5000);
  assertCodePhase(result, TransportCode::NACK,
                  TransferPhase::DEVICE_ADDRESS_READ);
  TEST_ASSERT_TRUE(result.firstDeviceAddressAcked);
  TEST_ASSERT_TRUE(result.memoryAddressAcked);
  TEST_ASSERT_FALSE(result.repeatedDeviceAddressAcked);
  TEST_ASSERT_TRUE(result.stopCompleted);
}

void test_esp32_random_read_restarts_and_host_ack_policy_are_exact() {
  uint8_t output[2] = {};
  SingleWireTransfer transfer{};
  transfer.speed = SpeedMode::HIGH_SPEED;
  transfer.deviceAddress = 0xA0;
  transfer.hasMemoryAddress = true;
  transfer.memoryAddress = 0x12;
  transfer.hasRepeatedStart = true;
  transfer.repeatedDeviceAddress = 0xA1;
  transfer.rxData = output;
  transfer.rxLength = 2;
  transfer.minimumPostTransferHighUs = 160;

  Esp32Transport transport;
  TestAccess::activateWithoutHardware(transport, -1);
  queueLevel(transport, true);
  queueLevel(transport, false);
  queueLevel(transport, false);
  queueLevel(transport, true);
  queueLevel(transport, false);
  for (uint8_t bit = 0; bit < 8; ++bit) {
    queueLevel(transport, false);
  }
  for (uint8_t bit = 0; bit < 8; ++bit) {
    queueLevel(transport, true);
  }
  queueLevel(transport, true);

  const TransferResult result =
      TestAccess::transfer(transport, transfer, 5000);
  assertCodePhase(result, TransportCode::OK, TransferPhase::STOP);
  TEST_ASSERT_TRUE(result.firstDeviceAddressAcked);
  TEST_ASSERT_TRUE(result.memoryAddressAcked);
  TEST_ASSERT_TRUE(result.repeatedDeviceAddressAcked);
  TEST_ASSERT_EQUAL_UINT32(2, result.dataBytesTransferred);
  TEST_ASSERT_TRUE(result.stopCompleted);
  TEST_ASSERT_EQUAL_HEX8(0x00, output[0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, output[1]);

  uint16_t lowIndices[48]{};
  TEST_ASSERT_EQUAL_UINT16(
      45, collectLowEvents(transport, lowIndices, 48));
  const uint16_t hostAckLow = lowIndices[35];
  TEST_ASSERT_EQUAL_UINT32(
      2400, TestAccess::lineEventCycle(transport, hostAckLow + 1u) -
                TestAccess::lineEventCycle(transport, hostAckLow));
  const uint16_t hostNackLow = lowIndices[44];
  TEST_ASSERT_EQUAL_UINT32(
      360, TestAccess::lineEventCycle(transport, hostNackLow + 1u) -
               TestAccess::lineEventCycle(transport, hostNackLow));
  TEST_ASSERT_EQUAL_UINT32(
      42240, TestAccess::lineEventCycle(transport, lowIndices[18]) -
                 TestAccess::lineEventCycle(transport, lowIndices[17]));
}

void test_esp32_maximum_frames_are_bounded_and_complete() {
  uint8_t writeData[8] = {0x00, 0xFF, 0xA5, 0x5A,
                          0x81, 0x7E, 0x18, 0xE7};
  SingleWireTransfer writeTransfer = addressOnly();
  writeTransfer.txData = writeData;
  writeTransfer.txLength = sizeof(writeData);

  Esp32Transport writeTransport;
  TestAccess::activateWithoutHardware(writeTransport, -1);
  queueLevel(writeTransport, true);
  queueLevel(writeTransport, false);
  for (size_t index = 0; index < sizeof(writeData); ++index) {
    queueLevel(writeTransport, false);
  }
  queueLevel(writeTransport, true);

  const TransferResult writeResult =
      TestAccess::transfer(writeTransport, writeTransfer, 9000);
  assertCodePhase(writeResult, TransportCode::OK, TransferPhase::STOP);
  TEST_ASSERT_TRUE(writeResult.firstDeviceAddressAcked);
  TEST_ASSERT_EQUAL_UINT32(sizeof(writeData),
                           writeResult.dataBytesTransferred);
  TEST_ASSERT_FALSE(writeResult.currentWriteByteMayBeAccepted);
  TEST_ASSERT_TRUE(writeResult.stopCompleted);
  TEST_ASSERT_TRUE(TestAccess::lineReleased(writeTransport));
  TEST_ASSERT_FALSE(TestAccess::testOverflow(writeTransport));
  TEST_ASSERT_EQUAL_UINT16(
      1, TestAccess::timingLockAcquireCount(writeTransport));
  TEST_ASSERT_EQUAL_UINT16(
      1, TestAccess::timingLockReleaseCount(writeTransport));
  TEST_ASSERT_EQUAL_UINT16(0,
                           TestAccess::timingLockDepth(writeTransport));

  uint8_t readData[8]{};
  const uint8_t expected[8] = {0x00, 0xFF, 0xA5, 0x5A,
                               0x81, 0x7E, 0x18, 0xE7};
  SingleWireTransfer readTransfer{};
  readTransfer.speed = SpeedMode::HIGH_SPEED;
  readTransfer.deviceAddress = 0xA1;
  readTransfer.rxData = readData;
  readTransfer.rxLength = sizeof(readData);
  readTransfer.minimumPostTransferHighUs = 160;

  Esp32Transport readTransport;
  TestAccess::activateWithoutHardware(readTransport, -1);
  queueLevel(readTransport, true);
  queueLevel(readTransport, false);
  for (uint8_t value : expected) {
    for (int bit = 7; bit >= 0; --bit) {
      queueLevel(readTransport,
                 ((static_cast<uint32_t>(value) >>
                   static_cast<uint32_t>(bit)) &
                  0x01u) != 0u);
    }
  }
  queueLevel(readTransport, true);

  const TransferResult readResult =
      TestAccess::transfer(readTransport, readTransfer, 9000);
  assertCodePhase(readResult, TransportCode::OK, TransferPhase::STOP);
  TEST_ASSERT_TRUE(readResult.firstDeviceAddressAcked);
  TEST_ASSERT_EQUAL_UINT32(sizeof(readData),
                           readResult.dataBytesTransferred);
  TEST_ASSERT_TRUE(readResult.stopCompleted);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, readData, sizeof(expected));
  TEST_ASSERT_TRUE(TestAccess::lineReleased(readTransport));
  TEST_ASSERT_FALSE(TestAccess::testOverflow(readTransport));
  TEST_ASSERT_EQUAL_UINT16(
      1, TestAccess::timingLockAcquireCount(readTransport));
  TEST_ASSERT_EQUAL_UINT16(
      1, TestAccess::timingLockReleaseCount(readTransport));
  TEST_ASSERT_EQUAL_UINT16(0,
                           TestAccess::timingLockDepth(readTransport));

  SingleWireTransfer standardTransfer = addressOnly(
      SpeedMode::STANDARD_SPEED);
  standardTransfer.txData = writeData;
  standardTransfer.txLength = sizeof(writeData);
  standardTransfer.minimumPostTransferHighUs = 650;

  Esp32Transport standardTransport;
  TestAccess::activateWithoutHardware(standardTransport, -1);
  queueLevel(standardTransport, true);
  queueLevel(standardTransport, false);
  for (size_t index = 0; index < sizeof(writeData); ++index) {
    queueLevel(standardTransport, false);
  }
  queueLevel(standardTransport, true);

  const TransferResult standardResult =
      TestAccess::transfer(standardTransport, standardTransfer, 9000);
  assertCodePhase(standardResult, TransportCode::OK,
                  TransferPhase::STOP);
  TEST_ASSERT_TRUE(standardResult.firstDeviceAddressAcked);
  TEST_ASSERT_EQUAL_UINT32(sizeof(writeData),
                           standardResult.dataBytesTransferred);
  TEST_ASSERT_TRUE(standardResult.stopCompleted);
  TEST_ASSERT_TRUE(TestAccess::lineReleased(standardTransport));
  TEST_ASSERT_FALSE(TestAccess::testOverflow(standardTransport));
  TEST_ASSERT_EQUAL_UINT16(
      1, TestAccess::timingLockAcquireCount(standardTransport));
  TEST_ASSERT_EQUAL_UINT16(
      1, TestAccess::timingLockReleaseCount(standardTransport));
  TEST_ASSERT_EQUAL_UINT16(0,
                           TestAccess::timingLockDepth(standardTransport));

  uint8_t standardReadData[8]{};
  SingleWireTransfer standardRead{};
  standardRead.speed = SpeedMode::STANDARD_SPEED;
  standardRead.deviceAddress = 0xA1;
  standardRead.rxData = standardReadData;
  standardRead.rxLength = sizeof(standardReadData);
  standardRead.minimumPostTransferHighUs = 650;

  Esp32Transport standardReadTransport;
  TestAccess::activateWithoutHardware(standardReadTransport, -1);
  queueLevel(standardReadTransport, true);
  queueLevel(standardReadTransport, false);
  for (uint8_t value : expected) {
    for (int bit = 7; bit >= 0; --bit) {
      queueLevel(standardReadTransport,
                 ((static_cast<uint32_t>(value) >>
                   static_cast<uint32_t>(bit)) &
                  0x01u) != 0u);
    }
  }
  queueLevel(standardReadTransport, true);

  const TransferResult standardReadResult =
      TestAccess::transfer(standardReadTransport, standardRead, 9000);
  assertCodePhase(standardReadResult, TransportCode::OK,
                  TransferPhase::STOP);
  TEST_ASSERT_TRUE(standardReadResult.firstDeviceAddressAcked);
  TEST_ASSERT_EQUAL_UINT32(sizeof(standardReadData),
                           standardReadResult.dataBytesTransferred);
  TEST_ASSERT_TRUE(standardReadResult.stopCompleted);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, standardReadData,
                                sizeof(expected));
  TEST_ASSERT_TRUE(TestAccess::lineReleased(standardReadTransport));
  TEST_ASSERT_FALSE(TestAccess::testOverflow(standardReadTransport));
  TEST_ASSERT_EQUAL_UINT16(
      1, TestAccess::timingLockAcquireCount(standardReadTransport));
  TEST_ASSERT_EQUAL_UINT16(
      1, TestAccess::timingLockReleaseCount(standardReadTransport));
  TEST_ASSERT_EQUAL_UINT16(
      0, TestAccess::timingLockDepth(standardReadTransport));
}

void test_esp32_instances_keep_descriptors_pins_and_line_state_independent() {
  Esp32Transport first;
  Esp32Transport second;
  Esp32TransportConfig firstConfig{};
  firstConfig.sioPin = 1;
  Esp32TransportConfig secondConfig{};
  secondConfig.sioPin = 33;
  TEST_ASSERT_TRUE(first.begin(firstConfig).ok());
  TEST_ASSERT_TRUE(second.begin(secondConfig).ok());
  const SingleWireTransport firstDescriptor = first.descriptor();
  const SingleWireTransport secondDescriptor = second.descriptor();
  TEST_ASSERT_EQUAL_PTR(&first, firstDescriptor.user);
  TEST_ASSERT_EQUAL_PTR(&second, secondDescriptor.user);
  TEST_ASSERT_NOT_EQUAL(firstDescriptor.user, secondDescriptor.user);
  TEST_ASSERT_EQUAL_INT(1, TestAccess::sioPin(first));
  TEST_ASSERT_EQUAL_INT(33, TestAccess::sioPin(second));

  const uint16_t secondEventsBefore = TestAccess::lineEventCount(second);
  queueLevel(first, false);
  const TransferResult firstResult =
      firstDescriptor.transfer(addressOnly(), 5000, firstDescriptor.user);
  assertCodePhase(firstResult, TransportCode::LINE_STUCK,
                  TransferPhase::START);
  TEST_ASSERT_EQUAL_UINT16(secondEventsBefore,
                           TestAccess::lineEventCount(second));
  TEST_ASSERT_TRUE(TestAccess::lineReleased(second));
  TEST_ASSERT_EQUAL_PTR(secondDescriptor.user, second.descriptor().user);
  TEST_ASSERT_EQUAL_PTR(secondDescriptor.transfer,
                        second.descriptor().transfer);

  first.end();
  TEST_ASSERT_TRUE(TestAccess::lineReleased(first));
  TEST_ASSERT_EQUAL_INT(-1, TestAccess::sioPin(first));
  TEST_ASSERT_TRUE(second.isInitialized());
  TEST_ASSERT_EQUAL_PTR(&second, second.descriptor().user);
  TEST_ASSERT_EQUAL_UINT16(secondEventsBefore,
                           TestAccess::lineEventCount(second));
}

void test_esp32_public_begin_end_and_presence_paths() {
  Esp32Transport invalid;
  Esp32TransportConfig invalidConfig{};
  invalidConfig.sioPin = -1;
  TEST_ASSERT_TRUE(invalid.begin(invalidConfig).is(Err::INVALID_CONFIG));
  TEST_ASSERT_EQUAL_UINT32(0, TestAccess::platformAccessCount(invalid));
  TEST_ASSERT_FALSE(invalid.isInitialized());

  invalidConfig.sioPin = 1;
  invalidConfig.presencePin = -2;
  TEST_ASSERT_TRUE(invalid.begin(invalidConfig).is(Err::INVALID_CONFIG));
  invalidConfig.presencePin = 49;
  TEST_ASSERT_TRUE(invalid.begin(invalidConfig).is(Err::INVALID_CONFIG));
  invalidConfig.presencePin = 1;
  TEST_ASSERT_TRUE(invalid.begin(invalidConfig).is(Err::INVALID_CONFIG));
  invalidConfig.sioPin = 49;
  invalidConfig.presencePin = -1;
  TEST_ASSERT_TRUE(invalid.begin(invalidConfig).is(Err::INVALID_CONFIG));
  TEST_ASSERT_EQUAL_UINT32(0, TestAccess::platformAccessCount(invalid));
  TEST_ASSERT_FALSE(invalid.isInitialized());

  Esp32Transport activeHigh;
  Esp32TransportConfig highConfig{};
  highConfig.sioPin = 1;
  highConfig.presencePin = 2;
  highConfig.presenceActiveHigh = true;
  TEST_ASSERT_TRUE(activeHigh.begin(highConfig).ok());
  const uint32_t highAccessAfterBegin =
      TestAccess::platformAccessCount(activeHigh);
  TEST_ASSERT_TRUE(activeHigh.begin(highConfig).is(Err::INVALID_STATE));
  TEST_ASSERT_EQUAL_UINT32(highAccessAfterBegin,
                           TestAccess::platformAccessCount(activeHigh));
  TEST_ASSERT_TRUE(activeHigh.isInitialized());
  const SingleWireTransport highDescriptor = activeHigh.descriptor();
  TEST_ASSERT_NOT_NULL(highDescriptor.readPresence);
  TestAccess::setPresenceLevel(activeHigh, true);
  TestAccess::setNowUs(activeHigh, 10);
  const uint16_t highLineEvents = TestAccess::lineEventCount(activeHigh);
  bool present = false;
  TransferResult result = highDescriptor.readPresence(
      present, 11, highDescriptor.user);
  assertCodePhase(result, TransportCode::OK, TransferPhase::PRESENCE);
  TEST_ASSERT_TRUE(present);
  TEST_ASSERT_EQUAL_UINT16(highLineEvents,
                           TestAccess::lineEventCount(activeHigh));

  present = true;
  result = highDescriptor.readPresence(present, 10, highDescriptor.user);
  assertCodePhase(result, TransportCode::TIMEOUT, TransferPhase::PRESENCE);
  TEST_ASSERT_FALSE(present);
  TEST_ASSERT_EQUAL_UINT16(highLineEvents,
                           TestAccess::lineEventCount(activeHigh));

  Esp32Transport activeLow;
  Esp32TransportConfig lowConfig{};
  lowConfig.sioPin = 33;
  lowConfig.presencePin = 34;
  lowConfig.presenceActiveHigh = false;
  TEST_ASSERT_TRUE(activeLow.begin(lowConfig).ok());
  const SingleWireTransport lowDescriptor = activeLow.descriptor();
  TestAccess::setPresenceLevel(activeLow, false);
  TestAccess::setNowUs(activeLow, 20);
  present = false;
  result = lowDescriptor.readPresence(present, 21, lowDescriptor.user);
  assertCodePhase(result, TransportCode::OK, TransferPhase::PRESENCE);
  TEST_ASSERT_TRUE(present);

  Esp32Transport noPresence;
  Esp32TransportConfig noPresenceConfig{};
  noPresenceConfig.sioPin = 3;
  TEST_ASSERT_TRUE(noPresence.begin(noPresenceConfig).ok());
  TEST_ASSERT_NULL(noPresence.descriptor().readPresence);

  activeHigh.end();
  activeLow.end();
  noPresence.end();
}
