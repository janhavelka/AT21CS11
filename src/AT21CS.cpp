#include "AT21CS/AT21CS.h"

#include <cstring>
#include <limits>

namespace AT21CS {
namespace {

constexpr bool isKnownPart(PartType part) {
  return part == PartType::UNKNOWN || part == PartType::AT21CS01 ||
         part == PartType::AT21CS11;
}

constexpr bool isKnownSpeed(SpeedMode speed) {
  return speed == SpeedMode::HIGH_SPEED || speed == SpeedMode::STANDARD_SPEED;
}

Status validateConfig(const Config& config) {
  if (config.addressBits > 7u || !isKnownPart(config.expectedPart) ||
      !isKnownSpeed(config.startupSpeed)) {
    return Status::Error(Err::INVALID_CONFIG);
  }
  if (config.startupSpeed == SpeedMode::STANDARD_SPEED &&
      config.expectedPart != PartType::AT21CS01) {
    return Status::Error(Err::INVALID_CONFIG);
  }
  return Status::Ok();
}

constexpr bool isManufacturerAddressNack(const Status& status) {
  return status.code == Err::NACK_DEVICE_ADDRESS &&
         protocolDetailPhase(status.detail) ==
             ProtocolPhase::DEVICE_ADDRESS_READ;
}

template <typename T>
void incrementSaturating(T& value) {
  if (value != std::numeric_limits<T>::max()) {
    ++value;
  }
}

}  // namespace

Status Driver::bind(Bus& bus, const Config& config) {
  const Status configStatus = validateConfig(config);
  if (!configStatus.ok()) {
    return configStatus;
  }
  const BusSnapshot busState = bus.snapshot();
  if (!busState.bound) {
    return Status::Error(Err::NOT_BOUND);
  }
  if (!busState.bindingEpochValid) {
    return Status::Error(Err::INVALID_STATE);
  }

  const bool keepsExistingClaim =
      _bound && _bus == &bus && _config.addressBits == config.addressBits;
  if (!keepsExistingClaim) {
    const Status claimStatus = bus._claimAddress(config.addressBits);
    if (!claimStatus.ok()) {
      return claimStatus;
    }
  }

  Bus* const previousBus = _bus;
  const uint8_t previousAddress = _config.addressBits;
  const bool hadPreviousClaim = _bound && previousBus != nullptr;
  if (hadPreviousClaim && !keepsExistingClaim) {
    previousBus->_releaseAddress(previousAddress);
  }

  _resetLocalState();
  _bus = &bus;
  _config = config;
  _bound = true;
  _seenBusBindingEpochValid = busState.bindingEpochValid;
  _seenBusBindingEpoch = busState.bindingEpoch;
  _seenBusGeneration = busState.generation;
  return Status::Ok();
}

Status Driver::initialize() {
  const Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }
  if (_initialized || _state != DriverState::UNINIT ||
      !_hasCurrentBusBinding()) {
    return Status::Error(Err::INVALID_STATE);
  }
  if (_bus->snapshot().generation ==
      std::numeric_limits<uint64_t>::max()) {
    return Status::Error(Err::INVALID_STATE);
  }

  const DriverState entryState = _state;
  if (_bus->hasPresenceIndicator()) {
    bool present = false;
    TransferResult presenceResult{};
    Status status = _bus->_readPresence(present, presenceResult);
    if (status.ok() && !present) {
      status = Status::Error(Err::NOT_PRESENT);
    }
    if (!status.ok()) {
      _speedKnown = false;
      _finishOperation(status, OperationKind::INITIALIZE, entryState);
      return status;
    }
  }

  _enterOperation(DriverState::PROBING);
  const Status status = _runInitializationSequence();
  _finishOperation(status, OperationKind::INITIALIZE, entryState);
  return status;
}

Status Driver::begin(Bus& bus, const Config& config) {
  const Status status = bind(bus, config);
  return status.ok() ? initialize() : status;
}

