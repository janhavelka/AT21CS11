/// @file AT21CS.cpp
/// @brief Implementation of the AT21CS01/AT21CS11 single-wire EEPROM driver.

#include "AT21CS/AT21CS.h"

#include <Arduino.h>

#include <climits>

#if defined(ARDUINO_ARCH_ESP32)
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif

namespace {

inline bool deadlinePassed(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

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

}  // namespace

namespace AT21CS {

constexpr Driver::TimingProfile Driver::HIGH_SPEED_TIMING;
constexpr Driver::TimingProfile Driver::STANDARD_SPEED_TIMING;

Status Driver::begin(const Config& config) {
  if (config.sioPin < 0) {
    _driverState = DriverState::FAULT;
    return Status::Error(Err::INVALID_CONFIG, "sioPin must be >= 0");
  }
  if (config.sioPin > 63) {
    _driverState = DriverState::FAULT;
    return Status::Error(Err::INVALID_CONFIG, "sioPin must be <= 63");
  }
  if (config.presencePin > 63) {
    _driverState = DriverState::FAULT;
    return Status::Error(Err::INVALID_CONFIG, "presencePin must be <= 63");
  }
  if (config.addressBits > 0x07) {
    _driverState = DriverState::FAULT;
    return Status::Error(Err::INVALID_CONFIG, "addressBits must be in range 0..7");
  }
  if (config.offlineThreshold == 0) {
    _driverState = DriverState::FAULT;
    return Status::Error(Err::INVALID_CONFIG, "offlineThreshold must be > 0");
  }
  if (config.writeTimeoutMs == 0) {
    _driverState = DriverState::FAULT;
    return Status::Error(Err::INVALID_CONFIG, "writeTimeoutMs must be > 0");
  }

  _config = config;
  _initialized = false;
  _driverState = DriverState::UNINIT;
  _detectedPart = PartType::UNKNOWN;
  _setSpeedMode(SpeedMode::HIGH_SPEED);
  _resetHealth();

  Status st = _configurePins();
  if (!st.ok()) {
    _driverState = DriverState::FAULT;
    return st;
  }

  _driverState = DriverState::PROBING;
  Status discovery = Status::Error(Err::DISCOVERY_FAILED, "Discovery failed");
  const uint8_t attempts = static_cast<uint8_t>(_config.discoveryRetries + 1U);
  for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
    discovery = _resetAndDiscoverRaw();
    if (discovery.ok()) {
      break;
    }
  }
  if (!discovery.ok()) {
    _driverState = DriverState::OFFLINE;
    return Status::Error(Err::NOT_PRESENT, "Device did not respond to reset/discovery");
  }

  uint32_t manufacturerId = 0;
  st = _readManufacturerIdRaw(manufacturerId);
  if (!st.ok()) {
    _driverState = DriverState::OFFLINE;
    return st;
  }

  PartType detected = PartType::UNKNOWN;
  if (manufacturerId == cmd::MANUFACTURER_ID_AT21CS01) {
    detected = PartType::AT21CS01;
  } else if (manufacturerId == cmd::MANUFACTURER_ID_AT21CS11) {
    detected = PartType::AT21CS11;
  } else {
    _driverState = DriverState::FAULT;
    return Status::Error(Err::PART_MISMATCH, "Unknown manufacturer ID", static_cast<int32_t>(manufacturerId));
  }

  if (_config.expectedPart != PartType::UNKNOWN && _config.expectedPart != detected) {
    _driverState = DriverState::FAULT;
    return Status::Error(Err::PART_MISMATCH, "Detected part does not match expectedPart");
  }

  if (_config.startupSpeed == SpeedMode::STANDARD_SPEED && detected == PartType::AT21CS11) {
    _driverState = DriverState::FAULT;
    return Status::Error(Err::INVALID_CONFIG, "AT21CS11 does not support Standard Speed");
  }

  _detectedPart = detected;

  _driverState = DriverState::INIT_CONFIG;
  if (_config.startupSpeed == SpeedMode::STANDARD_SPEED) {
    bool ack = false;
    st = _addressOnlyRaw(cmd::OPCODE_STANDARD_SPEED, false, ack);
    if (!st.ok()) {
      _driverState = DriverState::OFFLINE;
      return st;
    }
    if (!ack) {
      _driverState = DriverState::FAULT;
      return Status::Error(Err::NACK_DEVICE_ADDRESS, "Standard Speed command NACK during begin()");
    }
    _setSpeedMode(SpeedMode::STANDARD_SPEED);
  } else {
    _setSpeedMode(SpeedMode::HIGH_SPEED);
  }

