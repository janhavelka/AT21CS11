#include <cstddef>
#include <cstdint>
#include <limits>

#include <unity.h>

#include "AT21CS/AT21CS.h"
#include "support/ExpectedFrames.h"
#include "support/TestBuilders.h"

using namespace AT21CS;
using namespace AT21CS::test;

namespace {

constexpr uint32_t CS11_ID = 0x00D385u;

TransferScript randomReadSuccess(const ExpectedTransfer& expectedTransfer,
                                 const uint8_t* data,
                                 size_t length) {
  TransferScript script = randomReadOk(data, length);
  script.expected = expectedTransfer;
  return script;
}

TransferScript writeSuccess(const ExpectedTransfer& expectedTransfer,
                            size_t length) {
  TransferScript script{};
  script.expected = expectedTransfer;
  script.result.code = TransportCode::OK;
  script.result.phase = TransferPhase::STOP;
  script.result.dataBytesTransferred = length;
  script.result.firstDeviceAddressAcked = true;
  script.result.memoryAddressAcked = true;
  script.result.stopCompleted = true;
  return script;
}

TransferScript startFailure(const ExpectedTransfer& expectedTransfer,
                            TransportCode code,
                            int32_t detail) {
  TransferScript script{};
  script.expected = expectedTransfer;
  script.result.code = code;
  script.result.phase = TransferPhase::START;
  script.result.detail = detail;
  return script;
}

WaitScript successfulHold() {
  WaitScript script{};
  script.result = auxiliaryOk(TransferPhase::WAIT_HIGH);
  script.advanceToDeadline = true;
  return script;
}

void queueSecurityRead(ScriptedTransport& fake,
                       uint8_t address,
                       const uint8_t* expectedData,
                       size_t length) {
  size_t offset = 0;
  while (offset < length) {
    const size_t remaining = length - offset;
    const size_t chunk = remaining < 8u ? remaining : 8u;
    const uint8_t frameAddress =
        static_cast<uint8_t>(static_cast<size_t>(address) + offset);
    TEST_ASSERT_TRUE(fake.queueTransfer(randomReadSuccess(
        expected::randomRead(0xB0u, frameAddress, 0xB1u, chunk,
                             SpeedMode::HIGH_SPEED, 160u),
        expectedData + offset, chunk)));
    offset += chunk;
  }
}

void queueSecurityWrite(ScriptedTransport& fake,
                        uint8_t address,
                        const uint8_t* data,
                        size_t length) {
  size_t offset = 0;
  while (offset < length) {
    const size_t absolute = static_cast<size_t>(address) + offset;
    const size_t pageRemaining = 8u - (absolute % 8u);
    const size_t remaining = length - offset;
    const size_t chunk = remaining < pageRemaining ? remaining : pageRemaining;
    TEST_ASSERT_TRUE(fake.queueTransfer(writeSuccess(
        expected::pageWrite(0xB0u, static_cast<uint8_t>(absolute),
                            data + offset, chunk, SpeedMode::HIGH_SPEED, 160u),
        chunk)));
    TEST_ASSERT_TRUE(fake.queueWait(successfulHold()));
    offset += chunk;
  }
}

Err mappedError(TransportCode code) {
  if (code == TransportCode::TIMEOUT) {
    return Err::TRANSPORT_TIMEOUT;
  }
  if (code == TransportCode::LINE_STUCK) {
    return Err::LINE_STUCK;
  }
  return Err::IO_ERROR;
}

void assertOneTrackedFailure(const Driver& driver,
                             const SettingsSnapshot& before,
                             Err expectedError,
                             int32_t detail) {
  const SettingsSnapshot after = driver.snapshot();
  TEST_ASSERT_EQUAL_UINT32(before.totalSuccess, after.totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(before.totalFailures + 1u, after.totalFailures);
  TEST_ASSERT_EQUAL_UINT8(before.consecutiveFailures + 1u,
                          after.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(after.state));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expectedError),
                          static_cast<uint8_t>(after.lastStatusCode));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expectedError),
                          static_cast<uint8_t>(after.lastErrorCode));
  TEST_ASSERT_EQUAL_INT32(detail, after.lastStatusDetail);
  TEST_ASSERT_EQUAL_INT32(detail, after.lastErrorDetail);
}

}  // namespace

