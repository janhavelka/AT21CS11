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
  size_t bytesCommitted = 0;
  size_t lastPageBytesAccepted = 0;
  WriteEffect lastPageEffect = WriteEffect::NOT_ATTEMPTED;
};

struct MutationResult {
  MutationEffect effect = MutationEffect::NOT_ATTEMPTED;
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

  Status readSecurity(uint8_t address, uint8_t* data, size_t length);
  Status writeSecurityUserPage(uint8_t address,
                               const uint8_t* data,
                               size_t length,
                               WriteResult& result);
  Status writeSecurityUser(uint8_t address,
                           const uint8_t* data,
                           size_t length,
                           WriteResult& result);
  Status readSecurityLockState(bool& locked);
  Status permanentlyLockSecurity(MutationResult& result);

  Status readSerialNumber(SerialNumberInfo& serial);
  Status readManufacturerId(uint32_t& manufacturerId);

  Status readRomZoneState(uint8_t zoneIndex, bool& enabled);
  Status permanentlyEnableRomZone(uint8_t zoneIndex, MutationResult& result);
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
