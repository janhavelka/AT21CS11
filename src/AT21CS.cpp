/// @file AT21CS.cpp
/// @brief Implementation of the AT21CS01/AT21CS11 single-wire EEPROM driver.

#include "AT21CS/AT21CS.h"

#include <climits>
#include <cstring>

namespace {

inline void incrementWrap(uint8_t& value) {
  if (value != UINT8_MAX) {
    ++value;
  }
}

inline void incrementWrap(uint32_t& value) {
  if (value != UINT32_MAX) {
    ++value;
  }
}

inline uint16_t retryAttempts(uint8_t retries) {
  return static_cast<uint16_t>(retries) + 1U;
}

inline bool rangeFits(uint8_t startAddress, size_t len, size_t totalSize) {
  if (len == 0) {
    return false;
  }
  if (static_cast<size_t>(startAddress) >= totalSize) {
    return false;
  }
  const size_t available = totalSize - static_cast<size_t>(startAddress);
  return len <= available;
}

inline bool staysWithinPage(uint8_t startAddress, size_t len, size_t pageSize) {
  if (len == 0 || pageSize == 0) {
    return false;
  }
  const size_t pageOffset = static_cast<size_t>(startAddress) % pageSize;
  const size_t availableInPage = pageSize - pageOffset;
  return len <= availableInPage;
}

inline bool isValidPartType(AT21CS::PartType part) {
  switch (part) {
    case AT21CS::PartType::UNKNOWN:
    case AT21CS::PartType::AT21CS01:
    case AT21CS::PartType::AT21CS11:
      return true;
  }
  return false;
}

inline bool isValidSpeedMode(AT21CS::SpeedMode mode) {
  switch (mode) {
    case AT21CS::SpeedMode::HIGH_SPEED:
    case AT21CS::SpeedMode::STANDARD_SPEED:
      return true;
  }
  return false;
}

inline uint32_t saturatedAdd(uint32_t lhs, uint32_t rhs) {
  const uint32_t room = UINT32_MAX - lhs;
  return (rhs > room) ? UINT32_MAX : (lhs + rhs);
}

inline uint32_t waitReadyStallGuardIterations(uint32_t timeoutMs) {
  uint32_t polls = timeoutMs;
  if (polls > UINT32_MAX / 10U) {
    polls = UINT32_MAX;
  } else {
    polls *= 10U;  // waitReady sleeps 100 us between polls: 10 polls/ms.
  }
  polls = saturatedAdd(polls, 16U);
  return (polls < 32U) ? 32U : polls;
}

static constexpr uint32_t MAX_READY_TIMEOUT_MS = 250;

inline bool hasRequiredTransportPrimitives(const AT21CS::SingleWireTransport& transport,
                                           bool configHasSleepUs) {
  return transport.releaseLine != nullptr &&
         transport.writeByteReadAck != nullptr &&
         transport.readByteSendAck != nullptr &&
         transport.resetAndDiscover != nullptr &&
         (configHasSleepUs || transport.sleepUs != nullptr);
}

}  // namespace

