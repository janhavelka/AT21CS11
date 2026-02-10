/// @file AT21CS.h
/// @brief Main AT21CS01/AT21CS11 single-wire EEPROM driver.
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif

#include "AT21CS/CommandTable.h"
#include "AT21CS/Config.h"
#include "AT21CS/Status.h"
#include "AT21CS/Version.h"

namespace AT21CS {

/// Driver runtime state machine.
///
/// Transition overview:
/// - UNINIT -> PROBING -> INIT_CONFIG -> READY during begin()
/// - READY -> BUSY during blocking write-ready polling
/// - Any tracked failure: READY/BUSY/RECOVERING -> DEGRADED or OFFLINE
/// - recover(): DEGRADED/OFFLINE -> RECOVERING -> READY (success path)
/// - Fatal protocol/config mismatch may move to FAULT
enum class DriverState : uint8_t {
  UNINIT = 0,
  PROBING,
  INIT_CONFIG,
  READY,
  BUSY,
  DEGRADED,
  OFFLINE,
  RECOVERING,
  SLEEPING,
  FAULT
};

/// Factory serial number payload from the Security register.
struct SerialNumberInfo {
  uint8_t bytes[cmd::SECURITY_SERIAL_SIZE];
  bool productIdOk;
  bool crcOk;
};

/// AT21CS01/AT21CS11 driver.
/// Not thread-safe: serialize access from one task/thread or guard with an external mutex.
class Driver {
 public:
  // Lifecycle
  Status begin(const Config& config);
  void tick(uint32_t nowMs);
  void end();

  // Diagnostics and recovery
  Status probe();
  Status recover();
  Status resetAndDiscover();
  Status isPresent(bool& present);

  // Driver state and health
  DriverState state() const { return _driverState; }
  bool isOnline() const {
    return _driverState != DriverState::UNINIT && _driverState != DriverState::OFFLINE &&
           _driverState != DriverState::FAULT;
  }

  uint32_t lastOkMs() const { return _lastOkMs; }
  uint32_t lastErrorMs() const { return _lastErrorMs; }
  Status lastError() const { return _lastError; }
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }
  uint32_t totalFailures() const { return _totalFailures; }
  uint32_t totalSuccess() const { return _totalSuccess; }

  PartType detectedPart() const { return _detectedPart; }
  SpeedMode speedMode() const { return _speedMode; }

  // Busy poll helper
  Status waitReady(uint32_t timeoutMs);

  // EEPROM data area
  Status readCurrentAddress(uint8_t& value);
  Status readEeprom(uint8_t address, uint8_t* data, size_t len);
  Status writeEepromByte(uint8_t address, uint8_t value);
  Status writeEepromPage(uint8_t address, const uint8_t* data, size_t len);

  // Security register
  Status readSecurity(uint8_t address, uint8_t* data, size_t len);
  Status writeSecurityUserByte(uint8_t address, uint8_t value);
  Status writeSecurityUserPage(uint8_t address, const uint8_t* data, size_t len);
  Status lockSecurityRegister();
  Status isSecurityLocked(bool& locked);

  // IDs
  Status readSerialNumber(SerialNumberInfo& serial);
  Status readManufacturerId(uint32_t& manufacturerId);
  Status detectPart(PartType& part);

  // ROM zones / freeze
  Status readRomZoneRegister(uint8_t zoneIndex, uint8_t& value);
  Status isZoneRom(uint8_t zoneIndex, bool& isRom);
  Status setZoneRom(uint8_t zoneIndex);
  Status freezeRomZones();
  Status areRomZonesFrozen(bool& frozen);

  // Speed mode control
  Status setHighSpeed();
  Status isHighSpeed(bool& enabled);
  Status setStandardSpeed();
  Status isStandardSpeed(bool& enabled);

  // Utilities
  static uint8_t crc8_31(const uint8_t* data, size_t len);

 private:
  struct TimingProfile {
    uint16_t bitUs;
    uint16_t low0Us;
    uint16_t low1Us;
    uint16_t readLowUs;
    uint16_t readSampleUs;
    uint16_t htssUs;
  };

  static constexpr TimingProfile HIGH_SPEED_TIMING = {
      12,  // bitUs
      8,   // low0Us
      2,   // low1Us
      1,   // readLowUs
      3,   // readSampleUs
      150  // htssUs
  };

  static constexpr TimingProfile STANDARD_SPEED_TIMING = {
      60,  // bitUs
      32,  // low0Us
      6,   // low1Us
      6,   // readLowUs
      14,  // readSampleUs
      600  // htssUs
  };

  // Reset/discovery timing always uses high-speed table after reset.
  static constexpr uint16_t RESET_LOW_US = 150;
  static constexpr uint16_t DISCHARGE_LOW_US = 150;
  static constexpr uint16_t RESET_RECOVERY_US = 10;
  static constexpr uint16_t DISCOVERY_REQUEST_US = 1;
  static constexpr uint16_t DISCOVERY_STROBE_DELAY_US = 8;
  static constexpr uint16_t DISCOVERY_STROBE_US = 2;
  static constexpr uint16_t DISCOVERY_SAMPLE_DELAY_US = 1;

  // Transport wrappers (raw + tracked)
  Status _trackIo(const Status& st);
  Status _checkInitialized() const;

  // GPIO + PHY helpers
  Status _configurePins();
  bool _presencePinReportsPresent() const;
  void _releaseLine();
  void _lineLow();
  bool _readLine() const;

  void driveLow(uint32_t lowUs);
  void releaseLine();
  bool readLine();

  void txBit0();
  void txBit1();
  bool rxBit();
  bool txByte(uint8_t value);
  uint8_t rxByte(bool ack);

  void _sendStart();
  void _sendStop();

  // Protocol helpers (raw operations)
  uint8_t _deviceAddress(uint8_t opcode, bool read) const;
  Status _resetAndDiscoverRaw();
  Status _addressOnlyRaw(uint8_t opcode, bool read, bool& ack);
  Status _readRandomRaw(uint8_t opcode, uint8_t address, uint8_t* data, size_t len);
  Status _writeRaw(uint8_t opcode, uint8_t address, const uint8_t* data, size_t len);
  Status _readManufacturerIdRaw(uint32_t& manufacturerId);
  Status _readCurrentAddressRaw(uint8_t& value);

  // Validation helpers
  static bool _isZoneIndexValid(uint8_t zoneIndex);
  static bool _isSecurityUserAddressValid(uint8_t address);

  void _setSpeedMode(SpeedMode mode);
  void _resetHealth();

 private:
  Config _config{};
  bool _initialized = false;
  DriverState _driverState = DriverState::UNINIT;

  PartType _detectedPart = PartType::UNKNOWN;
  SpeedMode _speedMode = SpeedMode::HIGH_SPEED;
  TimingProfile _timing = HIGH_SPEED_TIMING;

  uint32_t _lastOkMs = 0;
  uint32_t _lastErrorMs = 0;
  Status _lastError = Status::Ok();
  uint8_t _consecutiveFailures = 0;
  uint32_t _totalFailures = 0;
  uint32_t _totalSuccess = 0;

  uint32_t _lastTickMs = 0;

#if defined(ARDUINO_ARCH_ESP32)
  mutable portMUX_TYPE _timingMux = portMUX_INITIALIZER_UNLOCKED;
#endif
};

}  // namespace AT21CS