void test_security_length_and_address_boundary_matrix_is_complete() {
  static constexpr size_t LENGTHS[] = {
      0u, 1u, 2u, 7u, 8u, 9u, 15u, 16u, 31u, 32u, 127u, 128u,
      129u, 0x10000u, std::numeric_limits<size_t>::max()};
  for (const size_t length : LENGTHS) {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    uint8_t output[32] = {};
    uint8_t expectedData[32] = {};
    for (size_t index = 0; index < sizeof(expectedData); ++index) {
      expectedData[index] = static_cast<uint8_t>(0x40u + index);
    }
    const size_t transfers = fake.transferCalls;
    if (length != 0u && length <= 32u) {
      queueSecurityRead(fake, 0u, expectedData, length);
      TEST_ASSERT_TRUE(driver.readSecurity(0u, output, length).ok());
      TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedData, output, length);
    } else {
      output[0] = 0xA5u;
      assertStatus(Err::INVALID_PARAM,
                   driver.readSecurity(0u, output, length));
      TEST_ASSERT_EQUAL_HEX8(0xA5u, output[0]);
      TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);
    }
    assertOracleClean(fake);
  }

  static constexpr uint8_t ADDRESSES[] = {
      0x00u, 0x0Fu, 0x10u, 0x17u, 0x18u, 0x1Eu, 0x1Fu, 0x20u};
  for (const uint8_t address : ADDRESSES) {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    uint8_t output = 0xA5u;
    const size_t transfers = fake.transferCalls;
    if (address <= 0x1Fu) {
      const uint8_t expectedByte = static_cast<uint8_t>(address ^ 0x5Au);
      queueSecurityRead(fake, address, &expectedByte, 1u);
      TEST_ASSERT_TRUE(driver.readSecurity(address, &output, 1u).ok());
      TEST_ASSERT_EQUAL_HEX8(expectedByte, output);
    } else {
      assertStatus(Err::INVALID_PARAM,
                   driver.readSecurity(address, &output, 1u));
      TEST_ASSERT_EQUAL_HEX8(0xA5u, output);
      TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);
    }
    assertOracleClean(fake);
  }

  for (const size_t length : LENGTHS) {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    uint8_t data[16] = {};
    WriteResult result{9u, 9u, WriteEffect::COMMITTED};
    const size_t transfers = fake.transferCalls;
    if (length != 0u && length <= 16u) {
      queueSecurityWrite(fake, 0x10u, data, length);
      TEST_ASSERT_TRUE(
          driver.writeSecurityUser(0x10u, data, length, result).ok());
      TEST_ASSERT_EQUAL_UINT32(length, result.bytesCommitted);
    } else {
      assertStatus(Err::INVALID_PARAM,
                   driver.writeSecurityUser(0x10u, data, length, result));
      TEST_ASSERT_EQUAL_UINT32(0u, result.bytesCommitted);
      TEST_ASSERT_EQUAL_UINT8(
          static_cast<uint8_t>(WriteEffect::NOT_ATTEMPTED),
          static_cast<uint8_t>(result.lastPageEffect));
      TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);
    }
    assertOracleClean(fake);
  }

  for (const size_t length : LENGTHS) {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    uint8_t data[8] = {};
    WriteResult result{9u, 9u, WriteEffect::COMMITTED};
    const size_t transfers = fake.transferCalls;
    if (length != 0u && length <= sizeof(data)) {
      queueSecurityWrite(fake, 0x10u, data, length);
      TEST_ASSERT_TRUE(driver.writeSecurityUserPage(
          0x10u, data, length, result).ok());
      TEST_ASSERT_EQUAL_UINT32(length, result.bytesCommitted);
    } else {
      assertStatus(Err::INVALID_PARAM,
                   driver.writeSecurityUserPage(
                       0x10u, data, length, result));
      TEST_ASSERT_EQUAL_UINT32(0u, result.bytesCommitted);
      TEST_ASSERT_EQUAL_UINT8(
          static_cast<uint8_t>(WriteEffect::NOT_ATTEMPTED),
          static_cast<uint8_t>(result.lastPageEffect));
      TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);
    }
    assertOracleClean(fake);
  }

  for (uint8_t operation = 0u; operation < 2u; ++operation) {
    for (const uint8_t address : ADDRESSES) {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      Driver driver;
      initializeDriver(driver, bus, fake, Config{}, CS11_ID);
      const uint8_t data = 0x6Cu;
      WriteResult result{9u, 9u, WriteEffect::COMMITTED};
      const size_t transfers = fake.transferCalls;
      Status status{};
      if (address >= 0x10u && address <= 0x1Fu) {
        queueSecurityWrite(fake, address, &data, 1u);
        status = operation == 0u
                     ? driver.writeSecurityUserPage(
                           address, &data, 1u, result)
                     : driver.writeSecurityUser(
                           address, &data, 1u, result);
        TEST_ASSERT_TRUE(status.ok());
        TEST_ASSERT_EQUAL_UINT32(1u, result.bytesCommitted);
      } else {
        status = operation == 0u
                     ? driver.writeSecurityUserPage(
                           address, &data, 1u, result)
                     : driver.writeSecurityUser(
                           address, &data, 1u, result);
        assertStatus(Err::INVALID_PARAM, status);
        TEST_ASSERT_EQUAL_UINT32(0u, result.bytesCommitted);
        TEST_ASSERT_EQUAL_UINT32(transfers, fake.transferCalls);
      }
      assertOracleClean(fake);
    }
  }
}