namespace AT21CS {

constexpr Driver::TimingProfile Driver::HIGH_SPEED_TIMING;
constexpr Driver::TimingProfile Driver::STANDARD_SPEED_TIMING;

Status Driver::begin(const Config& config) {
  if (_initialized) {
    end();
  } else if (_hasTransport()) {
    _releaseLine();
    if (_config.transport->end != nullptr) {
      _config.transport->end(_config.transport->user);
    }
  } else if (_config.sioPin >= 0) {
    // Clean up the line from a previously failed begin() that configured it
    // but didn't complete initialization.
    _releaseLine();
  }

  _config = Config{};
  _initialized = false;
  _driverState = DriverState::UNINIT;
  _detectedPart = PartType::UNKNOWN;
  _setSpeedMode(SpeedMode::HIGH_SPEED);
  _resetHealth();
  _lastTickMs = 0;

  auto failBegin = [this](Status failure, DriverState state) -> Status {
    _initialized = false;
    _driverState = state;
    _detectedPart = PartType::UNKNOWN;
    _setSpeedMode(SpeedMode::HIGH_SPEED);
    _resetHealth();
    return failure;
  };

  const bool usesTransport = (config.transport != nullptr);
  if (usesTransport &&
      !hasRequiredTransportPrimitives(*config.transport, config.sleepUs != nullptr)) {
    return failBegin(
        Status::Error(Err::INVALID_CONFIG, "transport missing required single-wire primitives"),
        DriverState::FAULT);
  }
  if (usesTransport && config.sioPin >= 0) {
    return failBegin(
        Status::Error(Err::INVALID_CONFIG, "sioPin is only valid with built-in backend"),
        DriverState::FAULT);
  }
  if (usesTransport && config.presencePin != -1) {
    return failBegin(
        Status::Error(Err::INVALID_CONFIG,
                      "presencePin is only valid with built-in backend"),
        DriverState::FAULT);
  }
  if (!usesTransport && config.sioPin < 0) {
    return failBegin(Status::Error(Err::INVALID_CONFIG, "sioPin must be >= 0"),
                     DriverState::FAULT);
  }
  if (!usesTransport && config.sioPin > 63) {
    return failBegin(Status::Error(Err::INVALID_CONFIG, "sioPin must be <= 63"),
                     DriverState::FAULT);
  }
  if (!usesTransport && config.presencePin > 63) {
    return failBegin(Status::Error(Err::INVALID_CONFIG, "presencePin must be <= 63"),
                     DriverState::FAULT);
  }
  if (!usesTransport && config.presencePin < -1) {
    return failBegin(
        Status::Error(Err::INVALID_CONFIG, "presencePin must be -1 or in range 0..63"),
        DriverState::FAULT);
  }
  if (!usesTransport && config.presencePin >= 0 && config.presencePin == config.sioPin) {
    return failBegin(
        Status::Error(Err::INVALID_CONFIG, "presencePin must be different from sioPin"),
        DriverState::FAULT);
  }
  if (config.addressBits > 0x07) {
    return failBegin(Status::Error(Err::INVALID_CONFIG, "addressBits must be in range 0..7"),
                     DriverState::FAULT);
  }
  if (config.writeTimeoutMs == 0) {
    return failBegin(Status::Error(Err::INVALID_CONFIG, "writeTimeoutMs must be > 0"),
                     DriverState::FAULT);
  }
  if (config.writeTimeoutMs > MAX_READY_TIMEOUT_MS) {
    return failBegin(Status::Error(Err::INVALID_CONFIG, "writeTimeoutMs must be <= 250"),
                     DriverState::FAULT);
  }
  if (!isValidPartType(config.expectedPart)) {
    return failBegin(Status::Error(Err::INVALID_CONFIG, "invalid expectedPart enum"),
                     DriverState::FAULT);
  }
  if (!isValidSpeedMode(config.startupSpeed)) {
    return failBegin(Status::Error(Err::INVALID_CONFIG, "invalid startupSpeed enum"),
                     DriverState::FAULT);
  }

  _config = config;
  if (_config.offlineThreshold == 0) {
    _config.offlineThreshold = 1;
  }
  _initialized = false;
  _driverState = DriverState::UNINIT;
  _detectedPart = PartType::UNKNOWN;
  _setSpeedMode(SpeedMode::HIGH_SPEED);
  _resetHealth();

  Status st = _configurePins();
  if (!st.ok()) {
    return failBegin(st, DriverState::FAULT);
  }

  if (_hasPresenceIndicator() && !_presencePinReportsPresent()) {
    return failBegin(Status::Error(Err::NOT_PRESENT, "Presence pin indicates device absent"),
                     DriverState::OFFLINE);
  }

  _driverState = DriverState::PROBING;
  Status discovery = Status::Error(Err::DISCOVERY_FAILED, "Discovery failed");
  const uint16_t attempts = retryAttempts(_config.discoveryRetries);
  for (uint16_t attempt = 0; attempt < attempts; ++attempt) {
    discovery = _resetAndDiscoverRaw();
    if (discovery.ok()) {
      break;
    }
  }
  if (!discovery.ok()) {
    return failBegin(
        Status::Error(Err::NOT_PRESENT, "Device did not respond to reset/discovery"),
        DriverState::OFFLINE);
  }

  uint32_t manufacturerId = 0;
  st = _readManufacturerIdRaw(manufacturerId);
  if (!st.ok()) {
    return failBegin(st, DriverState::OFFLINE);
  }

  PartType detected = PartType::UNKNOWN;
  if (manufacturerId == cmd::MANUFACTURER_ID_AT21CS01) {
    detected = PartType::AT21CS01;
  } else if (manufacturerId == cmd::MANUFACTURER_ID_AT21CS11) {
    detected = PartType::AT21CS11;
  } else {
    return failBegin(
        Status::Error(Err::PART_MISMATCH, "Unknown manufacturer ID",
                      static_cast<int32_t>(manufacturerId)),
        DriverState::FAULT);
  }

  if (_config.expectedPart != PartType::UNKNOWN && _config.expectedPart != detected) {
    return failBegin(Status::Error(Err::PART_MISMATCH, "Detected part does not match expectedPart"),
                     DriverState::FAULT);
  }

  if (_config.startupSpeed == SpeedMode::STANDARD_SPEED && detected == PartType::AT21CS11) {
    return failBegin(
        Status::Error(Err::INVALID_CONFIG, "AT21CS11 does not support Standard Speed"),
        DriverState::FAULT);
  }

  _detectedPart = detected;

  _driverState = DriverState::INIT_CONFIG;
  if (_config.startupSpeed == SpeedMode::STANDARD_SPEED) {
    bool ack = false;
    st = _addressOnlyRaw(cmd::OPCODE_STANDARD_SPEED, false, ack);
    if (!st.ok()) {
      return failBegin(st, DriverState::OFFLINE);
    }
    if (!ack) {
      return failBegin(
          Status::Error(Err::NACK_DEVICE_ADDRESS, "Standard Speed command NACK during begin()"),
          DriverState::FAULT);
    }
    _setSpeedMode(SpeedMode::STANDARD_SPEED);
  } else {
    _setSpeedMode(SpeedMode::HIGH_SPEED);
  }

  _initialized = true;
  _driverState = DriverState::READY;
  _lastOkMs = _nowMs();
  _lastTickMs = _lastOkMs;
  _lastError = Status::Ok();

  return Status::Ok();
}

void Driver::tick(uint32_t nowMs) {
  if (!_initialized) {
    return;
  }
  _lastTickMs = nowMs;
}

void Driver::end() {
  if (_hasTransport()) {
    _releaseLine();
    if (_config.transport->end != nullptr) {
      _config.transport->end(_config.transport->user);
    }
  } else if (_config.sioPin >= 0) {
    _releaseLine();
  }

  _config = Config{};
  _initialized = false;
  _driverState = DriverState::UNINIT;
  _detectedPart = PartType::UNKNOWN;
  _setSpeedMode(SpeedMode::HIGH_SPEED);
  _resetHealth();
  _backend = BuiltInBackendState{};
}

SettingsSnapshot Driver::getSettings() const {
  SettingsSnapshot snapshot;
  (void)getSettings(snapshot);
  return snapshot;
}

Status Driver::getSettings(SettingsSnapshot& out) const {
  out.config = _config;
  out.initialized = _initialized;
  out.state = _driverState;
  out.detectedPart = _detectedPart;
  out.speedMode = _speedMode;
  out.lastOkMs = _lastOkMs;
  out.lastErrorMs = _lastErrorMs;
  out.lastError = _lastError;
  out.consecutiveFailures = _consecutiveFailures;
  out.totalFailures = _totalFailures;
  out.totalSuccess = _totalSuccess;
  return Status::Ok();
}

Status Driver::probe() {
  Status st = _checkInitialized(true);
  if (!st.ok()) {
    return st;
  }

  if (_hasPresenceIndicator()) {
    if (!_presencePinReportsPresent()) {
      return Status::Error(Err::NOT_PRESENT, "Presence pin indicates device absent");
    }
    return Status::Ok();
  }

  const DriverState previous = _driverState;
  _driverState = DriverState::PROBING;
  st = _resetAndDiscoverRaw();
  _driverState = previous;
  return st;
}

Status Driver::recover() {
  Status st = _checkInitialized(true);
  if (!st.ok()) {
    return st;
  }

  if (_hasPresenceIndicator() && !_presencePinReportsPresent()) {
    _driverState = DriverState::OFFLINE;
    return _trackIo(Status::Error(Err::NOT_PRESENT, "Presence pin indicates device absent"));
  }

  _driverState = DriverState::RECOVERING;
  Status discovery = Status::Error(Err::DISCOVERY_FAILED, "Discovery failed");
  const uint16_t attempts = retryAttempts(_config.discoveryRetries);
  for (uint16_t attempt = 0; attempt < attempts; ++attempt) {
    discovery = _resetAndDiscoverRaw();
    if (discovery.ok()) {
      break;
    }
  }
  if (!discovery.ok()) {
    return _trackIo(discovery);
  }

  uint32_t manufacturerId = 0;
  st = _readManufacturerIdRaw(manufacturerId);
  if (!st.ok()) {
    return _trackIo(st);
  }

  if (manufacturerId == cmd::MANUFACTURER_ID_AT21CS01) {
    _detectedPart = PartType::AT21CS01;
  } else if (manufacturerId == cmd::MANUFACTURER_ID_AT21CS11) {
    _detectedPart = PartType::AT21CS11;
  } else {
    return _trackIo(Status::Error(Err::PART_MISMATCH, "Unknown manufacturer ID", static_cast<int32_t>(manufacturerId)));
  }

  if (_config.expectedPart != PartType::UNKNOWN && _config.expectedPart != _detectedPart) {
    return _trackIo(Status::Error(Err::PART_MISMATCH, "Detected part does not match expectedPart"));
  }

  // After reset+discovery, device is always in High-Speed mode.
  // Re-apply startup speed setting if Standard Speed was configured.
  if (_config.startupSpeed == SpeedMode::STANDARD_SPEED && _detectedPart == PartType::AT21CS01) {
    bool ack = false;
    st = _addressOnlyRaw(cmd::OPCODE_STANDARD_SPEED, false, ack);
    if (!st.ok()) {
      return _trackIo(st);
    }
    if (!ack) {
      return _trackIo(
          Status::Error(Err::NACK_DEVICE_ADDRESS, "Standard Speed command NACK during recovery"));
    }
    _setSpeedMode(SpeedMode::STANDARD_SPEED);
  } else {
    _setSpeedMode(SpeedMode::HIGH_SPEED);
  }

  return _trackIo(Status::Ok());
}

Status Driver::resetAndDiscover() {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  if (_hasPresenceIndicator() && !_presencePinReportsPresent()) {
    _driverState = DriverState::OFFLINE;
    return _trackIo(Status::Error(Err::NOT_PRESENT, "Presence pin indicates device absent"));
  }

  _driverState = DriverState::PROBING;
  Status discovery = Status::Error(Err::DISCOVERY_FAILED, "Discovery failed");
  const uint16_t attempts = retryAttempts(_config.discoveryRetries);
  for (uint16_t attempt = 0; attempt < attempts; ++attempt) {
    discovery = _resetAndDiscoverRaw();
    if (discovery.ok()) {
      return _trackIo(discovery);
    }
  }
  return _trackIo(discovery);
}

Status Driver::isPresent(bool& present) {
  present = false;

  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  if (_hasPresenceIndicator()) {
    present = _presencePinReportsPresent();
    return Status::Ok();
  }

  _driverState = DriverState::PROBING;
  Status discovery = Status::Error(Err::DISCOVERY_FAILED, "Discovery failed");
  const uint16_t attempts = retryAttempts(_config.discoveryRetries);
  for (uint16_t attempt = 0; attempt < attempts; ++attempt) {
    discovery = _resetAndDiscoverRaw();
    if (discovery.ok()) {
      present = true;
      return _trackIo(Status::Ok());
    }
  }

  present = false;
  return _trackIo(discovery);
}

Status Driver::waitReady(uint32_t timeoutMs) {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }
  if (timeoutMs == 0) {
    return Status::Error(Err::INVALID_PARAM, "timeoutMs must be > 0");
  }
  if (timeoutMs > MAX_READY_TIMEOUT_MS) {
    return Status::Error(Err::INVALID_PARAM, "timeoutMs must be <= 250");
  }