  _initialized = true;
  _driverState = DriverState::READY;
  _lastOkMs = millis();
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
  if (_config.sioPin >= 0) {
    _releaseLine();
  }

  _initialized = false;
  _driverState = DriverState::UNINIT;
  _detectedPart = PartType::UNKNOWN;
  _setSpeedMode(SpeedMode::HIGH_SPEED);
  _resetHealth();
}

Status Driver::probe() {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  const DriverState previous = _driverState;
  _driverState = DriverState::PROBING;
  st = _resetAndDiscoverRaw();
  _driverState = previous;
  return st;
}

Status Driver::recover() {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  _driverState = DriverState::RECOVERING;
  Status discovery = Status::Error(Err::DISCOVERY_FAILED, "Discovery failed");
  const uint8_t attempts = static_cast<uint8_t>(_config.discoveryRetries + 1U);
  for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
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

  return _trackIo(Status::Ok());
}

Status Driver::resetAndDiscover() {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  _driverState = DriverState::PROBING;
  Status discovery = Status::Error(Err::DISCOVERY_FAILED, "Discovery failed");
  const uint8_t attempts = static_cast<uint8_t>(_config.discoveryRetries + 1U);
  for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
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

  if (_config.presencePin >= 0) {
    const int level = digitalRead(_config.presencePin);
    const bool active = _config.presenceActiveHigh ? (level != 0) : (level == 0);
    if (!active) {
      present = false;
      return Status::Ok();
    }
  }

  _driverState = DriverState::PROBING;
  Status discovery = Status::Error(Err::DISCOVERY_FAILED, "Discovery failed");
  const uint8_t attempts = static_cast<uint8_t>(_config.discoveryRetries + 1U);
  for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
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

  _driverState = DriverState::BUSY;
  const uint32_t deadline = millis() + timeoutMs;
  while (true) {
    bool ack = false;
    st = _addressOnlyRaw(cmd::OPCODE_EEPROM, false, ack);
    if (!st.ok()) {
      return _trackIo(st);
    }
    if (ack) {
      return _trackIo(Status::Ok());
    }

    if (deadlinePassed(millis(), deadline)) {
      return _trackIo(Status::Error(Err::BUSY_TIMEOUT, "Timed out waiting for write cycle completion"));
    }

    delayMicroseconds(100);
  }
}

Status Driver::readCurrentAddress(uint8_t& value) {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  st = _readCurrentAddressRaw(value);
  return _trackIo(st);
}

Status Driver::readEeprom(uint8_t address, uint8_t* data, size_t len) {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }
  if (!_isEepromAddressValid(address)) {
    return Status::Error(Err::INVALID_PARAM, "EEPROM address out of range");
  }
  if (data == nullptr || len == 0 || len > cmd::EEPROM_SIZE) {
    return Status::Error(Err::INVALID_PARAM, "Invalid EEPROM read buffer/length");
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
  if (!_isEepromAddressValid(address)) {
    return Status::Error(Err::INVALID_PARAM, "EEPROM address out of range");
  }
  if (data == nullptr || len == 0 || len > cmd::PAGE_SIZE) {
    return Status::Error(Err::INVALID_PARAM, "EEPROM page write length must be 1..8");
  }

  st = _writeRaw(cmd::OPCODE_EEPROM, address, data, len);
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = waitReady(_config.writeTimeoutMs);
  return st;
}

Status Driver::readSecurity(uint8_t address, uint8_t* data, size_t len) {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }
  if (!_isSecurityAddressValid(address)) {
    return Status::Error(Err::INVALID_PARAM, "Security address out of range");
  }
  if (data == nullptr || len == 0 || len > cmd::SECURITY_SIZE) {
    return Status::Error(Err::INVALID_PARAM, "Invalid security read buffer/length");
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
  if (data == nullptr || len == 0 || len > cmd::PAGE_SIZE) {
    return Status::Error(Err::INVALID_PARAM, "Security page write length must be 1..8");
  }

  st = _writeRaw(cmd::OPCODE_SECURITY, address, data, len);
  if (!st.ok()) {
    return _trackIo(st);
  }

  st = waitReady(_config.writeTimeoutMs);
  return st;
}

Status Driver::lockSecurityRegister() {
  Status st = _checkInitialized();
  if (!st.ok()) {
    return st;
  }

  const uint8_t lockData = 0x00;
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

  bool ack = false;
  st = _addressOnlyRaw(cmd::OPCODE_LOCK_SECURITY, true, ack);
  if (!st.ok()) {
    return _trackIo(st);
  }

  locked = !ack;
  return _trackIo(Status::Ok());
}

Status Driver::readSerialNumber(SerialNumberInfo& serial) {
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

  bool ack = false;
  st = _addressOnlyRaw(cmd::OPCODE_STANDARD_SPEED, true, ack);
  if (!st.ok()) {
    return _trackIo(st);
  }

  enabled = ack;
  return _trackIo(Status::Ok());
}

uint8_t Driver::crc8_31(const uint8_t* data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((crc & 0x80U) != 0U) {
        crc = static_cast<uint8_t>((crc << 1U) ^ 0x31U);
      } else {
        crc = static_cast<uint8_t>(crc << 1U);
      }
    }
  }
  return crc;
}

