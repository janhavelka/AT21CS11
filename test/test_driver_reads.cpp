#include <cstddef>
#include <cstdint>
#include <limits>

#include <unity.h>

#include "AT21CS/AT21CS.h"
#include "support/DriverTestSupport.h"
#include "support/TestAccess.h"

using namespace AT21CS;
using namespace AT21CS::test;

namespace {

constexpr uint32_t CS01_ID = 0x00D202u;
constexpr uint32_t CS11_ID = 0x00D384u;

TransferScript randomReadFailure(TransportCode code,
                                 TransferPhase phase,
                                 int32_t detail,
                                 size_t bytesTransferred = 0) {
  TransferScript script{};
  script.result.code = code;
  script.result.phase = phase;
  script.result.detail = detail;
  script.result.dataBytesTransferred = bytesTransferred;
  if (phase != TransferPhase::START &&
      phase != TransferPhase::DEVICE_ADDRESS_WRITE) {
    script.result.firstDeviceAddressAcked = true;
  }
  if (phase == TransferPhase::RESTART ||
      phase == TransferPhase::DEVICE_ADDRESS_READ ||
      phase == TransferPhase::DATA_READ || phase == TransferPhase::STOP) {
    script.result.memoryAddressAcked = true;
  }
  if (phase == TransferPhase::DATA_READ || phase == TransferPhase::STOP) {
    script.result.repeatedDeviceAddressAcked = true;
  }
  return script;
}

}  // namespace

void test_read_boundaries_validate_complete_size_t_ranges() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);
  uint8_t data[20] = {};
  const size_t transfers = fake.transferCalls;

  assertStatus(Err::INVALID_PARAM, driver.readEeprom(0, nullptr, 1));
  assertStatus(Err::INVALID_PARAM, driver.readEeprom(0, data, 0));
  assertStatus(Err::INVALID_PARAM, driver.readEeprom(128, data, 1));
  assertStatus(Err::INVALID_PARAM, driver.readEeprom(127, data, 2));
  assertStatus(Err::INVALID_PARAM,
               driver.readEeprom(0, data,
                                  std::numeric_limits<size_t>::max()));
  assertStatus(Err::INVALID_PARAM, driver.readSecurity(0, nullptr, 1));
  assertStatus(Err::INVALID_PARAM, driver.readSecurity(0, data, 0));
  assertStatus(Err::INVALID_PARAM, driver.readSecurity(32, data, 1));
  assertStatus(Err::INVALID_PARAM, driver.readSecurity(31, data, 2));
  assertStatus(Err::INVALID_PARAM,
               driver.readSecurity(0, data,
                                   std::numeric_limits<size_t>::max()));
  TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);

  const uint8_t last = 0xE1u;
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&last, 1)));
  TEST_ASSERT_TRUE(driver.readEeprom(127, data, 1).ok());
  TEST_ASSERT_EQUAL_HEX8(last, data[0]);

  uint8_t expected[17] = {};
  for (size_t index = 0; index < sizeof(expected); ++index) {
    expected[index] = static_cast<uint8_t>(index + 1u);
  }
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(expected, 8)));
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(expected + 8, 8)));
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(expected + 16, 1)));
  TEST_ASSERT_TRUE(driver.readEeprom(0, data, sizeof(expected)).ok());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, data, sizeof(expected));

  const uint8_t security = 0x39u;
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&security, 1)));
  TEST_ASSERT_TRUE(driver.readSecurity(31, data, 1).ok());
  TEST_ASSERT_EQUAL_HEX8(security, data[0]);

  uint8_t fullEeprom[128] = {};
  uint8_t expectedEeprom[128] = {};
  fake.eventCount = 0;
  for (size_t offset = 0; offset < sizeof(expectedEeprom); offset += 8u) {
    for (size_t index = 0; index < 8u; ++index) {
      expectedEeprom[offset + index] =
          static_cast<uint8_t>(offset + index);
    }
    TEST_ASSERT_TRUE(
        fake.queueTransfer(randomReadOk(expectedEeprom + offset, 8)));
  }
  TEST_ASSERT_TRUE(
      driver.readEeprom(0, fullEeprom, sizeof(fullEeprom)).ok());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedEeprom, fullEeprom,
                                sizeof(fullEeprom));

  uint8_t fullSecurity[32] = {};
  uint8_t expectedSecurity[32] = {};
  fake.eventCount = 0;
  for (size_t offset = 0; offset < sizeof(expectedSecurity); offset += 8u) {
    for (size_t index = 0; index < 8u; ++index) {
      expectedSecurity[offset + index] =
          static_cast<uint8_t>(0x80u + offset + index);
    }
    TEST_ASSERT_TRUE(
        fake.queueTransfer(randomReadOk(expectedSecurity + offset, 8)));
  }
  TEST_ASSERT_TRUE(
      driver.readSecurity(0, fullSecurity, sizeof(fullSecurity)).ok());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedSecurity, fullSecurity,
                                sizeof(fullSecurity));
}

