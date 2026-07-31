#include <unity.h>

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include "AT21CS/AT21CS.h"
#include "AT21CS/platform/esp32/Esp32Transport.h"
#include "support/TestAccess.h"
#include "support/ScriptedTransport.h"

using namespace AT21CS;
using namespace AT21CS::test;

namespace {

TransferResult okFrame(const SingleWireTransfer& transfer) {
  TransferResult result{};
  result.code = TransportCode::OK;
  result.phase = TransferPhase::STOP;
  result.dataBytesTransferred =
      transfer.txLength != 0 ? transfer.txLength : transfer.rxLength;
  result.firstDeviceAddressAcked = true;
  result.memoryAddressAcked = transfer.hasMemoryAddress;
  result.repeatedDeviceAddressAcked = transfer.hasRepeatedStart;
  result.stopCompleted = true;
  return result;
}

TransferResult okAux(TransferPhase phase) {
  TransferResult result{};
  result.code = TransportCode::OK;
  result.phase = phase;
  return result;
}

TransferResult rawFailure(TransportCode code,
                          TransferPhase phase,
                          int32_t detail) {
  TransferResult result{};
  result.code = code;
  result.phase = phase;
  result.detail = detail;
  return result;
}

SingleWireTransfer addressOnly() {
  SingleWireTransfer transfer{};
  transfer.deviceAddress = 0xA0;
  transfer.minimumPostTransferHighUs = 160;
  return transfer;
}

SingleWireTransfer randomRead(uint8_t* data, size_t length = 1) {
  SingleWireTransfer transfer{};
  transfer.deviceAddress = 0xA0;
  transfer.hasMemoryAddress = true;
  transfer.memoryAddress = 0x12;
  transfer.hasRepeatedStart = true;
  transfer.repeatedDeviceAddress = 0xA1;
  transfer.rxData = data;
  transfer.rxLength = length;
  transfer.minimumPostTransferHighUs = 160;
  return transfer;
}

SingleWireTransfer writeFrame(const uint8_t* data, size_t length = 1) {
  SingleWireTransfer transfer{};
  transfer.deviceAddress = 0xA0;
  transfer.hasMemoryAddress = true;
  transfer.memoryAddress = 0x20;
  transfer.txData = data;
  transfer.txLength = length;
  transfer.minimumPostTransferHighUs = 160;
  return transfer;
}

void bindBus(Bus& bus, ScriptedTransport& fake, bool withPresence = true) {
  BusConfig config{};
  config.transport = fake.descriptor(withPresence);
  TEST_ASSERT_TRUE(bus.bind(config).ok());
}

void queueFrame(ScriptedTransport& fake, const TransferResult& result) {
  TransferScript script{};
  script.result = result;
  TEST_ASSERT_TRUE(fake.queueTransfer(script));
}

void queueWait(ScriptedTransport& fake,
               const TransferResult& result,
               bool advance = false) {
  WaitScript script{};
  script.result = result;
  script.advanceToDeadline = advance;
  TEST_ASSERT_TRUE(fake.queueWait(script));
}

void assertErr(Err expected, const Status& status) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected),
                          static_cast<uint8_t>(status.code));
}

void assertStaleResult(const TransferResult& result) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportCode::IO_ERROR),
                          static_cast<uint8_t>(result.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransferPhase::NONE),
                          static_cast<uint8_t>(result.phase));
  TEST_ASSERT_EQUAL_INT32(BACKEND_NOT_INITIALIZED_DETAIL, result.detail);
  TEST_ASSERT_EQUAL_UINT32(0, result.dataBytesTransferred);
  TEST_ASSERT_FALSE(result.currentWriteByteMayBeAccepted);
  TEST_ASSERT_FALSE(result.firstDeviceAddressAcked);
  TEST_ASSERT_FALSE(result.memoryAddressAcked);
  TEST_ASSERT_FALSE(result.repeatedDeviceAddressAcked);
  TEST_ASSERT_FALSE(result.stopCompleted);
}

void assertTransferEqual(const TransferResult& expected,
                         const TransferResult& actual) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.code),
                          static_cast<uint8_t>(actual.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.phase),
                          static_cast<uint8_t>(actual.phase));
  TEST_ASSERT_EQUAL_INT32(expected.detail, actual.detail);
  TEST_ASSERT_EQUAL_UINT32(expected.dataBytesTransferred,
                           actual.dataBytesTransferred);
  TEST_ASSERT_EQUAL(expected.currentWriteByteMayBeAccepted,
                    actual.currentWriteByteMayBeAccepted);
  TEST_ASSERT_EQUAL(expected.firstDeviceAddressAcked,
                    actual.firstDeviceAddressAcked);
  TEST_ASSERT_EQUAL(expected.memoryAddressAcked, actual.memoryAddressAcked);
  TEST_ASSERT_EQUAL(expected.repeatedDeviceAddressAcked,
                    actual.repeatedDeviceAddressAcked);
  TEST_ASSERT_EQUAL(expected.stopCompleted, actual.stopCompleted);
}

void assertWriteCycleEqual(const WriteCycleResult& expected,
                           const WriteCycleResult& actual) {
  assertTransferEqual(expected.frame, actual.frame);
  assertTransferEqual(expected.hold, actual.hold);
  TEST_ASSERT_EQUAL(expected.holdRequired, actual.holdRequired);
  TEST_ASSERT_EQUAL(expected.holdCompleted, actual.holdCompleted);
}

void assertBusSnapshotEqual(const BusSnapshot& expected,
                            const BusSnapshot& actual) {
  TEST_ASSERT_EQUAL(expected.bound, actual.bound);
  TEST_ASSERT_EQUAL(expected.bindingEpochValid, actual.bindingEpochValid);
  TEST_ASSERT_EQUAL_UINT64(expected.bindingEpoch, actual.bindingEpoch);
  TEST_ASSERT_EQUAL_UINT64(expected.generation, actual.generation);
  TEST_ASSERT_EQUAL_UINT8(expected.claimedAddressMask,
                          actual.claimedAddressMask);
  TEST_ASSERT_EQUAL(expected.resetEstablishedHighSpeed,
                    actual.resetEstablishedHighSpeed);
  TEST_ASSERT_EQUAL_UINT64(expected.writeHighUntilUs,
                           actual.writeHighUntilUs);
  assertTransferEqual(expected.previousTransfer, actual.previousTransfer);
  assertTransferEqual(expected.lastTransfer, actual.lastTransfer);
  assertWriteCycleEqual(expected.lastWriteCycle, actual.lastWriteCycle);
}

template <typename T, typename = void>
struct HasTick : std::false_type {};
template <typename T>
struct HasTick<T, std::void_t<decltype(std::declval<T&>().tick(uint32_t{}))>>
    : std::true_type {};

template <typename T, typename = void>
struct HasGetConfig : std::false_type {};
template <typename T>
struct HasGetConfig<T, std::void_t<decltype(std::declval<T&>().getConfig())>>
    : std::true_type {};

}  // namespace