  if (_hasPresenceIndicator() && !_presencePinReportsPresent()) {
    _driverState = DriverState::OFFLINE;
    return _trackIo(Status::Error(Err::NOT_PRESENT, "Presence pin indicates device absent"));
  }

  _driverState = DriverState::BUSY;
  const uint32_t startMs = _nowMs();
  uint32_t lastObservedMs = startMs;
  uint32_t stalledPolls = 0;
  const uint32_t maxStalledPolls = waitReadyStallGuardIterations(timeoutMs);
  while (true) {
    if (_hasPresenceIndicator() && !_presencePinReportsPresent()) {
      _driverState = DriverState::OFFLINE;
      return _trackIo(Status::Error(Err::NOT_PRESENT, "Presence pin indicates device absent"));
    }

    bool ack = false;
    st = _addressOnlyRaw(cmd::OPCODE_EEPROM, false, ack);
    if (!st.ok()) {
      return _trackIo(st);
    }
    if (ack) {
      return _trackIo(Status::Ok());
    }

    const uint32_t elapsedMs = _nowMs() - startMs;
    if (elapsedMs >= timeoutMs) {
      return _trackIo(Status::Error(Err::BUSY_TIMEOUT, "Timed out waiting for write cycle completion"));
    }
    const uint32_t observedMs = _nowMs();
    if (observedMs == lastObservedMs) {
      if (++stalledPolls >= maxStalledPolls) {
        return _trackIo(Status::Error(Err::BUSY_TIMEOUT, "Timed out waiting for write cycle completion"));
      }
    } else {
      lastObservedMs = observedMs;
      stalledPolls = 0;
    }

    _sleepUs(100);
  }
}

