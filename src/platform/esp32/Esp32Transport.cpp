#include "AT21CS/platform/esp32/Esp32Transport.h"

#include <cstddef>

#include "../../TransferValidation.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <driver/gpio.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#define AT21CS_HAS_ESP32_PLATFORM 1
#else
#define AT21CS_HAS_ESP32_PLATFORM 0
#endif

namespace AT21CS {
namespace {

constexpr int32_t INVALID_FRAME_DETAIL = -2;
constexpr int32_t DEADLINE_DETAIL = -3;
constexpr int32_t GPIO_DETAIL = -4;
constexpr uint32_t MAX_WAIT_US = 10000;

bool validBackendRequest(const SingleWireTransfer& transfer) {
  return detail::validTransferRequest(transfer, size_t{8}, uint32_t{160},
                                      uint32_t{650});
}

TransferResult failure(TransportCode code,
                       TransferPhase phase,
                       int32_t detail) {
  TransferResult result{};
  result.code = code;
  result.phase = phase;
  result.detail = detail;
  return result;
}

}  // namespace

Status Esp32Transport::begin(const Esp32TransportConfig& config) {
  if (_initialized) {
    return Status::Error(Err::INVALID_STATE);
  }
#if AT21CS_HAS_ESP32_PLATFORM
  if (!GPIO_IS_VALID_OUTPUT_GPIO(config.sioPin) ||
      (config.presencePin >= 0 &&
       !GPIO_IS_VALID_GPIO(config.presencePin)) ||
      config.presencePin == config.sioPin) {
    return Status::Error(Err::INVALID_CONFIG);
  }

  gpio_config_t sioConfig{};
  sioConfig.pin_bit_mask = UINT64_C(1) << static_cast<uint32_t>(config.sioPin);
  sioConfig.mode = GPIO_MODE_INPUT_OUTPUT_OD;
  sioConfig.pull_up_en = GPIO_PULLUP_DISABLE;
  sioConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
  sioConfig.intr_type = GPIO_INTR_DISABLE;
  if (gpio_config(&sioConfig) != ESP_OK ||
      gpio_set_level(static_cast<gpio_num_t>(config.sioPin), 1) != ESP_OK) {
    return Status::Error(Err::IO_ERROR, config.sioPin);
  }

  if (config.presencePin >= 0) {
    gpio_config_t presenceConfig{};
    presenceConfig.pin_bit_mask =
        UINT64_C(1) << static_cast<uint32_t>(config.presencePin);
    presenceConfig.mode = GPIO_MODE_INPUT;
    presenceConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    presenceConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    presenceConfig.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&presenceConfig) != ESP_OK) {
      (void)gpio_set_level(static_cast<gpio_num_t>(config.sioPin), 1);
      return Status::Error(Err::IO_ERROR, config.presencePin);
    }
  }

  _config = config;
  _initialized = true;
  return Status::Ok();
#else
  (void)config;
  return Status::Error(Err::UNSUPPORTED_COMMAND);
#endif
}

void Esp32Transport::end() {
  if (!_initialized) {
    return;
  }
#if AT21CS_HAS_ESP32_PLATFORM
  (void)gpio_set_level(static_cast<gpio_num_t>(_config.sioPin), 1);
#endif
  _initialized = false;
  _config = {};
}

bool Esp32Transport::isInitialized() const {
  return _initialized;
}

SingleWireTransport Esp32Transport::descriptor() {
  return _initialized ? _descriptorUnchecked() : SingleWireTransport{};
}

uint64_t Esp32Transport::_nowUsThunk(void* user) {
  auto* const self = static_cast<Esp32Transport*>(user);
  return self != nullptr && self->_initialized ? self->_nowUs() : 0;
}

TransferResult Esp32Transport::_transferThunk(const SingleWireTransfer& transfer,
                                              uint64_t deadlineUs,
                                              void* user) {
  auto* const self = static_cast<Esp32Transport*>(user);
  return self != nullptr && self->_initialized
             ? self->_transfer(transfer, deadlineUs)
             : _staleResult();
}

TransferResult Esp32Transport::_resetAndDiscoverThunk(bool& present,
                                                      uint64_t deadlineUs,
                                                      void* user) {
  present = false;
  auto* const self = static_cast<Esp32Transport*>(user);
  return self != nullptr && self->_initialized
             ? self->_resetAndDiscover(present, deadlineUs)
             : _staleResult();
}

