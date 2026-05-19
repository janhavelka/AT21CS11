/// @file AT21CS.h
/// @brief Main AT21CS01/AT21CS11 single-wire EEPROM driver.
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(ARDUINO_ARCH_ESP32) || defined(AT21CS_PLATFORM_IDF)
#ifndef AT21CS_PLATFORM_ESP32
#define AT21CS_PLATFORM_ESP32 1
#endif
#else
#ifndef AT21CS_PLATFORM_ESP32
#define AT21CS_PLATFORM_ESP32 0
#endif
#endif

#if AT21CS_PLATFORM_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <soc/gpio_reg.h>
#include <esp_cpu.h>
#include <esp_attr.h>
#define AT21CS_IRAM IRAM_ATTR
#else
#define AT21CS_IRAM
#endif

#include "AT21CS/CommandTable.h"
#include "AT21CS/Config.h"
#include "AT21CS/Status.h"
#include "AT21CS/Version.h"

namespace AT21CS {

/// @brief AT21CS runtime state machine.
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

/// @brief Factory serial number payload from the Security register.
struct SerialNumberInfo {
  uint8_t bytes[cmd::SECURITY_SERIAL_SIZE]; ///< Raw 8-byte serial payload.
  bool productIdOk;                         ///< True when the product marker byte is valid.
  bool crcOk;                               ///< True when the serial CRC matches.
};

/// @brief Cached settings and health state, read without bus I/O.
struct SettingsSnapshot {
  Config config;                         ///< Active runtime configuration snapshot.
  DriverState state = DriverState::UNINIT;
  bool initialized = false;
  PartType detectedPart = PartType::UNKNOWN;
  SpeedMode speedMode = SpeedMode::HIGH_SPEED;
  uint32_t lastOkMs = 0;
  uint32_t lastErrorMs = 0;
  Status lastError = Status::Ok();
  uint8_t consecutiveFailures = 0;
  uint32_t totalFailures = 0;
  uint32_t totalSuccess = 0;
};

/// @brief AT21CS01/AT21CS11 single-wire EEPROM driver.
/// Not thread-safe: serialize access from one task/thread or guard with an external mutex.
class Driver {
 public:
  // Lifecycle
  /// @brief Initialize the driver and discover the attached part.
  /// @param config GPIO, timing, address, and startup-speed configuration.
  /// @return Status::Ok() on success, error otherwise.
  Status begin(const Config& config);

  /// @brief Record the caller's current scheduler timestamp.
  /// @param nowMs Current monotonic time in milliseconds.
  void tick(uint32_t nowMs);

  /// @brief Stop the driver and release the single-wire line.
  void end();

  // Diagnostics and recovery
  /// @brief Check for a responding device without changing health counters.
  /// @return Status::Ok() when the device responds, error otherwise.
  Status probe();

  /// @brief Run reset/discovery and restore configured speed after a fault.
  /// @return Status::Ok() on recovery, error otherwise.
  Status recover();

  /// @brief Issue a reset and discovery sequence.
  /// @return Status::Ok() when discovery succeeds, error otherwise.
  Status resetAndDiscover();

  /// @brief Check whether the device is currently present.
  /// @param[out] present Set true when presence is detected.
  /// @return Status::Ok() on a completed check, error otherwise.
  Status isPresent(bool& present);

  // AT21CS state and health
  /// @brief Get current lifecycle/health state.
  /// @return Current DriverState.
  DriverState state() const { return _driverState; }

  /// @brief Alias for state() for cross-driver diagnostics.
  /// @return Current DriverState.
  DriverState driverState() const { return state(); }

  /// @brief Check if begin() has completed successfully.
  /// @return true after successful begin() and before end().
  bool isInitialized() const { return _initialized; }

  /// @brief Check whether normal operations are allowed.
  /// @return true when initialized and not OFFLINE, SLEEPING, or FAULT.
  bool isOnline() const {
    return _initialized && _driverState != DriverState::OFFLINE &&
           _driverState != DriverState::FAULT && _driverState != DriverState::SLEEPING;
  }

  /// @brief Get the active configuration snapshot.
  /// @return Active configuration copy.
  const Config& getConfig() const { return _config; }

  /// @brief Get cached settings and health state without bus I/O.
  /// @return Settings snapshot.
  SettingsSnapshot getSettings() const;

  /// @brief Copy cached settings and health state without bus I/O.
  /// @param[out] out Receives the settings snapshot.
  /// @return Status::Ok().
  Status getSettings(SettingsSnapshot& out) const;