Status Driver::_trackIo(const Status& st) {
  if (!_initialized) {
    return st;
  }

  const uint32_t nowMs = millis();
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

  if (_consecutiveFailures >= _config.offlineThreshold) {
    _driverState = DriverState::OFFLINE;
  } else {
    _driverState = DriverState::DEGRADED;
  }

  return st;
}

Status Driver::_checkInitialized() const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() must succeed before this operation");
  }
  return Status::Ok();
}

Status Driver::_configurePins() {
#if defined(ARDUINO_ARCH_ESP32)
  gpio_config_t sioCfg{};
  sioCfg.pin_bit_mask = (1ULL << static_cast<uint8_t>(_config.sioPin));
  sioCfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
  sioCfg.pull_up_en = GPIO_PULLUP_DISABLE;
  sioCfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  sioCfg.intr_type = GPIO_INTR_DISABLE;

  if (gpio_config(&sioCfg) != ESP_OK) {
    return Status::Error(Err::INVALID_CONFIG, "Failed to configure sioPin", _config.sioPin);
  }
  if (gpio_set_level(static_cast<gpio_num_t>(_config.sioPin), 1) != ESP_OK) {
    return Status::Error(Err::INVALID_CONFIG, "Failed to release sioPin", _config.sioPin);
  }

  if (_config.presencePin >= 0) {
    gpio_config_t presenceCfg{};
    presenceCfg.pin_bit_mask = (1ULL << static_cast<uint8_t>(_config.presencePin));
    presenceCfg.mode = GPIO_MODE_INPUT;
    presenceCfg.pull_up_en = GPIO_PULLUP_DISABLE;
    presenceCfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    presenceCfg.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&presenceCfg) != ESP_OK) {
      return Status::Error(Err::INVALID_CONFIG, "Failed to configure presencePin", _config.presencePin);
    }
  }
#else
  pinMode(static_cast<uint8_t>(_config.sioPin), OUTPUT_OPEN_DRAIN);
  digitalWrite(static_cast<uint8_t>(_config.sioPin), HIGH);
  if (_config.presencePin >= 0) {
    pinMode(static_cast<uint8_t>(_config.presencePin), INPUT);
  }
#endif

  return Status::Ok();
}

void Driver::_releaseLine() {
#if defined(ARDUINO_ARCH_ESP32)
  (void)gpio_set_level(static_cast<gpio_num_t>(_config.sioPin), 1);
#else
  digitalWrite(static_cast<uint8_t>(_config.sioPin), HIGH);
#endif
}

void Driver::_lineLow() {
#if defined(ARDUINO_ARCH_ESP32)
  (void)gpio_set_level(static_cast<gpio_num_t>(_config.sioPin), 0);
#else
  digitalWrite(static_cast<uint8_t>(_config.sioPin), LOW);
#endif
}

bool Driver::_readLine() const {
#if defined(ARDUINO_ARCH_ESP32)
  return gpio_get_level(static_cast<gpio_num_t>(_config.sioPin)) != 0;
#else
  return digitalRead(static_cast<uint8_t>(_config.sioPin)) != 0;
#endif
}

void Driver::driveLow(uint32_t lowUs) {
  _lineLow();
  delayMicroseconds(lowUs);
}