TransferResult Esp32Transport::_waitUntilUsThunk(uint64_t deadlineUs,
                                                void* user) {
  auto* const self = static_cast<Esp32Transport*>(user);
  return self != nullptr && self->_initialized
             ? self->_waitUntilUs(deadlineUs)
             : _staleResult();
}

TransferResult Esp32Transport::_readPresenceThunk(bool& present,
                                                  uint64_t deadlineUs,
                                                  void* user) {
  present = false;
  auto* const self = static_cast<Esp32Transport*>(user);
  return self != nullptr && self->_initialized
             ? self->_readPresence(present, deadlineUs)
             : _staleResult();
}

SingleWireTransport Esp32Transport::_descriptorUnchecked() {
  SingleWireTransport value{};
  value.user = this;
  value.nowUs = &_nowUsThunk;
  value.transfer = &_transferThunk;
  value.resetAndDiscover = &_resetAndDiscoverThunk;
  value.waitUntilUs = &_waitUntilUsThunk;
  if (_config.presencePin >= 0) {
    value.readPresence = &_readPresenceThunk;
  }
  return value;
}

TransferResult Esp32Transport::_transfer(const SingleWireTransfer& transfer,
                                         uint64_t deadlineUs) {
  if (!validBackendRequest(transfer)) {
    return failure(TransportCode::IO_ERROR, TransferPhase::NONE,
                   INVALID_FRAME_DETAIL);
  }

  const Timing timing = _timingFor(transfer.speed);
  TransferResult result{};
#if AT21CS_HAS_ESP32_PLATFORM
  portENTER_CRITICAL(&_timingMux);
#endif

  const auto finishFailure = [&](TransportCode code,
                                 TransferPhase phase,
                                 int32_t detail,
                                 bool currentWriteByteMayBeAccepted) {
    result.code = code;
    result.phase = phase;
    result.detail = detail;
    result.currentWriteByteMayBeAccepted = currentWriteByteMayBeAccepted;
    result.stopCompleted =
        _finishStop(transfer.minimumPostTransferHighUs, deadlineUs) ==
        TransportCode::OK;
    return result;
  };

  const auto writeAndAck = [&](uint8_t value,
                               TransferPhase phase,
                               bool mayBeWritePayload,
                               bool& ack) -> TransferResult {
    bool allBitsDelivered = false;
    if (!_writeEightBits(value, timing, deadlineUs, allBitsDelivered)) {
      const bool timeout = _deadlineReached(deadlineUs);
      return finishFailure(timeout ? TransportCode::TIMEOUT
                                   : TransportCode::IO_ERROR,
                           phase, timeout ? DEADLINE_DETAIL : GPIO_DETAIL,
                           mayBeWritePayload && allBitsDelivered);
    }
    if (!_readAck(ack, timing, deadlineUs)) {
      const bool timeout = _deadlineReached(deadlineUs);
      return finishFailure(timeout ? TransportCode::TIMEOUT
                                   : TransportCode::IO_ERROR,
                           phase, timeout ? DEADLINE_DETAIL : GPIO_DETAIL,
                           mayBeWritePayload);
    }
    if (!ack) {
      return finishFailure(TransportCode::NACK, phase, 0, false);
    }
    result.code = TransportCode::OK;
    return result;
  };

  if (!_setLine(true)) {
    result = finishFailure(TransportCode::IO_ERROR, TransferPhase::START,
                           GPIO_DETAIL, false);
  } else if (!_delayWithinDeadline(timing.startHighUs, deadlineUs)) {
    result = failure(TransportCode::TIMEOUT, TransferPhase::START,
                     DEADLINE_DETAIL);
  } else {
    bool ack = false;
    const TransferPhase firstPhase =
        (transfer.deviceAddress & 0x01u) != 0u
            ? TransferPhase::DEVICE_ADDRESS_READ
            : TransferPhase::DEVICE_ADDRESS_WRITE;
    result = writeAndAck(transfer.deviceAddress, firstPhase, false, ack);
    if (result.ok()) {
      result.firstDeviceAddressAcked = true;
      if (transfer.hasMemoryAddress) {
        result = writeAndAck(transfer.memoryAddress,
                             TransferPhase::MEMORY_ADDRESS, false, ack);
        if (result.ok()) {
          result.firstDeviceAddressAcked = true;
          result.memoryAddressAcked = true;
        }
      }
    }

    if (result.ok() && transfer.hasRepeatedStart) {
      if (!_setLine(true)) {
        result = finishFailure(TransportCode::IO_ERROR,
                               TransferPhase::RESTART, GPIO_DETAIL, false);
      } else if (!_delayWithinDeadline(timing.startHighUs, deadlineUs)) {
        result = finishFailure(TransportCode::TIMEOUT,
                               TransferPhase::RESTART, DEADLINE_DETAIL, false);
      } else {
        result = writeAndAck(transfer.repeatedDeviceAddress,
                             TransferPhase::DEVICE_ADDRESS_READ, false, ack);
        if (result.ok()) {
          result.firstDeviceAddressAcked = true;
          result.memoryAddressAcked = true;
          result.repeatedDeviceAddressAcked = true;
        }
      }
    }

    for (size_t index = 0; result.ok() && index < transfer.txLength; ++index) {
      result = writeAndAck(transfer.txData[index],
                           TransferPhase::DATA_WRITE, true, ack);
      if (result.ok()) {
        ++result.dataBytesTransferred;
        result.firstDeviceAddressAcked = true;
        result.memoryAddressAcked = transfer.hasMemoryAddress;
      }
    }

    for (size_t index = 0; result.ok() && index < transfer.rxLength; ++index) {
      uint8_t value = 0;
      if (!_readEightBits(value, timing, deadlineUs)) {
        const bool timeout = _deadlineReached(deadlineUs);
        result = finishFailure(timeout ? TransportCode::TIMEOUT
                                       : TransportCode::IO_ERROR,
                               TransferPhase::DATA_READ,
                               timeout ? DEADLINE_DETAIL : GPIO_DETAIL, false);
        break;
      }
      transfer.rxData[index] = value;
      ++result.dataBytesTransferred;
      const bool sendAck = index + 1u < transfer.rxLength;
      if (!_writeBit(!sendAck, timing, deadlineUs)) {
        const bool timeout = _deadlineReached(deadlineUs);
        result = finishFailure(timeout ? TransportCode::TIMEOUT
                                       : TransportCode::IO_ERROR,
                               TransferPhase::DATA_READ,
                               timeout ? DEADLINE_DETAIL : GPIO_DETAIL, false);
      }
    }

    if (result.ok()) {
      const TransportCode stopCode =
          _finishStop(transfer.minimumPostTransferHighUs, deadlineUs);
      result.stopCompleted = stopCode == TransportCode::OK;
      if (result.stopCompleted) {
        result.code = TransportCode::OK;
        result.phase = TransferPhase::STOP;
      } else {
        result.code = stopCode;
        result.phase = TransferPhase::STOP;
        result.detail = result.code == TransportCode::TIMEOUT ? DEADLINE_DETAIL
                                                               : GPIO_DETAIL;
      }
    }
  }

#if AT21CS_HAS_ESP32_PLATFORM
  portEXIT_CRITICAL(&_timingMux);
#endif
  return result;
}