void test_public_defaults_are_deterministic() {
  const Status status{};
  TEST_ASSERT_TRUE(status.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OK),
                          static_cast<uint8_t>(status.code));
  TEST_ASSERT_EQUAL_INT32(0, status.detail);
  TEST_ASSERT_EQUAL_STRING("OK", status.msg);
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(static_cast<PartType>(0xFF)));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(static_cast<SpeedMode>(0xFF)));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(static_cast<DriverState>(0xFF)));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(static_cast<WriteEffect>(0xFF)));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(static_cast<MutationEffect>(0xFF)));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(static_cast<Err>(0xFF)));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(static_cast<ProtocolPhase>(0xFF)));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(static_cast<TransportCode>(0xFF)));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", toString(static_cast<TransferPhase>(0xFF)));

  const Config config{};
  TEST_ASSERT_EQUAL_UINT8(0, config.addressBits);
  TEST_ASSERT_EQUAL_UINT8(5, config.offlineThreshold);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PartType::UNKNOWN),
                          static_cast<uint8_t>(config.expectedPart));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SpeedMode::HIGH_SPEED),
                          static_cast<uint8_t>(config.startupSpeed));
  const TransferResult transferResult{};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportCode::IO_ERROR),
                          static_cast<uint8_t>(transferResult.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransferPhase::NONE),
                          static_cast<uint8_t>(transferResult.phase));
  TEST_ASSERT_EQUAL_INT32(0, transferResult.detail);
  TEST_ASSERT_EQUAL_UINT32(0, transferResult.dataBytesTransferred);
  TEST_ASSERT_FALSE(transferResult.currentWriteByteMayBeAccepted);
  TEST_ASSERT_FALSE(transferResult.firstDeviceAddressAcked);
  TEST_ASSERT_FALSE(transferResult.memoryAddressAcked);
  TEST_ASSERT_FALSE(transferResult.repeatedDeviceAddressAcked);
  TEST_ASSERT_FALSE(transferResult.stopCompleted);
  const WriteCycleResult cycle{};
  assertTransferEqual(TransferResult{}, cycle.frame);
  assertTransferEqual(TransferResult{}, cycle.hold);
  TEST_ASSERT_FALSE(cycle.holdRequired);
  TEST_ASSERT_FALSE(cycle.holdCompleted);
  const SingleWireTransfer transfer{};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SpeedMode::HIGH_SPEED),
                          static_cast<uint8_t>(transfer.speed));
  TEST_ASSERT_EQUAL_UINT8(0, transfer.deviceAddress);
  TEST_ASSERT_FALSE(transfer.hasMemoryAddress);
  TEST_ASSERT_EQUAL_UINT8(0, transfer.memoryAddress);
  TEST_ASSERT_NULL(transfer.txData);
  TEST_ASSERT_EQUAL_UINT32(0, transfer.txLength);
  TEST_ASSERT_FALSE(transfer.hasRepeatedStart);
  TEST_ASSERT_EQUAL_UINT8(0, transfer.repeatedDeviceAddress);
  TEST_ASSERT_NULL(transfer.rxData);
  TEST_ASSERT_EQUAL_UINT32(0, transfer.rxLength);
  TEST_ASSERT_EQUAL_UINT32(0, transfer.minimumPostTransferHighUs);
  const SingleWireTransport descriptor{};
  TEST_ASSERT_NULL(descriptor.user);
  TEST_ASSERT_NULL(descriptor.nowUs);
  TEST_ASSERT_NULL(descriptor.transfer);
  TEST_ASSERT_NULL(descriptor.resetAndDiscover);
  TEST_ASSERT_NULL(descriptor.waitUntilUs);
  TEST_ASSERT_NULL(descriptor.readPresence);
  const BusConfig busConfig{};
  TEST_ASSERT_NULL(busConfig.transport.user);
  TEST_ASSERT_NULL(busConfig.transport.nowUs);
  TEST_ASSERT_NULL(busConfig.transport.transfer);
  TEST_ASSERT_NULL(busConfig.transport.resetAndDiscover);
  TEST_ASSERT_NULL(busConfig.transport.waitUntilUs);
  TEST_ASSERT_NULL(busConfig.transport.readPresence);
  const Esp32TransportConfig backendConfig{};
  TEST_ASSERT_EQUAL_INT(-1, backendConfig.sioPin);
  TEST_ASSERT_EQUAL_INT(-1, backendConfig.presencePin);
  TEST_ASSERT_TRUE(backendConfig.presenceActiveHigh);
  const BusSnapshot bus{};
  TEST_ASSERT_FALSE(bus.bound);
  TEST_ASSERT_TRUE(bus.bindingEpochValid);
  TEST_ASSERT_EQUAL_UINT64(0, bus.bindingEpoch);
  TEST_ASSERT_EQUAL_UINT64(0, bus.generation);
  TEST_ASSERT_EQUAL_UINT8(0, bus.claimedAddressMask);
  TEST_ASSERT_FALSE(bus.resetEstablishedHighSpeed);
  TEST_ASSERT_EQUAL_UINT64(0, bus.writeHighUntilUs);
  assertTransferEqual(TransferResult{}, bus.previousTransfer);
  assertTransferEqual(TransferResult{}, bus.lastTransfer);
  assertTransferEqual(TransferResult{}, bus.lastWriteCycle.frame);
  assertTransferEqual(TransferResult{}, bus.lastWriteCycle.hold);
  TEST_ASSERT_FALSE(bus.lastWriteCycle.holdRequired);
  TEST_ASSERT_FALSE(bus.lastWriteCycle.holdCompleted);
  const SettingsSnapshot settings{};
  TEST_ASSERT_FALSE(settings.bound);
  TEST_ASSERT_FALSE(settings.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(settings.state));
  TEST_ASSERT_EQUAL_UINT8(0, settings.addressBits);
  TEST_ASSERT_EQUAL_UINT8(0, settings.offlineThreshold);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PartType::UNKNOWN),
                          static_cast<uint8_t>(settings.expectedPart));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PartType::UNKNOWN),
                          static_cast<uint8_t>(settings.detectedPart));
  TEST_ASSERT_EQUAL_UINT32(0, settings.manufacturerId);
  TEST_ASSERT_EQUAL_UINT8(0, settings.siliconRevision);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SpeedMode::HIGH_SPEED),
                          static_cast<uint8_t>(settings.configuredSpeed));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SpeedMode::HIGH_SPEED),
                          static_cast<uint8_t>(settings.activeSpeed));
  TEST_ASSERT_FALSE(settings.speedKnown);
  TEST_ASSERT_FALSE(settings.seenBusBindingEpochValid);
  TEST_ASSERT_EQUAL_UINT64(0, settings.seenBusBindingEpoch);
  TEST_ASSERT_EQUAL_UINT64(0, settings.seenBusGeneration);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OK),
                          static_cast<uint8_t>(settings.lastStatusCode));
  TEST_ASSERT_EQUAL_INT32(0, settings.lastStatusDetail);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OK),
                          static_cast<uint8_t>(settings.lastErrorCode));
  TEST_ASSERT_EQUAL_INT32(0, settings.lastErrorDetail);
  TEST_ASSERT_EQUAL_UINT64(0, settings.lastOkUs);
  TEST_ASSERT_EQUAL_UINT64(0, settings.lastErrorUs);
  TEST_ASSERT_EQUAL_UINT8(0, settings.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(0, settings.totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(0, settings.totalFailures);
  const SerialNumberInfo serial{};
  const WriteResult write{};
  const MutationResult mutation{};
  for (const uint8_t value : serial.bytes) {
    TEST_ASSERT_EQUAL_UINT8(0, value);
  }
  TEST_ASSERT_FALSE(serial.productIdOk);
  TEST_ASSERT_FALSE(serial.crcOk);
  TEST_ASSERT_EQUAL_UINT32(0, write.bytesCommitted);
  TEST_ASSERT_EQUAL_UINT32(0, write.lastPageBytesAccepted);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteEffect::NOT_ATTEMPTED),
                          static_cast<uint8_t>(write.lastPageEffect));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MutationEffect::NOT_ATTEMPTED),
                          static_cast<uint8_t>(mutation.effect));
  TEST_ASSERT_FALSE(mutation.alreadyApplied);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ProtocolPhase::DATA_WRITE),
                          static_cast<uint8_t>(protocolDetailPhase(
                              makeProtocolDetail(ProtocolPhase::DATA_WRITE, 7))));
  TEST_ASSERT_EQUAL_UINT16(
      7, protocolDetailIndex(makeProtocolDetail(ProtocolPhase::DATA_WRITE, 7)));
}

void test_hardware_objects_are_noncopyable_nonmovable() {
  static_assert(!std::is_copy_constructible<Bus>::value);
  static_assert(!std::is_move_constructible<Bus>::value);
  static_assert(!std::is_copy_assignable<Bus>::value);
  static_assert(!std::is_move_assignable<Bus>::value);
  static_assert(!std::is_copy_constructible<Driver>::value);
  static_assert(!std::is_move_constructible<Driver>::value);
  static_assert(!std::is_copy_assignable<Driver>::value);
  static_assert(!std::is_move_assignable<Driver>::value);
  static_assert(!std::is_copy_constructible<Esp32Transport>::value);
  static_assert(!std::is_move_constructible<Esp32Transport>::value);
  static_assert(!std::is_copy_assignable<Esp32Transport>::value);
  static_assert(!std::is_move_assignable<Esp32Transport>::value);
  TEST_PASS();
}