Status Driver::readCurrentAddress(uint8_t& value) {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = _readCurrentAddressRaw(value);
  return _trackIo(st);
}

Status Driver::readEeprom(uint8_t address, uint8_t* data, size_t len) {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }
  if (data == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "EEPROM read buffer is null");
  }
  if (!rangeFits(address, len, cmd::EEPROM_SIZE)) {
    return Status::Error(Err::INVALID_PARAM, "EEPROM read range out of bounds");
  }

  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = _readRandomRaw(cmd::OPCODE_EEPROM, address, data, len);
  return _trackIo(st);
}

Status Driver::writeEepromByte(uint8_t address, uint8_t value) {
  return writeEepromPage(address, &value, 1);
}

Status Driver::writeEepromPage(uint8_t address, const uint8_t* data, size_t len) {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }
  if (data == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "EEPROM write buffer is null");
  }
  if (len > cmd::PAGE_SIZE) {
    return Status::Error(Err::INVALID_PARAM, "EEPROM page write length must be 1..8");
  }
  if (len == 0) {
    return Status::Error(Err::INVALID_PARAM, "EEPROM page write length must be 1..8");
  }
  if (!rangeFits(address, len, cmd::EEPROM_SIZE)) {
    return Status::Error(Err::INVALID_PARAM, "EEPROM write range out of bounds");
  }
  if (!staysWithinPage(address, len, cmd::PAGE_SIZE)) {
    return Status::Error(Err::INVALID_PARAM, "EEPROM page write crosses page boundary");
  }

  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = _writeRaw(cmd::OPCODE_EEPROM, address, data, len);
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = waitReady(_config.writeTimeoutMs);
  return st;
}