TransferResult Esp32Transport::_resetAndDiscover(bool& present,
                                                 uint64_t deadlineUs) {
  present = false;
  if (_deadlineReached(deadlineUs)) {
    (void)_setLine(true);
    return failure(TransportCode::TIMEOUT, TransferPhase::RESET_LOW,
                   DEADLINE_DETAIL);
  }
  if (!_setLine(false)) {
    (void)_setLine(true);
    return failure(TransportCode::IO_ERROR, TransferPhase::RESET_LOW,
                   GPIO_DETAIL);
  }
  _delayUs(600);
  if (!_setLine(true)) {
    return failure(TransportCode::IO_ERROR, TransferPhase::RESET_RECOVERY,
                   GPIO_DETAIL);
  }
  if (_deadlineReached(deadlineUs)) {
    return failure(TransportCode::TIMEOUT, TransferPhase::RESET_RECOVERY,
                   DEADLINE_DETAIL);
  }
  _delayUs(10);

  TransferResult result{};
#if AT21CS_HAS_ESP32_PLATFORM
  portENTER_CRITICAL(&_timingMux);
#endif
  if (!_setLine(false)) {
    result = failure(TransportCode::IO_ERROR,
                     TransferPhase::DISCOVERY_REQUEST, GPIO_DETAIL);
  } else if (_deadlineReached(deadlineUs)) {
    result = failure(TransportCode::TIMEOUT,
                     TransferPhase::DISCOVERY_REQUEST, DEADLINE_DETAIL);
  } else {
    _delayUs(1);
    if (!_setLine(true)) {
      result = failure(TransportCode::IO_ERROR,
                       TransferPhase::DISCOVERY_REQUEST, GPIO_DETAIL);
    } else {
      _delayUs(3);
      present = !_readLine();
      _delayUs(21);
      if (!_readLine()) {
        present = false;
        result = failure(TransportCode::LINE_STUCK,
                         TransferPhase::DISCOVERY_RELEASE, GPIO_DETAIL);
      } else if (_deadlineReached(deadlineUs)) {
        present = false;
        result = failure(TransportCode::TIMEOUT,
                         TransferPhase::DISCOVERY_RELEASE, DEADLINE_DETAIL);
      } else {
        result.code = TransportCode::OK;
        result.phase = TransferPhase::DISCOVERY_RELEASE;
      }
    }
  }
#if AT21CS_HAS_ESP32_PLATFORM
  portEXIT_CRITICAL(&_timingMux);
#endif
  if (result.ok()) {
    _delayUs(160);
    if (_deadlineReached(deadlineUs)) {
      present = false;
      result = failure(TransportCode::TIMEOUT,
                       TransferPhase::DISCOVERY_RELEASE, DEADLINE_DETAIL);
    }
  } else {
    (void)_setLine(true);
  }
  return result;
}

