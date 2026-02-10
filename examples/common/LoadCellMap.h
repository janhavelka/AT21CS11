/**
 * @file LoadCellMap.h
 * @brief Production-style load-cell memory map helpers for AT21CS examples.
 *
 * This file is example/application glue, not library API.
 * It keeps all addresses, record layouts, versions, and CRC handling in one place.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "AT21CS/AT21CS.h"

namespace lcmap {

// EEPROM zone bases (4 x 32-byte zones).
static constexpr uint8_t ZONE0_ADDR = 0x00;
static constexpr uint8_t ZONE1_ADDR = 0x20;
static constexpr uint8_t ZONE2_ADDR = 0x40;
static constexpr uint8_t ZONE3_ADDR = 0x60;
static constexpr uint8_t ZONE_SIZE = 0x20;

// Security user area map (0x10..0x1F).
static constexpr uint8_t SECURITY_IDENTITY_ADDR = 0x10;

// Record addresses.
static constexpr uint8_t CALIBRATION_MASTER_ADDR = ZONE0_ADDR;
static constexpr uint8_t CALIBRATION_MIRROR_ADDR = ZONE1_ADDR;
static constexpr uint8_t RUNTIME_ADDR = ZONE2_ADDR;
static constexpr uint8_t COUNTERS_ADDR = ZONE3_ADDR;

// Record identity/version constants.
static constexpr uint16_t SECURITY_IDENTITY_MAGIC = 0x4C49;     // "LI"
static constexpr uint32_t CALIBRATION_MAGIC = 0x4C43414C;       // "LCAL"
static constexpr uint32_t RUNTIME_MAGIC = 0x4C52554E;           // "LRUN"
static constexpr uint32_t COUNTERS_MAGIC = 0x4C434E54;          // "LCNT"
static constexpr uint8_t SECURITY_IDENTITY_VERSION = 1;
static constexpr uint16_t CALIBRATION_VERSION = 1;
static constexpr uint16_t RUNTIME_VERSION = 1;
static constexpr uint16_t COUNTERS_VERSION = 1;

enum class CalibrationSource : uint8_t {
  NONE = 0,
  MASTER,
  MIRROR
};

// 16-byte security user payload at 0x10..0x1F.
struct SecurityIdentityV1 {
  uint16_t magic;
  uint16_t modelId;
  uint32_t moduleSerial;
  uint16_t batchCode;
  uint16_t flags;
  uint8_t version;
  uint8_t hwRevision;
  uint16_t crc16;
};

// 32-byte immutable calibration record (zone 0 / mirror in zone 1).
struct CalibrationBlockV1 {
  uint32_t magic;
  uint16_t version;
  uint16_t flags;
  uint32_t capacityGrams;
  int32_t zeroBalanceRaw;
  int32_t spanRawAtCapacity;
  int32_t sensitivityNvPerV;
  int16_t tempCoeffPpmPerC;
  int16_t linearityPpm;
  uint32_t crc32;
};

// 32-byte mutable runtime state record (zone 2).
struct RuntimeBlockV1 {
  uint32_t magic;
  uint16_t version;
  uint16_t flags;
  uint32_t seq;
  int32_t installTareRaw;
  int32_t userZeroTrimRaw;
  int32_t userSpanTrimPpm;
  uint8_t filterProfile;
  uint8_t diagnosticsMode;
  uint16_t reserved;
  uint32_t crc32;
};

// 32-byte mutable lifecycle counters record (zone 3).
struct CounterBlockV1 {
  uint32_t magic;
  uint16_t version;
  uint16_t flags;
  uint32_t seq;
  uint32_t overloadCount;
  uint32_t overTempCount;
  uint32_t powerCycleCount;
  uint32_t saturationCount;
  uint32_t crc32;
};

static_assert(sizeof(SecurityIdentityV1) == 16, "SecurityIdentityV1 must be 16 bytes");
static_assert(sizeof(CalibrationBlockV1) == 32, "CalibrationBlockV1 must be 32 bytes");
static_assert(sizeof(RuntimeBlockV1) == 32, "RuntimeBlockV1 must be 32 bytes");
static_assert(sizeof(CounterBlockV1) == 32, "CounterBlockV1 must be 32 bytes");

static_assert(CALIBRATION_MASTER_ADDR + sizeof(CalibrationBlockV1) <= ZONE1_ADDR,
              "Calibration master must fit in zone 0");
static_assert(CALIBRATION_MIRROR_ADDR + sizeof(CalibrationBlockV1) <= ZONE2_ADDR,
              "Calibration mirror must fit in zone 1");
static_assert(RUNTIME_ADDR + sizeof(RuntimeBlockV1) <= ZONE3_ADDR,
              "Runtime block must fit in zone 2");
static_assert(COUNTERS_ADDR + sizeof(CounterBlockV1) <= AT21CS::cmd::EEPROM_SIZE,
              "Counter block must fit in zone 3");
static_assert(SECURITY_IDENTITY_ADDR >= AT21CS::cmd::SECURITY_USER_MIN,
              "Security identity must be in user-security range");
static_assert(SECURITY_IDENTITY_ADDR + sizeof(SecurityIdentityV1) <= AT21CS::cmd::SECURITY_SIZE,
              "Security identity must fit in user-security range");

namespace field {
static constexpr uint8_t CAPACITY_GRAMS =
    CALIBRATION_MASTER_ADDR + static_cast<uint8_t>(offsetof(CalibrationBlockV1, capacityGrams));
static constexpr uint8_t ZERO_BALANCE_RAW =
    CALIBRATION_MASTER_ADDR + static_cast<uint8_t>(offsetof(CalibrationBlockV1, zeroBalanceRaw));
static constexpr uint8_t SPAN_RAW_AT_CAPACITY = CALIBRATION_MASTER_ADDR +
                                                static_cast<uint8_t>(
                                                    offsetof(CalibrationBlockV1, spanRawAtCapacity));
static constexpr uint8_t INSTALL_TARE_RAW =
    RUNTIME_ADDR + static_cast<uint8_t>(offsetof(RuntimeBlockV1, installTareRaw));
static constexpr uint8_t OVERLOAD_COUNT =
    COUNTERS_ADDR + static_cast<uint8_t>(offsetof(CounterBlockV1, overloadCount));
}  // namespace field

inline uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint32_t>(data[i]);
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((crc & 1u) != 0u) {
        crc = (crc >> 1u) ^ 0xEDB88320u;
      } else {
        crc >>= 1u;
      }
    }
  }
  return ~crc;
}

inline uint16_t crc16Ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8u;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((crc & 0x8000u) != 0u) {
        crc = static_cast<uint16_t>((crc << 1u) ^ 0x1021u);
      } else {
        crc = static_cast<uint16_t>(crc << 1u);
      }
    }
  }
  return crc;
}

template <typename T>
inline uint32_t recordCrc32(const T& record) {
  static_assert(sizeof(T) >= sizeof(uint32_t), "Record too small for crc32 footer");
  return crc32(reinterpret_cast<const uint8_t*>(&record), sizeof(T) - sizeof(uint32_t));
}

inline AT21CS::Status writeEepromBytesPaged(AT21CS::Driver& driver, uint8_t address,
                                            const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0 || len > AT21CS::cmd::EEPROM_SIZE) {
    return AT21CS::Status::Error(AT21CS::Err::INVALID_PARAM, "Invalid EEPROM write buffer/length");
  }
  const uint16_t end = static_cast<uint16_t>(address) + static_cast<uint16_t>(len);
  if (end > AT21CS::cmd::EEPROM_SIZE) {
    return AT21CS::Status::Error(AT21CS::Err::INVALID_PARAM, "EEPROM write range out of bounds");
  }

  size_t offset = 0;
  while (offset < len) {
    const uint16_t currentAddress = static_cast<uint16_t>(address) + static_cast<uint16_t>(offset);
    const size_t remaining = len - offset;
    const uint8_t pageOffset = static_cast<uint8_t>(currentAddress % AT21CS::cmd::PAGE_SIZE);
    size_t chunk = static_cast<size_t>(AT21CS::cmd::PAGE_SIZE - pageOffset);
    if (chunk > remaining) {
      chunk = remaining;
    }

    const AT21CS::Status st =
        driver.writeEepromPage(static_cast<uint8_t>(currentAddress), &data[offset], chunk);
    if (!st.ok()) {
      return st;
    }
    offset += chunk;
  }

  return AT21CS::Status::Ok();
}

inline AT21CS::Status writeSecurityUserBytesPaged(AT21CS::Driver& driver, uint8_t address,
                                                  const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0 || len > AT21CS::cmd::SECURITY_SIZE) {
    return AT21CS::Status::Error(AT21CS::Err::INVALID_PARAM,
                                 "Invalid security write buffer/length");
  }
  if (address < AT21CS::cmd::SECURITY_USER_MIN || address > AT21CS::cmd::SECURITY_USER_MAX) {
    return AT21CS::Status::Error(AT21CS::Err::INVALID_PARAM, "Security write address out of range");
  }
  const uint16_t end = static_cast<uint16_t>(address) + static_cast<uint16_t>(len);
  if (end > static_cast<uint16_t>(AT21CS::cmd::SECURITY_USER_MAX) + 1u) {
    return AT21CS::Status::Error(AT21CS::Err::INVALID_PARAM,
                                 "Security write range exceeds user area");
  }

  size_t offset = 0;
  while (offset < len) {
    const uint16_t currentAddress = static_cast<uint16_t>(address) + static_cast<uint16_t>(offset);
    const size_t remaining = len - offset;
    const uint8_t pageOffset = static_cast<uint8_t>(currentAddress % AT21CS::cmd::PAGE_SIZE);
    size_t chunk = static_cast<size_t>(AT21CS::cmd::PAGE_SIZE - pageOffset);
    if (chunk > remaining) {
      chunk = remaining;
    }

    const AT21CS::Status st =
        driver.writeSecurityUserPage(static_cast<uint8_t>(currentAddress), &data[offset], chunk);
    if (!st.ok()) {
      return st;
    }
    offset += chunk;
  }

  return AT21CS::Status::Ok();
}

template <typename T>
inline AT21CS::Status readPodEeprom(AT21CS::Driver& driver, uint8_t address, T& value) {
  static_assert(std::is_trivially_copyable<T>::value,
                "readPodEeprom requires trivially copyable type");
  return driver.readEeprom(address, reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

template <typename T>
inline AT21CS::Status writePodEeprom(AT21CS::Driver& driver, uint8_t address, const T& value) {
  static_assert(std::is_trivially_copyable<T>::value,
                "writePodEeprom requires trivially copyable type");
  return writeEepromBytesPaged(driver, address, reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

inline AT21CS::Status readFloat32(AT21CS::Driver& driver, uint8_t address, float& value) {
  static_assert(sizeof(float) == 4, "This helper assumes IEEE-754 32-bit float");
  return readPodEeprom(driver, address, value);
}

inline AT21CS::Status writeFloat32(AT21CS::Driver& driver, uint8_t address, float value) {
  static_assert(sizeof(float) == 4, "This helper assumes IEEE-754 32-bit float");
  return writePodEeprom(driver, address, value);
}

inline void seal(SecurityIdentityV1& record) {
  record.magic = SECURITY_IDENTITY_MAGIC;
  record.version = SECURITY_IDENTITY_VERSION;
  record.crc16 =
      crc16Ccitt(reinterpret_cast<const uint8_t*>(&record), sizeof(record) - sizeof(record.crc16));
}

inline bool isValid(const SecurityIdentityV1& record) {
  if (record.magic != SECURITY_IDENTITY_MAGIC || record.version != SECURITY_IDENTITY_VERSION) {
    return false;
  }
  const uint16_t expected =
      crc16Ccitt(reinterpret_cast<const uint8_t*>(&record), sizeof(record) - sizeof(record.crc16));
  return expected == record.crc16;
}

inline void seal(CalibrationBlockV1& record) {
  record.magic = CALIBRATION_MAGIC;
  record.version = CALIBRATION_VERSION;
  record.crc32 = recordCrc32(record);
}

inline bool isValid(const CalibrationBlockV1& record) {
  if (record.magic != CALIBRATION_MAGIC || record.version != CALIBRATION_VERSION) {
    return false;
  }
  return recordCrc32(record) == record.crc32;
}

inline void seal(RuntimeBlockV1& record) {
  record.magic = RUNTIME_MAGIC;
  record.version = RUNTIME_VERSION;
  record.crc32 = recordCrc32(record);
}

inline bool isValid(const RuntimeBlockV1& record) {
  if (record.magic != RUNTIME_MAGIC || record.version != RUNTIME_VERSION) {
    return false;
  }
  return recordCrc32(record) == record.crc32;
}

inline void seal(CounterBlockV1& record) {
  record.magic = COUNTERS_MAGIC;
  record.version = COUNTERS_VERSION;
  record.crc32 = recordCrc32(record);
}

inline bool isValid(const CounterBlockV1& record) {
  if (record.magic != COUNTERS_MAGIC || record.version != COUNTERS_VERSION) {
    return false;
  }
  return recordCrc32(record) == record.crc32;
}

inline AT21CS::Status writeSecurityIdentity(AT21CS::Driver& driver, SecurityIdentityV1 record) {
  seal(record);
  return writeSecurityUserBytesPaged(driver, SECURITY_IDENTITY_ADDR,
                                     reinterpret_cast<const uint8_t*>(&record), sizeof(record));
}

inline AT21CS::Status readSecurityIdentity(AT21CS::Driver& driver, SecurityIdentityV1& record,
                                           bool& valid) {
  valid = false;
  const AT21CS::Status st = driver.readSecurity(
      SECURITY_IDENTITY_ADDR, reinterpret_cast<uint8_t*>(&record), sizeof(record));
  if (!st.ok()) {
    return st;
  }
  valid = isValid(record);
  return st;
}

inline AT21CS::Status writeCalibrationMaster(AT21CS::Driver& driver, CalibrationBlockV1 record) {
  seal(record);
  return writeEepromBytesPaged(driver, CALIBRATION_MASTER_ADDR,
                               reinterpret_cast<const uint8_t*>(&record), sizeof(record));
}

inline AT21CS::Status writeCalibrationMirror(AT21CS::Driver& driver, CalibrationBlockV1 record) {
  seal(record);
  return writeEepromBytesPaged(driver, CALIBRATION_MIRROR_ADDR,
                               reinterpret_cast<const uint8_t*>(&record), sizeof(record));
}

inline AT21CS::Status writeCalibrationBoth(AT21CS::Driver& driver, CalibrationBlockV1 record) {
  seal(record);
  AT21CS::Status st = writeEepromBytesPaged(driver, CALIBRATION_MASTER_ADDR,
                                            reinterpret_cast<const uint8_t*>(&record),
                                            sizeof(record));
  if (!st.ok()) {
    return st;
  }
  st = writeEepromBytesPaged(driver, CALIBRATION_MIRROR_ADDR,
                             reinterpret_cast<const uint8_t*>(&record), sizeof(record));
  return st;
}

inline AT21CS::Status readCalibrationMaster(AT21CS::Driver& driver, CalibrationBlockV1& record,
                                            bool& valid) {
  valid = false;
  const AT21CS::Status st =
      driver.readEeprom(CALIBRATION_MASTER_ADDR, reinterpret_cast<uint8_t*>(&record), sizeof(record));
  if (!st.ok()) {
    return st;
  }
  valid = isValid(record);
  return st;
}

inline AT21CS::Status readCalibrationMirror(AT21CS::Driver& driver, CalibrationBlockV1& record,
                                            bool& valid) {
  valid = false;
  const AT21CS::Status st =
      driver.readEeprom(CALIBRATION_MIRROR_ADDR, reinterpret_cast<uint8_t*>(&record), sizeof(record));
  if (!st.ok()) {
    return st;
  }
  valid = isValid(record);
  return st;
}

inline AT21CS::Status readCalibrationBest(AT21CS::Driver& driver, CalibrationBlockV1& record,
                                          CalibrationSource& source, bool& valid) {
  source = CalibrationSource::NONE;
  valid = false;

  CalibrationBlockV1 master{};
  bool masterValid = false;
  AT21CS::Status st = readCalibrationMaster(driver, master, masterValid);
  if (!st.ok()) {
    return st;
  }
  if (masterValid) {
    record = master;
    source = CalibrationSource::MASTER;
    valid = true;
    return st;
  }

  CalibrationBlockV1 mirror{};
  bool mirrorValid = false;
  st = readCalibrationMirror(driver, mirror, mirrorValid);
  if (!st.ok()) {
    return st;
  }
  if (mirrorValid) {
    record = mirror;
    source = CalibrationSource::MIRROR;
    valid = true;
    return st;
  }

  return AT21CS::Status::Error(AT21CS::Err::CRC_MISMATCH,
                               "Calibration CRC invalid in master and mirror");
}

inline AT21CS::Status writeRuntime(AT21CS::Driver& driver, RuntimeBlockV1 record) {
  seal(record);
  return writeEepromBytesPaged(driver, RUNTIME_ADDR, reinterpret_cast<const uint8_t*>(&record),
                               sizeof(record));
}

inline AT21CS::Status readRuntime(AT21CS::Driver& driver, RuntimeBlockV1& record, bool& valid) {
  valid = false;
  const AT21CS::Status st = driver.readEeprom(RUNTIME_ADDR, reinterpret_cast<uint8_t*>(&record),
                                               sizeof(record));
  if (!st.ok()) {
    return st;
  }
  valid = isValid(record);
  return st;
}

inline AT21CS::Status writeCounters(AT21CS::Driver& driver, CounterBlockV1 record) {
  seal(record);
  return writeEepromBytesPaged(driver, COUNTERS_ADDR, reinterpret_cast<const uint8_t*>(&record),
                               sizeof(record));
}

inline AT21CS::Status readCounters(AT21CS::Driver& driver, CounterBlockV1& record, bool& valid) {
  valid = false;
  const AT21CS::Status st = driver.readEeprom(COUNTERS_ADDR, reinterpret_cast<uint8_t*>(&record),
                                               sizeof(record));
  if (!st.ok()) {
    return st;
  }
  valid = isValid(record);
  return st;
}

}  // namespace lcmap