Status Driver::recover() {
  const Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }
  if (_state != DriverState::UNINIT && _state != DriverState::READY &&
      _state != DriverState::DEGRADED && _state != DriverState::OFFLINE) {
    return Status::Error(Err::INVALID_STATE);
  }
  if (_bus->snapshot().generation ==
      std::numeric_limits<uint64_t>::max()) {
    return Status::Error(Err::INVALID_STATE);
  }

  const DriverState entryState = _state;
  _setState(DriverState::RECOVERING, false);
  _speedKnown = false;

  if (_bus->hasPresenceIndicator()) {
    bool present = false;
    TransferResult presenceResult{};
    Status status = _bus->_readPresence(present, presenceResult);
    if (status.ok() && !present) {
      status = Status::Error(Err::NOT_PRESENT);
    }
    if (!status.ok()) {
      _finishOperation(status, OperationKind::RECOVER, entryState);
      return status;
    }
  }

  const Status status = _runInitializationSequence();
  _finishOperation(status, OperationKind::RECOVER, entryState);
  return status;
}

void Driver::end() {
  if (_bound && _bus != nullptr) {
    _bus->_releaseAddress(_config.addressBits);
  }
  _resetLocalState();
}

Status Driver::probe() {
  const Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED);
  }
  if (_state == DriverState::BUSY) {
    return Status::Error(Err::BUSY);
  }
  if (_state != DriverState::READY && _state != DriverState::DEGRADED &&
      _state != DriverState::OFFLINE) {
    return Status::Error(Err::INVALID_STATE);
  }

  const DriverState entryState = _state;
  Status status = _synchronizeBusState(true);
  if (!status.ok()) {
    if (status.code == Err::NOT_BOUND || status.code == Err::INVALID_STATE) {
      return status;
    }
    _finishOperation(status, OperationKind::PROBE, entryState);
    return status;
  }

  _enterOperation(DriverState::PROBING);
  uint32_t rawId = 0;
  status = _readManufacturerIdRaw(rawId);
  PartType part = PartType::UNKNOWN;
  uint8_t revision = 0;
  if (status.ok()) {
    status = _classifyManufacturerIdRaw(rawId, part, revision);
  }
  if (status.ok() && _config.expectedPart != PartType::UNKNOWN &&
      part != _config.expectedPart) {
    status = Status::Error(Err::PART_MISMATCH,
                           static_cast<int32_t>(rawId));
  }
  if (status.ok() && _detectedPart != PartType::UNKNOWN &&
      part != _detectedPart) {
    status = Status::Error(Err::PART_MISMATCH,
                           static_cast<int32_t>(rawId));
  }
  if (status.ok()) {
    _detectedPart = part;
    _manufacturerId = rawId;
    _siliconRevision = revision;
  }

  _finishOperation(status, OperationKind::PROBE, entryState);
  return status;
}

bool Driver::isBound() const {
  return _bound;
}

bool Driver::isInitialized() const {
  return _initialized;
}

bool Driver::isOnline() const {
  return _bound && _initialized && isSpeedKnown() &&
         (_state == DriverState::READY || _state == DriverState::DEGRADED);
}

DriverState Driver::state() const {
  return _state;
}

PartType Driver::detectedPart() const {
  return _detectedPart;
}

uint32_t Driver::manufacturerId() const {
  return _manufacturerId;
}

uint8_t Driver::siliconRevision() const {
  return _siliconRevision;
}

bool Driver::isSpeedKnown() const {
  if (!_speedKnown || !_hasCurrentBusBinding()) {
    return false;
  }
  return _bus->snapshot().generation == _seenBusGeneration;
}

SpeedMode Driver::speedMode() const {
  return _activeSpeed;
}

Status Driver::lastStatus() const {
  return Status::Error(_lastStatusCode, _lastStatusDetail);
}

Status Driver::lastError() const {
  return Status::Error(_lastErrorCode, _lastErrorDetail);
}

SettingsSnapshot Driver::snapshot() const {
  SettingsSnapshot value{};
  value.bound = _bound;
  value.initialized = _initialized;
  value.state = _state;
  value.addressBits = _config.addressBits;
  value.offlineThreshold = _config.offlineThreshold;
  value.expectedPart = _config.expectedPart;
  value.detectedPart = _detectedPart;
  value.manufacturerId = _manufacturerId;
  value.siliconRevision = _siliconRevision;
  value.configuredSpeed = _config.startupSpeed;
  value.activeSpeed = _activeSpeed;
  value.speedKnown = isSpeedKnown();
  value.seenBusBindingEpochValid = _seenBusBindingEpochValid;
  value.seenBusBindingEpoch = _seenBusBindingEpoch;
  value.seenBusGeneration = _seenBusGeneration;
  value.lastStatusCode = _lastStatusCode;
  value.lastStatusDetail = _lastStatusDetail;
  value.lastErrorCode = _lastErrorCode;
  value.lastErrorDetail = _lastErrorDetail;
  value.lastOkUs = _lastOkUs;
  value.lastErrorUs = _lastErrorUs;
  value.consecutiveFailures = _consecutiveFailures;
  value.totalSuccess = _totalSuccess;
  value.totalFailures = _totalFailures;
  return value;
}