TransferResult Esp32Transport::_waitUntilUs(uint64_t deadlineUs) {
  TransferResult result{};
  if (!_setLine(true)) {
    return failure(TransportCode::IO_ERROR, TransferPhase::WAIT_HIGH,
                   GPIO_DETAIL);
  }
  const uint64_t nowUs = _nowUs();
  if (nowUs >= deadlineUs) {
    result.code = TransportCode::OK;
    result.phase = TransferPhase::WAIT_HIGH;
    return result;
  }
  const uint64_t remainingUs = deadlineUs - nowUs;
  if (remainingUs > MAX_WAIT_US) {
    return failure(TransportCode::TIMEOUT, TransferPhase::WAIT_HIGH,
                   DEADLINE_DETAIL);
  }
  _delayUs(static_cast<uint32_t>(remainingUs));
  if (_nowUs() < deadlineUs) {
    return failure(TransportCode::TIMEOUT, TransferPhase::WAIT_HIGH,
                   DEADLINE_DETAIL);
  }
  result.code = TransportCode::OK;
  result.phase = TransferPhase::WAIT_HIGH;
  return result;
}

TransferResult Esp32Transport::_readPresence(bool& present,
                                             uint64_t deadlineUs) {
  present = false;
  if (_config.presencePin < 0) {
    return failure(TransportCode::IO_ERROR, TransferPhase::PRESENCE,
                   INVALID_FRAME_DETAIL);
  }
  if (_deadlineReached(deadlineUs)) {
    return failure(TransportCode::TIMEOUT, TransferPhase::PRESENCE,
                   DEADLINE_DETAIL);
  }
#if AT21CS_HAS_ESP32_PLATFORM
  const int level = gpio_get_level(static_cast<gpio_num_t>(_config.presencePin));
  present = _config.presenceActiveHigh ? level != 0 : level == 0;
  TransferResult result{};
  result.code = TransportCode::OK;
  result.phase = TransferPhase::PRESENCE;
  return result;
#else
  return failure(TransportCode::IO_ERROR, TransferPhase::PRESENCE, GPIO_DETAIL);
#endif
}

uint64_t Esp32Transport::_nowUs() {
#if defined(AT21CS_TESTING) && !AT21CS_HAS_ESP32_PLATFORM
  ++_timingMux;
#endif
#if AT21CS_HAS_ESP32_PLATFORM
  const int64_t value = esp_timer_get_time();
  return value > 0 ? static_cast<uint64_t>(value) : 0;
#else
  return 0;
#endif
}

bool Esp32Transport::_deadlineReached(uint64_t deadlineUs) {
  return _nowUs() >= deadlineUs;
}

bool Esp32Transport::_delayWithinDeadline(uint32_t durationUs,
                                          uint64_t deadlineUs) {
  const uint64_t nowUs = _nowUs();
  if (nowUs >= deadlineUs ||
      static_cast<uint64_t>(durationUs) >= deadlineUs - nowUs) {
    return false;
  }
  _delayUs(durationUs);
  return !_deadlineReached(deadlineUs);
}

bool Esp32Transport::_setLine(bool released) {
#if defined(AT21CS_TESTING) && !AT21CS_HAS_ESP32_PLATFORM
  ++_timingMux;
#endif
#if AT21CS_HAS_ESP32_PLATFORM
  return gpio_set_level(static_cast<gpio_num_t>(_config.sioPin), released ? 1 : 0) ==
         ESP_OK;
#else
  (void)released;
#if defined(AT21CS_TESTING)
  return true;
#else
  return false;
#endif
#endif
}