void test_security_public_api_transport_matrix_tracks_once() {
  static constexpr TransportCode CODES[] = {
      TransportCode::TIMEOUT, TransportCode::LINE_STUCK,
      TransportCode::IO_ERROR};
  for (size_t codeIndex = 0; codeIndex < 3u; ++codeIndex) {
    const TransportCode code = CODES[codeIndex];
    const Err error = mappedError(code);
    const int32_t baseDetail = static_cast<int32_t>(7200u + codeIndex * 10u);
    {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      Driver driver;
      initializeDriver(driver, bus, fake, Config{}, CS11_ID);
      const SettingsSnapshot before = driver.snapshot();
      TEST_ASSERT_TRUE(fake.queueTransfer(startFailure(
          expected::randomRead(0xB0u, 0u, 0xB1u, 1u,
                               SpeedMode::HIGH_SPEED, 160u),
          code, baseDetail)));
      uint8_t output = 0xA5u;
      assertStatus(error, driver.readSecurity(0u, &output, 1u));
      TEST_ASSERT_EQUAL_HEX8(0xA5u, output);
      assertOneTrackedFailure(driver, before, error, baseDetail);
    }
    {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      Driver driver;
      initializeDriver(driver, bus, fake, Config{}, CS11_ID);
      const uint8_t data = 0x11u;
      const SettingsSnapshot before = driver.snapshot();
      TEST_ASSERT_TRUE(fake.queueTransfer(startFailure(
          expected::pageWrite(0xB0u, 0x10u, &data, 1u,
                              SpeedMode::HIGH_SPEED, 160u),
          code, baseDetail + 1)));
      WriteResult result{9u, 9u, WriteEffect::COMMITTED};
      assertStatus(error, driver.writeSecurityUserPage(
                              0x10u, &data, 1u, result));
      TEST_ASSERT_EQUAL_UINT8(
          static_cast<uint8_t>(WriteEffect::NOT_ATTEMPTED),
          static_cast<uint8_t>(result.lastPageEffect));
      assertOneTrackedFailure(driver, before, error, baseDetail + 1);
    }
    {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      Driver driver;
      initializeDriver(driver, bus, fake, Config{}, CS11_ID);
      const uint8_t data = 0x12u;
      const SettingsSnapshot before = driver.snapshot();
      TEST_ASSERT_TRUE(fake.queueTransfer(startFailure(
          expected::pageWrite(0xB0u, 0x10u, &data, 1u,
                              SpeedMode::HIGH_SPEED, 160u),
          code, baseDetail + 2)));
      WriteResult result{};
      assertStatus(error,
                   driver.writeSecurityUser(0x10u, &data, 1u, result));
      assertOneTrackedFailure(driver, before, error, baseDetail + 2);
    }
    {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      Driver driver;
      initializeDriver(driver, bus, fake, Config{}, CS11_ID);
      const SettingsSnapshot before = driver.snapshot();
      ExpectedTransfer lockCheck = expected::addressOnly(
          0x20u, SpeedMode::HIGH_SPEED, 160u);
      lockCheck.hasMemoryAddress = true;
      lockCheck.memoryAddress = 0x60u;
      TEST_ASSERT_TRUE(fake.queueTransfer(
          startFailure(lockCheck, code, baseDetail + 3)));
      bool locked = true;
      assertStatus(error, driver.readSecurityLockState(locked));
      TEST_ASSERT_FALSE(locked);
      assertOneTrackedFailure(driver, before, error, baseDetail + 3);
    }
    {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      Driver driver;
      initializeDriver(driver, bus, fake, Config{}, CS11_ID);
      const SettingsSnapshot before = driver.snapshot();
      ExpectedTransfer lockCheck = expected::addressOnly(
          0x20u, SpeedMode::HIGH_SPEED, 160u);
      lockCheck.hasMemoryAddress = true;
      lockCheck.memoryAddress = 0x60u;
      TEST_ASSERT_TRUE(fake.queueTransfer(
          startFailure(lockCheck, code, baseDetail + 4)));
      MutationResult result{MutationEffect::VERIFIED, true};
      assertStatus(error, driver.permanentlyLockSecurity(result));
      TEST_ASSERT_EQUAL_UINT8(
          static_cast<uint8_t>(MutationEffect::NOT_ATTEMPTED),
          static_cast<uint8_t>(result.effect));
      TEST_ASSERT_FALSE(result.alreadyApplied);
      assertOneTrackedFailure(driver, before, error, baseDetail + 4);
    }
  }
}