Status Driver::readEeprom(uint8_t address, uint8_t* data, size_t length) {
  if (data == nullptr || length == 0 ||
      static_cast<size_t>(address) >= cmd::EEPROM_SIZE ||
      length > (cmd::EEPROM_SIZE - static_cast<size_t>(address))) {
    return Status::Error(Err::INVALID_PARAM);
  }
  const Status admission = _requireInitializedForIo();
  if (!admission.ok()) {
    return admission;
  }

  const DriverState entryState = _state;
  Status status = _synchronizeBusState(true);
  if (!status.ok()) {
    if (status.code != Err::NOT_BOUND && status.code != Err::INVALID_STATE) {
      _finishOperation(status, OperationKind::NORMAL_IO, entryState);
    }
    return status;
  }

  size_t offset = 0;
  while (offset < length) {
    const size_t remaining = length - offset;
    const size_t chunk = remaining < Bus::MAX_FRAME_DATA_BYTES
                             ? remaining
                             : Bus::MAX_FRAME_DATA_BYTES;
    status = _readRandomRaw(
        cmd::OPCODE_EEPROM,
        static_cast<uint8_t>(static_cast<size_t>(address) + offset),
        data + offset, chunk);
    if (!status.ok()) {
      break;
    }
    offset += chunk;
  }

  _finishOperation(status, OperationKind::NORMAL_IO, entryState);
  return status;
}

Status Driver::readSecurity(uint8_t address, uint8_t* data, size_t length) {
  if (data == nullptr || length == 0 ||
      static_cast<size_t>(address) >= cmd::SECURITY_SIZE ||
      length > (cmd::SECURITY_SIZE - static_cast<size_t>(address))) {
    return Status::Error(Err::INVALID_PARAM);
  }
  const Status admission = _requireInitializedForIo();
  if (!admission.ok()) {
    return admission;
  }

  const DriverState entryState = _state;
  Status status = _synchronizeBusState(true);
  if (!status.ok()) {
    if (status.code != Err::NOT_BOUND && status.code != Err::INVALID_STATE) {
      _finishOperation(status, OperationKind::NORMAL_IO, entryState);
    }
    return status;
  }

  size_t offset = 0;
  while (offset < length) {
    const size_t remaining = length - offset;
    const size_t chunk = remaining < Bus::MAX_FRAME_DATA_BYTES
                             ? remaining
                             : Bus::MAX_FRAME_DATA_BYTES;
    status = _readRandomRaw(
        cmd::OPCODE_SECURITY,
        static_cast<uint8_t>(static_cast<size_t>(address) + offset),
        data + offset, chunk);
    if (!status.ok()) {
      break;
    }
    offset += chunk;
  }

  _finishOperation(status, OperationKind::NORMAL_IO, entryState);
  return status;
}

Status Driver::readSerialNumber(SerialNumberInfo& serial) {
  serial = {};
  const Status admission = _requireInitializedForIo();
  if (!admission.ok()) {
    return admission;
  }

  const DriverState entryState = _state;
  Status status = _synchronizeBusState(true);
  if (!status.ok()) {
    if (status.code != Err::NOT_BOUND && status.code != Err::INVALID_STATE) {
      _finishOperation(status, OperationKind::NORMAL_IO, entryState);
    }
    return status;
  }

  status = _readRandomRaw(cmd::OPCODE_SECURITY,
                          cmd::SECURITY_SERIAL_START, serial.bytes,
                          cmd::SECURITY_SERIAL_SIZE);
  if (status.ok() && serial.bytes[0] != cmd::SECURITY_PRODUCT_ID) {
    status = Status::Error(Err::PART_MISMATCH,
                           static_cast<int32_t>(serial.bytes[0]));
  } else if (status.ok()) {
    serial.productIdOk = true;
    const uint8_t computed = crc8Maxim(serial.bytes,
                                       cmd::SECURITY_SERIAL_SIZE - 1u);
    const uint8_t stored = serial.bytes[cmd::SECURITY_SERIAL_SIZE - 1u];
    if (computed != stored) {
      status = Status::Error(
          Err::CRC_MISMATCH,
          static_cast<int32_t>((static_cast<uint16_t>(computed) << 8u) |
                               static_cast<uint16_t>(stored)));
    } else {
      serial.crcOk = true;
    }
  }

  _finishOperation(status, OperationKind::NORMAL_IO, entryState);
  return status;
}

