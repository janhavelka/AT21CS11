/// @file test_basic.cpp
/// @brief Native contract tests for AT21CS lifecycle and validation behavior.

#include <unity.h>

#include "AT21CS/AT21CS.h"
#include "AT21CS/Config.h"
#include "AT21CS/Status.h"

using namespace AT21CS;

void setUp() {}
void tearDown() {}

namespace {

struct FakeTransportState {
  bool beginCalled = false;
  bool endCalled = false;
  bool releaseCalled = false;
  uint32_t now = 100;
  uint32_t sleepCalls = 0;
  uint32_t resetCalls = 0;
  Status beginStatus = Status::Ok();
  Status resetStatus = Status::Ok();
  bool presencePresent = true;

  uint8_t expectedTx[64] = {};
  bool txAck[64] = {};
  size_t expectedTxLen = 0;
  size_t txIndex = 0;

  uint8_t rx[64] = {};
  bool expectedReadAck[64] = {};
  size_t rxLen = 0;
  size_t rxIndex = 0;

  bool txUnderflow = false;
  bool txMismatch = false;
  bool rxUnderflow = false;
  bool readAckMismatch = false;
};

uint8_t deviceAddress(uint8_t opcode, bool read, uint8_t addressBits = 0) {
  const uint8_t rw = read ? 0x01U : 0x00U;
  return static_cast<uint8_t>((opcode << 4U) | ((addressBits & 0x07U) << 1U) | rw);
}

Status fakeBegin(void* user) {
  auto* state = static_cast<FakeTransportState*>(user);
  state->beginCalled = true;
  return state->beginStatus;
}

void fakeEnd(void* user) {
  auto* state = static_cast<FakeTransportState*>(user);
  state->endCalled = true;
}

void fakeReleaseLine(void* user) {
  auto* state = static_cast<FakeTransportState*>(user);
  state->releaseCalled = true;
}

bool fakeWriteByteReadAck(uint8_t value,
                           const SingleWireTimingProfile&,
                           void* user) {
  auto* state = static_cast<FakeTransportState*>(user);
  if (state->txIndex >= state->expectedTxLen) {
    state->txUnderflow = true;
    return false;
  }

  const size_t index = state->txIndex++;
  if (state->expectedTx[index] != value) {
    state->txMismatch = true;
  }
  return state->txAck[index];
}

uint8_t fakeReadByteSendAck(bool ack,
                            const SingleWireTimingProfile&,
                            void* user) {
  auto* state = static_cast<FakeTransportState*>(user);
  if (state->rxIndex >= state->rxLen) {
    state->rxUnderflow = true;
    return 0xFF;
  }

  const size_t index = state->rxIndex++;
  if (state->expectedReadAck[index] != ack) {
    state->readAckMismatch = true;
  }
  return state->rx[index];
}

Status fakeResetAndDiscover(const SingleWireTimingProfile&, void* user) {
  auto* state = static_cast<FakeTransportState*>(user);
  ++state->resetCalls;
  return state->resetStatus;
}

bool fakePresencePresent(void* user) {
  auto* state = static_cast<FakeTransportState*>(user);
  return state->presencePresent;
}

uint32_t fakeNowMs(void* user) {
  auto* state = static_cast<FakeTransportState*>(user);
  return state->now;
}

void fakeSleepUs(uint32_t, void* user) {
  auto* state = static_cast<FakeTransportState*>(user);
  ++state->sleepCalls;
  ++state->now;
}

void queueTx(FakeTransportState& state, const uint8_t* bytes, const bool* acks, size_t len) {
  state.expectedTxLen = len;
  state.txIndex = 0;
  state.txUnderflow = false;
  state.txMismatch = false;
  for (size_t i = 0; i < len; ++i) {
    state.expectedTx[i] = bytes[i];
    state.txAck[i] = acks[i];
  }
}

void queueRx(FakeTransportState& state, const uint8_t* bytes, const bool* acks, size_t len) {
  state.rxLen = len;
  state.rxIndex = 0;
  state.rxUnderflow = false;
  state.readAckMismatch = false;
  for (size_t i = 0; i < len; ++i) {
    state.rx[i] = bytes[i];
    state.expectedReadAck[i] = acks[i];
  }
}

void clearQueues(FakeTransportState& state) {
  state.expectedTxLen = 0;
  state.txIndex = 0;
  state.rxLen = 0;
  state.rxIndex = 0;
  state.txUnderflow = false;
  state.txMismatch = false;
  state.rxUnderflow = false;
  state.readAckMismatch = false;
}

void queueManufacturerId(FakeTransportState& state, uint32_t manufacturerId) {
  const uint8_t tx[] = {deviceAddress(cmd::OPCODE_MANUFACTURER_ID, true)};
  const bool txAck[] = {true};
  const uint8_t rx[] = {
      static_cast<uint8_t>((manufacturerId >> 16U) & 0xFFU),
      static_cast<uint8_t>((manufacturerId >> 8U) & 0xFFU),
      static_cast<uint8_t>(manufacturerId & 0xFFU)};
  const bool rxAck[] = {true, true, false};
  queueTx(state, tx, txAck, sizeof(tx));
  queueRx(state, rx, rxAck, sizeof(rx));
}

void assertQueuesConsumed(const FakeTransportState& state) {
  TEST_ASSERT_FALSE(state.txUnderflow);
  TEST_ASSERT_FALSE(state.txMismatch);
  TEST_ASSERT_FALSE(state.rxUnderflow);
  TEST_ASSERT_FALSE(state.readAckMismatch);
  TEST_ASSERT_EQUAL_UINT32(state.expectedTxLen, state.txIndex);
  TEST_ASSERT_EQUAL_UINT32(state.rxLen, state.rxIndex);
}

SingleWireTransport makeTransport(FakeTransportState& state) {
  SingleWireTransport transport;
  transport.user = &state;
  transport.begin = &fakeBegin;
  transport.end = &fakeEnd;
  transport.releaseLine = &fakeReleaseLine;
  transport.writeByteReadAck = &fakeWriteByteReadAck;
  transport.readByteSendAck = &fakeReadByteSendAck;
  transport.resetAndDiscover = &fakeResetAndDiscover;
  transport.nowMs = &fakeNowMs;
  transport.sleepUs = &fakeSleepUs;
  return transport;
}

SingleWireTransport makeTransportWithPresence(FakeTransportState& state) {
  SingleWireTransport transport = makeTransport(state);
  transport.presencePresent = &fakePresencePresent;
  return transport;
}

Status beginWithAt21cs11(Driver& dev, FakeTransportState& state, SingleWireTransport& transport) {
  clearQueues(state);
  queueManufacturerId(state, cmd::MANUFACTURER_ID_AT21CS11);
  Config cfg;
  cfg.transport = &transport;
  cfg.discoveryRetries = 0;
  cfg.expectedPart = PartType::AT21CS11;
  const Status st = dev.begin(cfg);
  assertQueuesConsumed(state);
  return st;
}

SingleWireTransport makeFailingDiscoveryTransport() {
  static FakeTransportState state;
  state = FakeTransportState{};
  state.resetStatus = Status::Error(Err::DISCOVERY_FAILED, "fake discovery failed");
  SingleWireTransport transport;
  transport.user = &state;
  transport.releaseLine = &fakeReleaseLine;
  transport.writeByteReadAck = &fakeWriteByteReadAck;
  transport.readByteSendAck = &fakeReadByteSendAck;
  transport.resetAndDiscover = &fakeResetAndDiscover;
  transport.sleepUs = &fakeSleepUs;
  return transport;
}

}  // namespace