Status Driver::writeEeprom(uint8_t address, const uint8_t* data, size_t len) {
  Status init = _checkInitialized();
  if (!init.ok()) {
    return init;
  }
  if (data == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "EEPROM write buffer is null");
  }
  if (len == 0) {
    return Status::Error(Err::INVALID_PARAM, "EEPROM write length must be >= 1");
  }
  if (!rangeFits(address, len, cmd::EEPROM_SIZE)) {
    return Status::Error(Err::INVALID_PARAM, "EEPROM write range out of bounds");
  }

  size_t offset = 0;
  while (offset < len) {
    const uint8_t curAddr = static_cast<uint8_t>(address + offset);
    const uint8_t pageOffset = curAddr % cmd::PAGE_SIZE;
    size_t chunk = cmd::PAGE_SIZE - pageOffset;
    if (chunk > len - offset) {
      chunk = len - offset;
    }
    Status st = writeEepromPage(curAddr, data + offset, chunk);
    if (!st.ok()) {
      return st;
    }
    offset += chunk;
  }
  return Status::Ok();
}

Status Driver::readSecurity(uint8_t address, uint8_t* data, size_t len) {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }
  if (data == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Security read buffer is null");
  }
  if (!rangeFits(address, len, cmd::SECURITY_SIZE)) {
    return Status::Error(Err::INVALID_PARAM, "Security read range out of bounds");
  }

  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = _readRandomRaw(cmd::OPCODE_SECURITY, address, data, len);
  return _trackIo(st);
}

Status Driver::writeSecurityUserByte(uint8_t address, uint8_t value) {
  return writeSecurityUserPage(address, &value, 1);
}

Status Driver::writeSecurityUserPage(uint8_t address, const uint8_t* data, size_t len) {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }
  if (!_isSecurityUserAddressValid(address)) {
    return Status::Error(Err::INVALID_PARAM, "Security writes are allowed only in 0x10..0x1F");
  }
  if (data == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Security write buffer is null");
  }
  if (len == 0 || len > cmd::PAGE_SIZE) {
    return Status::Error(Err::INVALID_PARAM, "Security page write length must be 1..8");
  }
  const uint16_t endAddress = static_cast<uint16_t>(address) + static_cast<uint16_t>(len);
  if (endAddress > static_cast<uint16_t>(cmd::SECURITY_USER_MAX) + 1U) {
    return Status::Error(Err::INVALID_PARAM, "Security write exceeds user area 0x10..0x1F");
  }
  if (!staysWithinPage(address, len, cmd::PAGE_SIZE)) {
    return Status::Error(Err::INVALID_PARAM, "Security page write crosses page boundary");
  }

  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = _writeRaw(cmd::OPCODE_SECURITY, address, data, len);
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = waitReady(_config.writeTimeoutMs);
  return st;
}

Status Driver::writeSecurityUser(uint8_t address, const uint8_t* data, size_t len) {
  Status init = _checkInitialized();
  if (!init.ok()) {
    return init;
  }
  if (data == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Security write buffer is null");
  }
  if (len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Security write length must be >= 1");
  }
  if (!_isSecurityUserAddressValid(address)) {
    return Status::Error(Err::INVALID_PARAM, "Security writes are allowed only in 0x10..0x1F");
  }
  const uint16_t endAddress = static_cast<uint16_t>(address) + static_cast<uint16_t>(len);
  if (endAddress > static_cast<uint16_t>(cmd::SECURITY_USER_MAX) + 1U) {
    return Status::Error(Err::INVALID_PARAM, "Security write exceeds user area 0x10..0x1F");
  }

  size_t offset = 0;
  while (offset < len) {
    const uint8_t curAddr = static_cast<uint8_t>(address + offset);
    const uint8_t pageOffset = curAddr % cmd::PAGE_SIZE;
    size_t chunk = cmd::PAGE_SIZE - pageOffset;
    if (chunk > len - offset) {
      chunk = len - offset;
    }
    Status st = writeSecurityUserPage(curAddr, data + offset, chunk);
    if (!st.ok()) {
      return st;
    }
    offset += chunk;
  }
  return Status::Ok();
}