void test_invalid_bus_rebind_is_transactional_and_silent() {
  ScriptedTransport fake;
  ScriptedTransport replacementFake;
  Bus bus;
  bindBus(bus, fake);
  const BusSnapshot before = bus.snapshot();
  for (uint8_t missing = 0; missing < 4; ++missing) {
    BusConfig invalid{};
    invalid.transport = replacementFake.descriptor();
    if (missing == 0) invalid.transport.nowUs = nullptr;
    if (missing == 1) invalid.transport.transfer = nullptr;
    if (missing == 2) invalid.transport.resetAndDiscover = nullptr;
    if (missing == 3) invalid.transport.waitUntilUs = nullptr;
    assertErr(Err::INVALID_CONFIG, bus.bind(invalid));
    assertBusSnapshotEqual(before, bus.snapshot());
  }
  TEST_ASSERT_EQUAL_UINT32(0, fake.eventCount);
  TEST_ASSERT_EQUAL_UINT32(0, replacementFake.eventCount);
  queueFrame(fake, okFrame(addressOnly()));
  TransferResult result{};
  TEST_ASSERT_TRUE(TestAccess::execute(bus, addressOnly(), result).ok());
  TEST_ASSERT_EQUAL_UINT32(1, fake.transferCalls);
}

void test_binding_epoch_lifecycle_and_stale_driver_cache() {
  ScriptedTransport first;
  ScriptedTransport second;
  Bus bus;
  bindBus(bus, first);
  Driver driver;
  Config config{};
  TEST_ASSERT_TRUE(driver.bind(bus, config).ok());
  const uint64_t initialEpoch = bus.snapshot().bindingEpoch;

  BusConfig replacement{};
  replacement.transport = second.descriptor();
  TEST_ASSERT_TRUE(bus.bind(replacement).ok());
  TEST_ASSERT_EQUAL_UINT64(initialEpoch + 1, bus.snapshot().bindingEpoch);
  TEST_ASSERT_FALSE(TestAccess::hasCurrentBusBinding(driver));
  TEST_ASSERT_EQUAL_UINT32(0, first.transferCalls + second.transferCalls);
  TEST_ASSERT_TRUE(driver.bind(bus, config).ok());
  TEST_ASSERT_TRUE(TestAccess::hasCurrentBusBinding(driver));
  TEST_ASSERT_EQUAL_UINT32(0, first.transferCalls + second.transferCalls);

  driver.end();
  TEST_ASSERT_TRUE(bus.end().ok());
  const uint64_t endedEpoch = bus.snapshot().bindingEpoch;
  TEST_ASSERT_TRUE(bus.end().ok());
  TEST_ASSERT_EQUAL_UINT64(endedEpoch, bus.snapshot().bindingEpoch);

  ScriptedTransport boundaryFake;
  Bus boundaryBus;
  bindBus(boundaryBus, boundaryFake);
  TestAccess::seedBindingEpoch(
      boundaryBus, std::numeric_limits<uint64_t>::max());
  BusConfig same{};
  same.transport = boundaryFake.descriptor();
  assertErr(Err::INVALID_STATE, boundaryBus.bind(same));
  TEST_ASSERT_TRUE(boundaryBus.isBound());
  TEST_ASSERT_TRUE(boundaryBus.end().ok());
  TEST_ASSERT_FALSE(boundaryBus.snapshot().bindingEpochValid);
  assertErr(Err::INVALID_STATE, boundaryBus.bind(same));
  TEST_ASSERT_EQUAL_UINT32(0, boundaryFake.eventCount);
}

void test_rebind_and_end_preserve_retained_hold() {
  ScriptedTransport fake;
  ScriptedTransport replacementFake;
  Bus bus;
  bindBus(bus, fake);
  const uint8_t data = 0x5A;
  const SingleWireTransfer transfer = writeFrame(&data);
  queueFrame(fake, okFrame(transfer));
  queueWait(fake,
            rawFailure(TransportCode::TIMEOUT, TransferPhase::WAIT_HIGH, 8));
  WriteCycleResult write{};
  assertErr(Err::TRANSPORT_TIMEOUT,
            TestAccess::executeWrite(bus, transfer, write));
  const uint64_t retained = bus.snapshot().writeHighUntilUs;
  TEST_ASSERT_NOT_EQUAL(0, retained);
  TestAccess::seedBindingEpoch(bus, std::numeric_limits<uint64_t>::max());

  BusConfig replacement{};
  replacement.transport = replacementFake.descriptor();
  assertErr(Err::BUSY, bus.bind(replacement));
  TEST_ASSERT_EQUAL_UINT32(0, replacementFake.eventCount);

  queueWait(fake, okAux(TransferPhase::WAIT_HIGH), false);
  assertErr(Err::CLOCK_STALLED, bus.end());
  TEST_ASSERT_TRUE(bus.isBound());
  TEST_ASSERT_EQUAL_UINT64(retained, bus.snapshot().writeHighUntilUs);
  queueWait(fake, okAux(TransferPhase::WAIT_HIGH), true);
  TEST_ASSERT_TRUE(bus.end().ok());
  const size_t waitCalls = fake.waitCalls;
  TEST_ASSERT_TRUE(bus.end().ok());
  TEST_ASSERT_EQUAL_UINT32(waitCalls, fake.waitCalls);
}

void test_one_callback_owns_complete_frame() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  uint8_t output[2] = {};
  const SingleWireTransfer transfer = randomRead(output, 2);
  TransferScript script{};
  script.result = okFrame(transfer);
  script.rxData[0] = 0x12;
  script.rxData[1] = 0x34;
  script.rxLength = 2;
  TEST_ASSERT_TRUE(fake.queueTransfer(script));
  TransferResult result{};
  TEST_ASSERT_TRUE(TestAccess::execute(bus, transfer, result).ok());
  TEST_ASSERT_EQUAL_UINT32(1, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT64(10000, fake.captured[0].deadlineUs);
  TEST_ASSERT_EQUAL_UINT32(1, fake.eventCountFor(FakeEventKind::TRANSFER_BEGIN));
  TEST_ASSERT_EQUAL_UINT32(1, fake.eventCountFor(FakeEventKind::TRANSFER_END));
  TEST_ASSERT_EQUAL_UINT8(0x12, output[0]);
  TEST_ASSERT_FALSE(fake.overflow);
}

void test_every_nack_phase_maps_exactly() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  uint8_t output = 0;
  const SingleWireTransfer read = randomRead(&output);
  TransferResult result{};

  TransferResult raw{};
  raw.code = TransportCode::NACK;
  raw.phase = TransferPhase::DEVICE_ADDRESS_WRITE;
  raw.stopCompleted = true;
  queueFrame(fake, raw);
  Status status = TestAccess::execute(bus, read, result);
  assertErr(Err::NACK_DEVICE_ADDRESS, status);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ProtocolPhase::DEVICE_ADDRESS_WRITE),
      static_cast<uint8_t>(protocolDetailPhase(status.detail)));
  TEST_ASSERT_EQUAL_UINT32(0,
                           fake.eventCountFor(FakeEventKind::MEMORY_ADDRESS));
  TEST_ASSERT_EQUAL_UINT32(0, fake.eventCountFor(FakeEventKind::RESTART));
  TEST_ASSERT_EQUAL_UINT32(0, fake.eventCountFor(FakeEventKind::TX_DATA));
  TEST_ASSERT_EQUAL_UINT32(0, fake.eventCountFor(FakeEventKind::RX_DATA));

  raw.phase = TransferPhase::MEMORY_ADDRESS;
  raw.firstDeviceAddressAcked = true;
  queueFrame(fake, raw);
  status = TestAccess::execute(bus, read, result);
  assertErr(Err::NACK_MEMORY_ADDRESS, status);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ProtocolPhase::MEMORY_ADDRESS),
      static_cast<uint8_t>(protocolDetailPhase(status.detail)));

  raw.phase = TransferPhase::DEVICE_ADDRESS_READ;
  raw.memoryAddressAcked = true;
  queueFrame(fake, raw);
  status = TestAccess::execute(bus, read, result);
  assertErr(Err::NACK_DEVICE_ADDRESS, status);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ProtocolPhase::DEVICE_ADDRESS_READ),
      static_cast<uint8_t>(protocolDetailPhase(status.detail)));

  SingleWireTransfer directRead{};
  directRead.deviceAddress = 0xC1;
  directRead.rxData = &output;
  directRead.rxLength = 1;
  directRead.minimumPostTransferHighUs = 160;
  TransferResult directNack{};
  directNack.code = TransportCode::NACK;
  directNack.phase = TransferPhase::DEVICE_ADDRESS_READ;
  directNack.stopCompleted = true;
  queueFrame(fake, directNack);
  status = TestAccess::execute(bus, directRead, result);
  assertErr(Err::NACK_DEVICE_ADDRESS, status);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ProtocolPhase::DEVICE_ADDRESS_READ),
      static_cast<uint8_t>(protocolDetailPhase(status.detail)));

  directNack.firstDeviceAddressAcked = true;
  queueFrame(fake, directNack);
  assertErr(Err::IO_ERROR, TestAccess::execute(bus, directRead, result));

  const uint8_t data[2] = {1, 2};
  const SingleWireTransfer write = writeFrame(data, 2);
  raw.phase = TransferPhase::DATA_WRITE;
  raw.memoryAddressAcked = true;
  raw.dataBytesTransferred = 1;
  queueFrame(fake, raw);
  WriteCycleResult writeResult{};
  queueWait(fake, okAux(TransferPhase::WAIT_HIGH), true);
  status = TestAccess::executeWrite(bus, write, writeResult);
  assertErr(Err::NACK_DATA, status);
  TEST_ASSERT_EQUAL_UINT16(1, protocolDetailIndex(status.detail));
}