void test_random_read_address_phases_map_independently() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  Config config{};
  config.addressBits = 2;
  initializeDriver(driver, bus, fake, config, CS11_ID);
  uint8_t value = 0x55u;

  TransferScript firstAddressNack{};
  firstAddressNack.result.code = TransportCode::NACK;
  firstAddressNack.result.phase = TransferPhase::DEVICE_ADDRESS_WRITE;
  TEST_ASSERT_TRUE(fake.queueTransfer(firstAddressNack));
  Status status = driver.readEeprom(0x12, &value, 1);
  assertStatus(Err::NACK_DEVICE_ADDRESS, status);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ProtocolPhase::DEVICE_ADDRESS_WRITE),
      static_cast<uint8_t>(protocolDetailPhase(status.detail)));
  TEST_ASSERT_EQUAL_HEX8(0xA4u,
                         fake.captured[fake.capturedCount - 1u].deviceAddress);
  TEST_ASSERT_EQUAL_HEX8(
      0xA5u,
      fake.captured[fake.capturedCount - 1u].repeatedDeviceAddress);

  TransferScript repeatedAddressNack{};
  repeatedAddressNack.result.code = TransportCode::NACK;
  repeatedAddressNack.result.phase = TransferPhase::DEVICE_ADDRESS_READ;
  repeatedAddressNack.result.firstDeviceAddressAcked = true;
  repeatedAddressNack.result.memoryAddressAcked = true;
  TEST_ASSERT_TRUE(fake.queueTransfer(repeatedAddressNack));
  status = driver.readEeprom(0x12, &value, 1);
  assertStatus(Err::NACK_DEVICE_ADDRESS, status);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ProtocolPhase::DEVICE_ADDRESS_READ),
      static_cast<uint8_t>(protocolDetailPhase(status.detail)));
}

void test_read_scratch_commits_only_complete_frames() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);

  const uint8_t first[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(first, sizeof(first))));
  TransferScript failed = randomReadFailure(
      TransportCode::TIMEOUT, TransferPhase::DATA_READ, 2001, 4);
  failed.rxLength = 8;
  for (uint8_t& byte : failed.rxData) {
    byte = 0xEEu;
  }
  TEST_ASSERT_TRUE(fake.queueTransfer(failed));

  uint8_t output[20];
  for (uint8_t& byte : output) {
    byte = 0xA5u;
  }
  const Status status = driver.readEeprom(0, output, sizeof(output));
  assertStatus(Err::TRANSPORT_TIMEOUT, status);
  TEST_ASSERT_EQUAL_INT32(2001, status.detail);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first, output, sizeof(first));
  for (size_t index = 8; index < sizeof(output); ++index) {
    TEST_ASSERT_EQUAL_HEX8(0xA5u, output[index]);
  }
}

void test_serial_crc_product_and_diagnostics_are_independent() {
  static constexpr uint8_t vector[] = {'1', '2', '3', '4', '5',
                                       '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX8(0xA1u,
                         Driver::crc8Maxim(vector, sizeof(vector)));
  TEST_ASSERT_EQUAL_HEX8(0,
                         Driver::crc8Maxim(nullptr, sizeof(vector)));

  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  initializeDriver(driver, bus, fake, Config{}, CS11_ID);

  const uint8_t valid[8] = {0xA0u, 1u, 2u, 3u, 4u, 5u, 6u, 0xF8u};
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(valid, sizeof(valid))));
  SerialNumberInfo serial{};
  TEST_ASSERT_TRUE(driver.readSerialNumber(serial).ok());
  TEST_ASSERT_TRUE(serial.productIdOk);
  TEST_ASSERT_TRUE(serial.crcOk);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(valid, serial.bytes, sizeof(valid));

  uint8_t wrongProduct[8] = {0xA1u, 1u, 2u, 3u, 4u, 5u, 6u, 0u};
  TEST_ASSERT_TRUE(
      fake.queueTransfer(randomReadOk(wrongProduct, sizeof(wrongProduct))));
  Status status = driver.readSerialNumber(serial);
  assertStatus(Err::PART_MISMATCH, status);
  TEST_ASSERT_EQUAL_INT32(0xA1, status.detail);
  TEST_ASSERT_FALSE(serial.productIdOk);
  TEST_ASSERT_FALSE(serial.crcOk);

  uint8_t wrongCrc[8] = {0xA0u, 1u, 2u, 3u, 4u, 5u, 6u, 0u};
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(wrongCrc, sizeof(wrongCrc))));
  status = driver.readSerialNumber(serial);
  assertStatus(Err::CRC_MISMATCH, status);
  TEST_ASSERT_EQUAL_INT32(0xF800, status.detail);
  TEST_ASSERT_TRUE(serial.productIdOk);
  TEST_ASSERT_FALSE(serial.crcOk);
}