Status Driver::readManufacturerId(uint32_t& manufacturerId) {
  manufacturerId = 0;
  const Status admission = _requireInitializedForIo();
  if (!admission.ok()) {
    return admission;
  }

  const DriverState entryState = _state;
  Status status = _synchronizeBusState(true);
  if (!status.ok()) {
    if (status.code != Err::NOT_BOUND && status.code != Err::INVALID_STATE) {
      _finishOperation(status, OperationKind::NORMAL_IO, entryState);
    }
    return status;
  }
  status = _readManufacturerIdRaw(manufacturerId);
  _finishOperation(status, OperationKind::NORMAL_IO, entryState);
  return status;
}

Status Driver::setSpeedMode(SpeedMode mode) {
  if (!isKnownSpeed(mode)) {
    return Status::Error(Err::INVALID_PARAM);
  }
  const Status admission = _requireInitializedForIo();
  if (!admission.ok()) {
    return admission;
  }
  if (mode == SpeedMode::STANDARD_SPEED &&
      _detectedPart == PartType::AT21CS11) {
    return Status::Error(Err::UNSUPPORTED_COMMAND);
  }

  Status status = _synchronizeBusState(false);
  if (!status.ok()) {
    return status;
  }
  if (!_speedKnown) {
    return Status::Error(Err::INVALID_STATE);
  }
  if (mode == _activeSpeed) {
    _config.startupSpeed = mode;
    return Status::Ok();
  }

  const DriverState entryState = _state;
  _enterOperation(DriverState::BUSY);
  TransferResult transferResult{};
  status = _setSpeedModeRaw(mode, transferResult);
  if (status.ok()) {
    _config.startupSpeed = mode;
  }
  _finishOperation(status, OperationKind::MUTATION, entryState);
  return status;
}

uint8_t Driver::crc8Maxim(const uint8_t* data, size_t length) {
  if (data == nullptr && length != 0) {
    return 0;
  }
  uint8_t crc = 0;
  for (size_t index = 0; index < length; ++index) {
    crc = static_cast<uint8_t>(crc ^ data[index]);
    for (uint8_t bit = 0; bit < 8u; ++bit) {
      crc = (crc & 0x01u) != 0u
                ? static_cast<uint8_t>((crc >> 1u) ^ 0x8Cu)
                : static_cast<uint8_t>(crc >> 1u);
    }
  }
  return crc;
}

uint8_t Driver::_deviceAddress(uint8_t opcode, bool read) const {
  const uint8_t readBit = read ? 0x01u : 0x00u;
  return static_cast<uint8_t>((opcode << 4u) |
                              ((_config.addressBits & 0x07u) << 1u) |
                              readBit);
}

bool Driver::_hasCurrentBusBinding() const {
  if (!_bound || _bus == nullptr || !_seenBusBindingEpochValid) {
    return false;
  }
  const BusSnapshot busState = _bus->snapshot();
  return busState.bound && busState.bindingEpochValid &&
         busState.bindingEpoch == _seenBusBindingEpoch;
}

bool Driver::_canUseNormalIo() const {
  return _initialized &&
         (_state == DriverState::READY || _state == DriverState::DEGRADED);
}

Status Driver::_requireBound() const {
  if (!_bound || _bus == nullptr) {
    return Status::Error(Err::NOT_BOUND);
  }
  const BusSnapshot busState = _bus->snapshot();
  if (!busState.bound) {
    return Status::Error(Err::NOT_BOUND);
  }
  if (!busState.bindingEpochValid) {
    return Status::Error(Err::INVALID_STATE);
  }
  return Status::Ok();
}