void test_malformed_success_and_evidence_are_rejected() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  uint8_t output = 0;
  const SingleWireTransfer transfer = randomRead(&output);
  const size_t callbacksBeforeValidation = fake.transferCalls;
  TransferResult invalidResult{};
  WriteCycleResult invalidWriteResult{};
  uint8_t data[9] = {};

  SingleWireTransfer invalid = addressOnly();
  invalid.txData = data;
  assertErr(Err::INVALID_PARAM,
            TestAccess::execute(bus, invalid, invalidResult));
  invalid = writeFrame(data);
  invalid.rxData = &output;
  invalid.rxLength = 1;
  assertErr(Err::INVALID_PARAM,
            TestAccess::executeWrite(bus, invalid, invalidWriteResult));
  invalid = writeFrame(data, 9);
  assertErr(Err::INVALID_PARAM,
            TestAccess::executeWrite(bus, invalid, invalidWriteResult));
  invalid = writeFrame(data, std::numeric_limits<size_t>::max());
  assertErr(Err::INVALID_PARAM,
            TestAccess::executeWrite(bus, invalid, invalidWriteResult));
  invalid = randomRead(&output, std::numeric_limits<size_t>::max());
  assertErr(Err::INVALID_PARAM,
            TestAccess::execute(bus, invalid, invalidResult));
  invalid = randomRead(&output);
  invalid.hasMemoryAddress = false;
  invalid.memoryAddress = 0;
  assertErr(Err::INVALID_PARAM,
            TestAccess::execute(bus, invalid, invalidResult));
  invalid = randomRead(&output);
  invalid.deviceAddress = 0xA1;
  assertErr(Err::INVALID_PARAM,
            TestAccess::execute(bus, invalid, invalidResult));
  invalid = addressOnly();
  invalid.deviceAddress = 0x21;
  invalid.hasMemoryAddress = true;
  invalid.memoryAddress = 0x60;
  assertErr(Err::INVALID_PARAM,
            TestAccess::execute(bus, invalid, invalidResult));
  invalid = randomRead(&output);
  invalid.repeatedDeviceAddress = 0xB1;
  assertErr(Err::INVALID_PARAM,
            TestAccess::execute(bus, invalid, invalidResult));
  invalid = addressOnly();
  invalid.memoryAddress = 1;
  assertErr(Err::INVALID_PARAM,
            TestAccess::execute(bus, invalid, invalidResult));
  invalid = addressOnly();
  invalid.speed = static_cast<SpeedMode>(0xFF);
  assertErr(Err::INVALID_PARAM,
            TestAccess::execute(bus, invalid, invalidResult));
  invalid = addressOnly();
  invalid.minimumPostTransferHighUs = 159;
  assertErr(Err::INVALID_PARAM,
            TestAccess::execute(bus, invalid, invalidResult));
  invalid = addressOnly();
  invalid.deviceAddress = 0xA1;
  assertErr(Err::INVALID_PARAM,
            TestAccess::execute(bus, invalid, invalidResult));
  TEST_ASSERT_EQUAL_UINT32(callbacksBeforeValidation, fake.transferCalls);

  TransferResult raw = okFrame(transfer);
  raw.dataBytesTransferred = 0;
  queueFrame(fake, raw);
  TransferResult result{};
  assertErr(Err::IO_ERROR, TestAccess::execute(bus, transfer, result));

  raw = okFrame(transfer);
  raw.repeatedDeviceAddressAcked = false;
  queueFrame(fake, raw);
  assertErr(Err::IO_ERROR, TestAccess::execute(bus, transfer, result));

  raw = okFrame(transfer);
  raw.firstDeviceAddressAcked = false;
  queueFrame(fake, raw);
  assertErr(Err::IO_ERROR, TestAccess::execute(bus, transfer, result));

  raw = okFrame(transfer);
  raw.memoryAddressAcked = false;
  queueFrame(fake, raw);
  assertErr(Err::IO_ERROR, TestAccess::execute(bus, transfer, result));

  raw = okFrame(transfer);
  raw.stopCompleted = false;
  queueFrame(fake, raw);
  assertErr(Err::IO_ERROR, TestAccess::execute(bus, transfer, result));

  raw = okFrame(transfer);
  raw.phase = TransferPhase::DATA_READ;
  queueFrame(fake, raw);
  assertErr(Err::IO_ERROR, TestAccess::execute(bus, transfer, result));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransferPhase::DATA_READ),
                          static_cast<uint8_t>(bus.snapshot().lastTransfer.phase));

  raw = rawFailure(TransportCode::TIMEOUT, TransferPhase::START, 9);
  raw.firstDeviceAddressAcked = true;
  queueFrame(fake, raw);
  assertErr(Err::IO_ERROR, TestAccess::execute(bus, transfer, result));

  const size_t readEventsBefore =
      fake.eventCountFor(FakeEventKind::RX_DATA);
  raw = rawFailure(TransportCode::TIMEOUT, TransferPhase::DATA_READ, 10);
  raw.dataBytesTransferred = 1;
  raw.firstDeviceAddressAcked = true;
  raw.memoryAddressAcked = true;
  raw.repeatedDeviceAddressAcked = true;
  queueFrame(fake, raw);
  assertErr(Err::TRANSPORT_TIMEOUT,
            TestAccess::execute(bus, transfer, result));
  TEST_ASSERT_EQUAL_UINT32(
      readEventsBefore + 1, fake.eventCountFor(FakeEventKind::RX_DATA));

  ScriptedTransport overflowFake;
  Bus overflowBus;
  bindBus(overflowBus, overflowFake);
  queueFrame(overflowFake, okFrame(addressOnly()));
  overflowFake.eventCount = ScriptedTransport::EVENT_CAPACITY;
  assertErr(Err::IO_ERROR,
            TestAccess::execute(overflowBus, addressOnly(), result));
  TEST_ASSERT_TRUE(overflowFake.overflow);

  Esp32Transport backend;
  TestAccess::activateWithoutHardware(backend);
  TestAccess::resetPlatformAccessCount(backend);
  const SingleWireTransport backendDescriptor = backend.descriptor();
  invalid = addressOnly();
  invalid.deviceAddress = 0x21;
  invalid.hasMemoryAddress = true;
  invalid.memoryAddress = 0x60;
  TransferResult backendResult = backendDescriptor.transfer(
      invalid, std::numeric_limits<uint64_t>::max(), backendDescriptor.user);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportCode::IO_ERROR),
                          static_cast<uint8_t>(backendResult.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransferPhase::NONE),
                          static_cast<uint8_t>(backendResult.phase));
  invalid = randomRead(&output);
  invalid.repeatedDeviceAddress = 0xB1;
  backendResult = backendDescriptor.transfer(
      invalid, std::numeric_limits<uint64_t>::max(), backendDescriptor.user);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportCode::IO_ERROR),
                          static_cast<uint8_t>(backendResult.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransferPhase::NONE),
                          static_cast<uint8_t>(backendResult.phase));
  TEST_ASSERT_EQUAL_UINT32(0, TestAccess::platformAccessCount(backend));
}