void test_scalar_outputs_initialize_before_all_failures() {
  Driver unbound;
  uint32_t manufacturerId = 0xFFFFFFFFu;
  assertStatus(Err::NOT_BOUND,
               unbound.readManufacturerId(manufacturerId));
  TEST_ASSERT_EQUAL_UINT32(0, manufacturerId);
  SerialNumberInfo serial{};
  for (uint8_t& byte : serial.bytes) {
    byte = 0xFFu;
  }
  serial.productIdOk = true;
  serial.crcOk = true;
  assertStatus(Err::NOT_BOUND, unbound.readSerialNumber(serial));
  const uint8_t zeroSerial[8] = {};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(zeroSerial, serial.bytes, sizeof(zeroSerial));
  TEST_ASSERT_FALSE(serial.productIdOk);
  TEST_ASSERT_FALSE(serial.crcOk);

  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  TEST_ASSERT_TRUE(driver.bind(bus, Config{}).ok());
  manufacturerId = 0x123456u;
  assertStatus(Err::NOT_INITIALIZED,
               driver.readManufacturerId(manufacturerId));
  TEST_ASSERT_EQUAL_UINT32(0, manufacturerId);
  for (uint8_t& byte : serial.bytes) {
    byte = 0x7Eu;
  }
  serial.productIdOk = true;
  serial.crcOk = true;
  assertStatus(Err::NOT_INITIALIZED, driver.readSerialNumber(serial));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(zeroSerial, serial.bytes, sizeof(zeroSerial));
  TEST_ASSERT_FALSE(serial.productIdOk);
  TEST_ASSERT_FALSE(serial.crcOk);

  queueInitialize(fake, CS11_ID);
  TEST_ASSERT_TRUE(driver.initialize().ok());
  const uint8_t rawManufacturerBytes[3] = {0x00u, 0xD3u, 0x87u};
  TEST_ASSERT_TRUE(
      fake.queueTransfer(directReadOk(rawManufacturerBytes,
                                     sizeof(rawManufacturerBytes))));
  manufacturerId = 0;
  TEST_ASSERT_TRUE(driver.readManufacturerId(manufacturerId).ok());
  TEST_ASSERT_EQUAL_UINT32(0x00D387u, manufacturerId);
  TransferScript failure{};
  failure.result.code = TransportCode::TIMEOUT;
  failure.result.phase = TransferPhase::DATA_READ;
  failure.result.detail = 2201;
  failure.result.firstDeviceAddressAcked = true;
  failure.result.dataBytesTransferred = 2;
  failure.rxLength = 3;
  failure.rxData[0] = 0xAAu;
  failure.rxData[1] = 0xBBu;
  failure.rxData[2] = 0xCCu;
  TEST_ASSERT_TRUE(fake.queueTransfer(failure));
  manufacturerId = 0xABCDEFu;
  assertStatus(Err::TRANSPORT_TIMEOUT,
               driver.readManufacturerId(manufacturerId));
  TEST_ASSERT_EQUAL_UINT32(0, manufacturerId);

  TransferScript serialFailure = randomReadFailure(
      TransportCode::IO_ERROR, TransferPhase::DATA_READ, 2202, 4);
  serialFailure.rxLength = 8;
  for (uint8_t& byte : serialFailure.rxData) {
    byte = 0xDDu;
  }
  TEST_ASSERT_TRUE(fake.queueTransfer(serialFailure));
  for (uint8_t& byte : serial.bytes) {
    byte = 0x44u;
  }
  serial.productIdOk = true;
  serial.crcOk = true;
  assertStatus(Err::IO_ERROR, driver.readSerialNumber(serial));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(zeroSerial, serial.bytes, sizeof(zeroSerial));
  TEST_ASSERT_FALSE(serial.productIdOk);
  TEST_ASSERT_FALSE(serial.crcOk);
}