void test_status_ok() {
  Status st = Status::Ok();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OK), static_cast<uint8_t>(st.code));
}

void test_status_error() {
  Status st = Status::Error(Err::INVALID_PARAM, "bad", 7);
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_FALSE(st.inProgress());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(7, st.detail);
}

void test_config_defaults() {
  Config cfg;
  TEST_ASSERT_NULL(cfg.transport);
  TEST_ASSERT_EQUAL_INT16(-1, cfg.sioPin);
  TEST_ASSERT_EQUAL_INT16(-1, cfg.presencePin);
  TEST_ASSERT_EQUAL_UINT8(0, cfg.addressBits);
  TEST_ASSERT_EQUAL_UINT8(5, cfg.offlineThreshold);
}

void test_begin_rejects_missing_sio_pin() {
  Driver dev;
  Config cfg;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(dev.state()));
}

void test_begin_rejects_same_presence_and_sio_pin() {
  Driver dev;
  Config cfg;
  cfg.sioPin = 5;
  cfg.presencePin = 5;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(dev.state()));
}

void test_begin_rejects_incomplete_transport() {
  Driver dev;
  Config cfg;
  SingleWireTransport transport;
  cfg.transport = &transport;

  Status st = dev.begin(cfg);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(dev.state()));
}

void test_begin_rejects_mixed_transport_and_compatibility_pins() {
  Driver dev;
  FakeTransportState state;
  SingleWireTransport transport = makeTransport(state);
  Config cfg;
  cfg.transport = &transport;
  cfg.sioPin = 4;

  Status st = dev.begin(cfg);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(dev.state()));

  cfg.sioPin = -1;
  cfg.presencePin = 5;
  st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(dev.state()));
}