Status Driver::lockSecurityRegister() {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  const uint8_t lockData = 0x00;
  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = _writeRaw(cmd::OPCODE_LOCK_SECURITY, cmd::LOCK_SECURITY_ADDRESS, &lockData, 1);
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = waitReady(_config.writeTimeoutMs);
  return st;
}

Status Driver::isSecurityLocked(bool& locked) {
  locked = false;

  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  bool ack = false;
  st = _addressOnlyRaw(cmd::OPCODE_LOCK_SECURITY, true, ack);
  if (!st.ok()) {
    return _trackIo(st);
  }

  locked = !ack;
  return _trackIo(Status::Ok());
}

Status Driver::readSerialNumber(SerialNumberInfo& serial) {
  std::memset(&serial, 0, sizeof(serial));

  Status st = readSecurity(cmd::SECURITY_SERIAL_START, serial.bytes, cmd::SECURITY_SERIAL_SIZE);
  if (!st.ok()) {
    return st;
  }

  serial.productIdOk = (serial.bytes[0] == cmd::SECURITY_PRODUCT_ID);
  const uint8_t crc = crc8_31(serial.bytes, cmd::SECURITY_SERIAL_SIZE - 1U);
  serial.crcOk = (crc == serial.bytes[cmd::SECURITY_SERIAL_SIZE - 1U]);

  if (!serial.productIdOk) {
    return Status::Error(Err::PART_MISMATCH, "Serial product ID is not 0xA0");
  }
  if (!serial.crcOk) {
    return Status::Error(Err::CRC_MISMATCH, "Serial number CRC check failed");
  }

  return Status::Ok();
}

Status Driver::readManufacturerId(uint32_t& manufacturerId) {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = _readManufacturerIdRaw(manufacturerId);
  return _trackIo(st);
}

Status Driver::detectPart(PartType& part) {
  part = PartType::UNKNOWN;

  uint32_t manufacturerId = 0;
  Status st = readManufacturerId(manufacturerId);
  if (!st.ok()) {
    return st;
  }

  if (manufacturerId == cmd::MANUFACTURER_ID_AT21CS01) {
    part = PartType::AT21CS01;
  } else if (manufacturerId == cmd::MANUFACTURER_ID_AT21CS11) {
    part = PartType::AT21CS11;
  } else {
    return Status::Error(Err::PART_MISMATCH, "Unknown manufacturer ID", static_cast<int32_t>(manufacturerId));
  }

  return Status::Ok();
}

Status Driver::readRomZoneRegister(uint8_t zoneIndex, uint8_t& value) {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }
  if (!_isZoneIndexValid(zoneIndex)) {
    return Status::Error(Err::INVALID_PARAM, "zoneIndex must be in range 0..3");
  }

  const uint8_t zoneRegisterAddress = cmd::ROM_ZONE_REGISTERS[zoneIndex];
  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = _readRandomRaw(cmd::OPCODE_ROM_ZONE, zoneRegisterAddress, &value, 1);
  return _trackIo(st);
}

Status Driver::isZoneRom(uint8_t zoneIndex, bool& isRom) {
  isRom = false;
  uint8_t value = 0;
  Status st = readRomZoneRegister(zoneIndex, value);
  if (!st.ok()) {
    return st;
  }

  isRom = (value == cmd::ROM_ZONE_ROM_VALUE);
  return Status::Ok();
}

Status Driver::setZoneRom(uint8_t zoneIndex) {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }
  if (!_isZoneIndexValid(zoneIndex)) {
    return Status::Error(Err::INVALID_PARAM, "zoneIndex must be in range 0..3");
  }

  const uint8_t zoneRegisterAddress = cmd::ROM_ZONE_REGISTERS[zoneIndex];
  const uint8_t value = cmd::ROM_ZONE_ROM_VALUE;
  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = _writeRaw(cmd::OPCODE_ROM_ZONE, zoneRegisterAddress, &value, 1);
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = waitReady(_config.writeTimeoutMs);
  return st;
}

Status Driver::freezeRomZones() {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  const uint8_t data = cmd::FREEZE_ROM_DATA;
  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = _writeRaw(cmd::OPCODE_FREEZE_ROM, cmd::FREEZE_ROM_ADDR, &data, 1);
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = waitReady(_config.writeTimeoutMs);
  return st;
}

Status Driver::areRomZonesFrozen(bool& frozen) {
  frozen = false;

  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  bool ack = false;
  st = _addressOnlyRaw(cmd::OPCODE_FREEZE_ROM, true, ack);
  if (!st.ok()) {
    return _trackIo(st);
  }

  frozen = !ack;
  return _trackIo(Status::Ok());
}