void test_security_read_and_write_nack_phases_are_exact() {
  static constexpr TransferPhase READ_PHASES[] = {
      TransferPhase::DEVICE_ADDRESS_WRITE, TransferPhase::MEMORY_ADDRESS,
      TransferPhase::DEVICE_ADDRESS_READ};
  static constexpr Err READ_ERRORS[] = {
      Err::NACK_DEVICE_ADDRESS, Err::NACK_MEMORY_ADDRESS,
      Err::NACK_DEVICE_ADDRESS};
  for (size_t index = 0; index < 3u; ++index) {
    ScriptedTransport fake;
    Bus bus;
    bindBus(bus, fake);
    Driver driver;
    initializeDriver(driver, bus, fake, Config{}, CS11_ID);
    TransferScript script{};
    script.expected = expected::randomRead(
        0xB0u, 0x1Fu, 0xB1u, 1u, SpeedMode::HIGH_SPEED, 160u);
    script.result.code = TransportCode::NACK;
    script.result.phase = READ_PHASES[index];
    script.result.firstDeviceAddressAcked = index > 0u;
    script.result.memoryAddressAcked = index > 1u;
    TEST_ASSERT_TRUE(fake.queueTransfer(script));
    uint8_t value = 0xA5u;
    const SettingsSnapshot before = driver.snapshot();
    const Status status = driver.readSecurity(0x1Fu, &value, 1u);
    assertStatus(READ_ERRORS[index], status);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, value);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                            static_cast<uint8_t>(driver.state()));
    const SettingsSnapshot after = driver.snapshot();
    TEST_ASSERT_EQUAL_UINT32(before.totalSuccess, after.totalSuccess);
    TEST_ASSERT_EQUAL_UINT32(before.totalFailures + 1u,
                             after.totalFailures);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(READ_ERRORS[index]),
                            static_cast<uint8_t>(after.lastErrorCode));
    assertOracleClean(fake);
  }

  static constexpr TransferPhase WRITE_PHASES[] = {
      TransferPhase::DEVICE_ADDRESS_WRITE, TransferPhase::MEMORY_ADDRESS,
      TransferPhase::DATA_WRITE};
  static constexpr Err WRITE_ERRORS[] = {
      Err::NACK_DEVICE_ADDRESS, Err::NACK_MEMORY_ADDRESS, Err::NACK_DATA};
  for (uint8_t operation = 0u; operation < 2u; ++operation) {
    for (size_t index = 0; index < 3u; ++index) {
      ScriptedTransport fake;
      Bus bus;
      bindBus(bus, fake);
      Driver driver;
      initializeDriver(driver, bus, fake, Config{}, CS11_ID);
      const uint8_t data = 0xA7u;
      TransferScript script{};
      script.expected = expected::pageWrite(
          0xB0u, 0x10u, &data, 1u, SpeedMode::HIGH_SPEED, 160u);
      script.result.code = TransportCode::NACK;
      script.result.phase = WRITE_PHASES[index];
      script.result.firstDeviceAddressAcked = index > 0u;
      script.result.memoryAddressAcked = index > 1u;
      TEST_ASSERT_TRUE(fake.queueTransfer(script));
      WriteResult result{};
      const SettingsSnapshot before = driver.snapshot();
      const Status status = operation == 0u
                                ? driver.writeSecurityUserPage(
                                      0x10u, &data, 1u, result)
                                : driver.writeSecurityUser(
                                      0x10u, &data, 1u, result);
      assertStatus(WRITE_ERRORS[index], status);
      TEST_ASSERT_EQUAL_UINT8(
          static_cast<uint8_t>(WriteEffect::NOT_ATTEMPTED),
          static_cast<uint8_t>(result.lastPageEffect));
      TEST_ASSERT_EQUAL_UINT32(0u, fake.waitCalls);
      const SettingsSnapshot after = driver.snapshot();
      TEST_ASSERT_EQUAL_UINT32(before.totalSuccess, after.totalSuccess);
      TEST_ASSERT_EQUAL_UINT32(before.totalFailures + 1u,
                               after.totalFailures);
      TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WRITE_ERRORS[index]),
                              static_cast<uint8_t>(after.lastErrorCode));
      assertOracleClean(fake);
    }
  }
}