void test_begin_normalizes_zero_offline_threshold_before_probe() {
  Driver dev;
  Config cfg;
  SingleWireTransport transport = makeFailingDiscoveryTransport();
  cfg.transport = &transport;
  cfg.offlineThreshold = 0;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_PRESENT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_FALSE(dev.isInitialized());
  TEST_ASSERT_EQUAL_UINT8(1u, dev.getConfig().offlineThreshold);
}

void test_injected_transport_begin_reset_discovery_success() {
  Driver dev;
  FakeTransportState state;
  SingleWireTransport transport = makeTransport(state);

  const Status st = beginWithAt21cs11(dev, state, transport);

  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(state.beginCalled);
  TEST_ASSERT_TRUE(state.releaseCalled);
  TEST_ASSERT_TRUE(dev.isInitialized());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PartType::AT21CS11),
                          static_cast<uint8_t>(dev.detectedPart()));
  TEST_ASSERT_EQUAL_UINT32(1u, state.resetCalls);
}

void test_injected_presence_callback_blocks_begin_without_protocol_io() {
  Driver dev;
  FakeTransportState state;
  state.presencePresent = false;
  SingleWireTransport transport = makeTransportWithPresence(state);
  Config cfg;
  cfg.transport = &transport;
  cfg.discoveryRetries = 0;

  const Status st = dev.begin(cfg);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_PRESENT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_TRUE(state.beginCalled);
  TEST_ASSERT_EQUAL_UINT32(0u, state.resetCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, state.txIndex);
  TEST_ASSERT_EQUAL_UINT32(0u, state.rxIndex);
  TEST_ASSERT_FALSE(dev.isInitialized());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
}

void test_probe_uses_raw_reset_discovery_without_health_mutation() {
  Driver dev;
  FakeTransportState state;
  SingleWireTransport transport = makeTransport(state);
  TEST_ASSERT_TRUE(beginWithAt21cs11(dev, state, transport).ok());
  clearQueues(state);
  state.resetCalls = 0;
  state.resetStatus = Status::Error(Err::DISCOVERY_FAILED, "probe discovery failed");

  const Status st = dev.probe();

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DISCOVERY_FAILED),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(1u, state.resetCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
}

void test_read_eeprom_uses_injected_byte_primitives() {
  Driver dev;
  FakeTransportState state;
  SingleWireTransport transport = makeTransport(state);
  TEST_ASSERT_TRUE(beginWithAt21cs11(dev, state, transport).ok());
  clearQueues(state);

  const uint8_t tx[] = {
      deviceAddress(cmd::OPCODE_EEPROM, false), 0x12,
      deviceAddress(cmd::OPCODE_EEPROM, true)};
  const bool txAck[] = {true, true, true};
  const uint8_t rx[] = {0x34, 0x56};
  const bool rxAck[] = {true, false};
  queueTx(state, tx, txAck, sizeof(tx));
  queueRx(state, rx, rxAck, sizeof(rx));

  uint8_t data[2] = {};
  const Status st = dev.readEeprom(0x12, data, sizeof(data));

  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(0x34, data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x56, data[1]);
  assertQueuesConsumed(state);
}

void test_write_eeprom_byte_uses_injected_byte_primitives_and_ready_ack() {
  Driver dev;
  FakeTransportState state;
  SingleWireTransport transport = makeTransport(state);
  TEST_ASSERT_TRUE(beginWithAt21cs11(dev, state, transport).ok());
  clearQueues(state);

  const uint8_t tx[] = {
      deviceAddress(cmd::OPCODE_EEPROM, false), 0x20, 0xAB,
      deviceAddress(cmd::OPCODE_EEPROM, false)};
  const bool txAck[] = {true, true, true, true};
  queueTx(state, tx, txAck, sizeof(tx));

  const Status st = dev.writeEepromByte(0x20, 0xAB);

  TEST_ASSERT_TRUE(st.ok());
  assertQueuesConsumed(state);
}

