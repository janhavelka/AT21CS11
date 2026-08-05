/// @file AT21CS.h
/// @brief General-purpose AT21CS01/AT21CS11 device driver API.
#pragma once

#include <cstddef>
#include <cstdint>

#include "AT21CS/Bus.h"
#include "AT21CS/CommandTable.h"
#include "AT21CS/Config.h"
#include "AT21CS/Status.h"
#include "AT21CS/Types.h"
#include "AT21CS/Version.h"

namespace AT21CS {

#if defined(AT21CS_TESTING)
namespace test {
class TestAccess;
}
#endif

struct SerialNumberInfo {
  uint8_t bytes[cmd::SECURITY_SERIAL_SIZE] = {};
  bool productIdOk = false;
  bool crcOk = false;
};

struct WriteResult {
  /// Bytes from the start of the requested range proven fully committed.
  size_t bytesCommitted = 0;
  /// Data bytes accepted in the final attempted page frame.
  size_t lastPageBytesAccepted = 0;
  /// Conservative effect of the final attempted page.
  WriteEffect lastPageEffect = WriteEffect::NOT_ATTEMPTED;
};

/// Evidence returned by a one-way Security/ROM configuration operation.
///
/// @warning A successful provisioning step requires both `Status::ok()` and
/// `effect == MutationEffect::VERIFIED`. Never automatically replay
/// `MAY_HAVE_COMMITTED` or `ACCEPTED` evidence after a failed Status.
struct MutationResult {
  /// Strongest conservative evidence established by the call.
  MutationEffect effect = MutationEffect::NOT_ATTEMPTED;
  /// The precheck observed the requested permanent state before programming.
  bool alreadyApplied = false;
};

struct SettingsSnapshot {
  bool bound = false;
  bool initialized = false;
  DriverState state = DriverState::UNINIT;
  uint8_t addressBits = 0;
  uint8_t offlineThreshold = 0;
  PartType expectedPart = PartType::UNKNOWN;
  PartType detectedPart = PartType::UNKNOWN;
  uint32_t manufacturerId = 0;
  uint8_t siliconRevision = 0;
  SpeedMode configuredSpeed = SpeedMode::HIGH_SPEED;
  SpeedMode activeSpeed = SpeedMode::HIGH_SPEED;
  bool speedKnown = false;
  bool seenBusBindingEpochValid = false;
  uint64_t seenBusBindingEpoch = 0;
  uint64_t seenBusGeneration = 0;
  Err lastStatusCode = Err::OK;
  int32_t lastStatusDetail = 0;
  Err lastErrorCode = Err::OK;
  int32_t lastErrorDetail = 0;
  uint64_t lastOkUs = 0;
  uint64_t lastErrorUs = 0;
  uint8_t consecutiveFailures = 0;
  uint32_t totalSuccess = 0;
  uint32_t totalFailures = 0;
};

/// Synchronous device state. Callers serialize every Driver sharing a Bus.
class Driver {
 public:
  Driver() = default;
  ~Driver() = default;

  Driver(const Driver&) = delete;
  Driver& operator=(const Driver&) = delete;
  Driver(Driver&&) = delete;
  Driver& operator=(Driver&&) = delete;

  Status bind(Bus& bus, const Config& config);
  Status initialize();
  Status begin(Bus& bus, const Config& config);
  Status recover();
  void end();

  Status probe();

  bool isBound() const;
  bool isInitialized() const;
  bool isOnline() const;
  DriverState state() const;
  PartType detectedPart() const;
  uint32_t manufacturerId() const;
  uint8_t siliconRevision() const;
  bool isSpeedKnown() const;
  SpeedMode speedMode() const;
  Status lastStatus() const;
  Status lastError() const;
  SettingsSnapshot snapshot() const;

  Status readEeprom(uint8_t address, uint8_t* data, size_t length);
  /// One bounded page frame followed by the Bus-owned 10 ms high-only hold.
  Status writeEepromPage(uint8_t address,
                         const uint8_t* data,
                         size_t length,
                         WriteResult& result);
  /// Worst case: 16 pages * (one bounded frame + 10 ms high-only hold).
  /// Latency-sensitive firmware should schedule writeEepromPage() instead.
  Status writeEeprom(uint8_t address,
                     const uint8_t* data,
                     size_t length,
                     WriteResult& result);

  /// Reads a range within the 32-byte Security register transactionally.
  Status readSecurity(uint8_t address, uint8_t* data, size_t length);
  /// Writes one page within Security-user bytes 0x10..0x1F.
  Status writeSecurityUserPage(uint8_t address,
                               const uint8_t* data,
                               size_t length,
                               WriteResult& result);
  /// Writes a bounded range within Security-user bytes 0x10..0x1F.
  Status writeSecurityUser(uint8_t address,
                           const uint8_t* data,
                           size_t length,
                           WriteResult& result);
  /// Reports whether the Security register has been permanently locked.
  Status readSecurityLockState(bool& locked);
  /// Permanently prevents future Security-user writes.
  ///
  /// The method prechecks Lock state, sends the documented Lock command only
  /// when needed, then verifies the state. Lock affects Security bytes
  /// 0x10..0x1F; it does not protect main EEPROM.
  ///
  /// @warning This operation cannot be undone. Write and read-verify all
  /// Security-user data first. Do not automatically retry ambiguous evidence.
  Status permanentlyLockSecurity(MutationResult& result);