Status Driver::_requireInitializedForIo() const {
  const Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED);
  }
  if (_state == DriverState::BUSY) {
    return Status::Error(Err::BUSY);
  }
  if (!_canUseNormalIo()) {
    return Status::Error(Err::INVALID_STATE);
  }
  return Status::Ok();
}

void Driver::_setState(DriverState state, bool initialized) {
  _state = state;
  _initialized = initialized;
}

void Driver::_enterOperation(DriverState transient) {
  _setState(transient, _initialized);
}

void Driver::_finishOperation(const Status& status,
                              OperationKind kind,
                              DriverState entryState) {
  const uint64_t nowUs =
      (_bus != nullptr && _bus->_transport.nowUs != nullptr)
          ? _bus->_transport.nowUs(_bus->_transport.user)
          : 0;

  if (status.ok()) {
    _lastStatusCode = Err::OK;
    _lastStatusDetail = 0;
    _lastOkUs = nowUs;
    _consecutiveFailures = 0;
    incrementSaturating(_totalSuccess);

    const bool initialized =
        kind == OperationKind::INITIALIZE || kind == OperationKind::RECOVER
            ? true
            : _initialized;
    _setState(DriverState::READY, initialized);
    return;
  }

  _lastStatusCode = status.code;
  _lastStatusDetail = status.detail;
  _lastErrorCode = status.code;
  _lastErrorDetail = status.detail;
  _lastErrorUs = nowUs;
  incrementSaturating(_consecutiveFailures);
  incrementSaturating(_totalFailures);

  const bool lifecycleIdentityFailure =
      status.code == Err::PART_MISMATCH &&
      (kind == OperationKind::INITIALIZE || kind == OperationKind::RECOVER ||
       kind == OperationKind::PROBE);
  const bool definiteIdentityAbsence =
      isManufacturerAddressNack(status) &&
      (kind == OperationKind::INITIALIZE || kind == OperationKind::RECOVER ||
       kind == OperationKind::PROBE);
  const bool thresholdReached =
      _config.offlineThreshold != 0u &&
      _consecutiveFailures >= _config.offlineThreshold;
  const DriverState healthState =
      status.code == Err::NOT_PRESENT || definiteIdentityAbsence ||
              thresholdReached
          ? DriverState::OFFLINE
          : DriverState::DEGRADED;

  switch (kind) {
    case OperationKind::INITIALIZE:
      _setState(lifecycleIdentityFailure ? DriverState::FAULT : healthState,
                false);
      break;
    case OperationKind::RECOVER:
      _setState(lifecycleIdentityFailure
                    ? DriverState::FAULT
                    : (entryState == DriverState::OFFLINE
                           ? DriverState::OFFLINE
                           : healthState),
                false);
      break;
    case OperationKind::PROBE:
      _setState(lifecycleIdentityFailure
                    ? DriverState::FAULT
                    : (entryState == DriverState::OFFLINE
                           ? DriverState::OFFLINE
                           : healthState),
                lifecycleIdentityFailure ? false : _initialized);
      break;
    case OperationKind::NORMAL_IO:
    case OperationKind::MUTATION:
      _setState(healthState, _initialized);
      break;
  }
}

void Driver::_resetLocalState() {
  _bus = nullptr;
  _config = {};
  _bound = false;
  _setState(DriverState::UNINIT, false);
  _detectedPart = PartType::UNKNOWN;
  _manufacturerId = 0;
  _siliconRevision = 0;
  _activeSpeed = SpeedMode::HIGH_SPEED;
  _speedKnown = false;
  _seenBusBindingEpochValid = false;
  _seenBusBindingEpoch = 0;
  _seenBusGeneration = 0;
  _lastStatusCode = Err::OK;
  _lastStatusDetail = 0;
  _lastErrorCode = Err::OK;
  _lastErrorDetail = 0;
  _lastOkUs = 0;
  _lastErrorUs = 0;
  _consecutiveFailures = 0;
  _totalSuccess = 0;
  _totalFailures = 0;
}