void test_wait_ready_timeout_uses_injected_transport_and_updates_health() {
  Driver dev;
  FakeTransportState state;
  SingleWireTransport transport = makeTransport(state);
  TEST_ASSERT_TRUE(beginWithAt21cs11(dev, state, transport).ok());
  clearQueues(state);

  const uint8_t tx[] = {deviceAddress(cmd::OPCODE_EEPROM, false)};
  const bool txAck[] = {false};
  queueTx(state, tx, txAck, sizeof(tx));

  const Status st = dev.waitReady(1);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(1u, dev.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
  TEST_ASSERT_TRUE(state.sleepCalls > 0u);
  assertQueuesConsumed(state);
}

void test_injected_transport_reports_ack_nack_phase_errors() {
  Driver dev;
  FakeTransportState state;
  SingleWireTransport transport = makeTransport(state);
  TEST_ASSERT_TRUE(beginWithAt21cs11(dev, state, transport).ok());

  clearQueues(state);
  const uint8_t deviceNackTx[] = {deviceAddress(cmd::OPCODE_EEPROM, false)};
  const bool deviceNackAck[] = {false};
  queueTx(state, deviceNackTx, deviceNackAck, sizeof(deviceNackTx));
  uint8_t data = 0;
  Status st = dev.readEeprom(0x12, &data, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NACK_DEVICE_ADDRESS),
                          static_cast<uint8_t>(st.code));
  assertQueuesConsumed(state);

  clearQueues(state);
  const uint8_t memoryNackTx[] = {deviceAddress(cmd::OPCODE_EEPROM, false), 0x12};
  const bool memoryNackAck[] = {true, false};
  queueTx(state, memoryNackTx, memoryNackAck, sizeof(memoryNackTx));
  st = dev.readEeprom(0x12, &data, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NACK_MEMORY_ADDRESS),
                          static_cast<uint8_t>(st.code));
  assertQueuesConsumed(state);

  clearQueues(state);
  const uint8_t dataNackTx[] = {deviceAddress(cmd::OPCODE_EEPROM, false), 0x20, 0xAB};
  const bool dataNackAck[] = {true, true, false};
  queueTx(state, dataNackTx, dataNackAck, sizeof(dataNackTx));
  st = dev.writeEepromByte(0x20, 0xAB);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NACK_DATA),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(0, st.detail);
  assertQueuesConsumed(state);
}

void test_injected_transport_reset_error_propagates_and_updates_health() {
  Driver dev;
  FakeTransportState state;
  SingleWireTransport transport = makeTransport(state);
  TEST_ASSERT_TRUE(beginWithAt21cs11(dev, state, transport).ok());
  clearQueues(state);
  state.resetStatus = Status::Error(Err::IO_ERROR, "reset transport failure", 77);

  uint8_t value = 0;
  const Status st = dev.readCurrentAddress(value);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::IO_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(77, st.detail);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(1u, dev.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
}

void test_read_serial_number_validates_product_id_and_crc() {
  Driver dev;
  FakeTransportState state;
  SingleWireTransport transport = makeTransport(state);
  TEST_ASSERT_TRUE(beginWithAt21cs11(dev, state, transport).ok());
  clearQueues(state);

  uint8_t serial[cmd::SECURITY_SERIAL_SIZE] = {
      cmd::SECURITY_PRODUCT_ID, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0x00};
  serial[cmd::SECURITY_SERIAL_SIZE - 1U] =
      Driver::crc8_31(serial, cmd::SECURITY_SERIAL_SIZE - 1U);

  const uint8_t tx[] = {
      deviceAddress(cmd::OPCODE_SECURITY, false), cmd::SECURITY_SERIAL_START,
      deviceAddress(cmd::OPCODE_SECURITY, true)};
  const bool txAck[] = {true, true, true};
  const bool rxAck[] = {true, true, true, true, true, true, true, false};
  queueTx(state, tx, txAck, sizeof(tx));
  queueRx(state, serial, rxAck, sizeof(serial));

  SerialNumberInfo info{};
  Status st = dev.readSerialNumber(info);

  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(info.productIdOk);
  TEST_ASSERT_TRUE(info.crcOk);
  TEST_ASSERT_EQUAL_UINT8(serial[7], info.bytes[7]);
  assertQueuesConsumed(state);

  clearQueues(state);
  serial[cmd::SECURITY_SERIAL_SIZE - 1U] ^= 0x01U;
  queueTx(state, tx, txAck, sizeof(tx));
  queueRx(state, serial, rxAck, sizeof(serial));
  st = dev.readSerialNumber(info);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::CRC_MISMATCH),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_TRUE(info.productIdOk);
  TEST_ASSERT_FALSE(info.crcOk);
  assertQueuesConsumed(state);
}