  /// @brief Timestamp of the last successful tracked operation.
  /// @return Milliseconds from the configured timebase.
  uint32_t lastOkMs() const { return _lastOkMs; }

  /// @brief Timestamp of the last failed tracked operation.
  /// @return Milliseconds from the configured timebase.
  uint32_t lastErrorMs() const { return _lastErrorMs; }

  /// @brief Most recent tracked operation error.
  /// @return Last error Status.
  Status lastError() const { return _lastError; }

  /// @brief Consecutive tracked failures since the last success.
  /// @return Saturating failure count.
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }

  /// @brief Lifetime tracked failure count since begin().
  /// @return Saturating failure count.
  uint32_t totalFailures() const { return _totalFailures; }

  /// @brief Lifetime tracked success count since begin().
  /// @return Saturating success count.
  uint32_t totalSuccess() const { return _totalSuccess; }

  /// @brief Get the detected AT21CS part type.
  /// @return Detected part, or UNKNOWN before successful discovery.
  PartType detectedPart() const { return _detectedPart; }

  /// @brief Get the active bit timing profile.
  /// @return Active speed mode.
  SpeedMode speedMode() const { return _speedMode; }

  // Busy poll helper
  /// @brief Poll for t_WR completion using a bounded timeout.
  /// @param timeoutMs Timeout in milliseconds, range 1..250.
  /// @return Status::Ok() when ready, BUSY_TIMEOUT or other error otherwise.
  Status waitReady(uint32_t timeoutMs);

  // EEPROM data area
  /// @brief Read from the device's current EEPROM address pointer.
  /// @param[out] value Byte read from the current pointer.
  /// @return Status::Ok() on success, error otherwise.
  Status readCurrentAddress(uint8_t& value);

  /// @brief Read bytes from the EEPROM array.
  /// @param address Start address in the 128-byte EEPROM area.
  /// @param[out] data Destination buffer.
  /// @param len Number of bytes to read.
  /// @return Status::Ok() on success, error otherwise.
  Status readEeprom(uint8_t address, uint8_t* data, size_t len);

  /// @brief Write one EEPROM byte.
  /// @param address EEPROM byte address.
  /// @param value Byte value to write.
  /// @return Status::Ok() after the write cycle completes, error otherwise.
  Status writeEepromByte(uint8_t address, uint8_t value);

  /// @brief Write bytes within a single EEPROM page.
  /// @param address EEPROM start address.
  /// @param data Source buffer.
  /// @param len Number of bytes to write.
  /// @return Status::Ok() after the write cycle completes, error otherwise.
  Status writeEepromPage(uint8_t address, const uint8_t* data, size_t len);

  /// @brief Write bytes across EEPROM page boundaries.
  /// @param address EEPROM start address.
  /// @param data Source buffer.
  /// @param len Number of bytes to write.
  /// @return Status::Ok() after all write cycles complete, error otherwise.
  Status writeEeprom(uint8_t address, const uint8_t* data, size_t len);

  // Security register
  /// @brief Read bytes from the Security register.
  /// @param address Security register start address.
  /// @param[out] data Destination buffer.
  /// @param len Number of bytes to read.
  /// @return Status::Ok() on success, error otherwise.
  Status readSecurity(uint8_t address, uint8_t* data, size_t len);

  /// @brief Write one user byte in the Security register.
  /// @param address Security user-byte address.
  /// @param value Byte value to write.
  /// @return Status::Ok() after the write cycle completes, error otherwise.
  Status writeSecurityUserByte(uint8_t address, uint8_t value);

  /// @brief Write bytes within one Security user page.
  /// @param address Security user start address.
  /// @param data Source buffer.
  /// @param len Number of bytes to write.
  /// @return Status::Ok() after the write cycle completes, error otherwise.
  Status writeSecurityUserPage(uint8_t address, const uint8_t* data, size_t len);

  /// @brief Write bytes across Security user pages.
  /// @param address Security user start address.
  /// @param data Source buffer.
  /// @param len Number of bytes to write.
  /// @return Status::Ok() after all write cycles complete, error otherwise.
  Status writeSecurityUser(uint8_t address, const uint8_t* data, size_t len);

  /// @brief Permanently lock the Security register.
  /// @return Status::Ok() after the lock write cycle completes, error otherwise.
  Status lockSecurityRegister();

