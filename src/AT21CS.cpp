#include "AT21CS/AT21CS.h"

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

}  // namespace

Status Driver::bind(Bus& bus, const Config& config) {
  const Status configStatus = validateConfig(config);
  if (!configStatus.ok()) {
    return configStatus;
  }
  const BusSnapshot busState = bus.snapshot();
  if (!busState.bound || !busState.bindingEpochValid) {
    return Status::Error(Err::NOT_BOUND);
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

void Driver::end() {
  if (_bound && _bus != nullptr) {
    _bus->_releaseAddress(_config.addressBits);
  }
  _resetLocalState();
}

bool Driver::isBound() const {
  return _bound;
}

bool Driver::isInitialized() const {
  return _initialized;
}

bool Driver::isOnline() const {
  return _bound && _initialized && _speedKnown && _hasCurrentBusBinding() &&
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
  return _speedKnown && _hasCurrentBusBinding();
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
  value.speedKnown = _speedKnown;
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

void Driver::_resetLocalState() {
  _bus = nullptr;
  _config = {};
  _bound = false;
  _initialized = false;
  _state = DriverState::UNINIT;
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

}  // namespace AT21CS