void test_unknown_data_ack_arms_hold_without_replay() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  const uint8_t data = 0xA5;
  const SingleWireTransfer transfer = writeFrame(&data);
  TransferResult raw{};
  raw.code = TransportCode::TIMEOUT;
  raw.phase = TransferPhase::DATA_WRITE;
  raw.detail = 77;
  raw.currentWriteByteMayBeAccepted = true;
  raw.firstDeviceAddressAcked = true;
  raw.memoryAddressAcked = true;
  queueFrame(fake, raw);
  queueWait(fake, okAux(TransferPhase::WAIT_HIGH), true);
  WriteCycleResult result{};
  assertErr(Err::TRANSPORT_TIMEOUT,
            TestAccess::executeWrite(bus, transfer, result));
  TEST_ASSERT_TRUE(result.holdRequired);
  TEST_ASSERT_TRUE(result.holdCompleted);
  TEST_ASSERT_EQUAL_UINT64(11000, fake.lastWaitDeadlineUs);
  TEST_ASSERT_EQUAL_UINT32(1, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT32(0, result.frame.dataBytesTransferred);
  TEST_ASSERT_EQUAL_UINT32(1,
                           fake.eventCountFor(FakeEventKind::TX_DATA));

  ScriptedTransport failedHoldFake;
  Bus failedHoldBus;
  bindBus(failedHoldBus, failedHoldFake);
  queueFrame(failedHoldFake, raw);
  queueWait(failedHoldFake,
            rawFailure(TransportCode::LINE_STUCK,
                       TransferPhase::WAIT_HIGH, 88));
  const Status failedHoldStatus =
      TestAccess::executeWrite(failedHoldBus, transfer, result);
  assertErr(Err::TRANSPORT_TIMEOUT, failedHoldStatus);
  TEST_ASSERT_EQUAL_INT32(77, failedHoldStatus.detail);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(TransportCode::LINE_STUCK),
      static_cast<uint8_t>(result.hold.code));
  TEST_ASSERT_EQUAL_INT32(88, result.hold.detail);
  TEST_ASSERT_NOT_EQUAL(0, failedHoldBus.snapshot().writeHighUntilUs);
  queueWait(failedHoldFake, okAux(TransferPhase::WAIT_HIGH), true);
  TEST_ASSERT_TRUE(failedHoldBus.end().ok());

  TransferResult contradictoryNack = raw;
  contradictoryNack.code = TransportCode::NACK;
  queueFrame(fake, contradictoryNack);
  queueWait(fake, okAux(TransferPhase::WAIT_HIGH), true);
  assertErr(Err::IO_ERROR, TestAccess::executeWrite(bus, transfer, result));
  TEST_ASSERT_TRUE(result.holdRequired);
  TEST_ASSERT_TRUE(result.holdCompleted);

  raw.repeatedDeviceAddressAcked = true;
  queueFrame(fake, raw);
  queueWait(fake, okAux(TransferPhase::WAIT_HIGH), true);
  assertErr(Err::IO_ERROR, TestAccess::executeWrite(bus, transfer, result));
  TEST_ASSERT_TRUE(result.holdRequired);
  TEST_ASSERT_EQUAL_UINT32(3, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT32(3,
                           fake.eventCountFor(FakeEventKind::TX_DATA));

  const uint8_t twoBytes[2] = {0xA5, 0x5A};
  const SingleWireTransfer twoByteTransfer = writeFrame(twoBytes, 2);
  raw.repeatedDeviceAddressAcked = false;
  raw.dataBytesTransferred = 1;
  queueFrame(fake, raw);
  queueWait(fake, okAux(TransferPhase::WAIT_HIGH), true);
  assertErr(Err::TRANSPORT_TIMEOUT,
            TestAccess::executeWrite(bus, twoByteTransfer, result));
  TEST_ASSERT_EQUAL_UINT32(1, result.frame.dataBytesTransferred);
  TEST_ASSERT_TRUE(result.frame.currentWriteByteMayBeAccepted);
  TEST_ASSERT_EQUAL_UINT32(4, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT32(5,
                           fake.eventCountFor(FakeEventKind::TX_DATA));
}

void test_transport_errors_remain_distinct() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  const SingleWireTransfer transfer = addressOnly();
  const TransportCode codes[3] = {TransportCode::TIMEOUT,
                                  TransportCode::LINE_STUCK,
                                  TransportCode::IO_ERROR};
  const Err errors[3] = {Err::TRANSPORT_TIMEOUT, Err::LINE_STUCK, Err::IO_ERROR};
  for (size_t index = 0; index < 3; ++index) {
    TransferResult raw{};
    raw.code = codes[index];
    raw.phase = TransferPhase::START;
    raw.detail = static_cast<int32_t>(40 + index);
    queueFrame(fake, raw);
    TransferResult result{};
    const Status status = TestAccess::execute(bus, transfer, result);
    assertErr(errors[index], status);
    TEST_ASSERT_EQUAL_INT32(raw.detail, status.detail);
  }
}

void test_physical_diagnostics_shift_without_allocation() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  const SingleWireTransfer transfer = addressOnly();
  TransferResult first{};
  first.code = TransportCode::TIMEOUT;
  first.phase = TransferPhase::START;
  first.detail = 11;
  queueFrame(fake, first);
  TransferResult result{};
  (void)TestAccess::execute(bus, transfer, result);

  BooleanScript presence{};
  presence.result = okAux(TransferPhase::PRESENCE);
  TEST_ASSERT_TRUE(fake.queuePresence(presence));
  bool present = true;
  TEST_ASSERT_TRUE(bus.readPresenceIndicator(present).ok());
  BusSnapshot snapshot = bus.snapshot();
  TEST_ASSERT_EQUAL_INT32(11, snapshot.previousTransfer.detail);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransferPhase::PRESENCE),
                          static_cast<uint8_t>(snapshot.lastTransfer.phase));

  BooleanScript reset{};
  reset.result = rawFailure(TransportCode::LINE_STUCK,
                            TransferPhase::DISCOVERY_RELEASE, 22);
  TEST_ASSERT_TRUE(fake.queueReset(reset));
  TransferResult resetResult{};
  (void)TestAccess::resetAndDiscover(bus, present, resetResult);
  snapshot = bus.snapshot();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransferPhase::PRESENCE),
                          static_cast<uint8_t>(snapshot.previousTransfer.phase));
  TEST_ASSERT_EQUAL_INT32(22, snapshot.lastTransfer.detail);
}

void test_checked_deadlines_and_post_acceptance_overflow_fail_closed() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  const uint8_t data = 1;
  const SingleWireTransfer transfer = writeFrame(&data);
  TEST_ASSERT_TRUE(fake.queueNow(std::numeric_limits<uint64_t>::max() - 18000));
  WriteCycleResult write{};
  assertErr(Err::CLOCK_STALLED,
            TestAccess::executeWrite(bus, transfer, write));
  TEST_ASSERT_EQUAL_UINT32(0, fake.transferCalls);

  ScriptedTransport postAcceptanceFake;
  Bus postAcceptanceBus;
  bindBus(postAcceptanceBus, postAcceptanceFake);
  TEST_ASSERT_TRUE(postAcceptanceFake.queueNow(100));
  TEST_ASSERT_TRUE(postAcceptanceFake.queueNow(
      std::numeric_limits<uint64_t>::max() - 5000));
  queueFrame(postAcceptanceFake, okFrame(transfer));
  assertErr(Err::CLOCK_STALLED,
            TestAccess::executeWrite(postAcceptanceBus, transfer, write));
  TEST_ASSERT_EQUAL_UINT64(std::numeric_limits<uint64_t>::max(),
                           postAcceptanceBus.snapshot().writeHighUntilUs);
  const size_t transfers = postAcceptanceFake.transferCalls;
  TransferResult normal{};
  assertErr(Err::CLOCK_STALLED,
            TestAccess::execute(postAcceptanceBus, addressOnly(), normal));
  TEST_ASSERT_EQUAL_UINT32(transfers, postAcceptanceFake.transferCalls);

  ScriptedTransport clockFake;
  TEST_ASSERT_TRUE(clockFake.queueNow(10));
  TEST_ASSERT_TRUE(clockFake.queueNow(10));
  TEST_ASSERT_FALSE(clockFake.queueNow(9));

  ScriptedTransport advancedClockFake;
  TEST_ASSERT_TRUE(advancedClockFake.queueNow(100));
  TEST_ASSERT_TRUE(advancedClockFake.queueNow(200));
  const SingleWireTransport advancedClock = advancedClockFake.descriptor();
  TEST_ASSERT_EQUAL_UINT64(100, advancedClock.nowUs(advancedClock.user));
  queueWait(advancedClockFake, okAux(TransferPhase::WAIT_HIGH), true);
  TEST_ASSERT_TRUE(advancedClock.waitUntilUs(500, advancedClock.user).ok());
  TEST_ASSERT_EQUAL_UINT64(500, advancedClock.nowUs(advancedClock.user));
  TEST_ASSERT_TRUE(advancedClockFake.overflow);

  ScriptedTransport backwardWaitFake;
  TEST_ASSERT_TRUE(backwardWaitFake.queueNow(200));
  const SingleWireTransport backwardWait = backwardWaitFake.descriptor();
  TEST_ASSERT_EQUAL_UINT64(200, backwardWait.nowUs(backwardWait.user));
  queueWait(backwardWaitFake, okAux(TransferPhase::WAIT_HIGH), true);
  const TransferResult backwardResult =
      backwardWait.waitUntilUs(199, backwardWait.user);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportCode::OK),
                          static_cast<uint8_t>(backwardResult.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransferPhase::WAIT_HIGH),
                          static_cast<uint8_t>(backwardResult.phase));
  TEST_ASSERT_EQUAL_UINT64(200, backwardWait.nowUs(backwardWait.user));
  TEST_ASSERT_FALSE(backwardWaitFake.overflow);

  ScriptedTransport deadlineFake;
  Bus deadlineBus;
  bindBus(deadlineBus, deadlineFake);
  TEST_ASSERT_TRUE(deadlineFake.queueNow(
      std::numeric_limits<uint64_t>::max() - 8000));
  assertErr(Err::CLOCK_STALLED,
            TestAccess::execute(deadlineBus, addressOnly(), normal));
  TEST_ASSERT_EQUAL_UINT32(0, deadlineFake.transferCalls);
  TEST_ASSERT_TRUE(deadlineFake.queueNow(
      std::numeric_limits<uint64_t>::max() - 4000));
  bool present = true;
  TransferResult resetResult{};
  assertErr(Err::CLOCK_STALLED,
            TestAccess::resetAndDiscover(deadlineBus, present, resetResult));
  TEST_ASSERT_FALSE(present);
  TEST_ASSERT_EQUAL_UINT32(0, deadlineFake.resetCalls);
}