  /// @brief Read the Security register lock state.
  /// @param[out] locked Set true when locked.
  /// @return Status::Ok() on success, error otherwise.
  Status isSecurityLocked(bool& locked);

  // IDs
  /// @brief Read and validate the factory serial number.
  /// @param[out] serial Serial payload and validation flags.
  /// @return Status::Ok() on read success, error otherwise.
  Status readSerialNumber(SerialNumberInfo& serial);

  /// @brief Read the 24-bit manufacturer/device identifier.
  /// @param[out] manufacturerId Raw 24-bit identifier.
  /// @return Status::Ok() on success, error otherwise.
  Status readManufacturerId(uint32_t& manufacturerId);

  /// @brief Detect the attached AT21CS part from manufacturer ID.
  /// @param[out] part Detected part type.
  /// @return Status::Ok() on success, error otherwise.
  Status detectPart(PartType& part);

  // ROM zones / freeze
  /// @brief Read one ROM-zone register.
  /// @param zoneIndex Zone index 0..3.
  /// @param[out] value Raw zone register value.
  /// @return Status::Ok() on success, error otherwise.
  Status readRomZoneRegister(uint8_t zoneIndex, uint8_t& value);

  /// @brief Check whether a zone is configured read-only.
  /// @param zoneIndex Zone index 0..3.
  /// @param[out] isRom Set true when the zone is ROM.
  /// @return Status::Ok() on success, error otherwise.
  Status isZoneRom(uint8_t zoneIndex, bool& isRom);

  /// @brief Configure one zone as ROM.
  /// @param zoneIndex Zone index 0..3.
  /// @return Status::Ok() after the write cycle completes, error otherwise.
  Status setZoneRom(uint8_t zoneIndex);

  /// @brief Permanently freeze the ROM-zone configuration.
  /// @return Status::Ok() after the command completes, error otherwise.
  Status freezeRomZones();

  /// @brief Check whether ROM-zone configuration is frozen.
  /// @param[out] frozen Set true when frozen.
  /// @return Status::Ok() on success, error otherwise.
  Status areRomZonesFrozen(bool& frozen);

  // Speed mode control
  /// @brief Switch the device to High-Speed mode.
  /// @return Status::Ok() on acknowledged switch, error otherwise.
  Status setHighSpeed();

  /// @brief Check whether the device acknowledges High-Speed mode.
  /// @param[out] enabled Set true when High-Speed is active.
  /// @return Status::Ok() on completed check, error otherwise.
  Status isHighSpeed(bool& enabled);

  /// @brief Switch AT21CS01 to Standard Speed mode.
  /// @return Status::Ok() on acknowledged switch, error otherwise.
  Status setStandardSpeed();

  /// @brief Check whether AT21CS01 acknowledges Standard Speed mode.
  /// @param[out] enabled Set true when Standard Speed is active.
  /// @return Status::Ok() on completed check, error otherwise.
  Status isStandardSpeed(bool& enabled);

  // Utilities
  /// @brief Compute CRC-8 with polynomial 0x31 over a byte buffer.
  /// @param data Input bytes.
  /// @param len Number of bytes to process.
  /// @return CRC byte.
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
      1,   // low1Us   (actual ~1.6us with overhead, within t_LOW1 1-2us)
      1,   // readLowUs
      1,   // readSampleUs  (sample at t_RD+1 = 2us, within t_MRS ~3us and t_HLD0 min 2us)
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
  static constexpr uint16_t DISCOVERY_STROBE_DELAY_US = 2;
  static constexpr uint16_t DISCOVERY_STROBE_US = 2;
  static constexpr uint16_t DISCOVERY_SAMPLE_DELAY_US = 1;

  // Transport wrappers (raw + tracked)
  Status _trackIo(const Status& st);
  Status _checkInitialized(bool allowOffline = false) const;

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
  Status _activateDevice();
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
  uint32_t _nowMs() const;
  void _sleepUs(uint32_t us) const;

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

#if AT21CS_PLATFORM_ESP32
  mutable portMUX_TYPE _timingMux = portMUX_INITIALIZER_UNLOCKED;
  // Direct-register GPIO for sub-microsecond bit-bang timing.
  volatile uint32_t* _gpioSetReg = nullptr;
  volatile uint32_t* _gpioClrReg = nullptr;
  volatile uint32_t* _gpioInReg = nullptr;
  uint32_t _gpioMask = 0;
  uint32_t _cyclesPerUs = 240;  // CPU cycles per microsecond (cached at begin)
#endif
};
}  // namespace AT21CS