void test_composite_operations_update_health_once() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  Config config{};
  config.expectedPart = PartType::AT21CS01;
  config.startupSpeed = SpeedMode::STANDARD_SPEED;
  queueInitialize(fake, CS01_ID, true);
  TEST_ASSERT_TRUE(driver.begin(bus, config).ok());
  TEST_ASSERT_EQUAL_UINT32(1, driver.snapshot().totalSuccess);

  TEST_ASSERT_TRUE(fake.queueTransfer(manufacturerIdOk(CS01_ID)));
  TEST_ASSERT_TRUE(driver.probe().ok());
  TEST_ASSERT_EQUAL_UINT32(2, driver.snapshot().totalSuccess);

  const uint8_t valid[8] = {0xA0u, 1u, 2u, 3u, 4u, 5u, 6u, 0xF8u};
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(valid, sizeof(valid))));
  SerialNumberInfo serial{};
  TEST_ASSERT_TRUE(driver.readSerialNumber(serial).ok());
  TEST_ASSERT_EQUAL_UINT32(3, driver.snapshot().totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(0, driver.snapshot().totalFailures);

  uint8_t wrongProduct[8] = {0xA1u, 1u, 2u, 3u,
                             4u,    5u, 6u, 0u};
  TEST_ASSERT_TRUE(
      fake.queueTransfer(randomReadOk(wrongProduct, sizeof(wrongProduct))));
  const uint32_t successesBeforeFailure = driver.snapshot().totalSuccess;
  const uint32_t failuresBeforeFailure = driver.snapshot().totalFailures;
  assertStatus(Err::PART_MISMATCH, driver.readSerialNumber(serial));
  TEST_ASSERT_EQUAL_UINT32(successesBeforeFailure,
                           driver.snapshot().totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(failuresBeforeFailure + 1u,
                           driver.snapshot().totalFailures);
  TEST_ASSERT_EQUAL_UINT32(1,
                           fake.eventCountFor(FakeEventKind::RESET_DISCOVER));
}

void test_health_saturates_and_last_error_persists_across_success() {
  ScriptedTransport fake;
  Bus bus;
  bindBus(bus, fake);
  Driver driver;
  Config config{};
  config.offlineThreshold = 0;
  initializeDriver(driver, bus, fake, config, CS11_ID);
  TestAccess::seedDriverHealth(
      driver, std::numeric_limits<uint8_t>::max(),
      std::numeric_limits<uint32_t>::max(),
      std::numeric_limits<uint32_t>::max());

  TransferScript failure = randomReadFailure(
      TransportCode::IO_ERROR, TransferPhase::START, 2401);
  TEST_ASSERT_TRUE(fake.queueTransfer(failure));
  uint8_t value = 0;
  assertStatus(Err::IO_ERROR, driver.readEeprom(0, &value, 1));
  SettingsSnapshot snapshot = driver.snapshot();
  TEST_ASSERT_EQUAL_UINT8(std::numeric_limits<uint8_t>::max(),
                          snapshot.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(std::numeric_limits<uint32_t>::max(),
                           snapshot.totalFailures);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(snapshot.state));
  TEST_ASSERT_EQUAL_INT32(2401, snapshot.lastErrorDetail);

  const uint8_t expected = 0x5Cu;
  TEST_ASSERT_TRUE(fake.queueTransfer(randomReadOk(&expected, 1)));
  TEST_ASSERT_TRUE(driver.readEeprom(0, &value, 1).ok());
  snapshot = driver.snapshot();
  TEST_ASSERT_EQUAL_UINT8(0, snapshot.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(std::numeric_limits<uint32_t>::max(),
                           snapshot.totalSuccess);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OK),
                          static_cast<uint8_t>(snapshot.lastStatusCode));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::IO_ERROR),
                          static_cast<uint8_t>(snapshot.lastErrorCode));
  TEST_ASSERT_EQUAL_INT32(2401, snapshot.lastErrorDetail);
}