bool Esp32Transport::_readLine() const {
#if defined(AT21CS_TESTING) && !AT21CS_HAS_ESP32_PLATFORM
  ++_timingMux;
#endif
#if AT21CS_HAS_ESP32_PLATFORM
  return gpio_get_level(static_cast<gpio_num_t>(_config.sioPin)) != 0;
#else
  return true;
#endif
}

void Esp32Transport::_delayUs(uint32_t durationUs) const {
#if defined(AT21CS_TESTING) && !AT21CS_HAS_ESP32_PLATFORM
  ++_timingMux;
#endif
#if AT21CS_HAS_ESP32_PLATFORM
  esp_rom_delay_us(durationUs);
#else
  (void)durationUs;
#endif
}

bool Esp32Transport::_writeBit(bool one,
                               const Timing& timing,
                               uint64_t deadlineUs,
                               bool* delivered) {
  if (delivered != nullptr) {
    *delivered = false;
  }
  if (_deadlineReached(deadlineUs) || !_setLine(false)) {
    return false;
  }
  const uint16_t lowUs = one ? timing.low1Us : timing.low0Us;
  _delayUs(lowUs);
  if (!_setLine(true)) {
    return false;
  }
  if (timing.bitUs > lowUs) {
    _delayUs(static_cast<uint32_t>(timing.bitUs - lowUs));
  }
  if (delivered != nullptr) {
    *delivered = true;
  }
  return !_deadlineReached(deadlineUs);
}

bool Esp32Transport::_readBit(bool& one,
                              const Timing& timing,
                              uint64_t deadlineUs) {
  one = true;
  if (_deadlineReached(deadlineUs) || !_setLine(false)) {
    return false;
  }
  _delayUs(timing.readLowUs);
  if (!_setLine(true)) {
    return false;
  }
  _delayUs(timing.readSampleUs);
  one = _readLine();
  const uint16_t elapsed =
      static_cast<uint16_t>(timing.readLowUs + timing.readSampleUs);
  if (timing.bitUs > elapsed) {
    _delayUs(static_cast<uint32_t>(timing.bitUs - elapsed));
  }
  return !_deadlineReached(deadlineUs);
}

bool Esp32Transport::_writeEightBits(uint8_t value,
                                     const Timing& timing,
                                     uint64_t deadlineUs,
                                     bool& allBitsDelivered) {
  allBitsDelivered = false;
  for (int bit = 7; bit >= 0; --bit) {
    bool bitDelivered = false;
    if (!_writeBit(((value >> static_cast<uint32_t>(bit)) & 0x01u) != 0u,
                   timing, deadlineUs, &bitDelivered)) {
      allBitsDelivered = bit == 0 && bitDelivered;
      return false;
    }
  }
  allBitsDelivered = true;
  return true;
}

bool Esp32Transport::_readEightBits(uint8_t& value,
                                    const Timing& timing,
                                    uint64_t deadlineUs) {
  value = 0;
  for (int bit = 7; bit >= 0; --bit) {
    bool one = true;
    if (!_readBit(one, timing, deadlineUs)) {
      return false;
    }
    if (one) {
      value = static_cast<uint8_t>(
          value | static_cast<uint8_t>(1u << static_cast<uint32_t>(bit)));
    }
  }
  return true;
}

bool Esp32Transport::_readAck(bool& ack,
                              const Timing& timing,
                              uint64_t deadlineUs) {
  bool value = true;
  const bool completed = _readBit(value, timing, deadlineUs);
  ack = completed && !value;
  return completed;
}

TransportCode Esp32Transport::_finishStop(uint32_t highUs,
                                          uint64_t deadlineUs) {
  if (!_setLine(true)) {
    return TransportCode::IO_ERROR;
  }
  return _delayWithinDeadline(highUs, deadlineUs)
             ? TransportCode::OK
             : TransportCode::TIMEOUT;
}

TransferResult Esp32Transport::_staleResult() {
  TransferResult result{};
  result.detail = BACKEND_NOT_INITIALIZED_DETAIL;
  return result;
}

Esp32Transport::Timing Esp32Transport::_timingFor(SpeedMode speed) {
  if (speed == SpeedMode::STANDARD_SPEED) {
    return Timing{60, 32, 6, 6, 14, 650};
  }
  return Timing{};
}

}  // namespace AT21CS