void test_presence_false_is_not_transport_failure() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  BooleanScript absent{};
  absent.result = okAux(TransferPhase::PRESENCE);
  absent.value = false;
  TEST_ASSERT_TRUE(fake.queuePresence(absent));
  bool present = true;
  TEST_ASSERT_TRUE(bus.readPresenceIndicator(present).ok());
  TEST_ASSERT_FALSE(present);
  TEST_ASSERT_EQUAL_UINT64(10000, fake.lastPresenceDeadlineUs);

  BooleanScript failed{};
  failed.result = rawFailure(TransportCode::LINE_STUCK,
                             TransferPhase::PRESENCE, 12);
  failed.value = true;
  TEST_ASSERT_TRUE(fake.queuePresence(failed));
  assertErr(Err::LINE_STUCK, bus.readPresenceIndicator(present));
  TEST_ASSERT_FALSE(present);

  BooleanScript nack{};
  nack.result = rawFailure(TransportCode::NACK, TransferPhase::PRESENCE, 13);
  TEST_ASSERT_TRUE(fake.queuePresence(nack));
  assertErr(Err::IO_ERROR, bus.readPresenceIndicator(present));
  TEST_ASSERT_FALSE(present);

  BooleanScript malformed{};
  malformed.result = okAux(TransferPhase::PRESENCE);
  malformed.result.stopCompleted = true;
  TEST_ASSERT_TRUE(fake.queuePresence(malformed));
  assertErr(Err::IO_ERROR, bus.readPresenceIndicator(present));

  malformed.result = okAux(TransferPhase::DISCOVERY_RELEASE);
  TEST_ASSERT_TRUE(fake.queuePresence(malformed));
  assertErr(Err::IO_ERROR, bus.readPresenceIndicator(present));
  TEST_ASSERT_EQUAL_UINT32(0, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT32(0, fake.resetCalls);
  TEST_ASSERT_EQUAL_UINT32(0, fake.waitCalls);

  ScriptedTransport noPresenceFake;
  Bus noPresence;
  bindBus(noPresence, noPresenceFake, false);
  assertErr(Err::UNSUPPORTED_COMMAND,
            noPresence.readPresenceIndicator(present));
  TEST_ASSERT_EQUAL_UINT32(0, noPresenceFake.eventCount);
}

void test_write_cycle_keeps_frame_and_hold_results_and_blocks_bus() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver first;
  Driver second;
  Config firstConfig{};
  Config secondConfig{};
  secondConfig.addressBits = 1;
  TEST_ASSERT_TRUE(first.bind(bus, firstConfig).ok());
  TEST_ASSERT_TRUE(second.bind(bus, secondConfig).ok());
  const uint8_t data = 2;
  const SingleWireTransfer writeTransfer = writeFrame(&data);
  queueFrame(fake, okFrame(writeTransfer));
  queueWait(fake, rawFailure(TransportCode::NACK,
                             TransferPhase::WAIT_HIGH, 33));
  WriteCycleResult write{};
  assertErr(Err::IO_ERROR,
            TestAccess::executeWrite(bus, writeTransfer, write));
  const size_t transfers = fake.transferCalls;
  queueWait(fake, rawFailure(TransportCode::TIMEOUT,
                             TransferPhase::WAIT_HIGH, 34));
  TransferResult normal{};
  assertErr(Err::TRANSPORT_TIMEOUT,
            TestAccess::execute(bus, addressOnly(), normal));
  TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportCode::OK),
                          static_cast<uint8_t>(bus.snapshot().lastWriteCycle.frame.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportCode::TIMEOUT),
                          static_cast<uint8_t>(bus.snapshot().lastWriteCycle.hold.code));
  first.end();
  second.end();

  ScriptedTransport wrongPhaseFake;
  Bus wrongPhaseBus;
  bindBus(wrongPhaseBus, wrongPhaseFake);
  queueFrame(wrongPhaseFake, okFrame(writeTransfer));
  queueWait(wrongPhaseFake, okAux(TransferPhase::STOP));
  assertErr(Err::IO_ERROR,
            TestAccess::executeWrite(wrongPhaseBus, writeTransfer, write));
  TEST_ASSERT_NOT_EQUAL(0, wrongPhaseBus.snapshot().writeHighUntilUs);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(TransferPhase::STOP),
      static_cast<uint8_t>(wrongPhaseBus.snapshot().lastWriteCycle.hold.phase));
  queueWait(wrongPhaseFake, okAux(TransferPhase::WAIT_HIGH), true);
  TEST_ASSERT_TRUE(wrongPhaseBus.end().ok());

  ScriptedTransport evidenceFake;
  Bus evidenceBus;
  bindBus(evidenceBus, evidenceFake);
  queueFrame(evidenceFake, okFrame(writeTransfer));
  TransferResult malformedWait = okAux(TransferPhase::WAIT_HIGH);
  malformedWait.firstDeviceAddressAcked = true;
  queueWait(evidenceFake, malformedWait);
  assertErr(Err::IO_ERROR,
            TestAccess::executeWrite(evidenceBus, writeTransfer, write));
  TEST_ASSERT_NOT_EQUAL(0, evidenceBus.snapshot().writeHighUntilUs);
  TEST_ASSERT_TRUE(
      evidenceBus.snapshot().lastWriteCycle.hold.firstDeviceAddressAcked);
  queueWait(evidenceFake, okAux(TransferPhase::WAIT_HIGH), true);
  TEST_ASSERT_TRUE(evidenceBus.end().ok());
}

void test_reset_generation_is_shared() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver first;
  Driver second;
  Config firstConfig{};
  Config secondConfig{};
  secondConfig.addressBits = 1;
  TEST_ASSERT_TRUE(first.bind(bus, firstConfig).ok());
  TEST_ASSERT_TRUE(second.bind(bus, secondConfig).ok());
  const uint64_t seenFirst = first.snapshot().seenBusGeneration;
  const uint64_t seenSecond = second.snapshot().seenBusGeneration;
  BooleanScript reset{};
  reset.result = okAux(TransferPhase::DISCOVERY_RELEASE);
  reset.value = true;
  TEST_ASSERT_TRUE(fake.queueReset(reset));
  bool present = false;
  TransferResult result{};
  TEST_ASSERT_TRUE(TestAccess::resetAndDiscover(bus, present, result).ok());
  TEST_ASSERT_EQUAL_UINT64(6000, fake.lastResetDeadlineUs);
  TEST_ASSERT_EQUAL_UINT64(seenFirst + 1, bus.generation());
  TEST_ASSERT_EQUAL_UINT64(seenFirst, first.snapshot().seenBusGeneration);
  TEST_ASSERT_EQUAL_UINT64(seenSecond, second.snapshot().seenBusGeneration);
  first.end();
  second.end();
}