Status Driver::setHighSpeed() {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  bool ack = false;
  st = _addressOnlyRaw(cmd::OPCODE_HIGH_SPEED, false, ack);
  if (!st.ok()) {
    return _trackIo(st);
  }
  if (!ack) {
    return _trackIo(Status::Error(Err::NACK_DEVICE_ADDRESS, "High-Speed command NACK"));
  }

  _setSpeedMode(SpeedMode::HIGH_SPEED);
  return _trackIo(Status::Ok());
}

Status Driver::isHighSpeed(bool& enabled) {
  enabled = false;

  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  bool ack = false;
  st = _addressOnlyRaw(cmd::OPCODE_HIGH_SPEED, true, ack);
  if (!st.ok()) {
    return _trackIo(st);
  }

  enabled = ack;
  return _trackIo(Status::Ok());
}

Status Driver::setStandardSpeed() {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  bool ack = false;
  st = _addressOnlyRaw(cmd::OPCODE_STANDARD_SPEED, false, ack);
  if (!st.ok()) {
    return _trackIo(st);
  }
  if (!ack) {
    if (_detectedPart == PartType::AT21CS11) {
      return _trackIo(Status::Error(Err::UNSUPPORTED_COMMAND, "AT21CS11 does not support Standard Speed"));
    }
    return _trackIo(Status::Error(Err::NACK_DEVICE_ADDRESS, "Standard Speed command NACK"));
  }

  _setSpeedMode(SpeedMode::STANDARD_SPEED);
  return _trackIo(Status::Ok());
}

Status Driver::isStandardSpeed(bool& enabled) {
  enabled = false;

  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }
  if (_detectedPart == PartType::AT21CS11) {
    return Status::Error(Err::UNSUPPORTED_COMMAND, "AT21CS11 does not support Standard Speed");
  }

  st = _activateDevice();
  if (!st.ok()) {
    return _trackIo(st);
  }

  bool ack = false;
  st = _addressOnlyRaw(cmd::OPCODE_STANDARD_SPEED, true, ack);
  if (!st.ok()) {
    return _trackIo(st);
  }

  enabled = ack;
  return _trackIo(Status::Ok());
}

uint8_t Driver::crc8_31(const uint8_t* data, size_t len) {
  if (len == 0) {
    return 0;
  }
  if (data == nullptr) {
    return 0;
  }

  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((crc & 0x01U) != 0U) {
        crc = static_cast<uint8_t>((crc >> 1U) ^ 0x8CU);
      } else {
        crc = static_cast<uint8_t>(crc >> 1U);
      }
    }
  }
  return crc;
}

Status Driver::_trackIo(const Status& st) {
  if (!_initialized) {
    return st;
  }

  const uint32_t nowMs = _nowMs();
  if (st.ok()) {
    _lastOkMs = nowMs;
    _lastError = Status::Ok();
    _consecutiveFailures = 0;
    incrementWrap(_totalSuccess);
    if (_driverState != DriverState::SLEEPING) {
      _driverState = DriverState::READY;
    }
    return st;
  }

  _lastErrorMs = nowMs;
  _lastError = st;
  incrementWrap(_totalFailures);
  incrementWrap(_consecutiveFailures);

  if (st.code == Err::PART_MISMATCH || st.code == Err::INVALID_CONFIG) {
    _driverState = DriverState::FAULT;
    return st;
  }
  if (st.code == Err::NOT_PRESENT) {
    _driverState = DriverState::OFFLINE;
    return st;
  }

  if (_consecutiveFailures >= _config.offlineThreshold) {
    _driverState = DriverState::OFFLINE;
  } else {
    _driverState = DriverState::DEGRADED;
  }

  return st;
}

Status Driver::_checkInitialized(bool allowOffline) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() must succeed before this operation");
  }
  if (!allowOffline && _driverState == DriverState::OFFLINE) {
    return Status::Error(Err::INVALID_STATE, "Driver is offline; call recover()");
  }
  if (_driverState == DriverState::FAULT) {
    return Status::Error(Err::INVALID_STATE, "Driver in FAULT state; call begin() to reinitialize");
  }
  if (_driverState == DriverState::SLEEPING) {
    return Status::Error(Err::INVALID_STATE, "Driver is sleeping");
  }
  return Status::Ok();
}

bool Driver::_hasTransport() const {
  return _config.transport != nullptr;
}

bool Driver::_hasPresenceIndicator() const {
  if (_hasTransport() && _config.transport->presencePresent != nullptr) {
    return true;
  }
  return _config.presencePin >= 0;
}

uint8_t Driver::_deviceAddress(uint8_t opcode, bool read) const {
  const uint8_t rw = read ? 0x01U : 0x00U;
  return static_cast<uint8_t>((opcode << 4U) | ((_config.addressBits & 0x07U) << 1U) | rw);
}