Status Driver::_synchronizeBusState(bool restoreConfiguredSpeed) {
  const Status boundStatus = _requireBound();
  if (!boundStatus.ok()) {
    return boundStatus;
  }
  const BusSnapshot busState = _bus->snapshot();
  if (!_seenBusBindingEpochValid ||
      busState.bindingEpoch != _seenBusBindingEpoch) {
    _speedKnown = false;
    return Status::Error(Err::INVALID_STATE);
  }
  if (busState.generation == _seenBusGeneration) {
    if (!_speedKnown) {
      return Status::Error(Err::INVALID_STATE);
    }
    // A definitely rejected lazy restoration leaves High-Speed known and the
    // configured Standard mode pending. Ambiguous failures clear speedKnown
    // and are rejected above until explicit recovery.
    if (restoreConfiguredSpeed &&
        _config.startupSpeed == SpeedMode::STANDARD_SPEED &&
        _detectedPart == PartType::AT21CS01 &&
        _activeSpeed != SpeedMode::STANDARD_SPEED) {
      TransferResult transferResult{};
      return _setSpeedModeRaw(SpeedMode::STANDARD_SPEED, transferResult);
    }
    return Status::Ok();
  }
  if (!busState.resetEstablishedHighSpeed) {
    _speedKnown = false;
    return Status::Error(Err::INVALID_STATE);
  }

  _activeSpeed = SpeedMode::HIGH_SPEED;
  _speedKnown = true;
  _seenBusGeneration = busState.generation;
  if (restoreConfiguredSpeed &&
      _config.startupSpeed == SpeedMode::STANDARD_SPEED &&
      _detectedPart == PartType::AT21CS01) {
    TransferResult transferResult{};
    return _setSpeedModeRaw(SpeedMode::STANDARD_SPEED, transferResult);
  }
  return Status::Ok();
}

Status Driver::_readRandomRaw(uint8_t opcode,
                              uint8_t address,
                              uint8_t* data,
                              size_t length) {
  if (data == nullptr || length == 0 ||
      length > Bus::MAX_FRAME_DATA_BYTES) {
    return Status::Error(Err::INVALID_PARAM);
  }

  uint8_t scratch[Bus::MAX_FRAME_DATA_BYTES] = {};
  SingleWireTransfer transfer{};
  transfer.speed = _activeSpeed;
  transfer.deviceAddress = _deviceAddress(opcode, false);
  transfer.hasMemoryAddress = true;
  transfer.memoryAddress = address;
  transfer.hasRepeatedStart = true;
  transfer.repeatedDeviceAddress = _deviceAddress(opcode, true);
  transfer.rxData = scratch;
  transfer.rxLength = length;
  transfer.minimumPostTransferHighUs =
      _activeSpeed == SpeedMode::HIGH_SPEED ? Bus::HIGH_SPEED_HTSS_US
                                            : Bus::STANDARD_SPEED_HTSS_US;

  TransferResult result{};
  const Status status = _bus->_execute(transfer, result);
  if (status.ok()) {
    std::memcpy(data, scratch, length);
  }
  return status;
}

Status Driver::_readDirectRaw(uint8_t opcode,
                              uint8_t* data,
                              size_t length) {
  if (data == nullptr || length == 0 ||
      length > Bus::MAX_FRAME_DATA_BYTES) {
    return Status::Error(Err::INVALID_PARAM);
  }

  uint8_t scratch[Bus::MAX_FRAME_DATA_BYTES] = {};
  SingleWireTransfer transfer{};
  transfer.speed = _activeSpeed;
  transfer.deviceAddress = _deviceAddress(opcode, true);
  transfer.rxData = scratch;
  transfer.rxLength = length;
  transfer.minimumPostTransferHighUs =
      _activeSpeed == SpeedMode::HIGH_SPEED ? Bus::HIGH_SPEED_HTSS_US
                                            : Bus::STANDARD_SPEED_HTSS_US;

  TransferResult result{};
  const Status status = _bus->_execute(transfer, result);
  if (status.ok()) {
    std::memcpy(data, scratch, length);
  }
  return status;
}

Status Driver::_readManufacturerIdRaw(uint32_t& manufacturerId) {
  manufacturerId = 0;
  uint8_t bytes[3] = {};
  const Status status =
      _readDirectRaw(cmd::OPCODE_MANUFACTURER_ID, bytes, sizeof(bytes));
  if (status.ok()) {
    manufacturerId = (static_cast<uint32_t>(bytes[0]) << 16u) |
                     (static_cast<uint32_t>(bytes[1]) << 8u) |
                     static_cast<uint32_t>(bytes[2]);
  }
  return status;
}