void test_failed_reset_invalidates_speed_knowledge() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  BooleanScript reset{};
  reset.result = rawFailure(TransportCode::TIMEOUT,
                            TransferPhase::DISCOVERY_SAMPLE, 55);
  reset.value = true;
  TEST_ASSERT_TRUE(fake.queueReset(reset));
  bool present = true;
  TransferResult result{};
  assertErr(Err::TRANSPORT_TIMEOUT,
            TestAccess::resetAndDiscover(bus, present, result));
  TEST_ASSERT_FALSE(present);
  TEST_ASSERT_EQUAL_UINT64(1, bus.generation());
  TEST_ASSERT_FALSE(bus.snapshot().resetEstablishedHighSpeed);

  TestAccess::seedGeneration(bus, std::numeric_limits<uint64_t>::max());
  const size_t resetCalls = fake.resetCalls;
  assertErr(Err::INVALID_STATE,
            TestAccess::resetAndDiscover(bus, present, result));
  TEST_ASSERT_EQUAL_UINT32(resetCalls, fake.resetCalls);
}

void test_discovery_sample_and_release_are_distinct() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  BooleanScript success{};
  success.result = okAux(TransferPhase::DISCOVERY_RELEASE);
  success.value = true;
  TEST_ASSERT_TRUE(fake.queueReset(success));
  bool present = false;
  TransferResult result{};
  TEST_ASSERT_TRUE(TestAccess::resetAndDiscover(bus, present, result).ok());

  BooleanScript absent = success;
  absent.value = false;
  TEST_ASSERT_TRUE(fake.queueReset(absent));
  assertErr(Err::NOT_PRESENT,
            TestAccess::resetAndDiscover(bus, present, result));

  BooleanScript held{};
  held.result = rawFailure(TransportCode::LINE_STUCK,
                           TransferPhase::DISCOVERY_RELEASE, 66);
  TEST_ASSERT_TRUE(fake.queueReset(held));
  assertErr(Err::LINE_STUCK,
            TestAccess::resetAndDiscover(bus, present, result));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransferPhase::DISCOVERY_RELEASE),
                          static_cast<uint8_t>(result.phase));

  BooleanScript nack{};
  nack.result = rawFailure(TransportCode::NACK,
                           TransferPhase::DISCOVERY_SAMPLE, 67);
  TEST_ASSERT_TRUE(fake.queueReset(nack));
  assertErr(Err::IO_ERROR,
            TestAccess::resetAndDiscover(bus, present, result));
  TEST_ASSERT_FALSE(present);

  const uint64_t generationBeforeMalformed = bus.generation();
  BooleanScript wrongPhase{};
  wrongPhase.result = okAux(TransferPhase::RESET_RECOVERY);
  wrongPhase.value = true;
  TEST_ASSERT_TRUE(fake.queueReset(wrongPhase));
  assertErr(Err::IO_ERROR,
            TestAccess::resetAndDiscover(bus, present, result));
  TEST_ASSERT_FALSE(present);
  TEST_ASSERT_EQUAL_UINT64(generationBeforeMalformed + 1, bus.generation());
  assertTransferEqual(wrongPhase.result, bus.snapshot().lastTransfer);

  BooleanScript impossibleEvidence{};
  impossibleEvidence.result = okAux(TransferPhase::DISCOVERY_RELEASE);
  impossibleEvidence.result.stopCompleted = true;
  impossibleEvidence.value = true;
  TEST_ASSERT_TRUE(fake.queueReset(impossibleEvidence));
  assertErr(Err::IO_ERROR,
            TestAccess::resetAndDiscover(bus, present, result));
  TEST_ASSERT_FALSE(present);
  TEST_ASSERT_EQUAL_UINT64(generationBeforeMalformed + 2, bus.generation());
  assertTransferEqual(impossibleEvidence.result, bus.snapshot().lastTransfer);
}

void test_core_headers_are_framework_neutral() {
  TEST_ASSERT_EQUAL_STRING("READY", toString(DriverState::READY));
  TEST_ASSERT_EQUAL_STRING("WAIT_HIGH", toString(TransferPhase::WAIT_HIGH));
}

void test_v1_surface_is_absent() {
  static_assert(!HasTick<Driver>::value);
  static_assert(!HasGetConfig<Driver>::value);
  TEST_PASS();
}

void test_scripted_transport_capacity_failures_are_explicit() {
  TransferScript transferScript{};
  ScriptedTransport transferFake;
  for (size_t index = 0; index < ScriptedTransport::TRANSFER_CAPACITY;
       ++index) {
    TEST_ASSERT_TRUE(transferFake.queueTransfer(transferScript));
  }
  TEST_ASSERT_FALSE(transferFake.queueTransfer(transferScript));
  TEST_ASSERT_TRUE(transferFake.overflow);

  BooleanScript booleanScript{};
  ScriptedTransport resetFake;
  ScriptedTransport presenceFake;
  for (size_t index = 0; index < ScriptedTransport::AUX_CAPACITY; ++index) {
    TEST_ASSERT_TRUE(resetFake.queueReset(booleanScript));
    TEST_ASSERT_TRUE(presenceFake.queuePresence(booleanScript));
  }
  TEST_ASSERT_FALSE(resetFake.queueReset(booleanScript));
  TEST_ASSERT_FALSE(presenceFake.queuePresence(booleanScript));
  TEST_ASSERT_TRUE(resetFake.overflow);
  TEST_ASSERT_TRUE(presenceFake.overflow);

  WaitScript waitScript{};
  ScriptedTransport waitFake;
  for (size_t index = 0; index < ScriptedTransport::AUX_CAPACITY; ++index) {
    TEST_ASSERT_TRUE(waitFake.queueWait(waitScript));
  }
  TEST_ASSERT_FALSE(waitFake.queueWait(waitScript));
  TEST_ASSERT_TRUE(waitFake.overflow);

  ScriptedTransport nowFake;
  for (size_t index = 0; index < ScriptedTransport::NOW_CAPACITY; ++index) {
    TEST_ASSERT_TRUE(nowFake.queueNow(static_cast<uint64_t>(index)));
  }
  TEST_ASSERT_FALSE(nowFake.queueNow(ScriptedTransport::NOW_CAPACITY));
  TEST_ASSERT_TRUE(nowFake.overflow);

  ScriptedTransport captureFake;
  captureFake.capturedCount = ScriptedTransport::TRANSFER_CAPACITY;
  TEST_ASSERT_TRUE(captureFake.queueTransfer(transferScript));
  const SingleWireTransport captureDescriptor = captureFake.descriptor();
  const TransferResult captureResult = captureDescriptor.transfer(
      addressOnly(), 10000, captureDescriptor.user);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportCode::IO_ERROR),
                          static_cast<uint8_t>(captureResult.code));
  TEST_ASSERT_EQUAL_INT32(ScriptedTransport::SCRIPT_ERROR_DETAIL,
                          captureResult.detail);
  TEST_ASSERT_TRUE(captureFake.overflow);
}