Status Driver::_activateDevice() {
  const SpeedMode desiredSpeed = _speedMode;
  Status st = Status::Error(Err::DISCOVERY_FAILED, "Discovery failed");
  const uint16_t attempts = retryAttempts(_config.discoveryRetries);
  for (uint16_t attempt = 0; attempt < attempts; ++attempt) {
    st = _resetAndDiscoverRaw();
    if (st.ok()) {
      break;
    }
  }
  if (!st.ok()) {
    return st;
  }

  if (desiredSpeed == SpeedMode::STANDARD_SPEED) {
    if (_detectedPart == PartType::AT21CS11) {
      return Status::Error(Err::UNSUPPORTED_COMMAND, "AT21CS11 does not support Standard Speed");
    }

    bool ack = false;
    st = _addressOnlyRaw(cmd::OPCODE_STANDARD_SPEED, false, ack);
    if (!st.ok()) {
      return st;
    }
    if (!ack) {
      return Status::Error(Err::NACK_DEVICE_ADDRESS, "Standard Speed command NACK during activation");
    }
    _setSpeedMode(SpeedMode::STANDARD_SPEED);
  }

  return Status::Ok();
}

Status Driver::_addressOnlyRaw(uint8_t opcode, bool read, bool& ack) {
  _sendStart();
  ack = txByte(_deviceAddress(opcode, read));
  _sendStop();
  return Status::Ok();
}

Status Driver::_readRandomRaw(uint8_t opcode, uint8_t address, uint8_t* data, size_t len) {
  _sendStart();
  if (!txByte(_deviceAddress(opcode, false))) {
    _sendStop();
    return Status::Error(Err::NACK_DEVICE_ADDRESS, "Device address NACK");
  }

  if (!txByte(address)) {
    _sendStop();
    return Status::Error(Err::NACK_MEMORY_ADDRESS, "Memory address NACK");
  }

  _sendStart();
  if (!txByte(_deviceAddress(opcode, true))) {
    _sendStop();
    return Status::Error(Err::NACK_DEVICE_ADDRESS, "Device address NACK");
  }

  for (size_t i = 0; i < len; ++i) {
    const bool ack = (i + 1U) < len;
    data[i] = rxByte(ack);
  }

  _sendStop();
  return Status::Ok();
}

Status Driver::_writeRaw(uint8_t opcode, uint8_t address, const uint8_t* data, size_t len) {
  _sendStart();
  if (!txByte(_deviceAddress(opcode, false))) {
    _sendStop();
    return Status::Error(Err::NACK_DEVICE_ADDRESS, "Device address NACK");
  }

  if (!txByte(address)) {
    _sendStop();
    return Status::Error(Err::NACK_MEMORY_ADDRESS, "Memory address NACK");
  }

  for (size_t i = 0; i < len; ++i) {
    if (!txByte(data[i])) {
      _sendStop();
      return Status::Error(Err::NACK_DATA, "Data byte NACK", static_cast<int32_t>(i));
    }
  }

  _sendStop();
  return Status::Ok();
}

Status Driver::_readManufacturerIdRaw(uint32_t& manufacturerId) {
  _sendStart();
  if (!txByte(_deviceAddress(cmd::OPCODE_MANUFACTURER_ID, true))) {
    _sendStop();
    return Status::Error(Err::NACK_DEVICE_ADDRESS, "Manufacturer ID command NACK");
  }

  const uint8_t b0 = rxByte(true);
  const uint8_t b1 = rxByte(true);
  const uint8_t b2 = rxByte(false);

  _sendStop();

  manufacturerId =
      (static_cast<uint32_t>(b0) << 16U) |
      (static_cast<uint32_t>(b1) << 8U) |
      static_cast<uint32_t>(b2);

  return Status::Ok();
}

Status Driver::_readCurrentAddressRaw(uint8_t& value) {
  _sendStart();
  if (!txByte(_deviceAddress(cmd::OPCODE_EEPROM, true))) {
    _sendStop();
    return Status::Error(Err::NACK_DEVICE_ADDRESS, "Current address read NACK");
  }

  value = rxByte(false);
  _sendStop();
  return Status::Ok();
}

bool Driver::_isZoneIndexValid(uint8_t zoneIndex) {
  return zoneIndex < cmd::ROM_ZONE_REGISTER_COUNT;
}

bool Driver::_isSecurityUserAddressValid(uint8_t address) {
  return address >= cmd::SECURITY_USER_MIN && address <= cmd::SECURITY_USER_MAX;
}

void Driver::_setSpeedMode(SpeedMode mode) {
  _speedMode = mode;
  _timing = (mode == SpeedMode::STANDARD_SPEED) ? STANDARD_SPEED_TIMING : HIGH_SPEED_TIMING;
}

void Driver::_resetHealth() {
  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
}

}  // namespace AT21CS