Status Driver::_classifyManufacturerIdRaw(uint32_t manufacturerId,
                                          PartType& part,
                                          uint8_t& siliconRevision) {
  part = PartType::UNKNOWN;
  siliconRevision = static_cast<uint8_t>(
      manufacturerId & cmd::MANUFACTURER_ID_REVISION_MASK);
  const uint32_t partCode = manufacturerId & cmd::MANUFACTURER_ID_PART_MASK;
  if (partCode == cmd::MANUFACTURER_ID_AT21CS01_BASE) {
    part = PartType::AT21CS01;
    return Status::Ok();
  }
  if (partCode == cmd::MANUFACTURER_ID_AT21CS11_BASE) {
    part = PartType::AT21CS11;
    return Status::Ok();
  }
  return Status::Error(Err::PART_MISMATCH,
                       static_cast<int32_t>(manufacturerId));
}

Status Driver::_setSpeedModeRaw(SpeedMode mode,
                                TransferResult& transferResult) {
  transferResult = {};
  if (!isKnownSpeed(mode)) {
    return Status::Error(Err::INVALID_PARAM);
  }

  SingleWireTransfer transfer{};
  transfer.speed = _activeSpeed;
  transfer.deviceAddress = _deviceAddress(
      mode == SpeedMode::STANDARD_SPEED ? cmd::OPCODE_STANDARD_SPEED
                                        : cmd::OPCODE_HIGH_SPEED,
      false);
  transfer.minimumPostTransferHighUs = Bus::SPEED_CHANGE_HOLD_US;

  const Status status = _bus->_execute(transfer, transferResult);
  if (status.ok()) {
    _activeSpeed = mode;
    _speedKnown = true;
    return status;
  }

  const bool definiteAddressNack =
      status.code == Err::NACK_DEVICE_ADDRESS &&
      protocolDetailPhase(status.detail) ==
          ProtocolPhase::DEVICE_ADDRESS_WRITE;
  const bool typedTransportFailure =
      transferResult.code == TransportCode::TIMEOUT ||
      transferResult.code == TransportCode::LINE_STUCK ||
      transferResult.code == TransportCode::IO_ERROR;
  const bool failedBeforeAddress =
      typedTransportFailure &&
      transferResult.phase == TransferPhase::START &&
      !transferResult.firstDeviceAddressAcked &&
      !transferResult.memoryAddressAcked &&
      !transferResult.repeatedDeviceAddressAcked &&
      transferResult.dataBytesTransferred == 0 &&
      !transferResult.currentWriteByteMayBeAccepted;
  if (!definiteAddressNack && !failedBeforeAddress) {
    _speedKnown = false;
  }
  return status;
}

Status Driver::_runInitializationSequence() {
  bool present = false;
  TransferResult resetResult{};
  Status status = _bus->_resetAndDiscover(present, resetResult);
  if (!status.ok()) {
    _speedKnown = false;
    return status;
  }

  const BusSnapshot busState = _bus->snapshot();
  _seenBusBindingEpochValid = busState.bindingEpochValid;
  _seenBusBindingEpoch = busState.bindingEpoch;
  _seenBusGeneration = busState.generation;
  _activeSpeed = SpeedMode::HIGH_SPEED;
  _speedKnown = true;
  if (_state == DriverState::PROBING) {
    _setState(DriverState::INIT_CONFIG, false);
  }

  uint32_t rawId = 0;
  status = _readManufacturerIdRaw(rawId);
  if (!status.ok()) {
    return status;
  }

  PartType part = PartType::UNKNOWN;
  uint8_t revision = 0;
  status = _classifyManufacturerIdRaw(rawId, part, revision);
  _manufacturerId = rawId;
  _siliconRevision = revision;
  _detectedPart = part;
  if (!status.ok()) {
    return status;
  }
  if (_config.expectedPart != PartType::UNKNOWN &&
      part != _config.expectedPart) {
    return Status::Error(Err::PART_MISMATCH,
                         static_cast<int32_t>(rawId));
  }

  if (_config.startupSpeed == SpeedMode::STANDARD_SPEED) {
    TransferResult speedResult{};
    status = _setSpeedModeRaw(SpeedMode::STANDARD_SPEED, speedResult);
  }
  return status;
}

}  // namespace AT21CS