void test_independent_buses_are_fully_isolated() {
  ScriptedTransport fakeA;
  ScriptedTransport fakeB;
  Bus busA;
  Bus busB;
  bindBus(busA, fakeA);
  bindBus(busB, fakeB);
  Driver driverA;
  Driver driverB;
  Config config{};
  TEST_ASSERT_TRUE(driverA.bind(busA, config).ok());
  TEST_ASSERT_TRUE(driverB.bind(busB, config).ok());
  const BusSnapshot beforeB = busB.snapshot();

  BooleanScript reset{};
  reset.result = okAux(TransferPhase::DISCOVERY_RELEASE);
  reset.value = true;
  TEST_ASSERT_TRUE(fakeA.queueReset(reset));
  bool present = false;
  TransferResult result{};
  TEST_ASSERT_TRUE(TestAccess::resetAndDiscover(busA, present, result).ok());
  TEST_ASSERT_EQUAL_UINT64(beforeB.generation, busB.snapshot().generation);
  TEST_ASSERT_EQUAL_UINT64(beforeB.bindingEpoch, busB.snapshot().bindingEpoch);

  const uint8_t data = 0xC3;
  const SingleWireTransfer write = writeFrame(&data);
  queueFrame(fakeA, okFrame(write));
  queueWait(fakeA, rawFailure(TransportCode::TIMEOUT,
                              TransferPhase::WAIT_HIGH, 70));
  WriteCycleResult writeResult{};
  assertErr(Err::TRANSPORT_TIMEOUT,
            TestAccess::executeWrite(busA, write, writeResult));

  queueFrame(fakeB, okFrame(addressOnly()));
  TransferResult independentResult{};
  TEST_ASSERT_TRUE(
      TestAccess::execute(busB, addressOnly(), independentResult).ok());
  TEST_ASSERT_EQUAL_UINT32(1, fakeB.transferCalls);
  driverB.end();
  TEST_ASSERT_TRUE(busB.end().ok());
  const BusSnapshot stableB = busB.snapshot();

  BooleanScript presenceFailure{};
  presenceFailure.result = rawFailure(TransportCode::IO_ERROR,
                                      TransferPhase::PRESENCE, 71);
  TEST_ASSERT_TRUE(fakeA.queuePresence(presenceFailure));
  assertErr(Err::IO_ERROR, busA.readPresenceIndicator(present));

  ScriptedTransport replacement;
  BusConfig replacementConfig{};
  replacementConfig.transport = replacement.descriptor();
  assertErr(Err::BUSY, busA.bind(replacementConfig));
  assertErr(Err::BUSY, busA.end());
  driverA.end();
  queueWait(fakeA, okAux(TransferPhase::WAIT_HIGH), true);
  TEST_ASSERT_TRUE(busA.end().ok());

  assertBusSnapshotEqual(stableB, busB.snapshot());
  TEST_ASSERT_EQUAL_UINT32(0, fakeB.resetCalls);
  TEST_ASSERT_EQUAL_UINT32(0, fakeB.waitCalls);
  TEST_ASSERT_EQUAL_UINT32(0, fakeB.presenceCalls);
  TEST_ASSERT_EQUAL_PTR(&fakeA, fakeA.descriptor().user);
  TEST_ASSERT_EQUAL_PTR(&fakeB, fakeB.descriptor().user);
}

void test_esp32_descriptor_lifecycle_and_stale_callbacks() {
  Esp32Transport transport;
  const SingleWireTransport empty = transport.descriptor();
  TEST_ASSERT_NULL(empty.user);
  TEST_ASSERT_NULL(empty.nowUs);
  TEST_ASSERT_NULL(empty.transfer);
  TEST_ASSERT_NULL(empty.resetAndDiscover);
  TEST_ASSERT_NULL(empty.waitUntilUs);
  TEST_ASSERT_NULL(empty.readPresence);
  TestAccess::activateWithoutHardware(transport);
  TestAccess::resetPlatformAccessCount(transport);
  TEST_ASSERT_FALSE(TestAccess::delayWithinDeadline(
      transport, std::numeric_limits<uint32_t>::max(), 9000));
  TEST_ASSERT_EQUAL_UINT32(1, TestAccess::platformAccessCount(transport));
  TestAccess::resetPlatformAccessCount(transport);
  TEST_ASSERT_FALSE(TestAccess::delayWithinDeadline(transport, 160, 160));
  TEST_ASSERT_EQUAL_UINT32(1, TestAccess::platformAccessCount(transport));
  TestAccess::resetPlatformAccessCount(transport);
  TEST_ASSERT_TRUE(TestAccess::delayWithinDeadline(transport, 159, 160));
  TEST_ASSERT_EQUAL_UINT32(3, TestAccess::platformAccessCount(transport));
  TestAccess::resetPlatformAccessCount(transport);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(TransportCode::TIMEOUT),
      static_cast<uint8_t>(TestAccess::finishStop(transport, 160, 160)));
  TEST_ASSERT_EQUAL_UINT32(2, TestAccess::platformAccessCount(transport));
  TEST_ASSERT_EQUAL_UINT16(160,
                           TestAccess::startHighUs(SpeedMode::HIGH_SPEED));
  TEST_ASSERT_EQUAL_UINT16(650,
                           TestAccess::startHighUs(SpeedMode::STANDARD_SPEED));
  const SingleWireTransport stale = transport.descriptor();
  TEST_ASSERT_NOT_NULL(stale.transfer);
  transport.end();
  const SingleWireTransport ended = transport.descriptor();
  TEST_ASSERT_NULL(ended.user);
  TEST_ASSERT_NULL(ended.nowUs);
  TEST_ASSERT_NULL(ended.transfer);
  TEST_ASSERT_NULL(ended.resetAndDiscover);
  TEST_ASSERT_NULL(ended.waitUntilUs);
  TEST_ASSERT_NULL(ended.readPresence);
  const uint32_t before = TestAccess::platformAccessCount(transport);

  TransferResult result = stale.transfer(addressOnly(), 1, stale.user);
  assertStaleResult(result);
  bool present = true;
  result = stale.resetAndDiscover(present, 1, stale.user);
  TEST_ASSERT_FALSE(present);
  assertStaleResult(result);
  result = stale.waitUntilUs(1, stale.user);
  assertStaleResult(result);
  present = true;
  result = stale.readPresence(present, 1, stale.user);
  TEST_ASSERT_FALSE(present);
  assertStaleResult(result);
  TEST_ASSERT_EQUAL_UINT64(0, stale.nowUs(stale.user));
  TEST_ASSERT_EQUAL_UINT32(before,
                           TestAccess::platformAccessCount(transport));
}

void test_address_claims_and_transactional_rebind() {
  ScriptedTransport fakeA;
  ScriptedTransport fakeB;
  Bus busA;
  Bus busB;
  bindBus(busA, fakeA);
  bindBus(busB, fakeB);
  Driver first;
  Driver duplicate;
  Driver independent;
  Config addressZero{};
  TEST_ASSERT_TRUE(first.bind(busA, addressZero).ok());
  assertErr(Err::INVALID_CONFIG, duplicate.bind(busA, addressZero));
  TEST_ASSERT_TRUE(independent.bind(busB, addressZero).ok());

  Config invalid = addressZero;
  invalid.addressBits = 8;
  assertErr(Err::INVALID_CONFIG, first.bind(busB, invalid));
  TEST_ASSERT_EQUAL_UINT8(0x01, busA.snapshot().claimedAddressMask);
  TEST_ASSERT_TRUE(first.isBound());

  Config claimed = addressZero;
  assertErr(Err::INVALID_CONFIG, first.bind(busB, claimed));
  TEST_ASSERT_EQUAL_UINT8(0x01, busA.snapshot().claimedAddressMask);
  TEST_ASSERT_EQUAL_UINT32(0, fakeA.eventCount + fakeB.eventCount);
  first.end();
  TEST_ASSERT_EQUAL_UINT8(0, busA.snapshot().claimedAddressMask);
  TEST_ASSERT_EQUAL_UINT8(0x01, busB.snapshot().claimedAddressMask);
  independent.end();
  TEST_ASSERT_EQUAL_UINT8(0, busB.snapshot().claimedAddressMask);
}

void test_live_claims_block_bus_end_until_driver_end() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  Config config{};
  TEST_ASSERT_TRUE(driver.bind(bus, config).ok());
  const uint8_t data = 0x44;
  const SingleWireTransfer transfer = writeFrame(&data);
  queueFrame(fake, okFrame(transfer));
  queueWait(fake, rawFailure(TransportCode::TIMEOUT,
                             TransferPhase::WAIT_HIGH, 80));
  WriteCycleResult write{};
  assertErr(Err::TRANSPORT_TIMEOUT,
            TestAccess::executeWrite(bus, transfer, write));
  const size_t waitCalls = fake.waitCalls;
  assertErr(Err::BUSY, bus.end());
  TEST_ASSERT_EQUAL_INT32(1, bus.end().detail);
  TEST_ASSERT_EQUAL_UINT32(waitCalls, fake.waitCalls);
  driver.end();
  TEST_ASSERT_EQUAL_UINT8(0, bus.snapshot().claimedAddressMask);
  queueWait(fake, okAux(TransferPhase::WAIT_HIGH), true);
  TEST_ASSERT_TRUE(bus.end().ok());
  TEST_ASSERT_FALSE(bus.isBound());
}