void test_begin_rejects_write_timeout_over_limit() {
  Driver dev;
  Config cfg;
  cfg.sioPin = 6;
  cfg.writeTimeoutMs = 251;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(dev.state()));
}

void test_begin_rejects_invalid_expected_part_enum() {
  Driver dev;
  Config cfg;
  cfg.sioPin = 6;
  cfg.expectedPart = static_cast<PartType>(0xFF);

  Status st = dev.begin(cfg);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(dev.state()));
}

void test_begin_rejects_invalid_startup_speed_enum() {
  Driver dev;
  Config cfg;
  cfg.sioPin = 6;
  cfg.startupSpeed = static_cast<SpeedMode>(0xFF);

  Status st = dev.begin(cfg);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::FAULT),
                          static_cast<uint8_t>(dev.state()));
}

void test_probe_requires_begin() {
  Driver dev;
  Status st = dev.probe();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(st.code));
}

void test_recover_requires_begin() {
  Driver dev;
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(st.code));
}

void test_multi_page_write_helpers_check_initialization_first() {
  Driver dev;
  uint8_t data = 0;

  Status st = dev.writeEeprom(0, nullptr, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(st.code));

  st = dev.writeEeprom(0, &data, 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(st.code));

  st = dev.writeSecurityUser(0x10, nullptr, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(st.code));

  st = dev.writeSecurityUser(0x10, &data, 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(st.code));
}

void test_end_without_begin_keeps_uninit() {
  Driver dev;
  dev.end();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
}

void test_settings_snapshot_reports_cached_state_without_io() {
  Driver dev;

  SettingsSnapshot snap;
  TEST_ASSERT_TRUE(dev.getSettings(snap).ok());
  TEST_ASSERT_FALSE(snap.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(snap.state));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PartType::UNKNOWN),
                          static_cast<uint8_t>(snap.detectedPart));
  TEST_ASSERT_EQUAL_UINT8(5u, snap.config.offlineThreshold);
  TEST_ASSERT_EQUAL_UINT32(0u, snap.totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(0u, snap.totalFailures);

  const SettingsSnapshot byValue = dev.getSettings();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(snap.state),
                          static_cast<uint8_t>(byValue.state));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_status_ok);
  RUN_TEST(test_status_error);
  RUN_TEST(test_config_defaults);
  RUN_TEST(test_begin_rejects_missing_sio_pin);
  RUN_TEST(test_begin_rejects_same_presence_and_sio_pin);
  RUN_TEST(test_begin_rejects_incomplete_transport);
  RUN_TEST(test_begin_rejects_mixed_transport_and_compatibility_pins);
  RUN_TEST(test_begin_normalizes_zero_offline_threshold_before_probe);
  RUN_TEST(test_injected_transport_begin_reset_discovery_success);
  RUN_TEST(test_injected_presence_callback_blocks_begin_without_protocol_io);
  RUN_TEST(test_probe_uses_raw_reset_discovery_without_health_mutation);
  RUN_TEST(test_read_eeprom_uses_injected_byte_primitives);
  RUN_TEST(test_write_eeprom_byte_uses_injected_byte_primitives_and_ready_ack);
  RUN_TEST(test_wait_ready_timeout_uses_injected_transport_and_updates_health);
  RUN_TEST(test_injected_transport_reports_ack_nack_phase_errors);
  RUN_TEST(test_injected_transport_reset_error_propagates_and_updates_health);
  RUN_TEST(test_read_serial_number_validates_product_id_and_crc);
  RUN_TEST(test_begin_rejects_write_timeout_over_limit);
  RUN_TEST(test_begin_rejects_invalid_expected_part_enum);
  RUN_TEST(test_begin_rejects_invalid_startup_speed_enum);
  RUN_TEST(test_probe_requires_begin);
  RUN_TEST(test_recover_requires_begin);
  RUN_TEST(test_multi_page_write_helpers_check_initialization_first);
  RUN_TEST(test_end_without_begin_keeps_uninit);
  RUN_TEST(test_settings_snapshot_reports_cached_state_without_io);
  return UNITY_END();
}