void Driver::releaseLine() {
  _releaseLine();
}

bool Driver::readLine() {
  return _readLine();
}

void Driver::txBit0() {
  _lineLow();
  delayMicroseconds(_timing.low0Us);
  _releaseLine();

  if (_timing.bitUs > _timing.low0Us) {
    delayMicroseconds(static_cast<uint32_t>(_timing.bitUs - _timing.low0Us));
  }
}

void Driver::txBit1() {
  _lineLow();
  delayMicroseconds(_timing.low1Us);
  _releaseLine();

  if (_timing.bitUs > _timing.low1Us) {
    delayMicroseconds(static_cast<uint32_t>(_timing.bitUs - _timing.low1Us));
  }
}

bool Driver::rxBit() {
  _lineLow();
  delayMicroseconds(_timing.readLowUs);
  _releaseLine();

  if (_timing.readSampleUs > 0) {
    delayMicroseconds(_timing.readSampleUs);
  }

  const bool bit = _readLine();
  const uint16_t elapsed = static_cast<uint16_t>(_timing.readLowUs + _timing.readSampleUs);
  if (_timing.bitUs > elapsed) {
    delayMicroseconds(static_cast<uint32_t>(_timing.bitUs - elapsed));
  }

  return bit;
}

bool Driver::txByte(uint8_t value) {
#if defined(ARDUINO_ARCH_ESP32)
  portENTER_CRITICAL(&_timingMux);
#endif

  for (int8_t bit = 7; bit >= 0; --bit) {
    const bool one = ((value >> bit) & 0x01U) != 0U;
    if (one) {
      txBit1();
    } else {
      txBit0();
    }
  }

  const bool ack = !rxBit();

#if defined(ARDUINO_ARCH_ESP32)
  portEXIT_CRITICAL(&_timingMux);
#endif

  return ack;
}

uint8_t Driver::rxByte(bool ack) {
#if defined(ARDUINO_ARCH_ESP32)
  portENTER_CRITICAL(&_timingMux);
#endif

  uint8_t value = 0;
  for (int8_t bit = 7; bit >= 0; --bit) {
    if (rxBit()) {
      value = static_cast<uint8_t>(value | static_cast<uint8_t>(1U << bit));
    }
  }

  if (ack) {
    txBit0();
  } else {
    txBit1();
  }

#if defined(ARDUINO_ARCH_ESP32)
  portEXIT_CRITICAL(&_timingMux);
#endif

  return value;
}

void Driver::_sendStart() {
  _releaseLine();
  delayMicroseconds(_timing.htssUs);
}

void Driver::_sendStop() {
  _releaseLine();
  delayMicroseconds(_timing.htssUs);
}

uint8_t Driver::_deviceAddress(uint8_t opcode, bool read) const {
  const uint8_t rw = read ? 0x01U : 0x00U;
  return static_cast<uint8_t>((opcode << 4U) | ((_config.addressBits & 0x07U) << 1U) | rw);
}

Status Driver::_resetAndDiscoverRaw() {
  driveLow(DISCHARGE_LOW_US);
  releaseLine();
  delayMicroseconds(RESET_RECOVERY_US);

#if defined(ARDUINO_ARCH_ESP32)
  portENTER_CRITICAL(&_timingMux);
#endif

  _lineLow();
  delayMicroseconds(DISCOVERY_REQUEST_US);
  _releaseLine();

  delayMicroseconds(DISCOVERY_STROBE_DELAY_US);

  _lineLow();
  delayMicroseconds(DISCOVERY_STROBE_US);
  _releaseLine();

  delayMicroseconds(DISCOVERY_SAMPLE_DELAY_US);
  const bool present = !_readLine();

#if defined(ARDUINO_ARCH_ESP32)
  portEXIT_CRITICAL(&_timingMux);
#endif

  delayMicroseconds(HIGH_SPEED_TIMING.htssUs);

  if (!present) {
    return Status::Error(Err::DISCOVERY_FAILED, "Discovery response not detected");
  }

  _setSpeedMode(SpeedMode::HIGH_SPEED);
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

bool Driver::_isEepromAddressValid(uint8_t address) {
  return address < cmd::EEPROM_SIZE;
}

bool Driver::_isSecurityAddressValid(uint8_t address) {
  return address < cmd::SECURITY_SIZE;
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