  Status readSerialNumber(SerialNumberInfo& serial);
  Status readManufacturerId(uint32_t& manufacturerId);

  /// Reads one ROM-zone bit; zone 0..3 maps to EEPROM 0x00..0x7F in 32-byte
  /// increments. `enabled == true` means that zone is permanently read-only.
  Status readRomZoneState(uint8_t zoneIndex, bool& enabled);
  /// Permanently makes one 32-byte EEPROM zone read-only.
  ///
  /// Zone 0 is 0x00..0x1F, zone 1 is 0x20..0x3F, zone 2 is
  /// 0x40..0x5F, and zone 3 is 0x60..0x7F.
  ///
  /// @warning Program and read-verify the zone first. This cannot be undone.
  Status permanentlyEnableRomZone(uint8_t zoneIndex, MutationResult& result);
  /// Permanently locks the current four ROM-zone configuration bits.
  ///
  /// This freezes configuration, not EEPROM data. Enabled zones remain
  /// read-only. Disabled zones remain writable forever and can never later be
  /// made ROM. The call observes state first and mutates when not yet frozen;
  /// it is not a read-only query for an unknown device.
  ///
  /// @warning Verify the final four-zone map and obtain explicit authorization
  /// before calling. Do not automatically retry ambiguous evidence.
  Status permanentlyFreezeRomZones(MutationResult& result);

  Status setSpeedMode(SpeedMode mode);

  static uint8_t crc8Maxim(const uint8_t* data, size_t length);

 private:
#if defined(AT21CS_TESTING)
  friend class test::TestAccess;
#endif

  enum class OperationKind : uint8_t {
    INITIALIZE = 0,
    RECOVER,
    PROBE,
    NORMAL_IO,
    MUTATION
  };

  uint8_t _deviceAddress(uint8_t opcode, bool read) const;
  bool _hasCurrentBusBinding() const;
  bool _canUseNormalIo() const;

  Status _requireBound() const;
  Status _requireInitializedForIo() const;
  void _setState(DriverState state, bool initialized);
  void _enterOperation(DriverState transient);
  void _finishOperation(const Status& status,
                        OperationKind kind,
                        DriverState entryState);
  void _resetLocalState();

  Status _synchronizeBusState(bool restoreConfiguredSpeed);
  Status _readRandomRaw(uint8_t opcode,
                        uint8_t address,
                        uint8_t* data,
                        size_t length);
  Status _readRandomRangeRaw(uint8_t opcode,
                             uint8_t address,
                             size_t capacity,
                             uint8_t* data,
                             size_t length);
  Status _readDirectRaw(uint8_t opcode, uint8_t* data, size_t length);
  Status _writePageRaw(uint8_t opcode,
                       uint8_t address,
                       const uint8_t* data,
                       size_t length,
                       WriteResult& result);
  Status _writeRange(uint8_t opcode,
                     uint8_t firstWritableAddress,
                     uint8_t lastWritableAddress,
                     uint8_t address,
                     const uint8_t* data,
                     size_t length,
                     WriteResult& result);
  Status _readSecurityLockStateRaw(bool& locked);
  Status _readRomZoneStateRaw(uint8_t zoneIndex, bool& enabled);
  Status _observeFreezeStateRaw(bool& frozen);
  Status _readManufacturerIdRaw(uint32_t& manufacturerId);
  Status _classifyManufacturerIdRaw(uint32_t manufacturerId,
                                    PartType& part,
                                    uint8_t& siliconRevision);
  Status _setSpeedModeRaw(SpeedMode mode, TransferResult& transferResult);
  Status _runInitializationSequence();

  Bus* _bus = nullptr;
  Config _config{};
  bool _bound = false;
  bool _initialized = false;
  DriverState _state = DriverState::UNINIT;
  PartType _detectedPart = PartType::UNKNOWN;
  uint32_t _manufacturerId = 0;
  uint8_t _siliconRevision = 0;
  SpeedMode _activeSpeed = SpeedMode::HIGH_SPEED;
  bool _speedKnown = false;
  bool _seenBusBindingEpochValid = false;
  uint64_t _seenBusBindingEpoch = 0;
  uint64_t _seenBusGeneration = 0;
  Err _lastStatusCode = Err::OK;
  int32_t _lastStatusDetail = 0;
  Err _lastErrorCode = Err::OK;
  int32_t _lastErrorDetail = 0;
  uint64_t _lastOkUs = 0;
  uint64_t _lastErrorUs = 0;
  uint8_t _consecutiveFailures = 0;
  uint32_t _totalSuccess = 0;
  uint32_t _totalFailures = 0;
};

}  // namespace AT21CS
