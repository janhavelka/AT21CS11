#include "AT21CS/platform/esp32/Esp32Transport.h"

#include <cstddef>
#include <cstdint>
#include <limits>

#include "../../TransferValidation.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <driver/gpio.h>
#include <esp_cpu.h>
#include <esp_pm.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/task.h>
#include <hal/gpio_ll.h>
#include <soc/gpio_struct.h>
#include <soc/soc_caps.h>
#if !defined(CONFIG_IDF_TARGET_ESP32S2) && \
    !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Esp32Transport supports only ESP32-S2 and ESP32-S3"
#endif
#define AT21CS_HAS_ESP32_PLATFORM 1
#define AT21CS_ESP32_IRAM_ATTR IRAM_ATTR
#else
#define AT21CS_HAS_ESP32_PLATFORM 0
#define AT21CS_ESP32_IRAM_ATTR
#endif

namespace AT21CS {
namespace {

#if AT21CS_HAS_ESP32_PLATFORM
#if defined(CONFIG_IDF_TARGET_ESP32S2)
static_assert(SOC_GPIO_PIN_COUNT == 47,
              "ESP32-S2 GPIO count changed; re-audit pin validation");
static_assert(!GPIO_IS_VALID_OUTPUT_GPIO(46),
              "ESP32-S2 input-only GPIO classification changed");
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
static_assert(SOC_GPIO_PIN_COUNT == 49,
              "ESP32-S3 GPIO count changed; re-audit pin validation");
#endif
static_assert(GPIO_IS_VALID_OUTPUT_GPIO(31) &&
                  GPIO_IS_VALID_OUTPUT_GPIO(32),
              "ESP32 GPIO bank-boundary output support changed");
#endif

constexpr int32_t INVALID_FRAME_DETAIL = -2;
constexpr int32_t DEADLINE_DETAIL = -3;
constexpr int32_t GPIO_DETAIL = -4;
constexpr int32_t TIMING_LOCK_DETAIL = -6;
constexpr uint32_t MAX_WAIT_US = 10000;
constexpr uint32_t RESET_LOW_US = 600;
constexpr uint32_t RESET_RECOVERY_US = 10;
constexpr uint32_t DISCOVERY_REQUEST_NS = 1200;
constexpr uint32_t DISCOVERY_SAMPLE_FROM_FALL_NS = 4000;
constexpr uint32_t DISCOVERY_RESPONSE_MAX_US = 24;
constexpr uint32_t DISCOVERY_RELEASE_CHECK_US = 25;
constexpr uint32_t DISCOVERY_RELEASE_CHECK_NS =
    DISCOVERY_RELEASE_CHECK_US * 1000u;
constexpr uint32_t POST_DISCOVERY_HIGH_US = 160;
constexpr uint32_t MIN_RESET_DISCOVERY_US =
    RESET_LOW_US + RESET_RECOVERY_US + DISCOVERY_RELEASE_CHECK_US +
    POST_DISCOVERY_HIGH_US;
static_assert(DISCOVERY_RESPONSE_MAX_US < DISCOVERY_RELEASE_CHECK_US,
              "Discovery release check must follow the response window");

bool validBackendRequest(const SingleWireTransfer& transfer) {
  return detail::validTransferRequest(transfer, size_t{8}, uint32_t{160},
                                      uint32_t{650});
}

AT21CS_ESP32_IRAM_ATTR TransferResult failure(TransportCode code,
                                              TransferPhase phase,
                                              int32_t detail) {
  TransferResult result{};
  result.code = code;
  result.phase = phase;
  result.detail = detail;
  return result;
}

AT21CS_ESP32_IRAM_ATTR TransferPhase phaseAfterAck(
    const SingleWireTransfer& transfer,
    TransferPhase ackPhase,
    size_t payloadIndex) {
  if (ackPhase == TransferPhase::DEVICE_ADDRESS_WRITE ||
      (ackPhase == TransferPhase::DEVICE_ADDRESS_READ &&
       !transfer.hasRepeatedStart)) {
    if (transfer.hasMemoryAddress) {
      return TransferPhase::MEMORY_ADDRESS;
    }
    if (transfer.rxLength != 0) {
      return TransferPhase::DATA_READ;
    }
    if (transfer.txLength != 0) {
      return TransferPhase::DATA_WRITE;
    }
    return TransferPhase::STOP;
  }
  if (ackPhase == TransferPhase::MEMORY_ADDRESS) {
    if (transfer.hasRepeatedStart) {
      return TransferPhase::RESTART;
    }
    return transfer.txLength != 0 ? TransferPhase::DATA_WRITE
                                  : TransferPhase::STOP;
  }
  if (ackPhase == TransferPhase::DEVICE_ADDRESS_READ) {
    return transfer.rxLength != 0 ? TransferPhase::DATA_READ
                                  : TransferPhase::STOP;
  }
  if (ackPhase == TransferPhase::DATA_WRITE) {
    return payloadIndex + 1u < transfer.txLength
               ? TransferPhase::DATA_WRITE
               : TransferPhase::STOP;
  }
  return TransferPhase::STOP;
}

}  // namespace

Status Esp32Transport::begin(const Esp32TransportConfig& config) {
  if (_initialized) {
    return Status::Error(Err::INVALID_STATE);
  }
#if AT21CS_HAS_ESP32_PLATFORM
  if (!_pinNumbersInRange(config, SOC_GPIO_PIN_COUNT)) {
    return Status::Error(Err::INVALID_CONFIG);
  }
  const bool presenceDisabled = config.presencePin == -1;
  if (!GPIO_IS_VALID_OUTPUT_GPIO(config.sioPin) ||
      (!presenceDisabled && !GPIO_IS_VALID_GPIO(config.presencePin)) ||
      config.presencePin == config.sioPin) {
    return Status::Error(Err::INVALID_CONFIG);
  }

#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
  esp_pm_lock_handle_t pmLock = nullptr;
  const esp_err_t pmStatus =
      esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "at21cs", &pmLock);
  if (pmStatus != ESP_OK) {
    return Status::Error(Err::IO_ERROR, static_cast<int32_t>(pmStatus));
  }
#endif

  const auto leaveSioReleasedInput = [&config]() {
    gpio_ll_set_level(&GPIO, static_cast<uint32_t>(config.sioPin), 1);
    gpio_config_t rollback{};
    rollback.pin_bit_mask =
        UINT64_C(1) << static_cast<uint32_t>(config.sioPin);
    rollback.mode = GPIO_MODE_INPUT;
    rollback.pull_up_en = GPIO_PULLUP_DISABLE;
    rollback.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rollback.intr_type = GPIO_INTR_DISABLE;
    (void)gpio_config(&rollback);
  };

  // Preload the output latch before enabling open-drain output. This prevents
  // a stale reset-low latch from creating an unintended protocol pulse.
  gpio_ll_set_level(&GPIO, static_cast<uint32_t>(config.sioPin), 1);
  gpio_config_t sioConfig{};
  sioConfig.pin_bit_mask =
      UINT64_C(1) << static_cast<uint32_t>(config.sioPin);
  sioConfig.mode = GPIO_MODE_INPUT_OUTPUT_OD;
  sioConfig.pull_up_en = GPIO_PULLUP_DISABLE;
  sioConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
  sioConfig.intr_type = GPIO_INTR_DISABLE;
  if (gpio_config(&sioConfig) != ESP_OK) {
    leaveSioReleasedInput();
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
    (void)esp_pm_lock_delete(pmLock);
#endif
    return Status::Error(Err::IO_ERROR, config.sioPin);
  }

  if (!presenceDisabled) {
    gpio_config_t presenceConfig{};
    presenceConfig.pin_bit_mask =
        UINT64_C(1) << static_cast<uint32_t>(config.presencePin);
    presenceConfig.mode = GPIO_MODE_INPUT;
    presenceConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    presenceConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    presenceConfig.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&presenceConfig) != ESP_OK) {
      leaveSioReleasedInput();
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
      (void)esp_pm_lock_delete(pmLock);
#endif
      return Status::Error(Err::IO_ERROR, config.presencePin);
    }
  }

  _config = config;
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
  _pmLock = pmLock;
#endif
  _initialized = true;
  return Status::Ok();
#else
  if (!_pinNumbersInRange(config, 49)) {
    return Status::Error(Err::INVALID_CONFIG);
  }
  _config = config;
  _initialized = true;
  _setLine(true);
  return Status::Ok();
#endif
}

void Esp32Transport::end() {
  if (!_initialized) {
    return;
  }
#if AT21CS_HAS_ESP32_PLATFORM
  gpio_ll_set_level(&GPIO, static_cast<uint32_t>(_config.sioPin), 1);
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
  if (_pmLock != nullptr) {
    (void)esp_pm_lock_delete(
        static_cast<esp_pm_lock_handle_t>(_pmLock));
    _pmLock = nullptr;
  }
#endif
#else
  _setLine(true);
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

TransferResult Esp32Transport::_transfer(
    const SingleWireTransfer& transfer,
    uint64_t deadlineUs) {
  if (!validBackendRequest(transfer)) {
    return failure(TransportCode::IO_ERROR, TransferPhase::NONE,
                   INVALID_FRAME_DETAIL);
  }

  uint8_t txCopy[8]{};
  uint8_t rxCopy[8]{};
  for (size_t index = 0; index < transfer.txLength; ++index) {
    txCopy[index] = transfer.txData[index];
  }

  const Timing timing = _timingFor(transfer.speed);
  const uint64_t initialNowUs = _nowUs();
  if (initialNowUs >= deadlineUs ||
      static_cast<uint64_t>(timing.startHighUs) >=
          deadlineUs - initialNowUs) {
    return failure(TransportCode::TIMEOUT, TransferPhase::START,
                   DEADLINE_DETAIL);
  }
  const bool highSpeed = transfer.speed == SpeedMode::HIGH_SPEED;
  TransferResult result{};
  result.code = TransportCode::OK;
  SegmentClock clock{};
  bool timingActive = false;
  bool frameStarted = false;

  if (!_acquireTimingLock()) {
    return failure(TransportCode::IO_ERROR, TransferPhase::START,
                   TIMING_LOCK_DETAIL);
  }

  _suspendScheduler();

  const auto closeTiming = [&]() AT21CS_ESP32_IRAM_ATTR {
    if (timingActive) {
      _exitCritical();
      timingActive = false;
    }
  };
  const auto beginTiming = [&]() AT21CS_ESP32_IRAM_ATTR {
    if (timingActive) {
      return true;
    }
    _enterCritical();
    timingActive = true;
    if (!_beginSegment(deadlineUs, clock)) {
      closeTiming();
      return false;
    }
    return true;
  };
  const auto finishStandardByte = [&]() AT21CS_ESP32_IRAM_ATTR {
    if (!highSpeed) {
      closeTiming();
    }
  };
  const auto setFailure = [&](TransportCode code,
                              TransferPhase phase,
                              int32_t detail,
                              bool currentWriteByteMayBeAccepted)
      AT21CS_ESP32_IRAM_ATTR {
    result.code = code;
    result.phase = phase;
    result.detail = detail;
    result.currentWriteByteMayBeAccepted =
        currentWriteByteMayBeAccepted;
  };

  enum class AckEvidence : uint8_t {
    FIRST_ADDRESS,
    MEMORY_ADDRESS,
    REPEATED_ADDRESS,
    WRITE_PAYLOAD
  };

  const auto writeAndAck = [&](uint8_t value,
                               TransferPhase phase,
                               AckEvidence evidence,
                               size_t payloadIndex)
      AT21CS_ESP32_IRAM_ATTR {
    if (!beginTiming()) {
      setFailure(TransportCode::TIMEOUT, phase, DEADLINE_DETAIL, false);
      return false;
    }
    frameStarted = true;
    bool allBitsDelivered = false;
    if (!_writeEightBits(value, timing, clock, allBitsDelivered)) {
      closeTiming();
      setFailure(TransportCode::TIMEOUT, phase, DEADLINE_DETAIL,
                 evidence == AckEvidence::WRITE_PAYLOAD &&
                     allBitsDelivered);
      return false;
    }

    const ReadBitResult ackBit = _readAck(timing, clock);
    finishStandardByte();
    if (!ackBit.sampled) {
      closeTiming();
      setFailure(TransportCode::TIMEOUT, phase, DEADLINE_DETAIL,
                 evidence == AckEvidence::WRITE_PAYLOAD &&
                     allBitsDelivered);
      return false;
    }
    if (ackBit.value) {
      closeTiming();
      setFailure(TransportCode::NACK, phase, 0, false);
      return false;
    }

    switch (evidence) {
      case AckEvidence::FIRST_ADDRESS:
        result.firstDeviceAddressAcked = true;
        break;
      case AckEvidence::MEMORY_ADDRESS:
        result.memoryAddressAcked = true;
        break;
      case AckEvidence::REPEATED_ADDRESS:
        result.repeatedDeviceAddressAcked = true;
        break;
      case AckEvidence::WRITE_PAYLOAD:
        ++result.dataBytesTransferred;
        break;
    }
    result.currentWriteByteMayBeAccepted = false;

    if (!ackBit.completed) {
      closeTiming();
      setFailure(TransportCode::TIMEOUT,
                 phaseAfterAck(transfer, phase, payloadIndex),
                 DEADLINE_DETAIL, false);
      return false;
    }
    return true;
  };

  const TransportCode preStartCode =
      _finishStop(timing.startHighUs, deadlineUs);
  if (preStartCode != TransportCode::OK) {
    setFailure(preStartCode, TransferPhase::START,
               preStartCode == TransportCode::TIMEOUT ? DEADLINE_DETAIL
                                                       : GPIO_DETAIL,
               false);
  }

  const TransferPhase firstPhase =
      (transfer.deviceAddress & 0x01u) != 0u
          ? TransferPhase::DEVICE_ADDRESS_READ
          : TransferPhase::DEVICE_ADDRESS_WRITE;
  if (result.ok()) {
    (void)writeAndAck(transfer.deviceAddress, firstPhase,
                      AckEvidence::FIRST_ADDRESS, 0);
  }
  if (result.ok() && transfer.hasMemoryAddress) {
    (void)writeAndAck(transfer.memoryAddress,
                      TransferPhase::MEMORY_ADDRESS,
                      AckEvidence::MEMORY_ADDRESS, 0);
  }

  if (result.ok() && transfer.hasRepeatedStart) {
    closeTiming();
    const TransportCode restartCode =
        _finishStop(timing.startHighUs, deadlineUs);
    if (restartCode != TransportCode::OK) {
      setFailure(restartCode, TransferPhase::RESTART,
                 restartCode == TransportCode::TIMEOUT ? DEADLINE_DETAIL
                                                        : GPIO_DETAIL,
                 false);
    } else {
      (void)writeAndAck(transfer.repeatedDeviceAddress,
                        TransferPhase::DEVICE_ADDRESS_READ,
                        AckEvidence::REPEATED_ADDRESS, 0);
    }
  }

  for (size_t index = 0; result.ok() && index < transfer.txLength; ++index) {
    (void)writeAndAck(txCopy[index], TransferPhase::DATA_WRITE,
                      AckEvidence::WRITE_PAYLOAD, index);
  }

  for (size_t index = 0; result.ok() && index < transfer.rxLength; ++index) {
    if (!beginTiming()) {
      setFailure(TransportCode::TIMEOUT, TransferPhase::DATA_READ,
                 DEADLINE_DETAIL, false);
      break;
    }
    frameStarted = true;
    uint8_t value = 0;
    if (!_readEightBits(value, timing, clock)) {
      closeTiming();
      setFailure(TransportCode::TIMEOUT, TransferPhase::DATA_READ,
                 DEADLINE_DETAIL, false);
      break;
    }

    rxCopy[index] = value;
    ++result.dataBytesTransferred;
    const bool sendAck = index + 1u < transfer.rxLength;
    if (!_writeBit(!sendAck, timing, clock)) {
      closeTiming();
      setFailure(TransportCode::TIMEOUT, TransferPhase::DATA_READ,
                 DEADLINE_DETAIL, false);
      break;
    }
    finishStandardByte();
  }

  closeTiming();
  for (size_t index = 0; index < result.dataBytesTransferred &&
                         index < transfer.rxLength;
       ++index) {
    transfer.rxData[index] = rxCopy[index];
  }

  if (result.ok()) {
    const TransportCode stopCode =
        _finishStop(transfer.minimumPostTransferHighUs, deadlineUs);
    result.stopCompleted = stopCode == TransportCode::OK;
    result.phase = TransferPhase::STOP;
    if (stopCode != TransportCode::OK) {
      result.code = stopCode;
      result.detail = stopCode == TransportCode::TIMEOUT ? DEADLINE_DETAIL
                                                          : GPIO_DETAIL;
    }
  } else if (frameStarted) {
    result.stopCompleted =
        _finishStop(transfer.minimumPostTransferHighUs, deadlineUs) ==
        TransportCode::OK;
  } else {
    _setLine(true);
  }

  _resumeScheduler();
  _releaseTimingLock();
  return result;
}

TransferResult Esp32Transport::_resetAndDiscover(
    bool& present,
    uint64_t deadlineUs) {
  present = false;
  const uint64_t initialNowUs = _nowUs();
  if (initialNowUs >= deadlineUs ||
      deadlineUs - initialNowUs <= MIN_RESET_DISCOVERY_US) {
    return failure(TransportCode::TIMEOUT, TransferPhase::RESET_LOW,
                   DEADLINE_DETAIL);
  }
  if (!_acquireTimingLock()) {
    return failure(TransportCode::IO_ERROR, TransferPhase::RESET_LOW,
                   TIMING_LOCK_DETAIL);
  }
  _setLine(false);
  if (!_delayWithinDeadline(RESET_LOW_US, deadlineUs)) {
    _setLine(true);
    _releaseTimingLock();
    return failure(TransportCode::TIMEOUT, TransferPhase::RESET_LOW,
                   DEADLINE_DETAIL);
  }
  _setLine(true);
  if (!_delayWithinDeadline(RESET_RECOVERY_US, deadlineUs)) {
    _setLine(true);
    _releaseTimingLock();
    return failure(TransportCode::TIMEOUT,
                   TransferPhase::RESET_RECOVERY, DEADLINE_DETAIL);
  }

  TransferResult result{};
  result.code = TransportCode::OK;
  SegmentClock clock{};
  bool responseLow = false;
  _suspendScheduler();
  _enterCritical();
  if (!_beginSegment(deadlineUs, clock)) {
    result = failure(TransportCode::TIMEOUT,
                     TransferPhase::DISCOVERY_REQUEST, DEADLINE_DETAIL);
  } else {
    _setLine(false);
    const uint32_t requestFallCycle = _cycleCount();
    if (!_spinFrom(requestFallCycle, DISCOVERY_REQUEST_NS, clock)) {
      result = failure(TransportCode::TIMEOUT,
                       TransferPhase::DISCOVERY_REQUEST, DEADLINE_DETAIL);
    } else {
      _setLine(true);
      if (!_spinFrom(requestFallCycle,
                     DISCOVERY_SAMPLE_FROM_FALL_NS, clock)) {
        result = failure(TransportCode::TIMEOUT,
                         TransferPhase::DISCOVERY_SAMPLE, DEADLINE_DETAIL);
      } else {
        responseLow = !_readLine();
        if (!_spinFrom(requestFallCycle, DISCOVERY_RELEASE_CHECK_NS, clock)) {
          result = failure(TransportCode::TIMEOUT,
                           TransferPhase::DISCOVERY_RELEASE,
                           DEADLINE_DETAIL);
        } else if (!_readLine()) {
          result = failure(TransportCode::LINE_STUCK,
                           TransferPhase::DISCOVERY_RELEASE, GPIO_DETAIL);
        } else {
          result.phase = TransferPhase::DISCOVERY_RELEASE;
        }
      }
    }
  }
  _setLine(true);
  _exitCritical();

  if (result.ok()) {
    const TransportCode postCode =
        _finishStop(POST_DISCOVERY_HIGH_US, deadlineUs);
    if (postCode != TransportCode::OK) {
      result = failure(postCode, TransferPhase::DISCOVERY_RELEASE,
                       postCode == TransportCode::TIMEOUT ? DEADLINE_DETAIL
                                                          : GPIO_DETAIL);
    }
  }
  _resumeScheduler();
  _releaseTimingLock();

  if (!result.ok()) {
    present = false;
    _setLine(true);
    return result;
  }
  present = responseLow;
  return result;
}

TransferResult Esp32Transport::_waitUntilUs(uint64_t deadlineUs) {
  TransferResult result{};
  const uint64_t initialNowUs = _nowUs();
  if (initialNowUs < deadlineUs &&
      deadlineUs - initialNowUs > MAX_WAIT_US) {
    return failure(TransportCode::TIMEOUT, TransferPhase::WAIT_HIGH,
                   DEADLINE_DETAIL);
  }
  _setLine(true);
  if (initialNowUs >= deadlineUs) {
    if (!_readLine()) {
      return failure(TransportCode::LINE_STUCK, TransferPhase::WAIT_HIGH,
                     GPIO_DETAIL);
    }
    result.code = TransportCode::OK;
    result.phase = TransferPhase::WAIT_HIGH;
    return result;
  }

  const uint32_t remainingUs =
      static_cast<uint32_t>(deadlineUs - initialNowUs);
  _delayUs(remainingUs);
  if (!_acquireTimingLock()) {
    return failure(TransportCode::IO_ERROR, TransferPhase::WAIT_HIGH,
                   TIMING_LOCK_DETAIL);
  }
  const uint32_t guardStartCycle = _cycleCount();
  uint32_t calibratedCyclesPerUs = QUALIFIED_MIN_CPU_CYCLES_PER_US;
#if AT21CS_HAS_ESP32_PLATFORM
  calibratedCyclesPerUs = esp_rom_get_cpu_ticks_per_us();
#endif
  if (calibratedCyclesPerUs == 0 ||
      calibratedCyclesPerUs > QUALIFIED_MIN_CPU_CYCLES_PER_US) {
    calibratedCyclesPerUs = QUALIFIED_MIN_CPU_CYCLES_PER_US;
  }
  const uint32_t guardCycles =
      calibratedCyclesPerUs * FINAL_WAIT_CYCLE_GUARD_US;
  for (uint32_t poll = 0; poll < FINAL_WAIT_POLL_LIMIT; ++poll) {
    if (_nowUs() >= deadlineUs) {
      if (!_readLine()) {
        _releaseTimingLock();
        return failure(TransportCode::LINE_STUCK,
                       TransferPhase::WAIT_HIGH, GPIO_DETAIL);
      }
      result.code = TransportCode::OK;
      result.phase = TransferPhase::WAIT_HIGH;
      _releaseTimingLock();
      return result;
    }
#if defined(AT21CS_TESTING) && !AT21CS_HAS_ESP32_PLATFORM
    ++_testCycle;
#endif
    if (static_cast<uint32_t>(_cycleCount() - guardStartCycle) >=
        guardCycles) {
      _releaseTimingLock();
      return failure(TransportCode::TIMEOUT, TransferPhase::WAIT_HIGH,
                     FROZEN_CLOCK_DETAIL);
    }
  }
  _releaseTimingLock();
  return failure(TransportCode::TIMEOUT, TransferPhase::WAIT_HIGH,
                 FROZEN_CLOCK_DETAIL);
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
#if defined(AT21CS_TESTING) && !AT21CS_HAS_ESP32_PLATFORM
  present = _config.presenceActiveHigh ? _testPresenceLevel
                                       : !_testPresenceLevel;
  TransferResult result{};
  result.code = TransportCode::OK;
  result.phase = TransferPhase::PRESENCE;
  return result;
#elif AT21CS_HAS_ESP32_PLATFORM
  const int level =
      gpio_get_level(static_cast<gpio_num_t>(_config.presencePin));
  present = _config.presenceActiveHigh ? level != 0 : level == 0;
  TransferResult result{};
  result.code = TransportCode::OK;
  result.phase = TransferPhase::PRESENCE;
  return result;
#endif
}

uint64_t Esp32Transport::_nowUs() {
#if defined(AT21CS_TESTING) && !AT21CS_HAS_ESP32_PLATFORM
  ++_timingMux;
  _testCycle += _testNowCallCycles;
  return _testNowUs;
#elif AT21CS_HAS_ESP32_PLATFORM
  const int64_t value = esp_timer_get_time();
  return value > 0 ? static_cast<uint64_t>(value) : 0;
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

bool Esp32Transport::_beginSegment(
    uint64_t deadlineUs,
    SegmentClock& clock) {
#if AT21CS_HAS_ESP32_PLATFORM
  const uint32_t cyclesPerUs = esp_rom_get_cpu_ticks_per_us();
#else
  const uint32_t cyclesPerUs = 240;
#endif
  if (cyclesPerUs == 0) {
    return false;
  }
  // Anchor before reading the absolute clock. Any cycles spent sampling that
  // clock are then charged against the remaining interval instead of being
  // added after the caller's deadline.
  const uint32_t startCycle = _cycleCount();
  const uint64_t nowUs = _nowUs();
  if (nowUs >= deadlineUs) {
    return false;
  }
  const uint64_t remainingUs = deadlineUs - nowUs;
  const uint64_t maximumDeadlineCycles =
      std::numeric_limits<uint32_t>::max();
  const uint64_t availableCycles =
      remainingUs > maximumDeadlineCycles / cyclesPerUs
          ? maximumDeadlineCycles
          : remainingUs * cyclesPerUs;
  clock.startCycle = startCycle;
  clock.cyclesPerUs = cyclesPerUs;
  clock.deadlineCycles = static_cast<uint32_t>(availableCycles);
  clock.startUs = nowUs;
  return clock.deadlineCycles != 0 &&
         !_segmentExpired(clock, _cycleCount());
}

bool Esp32Transport::_spinFrom(
    uint32_t edgeCycle,
    uint32_t durationNs,
    const SegmentClock& clock) const {
  const uint32_t targetCycles = _cyclesForNs(durationNs, clock.cyclesPerUs);
#if defined(AT21CS_TESTING) && !AT21CS_HAS_ESP32_PLATFORM
  const uint32_t targetCycle = edgeCycle + targetCycles;
  if (_segmentExpired(clock, targetCycle)) {
    _testCycle = clock.startCycle + clock.deadlineCycles;
    if (!_testClockFrozen) {
      _testNowUs = clock.startUs +
                   clock.deadlineCycles / clock.cyclesPerUs;
    }
    return false;
  }
  _testCycle = targetCycle;
  if (!_testClockFrozen) {
    _testNowUs = clock.startUs +
                 static_cast<uint32_t>(_testCycle - clock.startCycle) /
                     clock.cyclesPerUs;
  }
  return true;
#else
  for (uint32_t spin = 0; spin < TIMING_SPIN_LIMIT; ++spin) {
    const uint32_t currentCycle = _cycleCount();
    if (_segmentExpired(clock, currentCycle)) {
      return false;
    }
    if (static_cast<uint32_t>(currentCycle - edgeCycle) >= targetCycles) {
      return true;
    }
  }
  return false;
#endif
}

uint32_t Esp32Transport::_cyclesForNs(
    uint32_t durationNs,
    uint32_t cyclesPerUs) {
  const uint64_t scaled =
      static_cast<uint64_t>(durationNs) * cyclesPerUs;
  return static_cast<uint32_t>((scaled + 999u) / 1000u);
}

bool Esp32Transport::_segmentExpired(
    const SegmentClock& clock,
    uint32_t currentCycle) {
  return static_cast<uint32_t>(currentCycle - clock.startCycle) >=
         clock.deadlineCycles;
}

uint32_t Esp32Transport::_cycleCount() const {
#if AT21CS_HAS_ESP32_PLATFORM
  return esp_cpu_get_cycle_count();
#elif defined(AT21CS_TESTING)
  return _testCycle;
#endif
}

bool Esp32Transport::_acquireTimingLock() {
#if defined(AT21CS_TESTING) && !AT21CS_HAS_ESP32_PLATFORM
  ++_testTimingLockAcquireCount;
  if (_testTimingLockFailure) {
    return false;
  }
  ++_testTimingLockDepth;
  return true;
#elif AT21CS_HAS_ESP32_PLATFORM && defined(CONFIG_PM_ENABLE) && \
    CONFIG_PM_ENABLE
  return _pmLock != nullptr &&
         esp_pm_lock_acquire(
             static_cast<esp_pm_lock_handle_t>(_pmLock)) == ESP_OK;
#else
  return true;
#endif
}

void Esp32Transport::_releaseTimingLock() {
#if defined(AT21CS_TESTING) && !AT21CS_HAS_ESP32_PLATFORM
  if (_testTimingLockDepth != 0) {
    --_testTimingLockDepth;
    ++_testTimingLockReleaseCount;
  }
#elif AT21CS_HAS_ESP32_PLATFORM && defined(CONFIG_PM_ENABLE) && \
    CONFIG_PM_ENABLE
  if (_pmLock != nullptr) {
    (void)esp_pm_lock_release(
        static_cast<esp_pm_lock_handle_t>(_pmLock));
  }
#endif
}

void Esp32Transport::_enterCritical() {
#if AT21CS_HAS_ESP32_PLATFORM
  portENTER_CRITICAL(&_timingMux);
#elif defined(AT21CS_TESTING)
  ++_timingMux;
#endif
}

void Esp32Transport::_exitCritical() {
#if AT21CS_HAS_ESP32_PLATFORM
  portEXIT_CRITICAL(&_timingMux);
#elif defined(AT21CS_TESTING)
  ++_timingMux;
#endif
}

void Esp32Transport::_suspendScheduler() {
#if AT21CS_HAS_ESP32_PLATFORM
  vTaskSuspendAll();
#endif
}

void Esp32Transport::_resumeScheduler() {
#if AT21CS_HAS_ESP32_PLATFORM
  (void)xTaskResumeAll();
#endif
}

void Esp32Transport::_setLine(bool released) {
#if defined(AT21CS_TESTING) && !AT21CS_HAS_ESP32_PLATFORM
  ++_timingMux;
  _testLineReleased = released;
  if (_testEventCount < TEST_EVENT_CAPACITY) {
    _testEvents[_testEventCount].cycle = _testCycle;
    _testEvents[_testEventCount].released = released;
    ++_testEventCount;
  } else {
    _testOverflow = true;
  }
#elif AT21CS_HAS_ESP32_PLATFORM
  gpio_ll_set_level(&GPIO, static_cast<uint32_t>(_config.sioPin),
                    released ? 1u : 0u);
#endif
}

bool Esp32Transport::_readLine() const {
#if defined(AT21CS_TESTING) && !AT21CS_HAS_ESP32_PLATFORM
  ++_timingMux;
  if (_testLevelRead < _testLevelCount) {
    if (_testReadCount < TEST_LEVEL_CAPACITY) {
      _testReadCycles[_testReadCount++] = _testCycle;
    } else {
      _testOverflow = true;
    }
    return _testLevels[_testLevelRead++];
  }
  if (_testReadCount < TEST_LEVEL_CAPACITY) {
    _testReadCycles[_testReadCount++] = _testCycle;
  } else {
    _testOverflow = true;
  }
  return _testLineReleased;
#elif AT21CS_HAS_ESP32_PLATFORM
  return gpio_ll_get_level(&GPIO,
                           static_cast<uint32_t>(_config.sioPin)) != 0;
#endif
}

void Esp32Transport::_delayUs(uint32_t durationUs) const {
#if defined(AT21CS_TESTING) && !AT21CS_HAS_ESP32_PLATFORM
  ++_timingMux;
  _testCycle += durationUs * 240u;
  if (!_testClockFrozen) {
    const uint32_t advance =
        durationUs < _testDelayAdvanceLimitUs ? durationUs
                                              : _testDelayAdvanceLimitUs;
    _testNowUs += advance;
    if (_testFreezeAfterDelay) {
      _testClockFrozen = true;
    }
  }
#elif AT21CS_HAS_ESP32_PLATFORM
  esp_rom_delay_us(durationUs);
#endif
}

bool Esp32Transport::_writeBit(
    bool one,
    const Timing& timing,
    const SegmentClock& clock,
    bool* delivered) {
  if (delivered != nullptr) {
    *delivered = false;
  }
  const uint32_t beforeEdge = _cycleCount();
  const uint32_t bitCycles =
      _cyclesForNs(timing.bitNs, clock.cyclesPerUs);
  if (_segmentExpired(clock, beforeEdge) ||
      _segmentExpired(clock, beforeEdge + bitCycles)) {
    return false;
  }
  _setLine(false);
  const uint32_t fallingEdge = _cycleCount();
  if (_segmentExpired(clock, fallingEdge + bitCycles)) {
    _setLine(true);
    return false;
  }
  const uint32_t lowNs = one ? timing.low1Ns : timing.low0Ns;
  if (!_spinFrom(fallingEdge, lowNs, clock)) {
    _setLine(true);
    return false;
  }
  _setLine(true);
  if (delivered != nullptr) {
    *delivered = true;
  }
  if (!_spinFrom(fallingEdge, timing.bitNs, clock)) {
    _setLine(true);
    return false;
  }
  return true;
}

Esp32Transport::ReadBitResult
Esp32Transport::_readBit(const Timing& timing,
                         const SegmentClock& clock) {
  ReadBitResult result{};
  const uint32_t beforeEdge = _cycleCount();
  const uint32_t bitCycles =
      _cyclesForNs(timing.bitNs, clock.cyclesPerUs);
  if (_segmentExpired(clock, beforeEdge) ||
      _segmentExpired(clock, beforeEdge + bitCycles)) {
    return result;
  }
  _setLine(false);
  const uint32_t fallingEdge = _cycleCount();
  if (_segmentExpired(clock, fallingEdge + bitCycles)) {
    _setLine(true);
    return result;
  }
  if (!_spinFrom(fallingEdge, timing.readLowNs, clock)) {
    _setLine(true);
    return result;
  }
  _setLine(true);
  if (!_spinFrom(fallingEdge, timing.readSampleFromFallNs, clock)) {
    _setLine(true);
    return result;
  }
  result.value = _readLine();
  result.sampled = true;
  result.completed = _spinFrom(fallingEdge, timing.bitNs, clock);
  if (!result.completed) {
    _setLine(true);
  }
  return result;
}

bool Esp32Transport::_writeEightBits(
    uint8_t value,
    const Timing& timing,
    const SegmentClock& clock,
    bool& allBitsDelivered) {
  allBitsDelivered = false;
  for (int bit = 7; bit >= 0; --bit) {
    bool bitDelivered = false;
    if (!_writeBit(((static_cast<uint32_t>(value) >>
                     static_cast<uint32_t>(bit)) &
                    0x01u) != 0u,
                   timing, clock, &bitDelivered)) {
      allBitsDelivered = bit == 0 && bitDelivered;
      return false;
    }
  }
  allBitsDelivered = true;
  return true;
}

bool Esp32Transport::_readEightBits(
    uint8_t& value,
    const Timing& timing,
    const SegmentClock& clock) {
  value = 0;
  for (int bit = 7; bit >= 0; --bit) {
    const ReadBitResult bitResult = _readBit(timing, clock);
    if (!bitResult.sampled) {
      return false;
    }
    if (bitResult.value) {
      value = static_cast<uint8_t>(
          value | static_cast<uint8_t>(1u << static_cast<uint32_t>(bit)));
    }
    if (!bitResult.completed) {
      return false;
    }
  }
  return true;
}

Esp32Transport::ReadBitResult
Esp32Transport::_readAck(const Timing& timing,
                         const SegmentClock& clock) {
  return _readBit(timing, clock);
}

TransportCode Esp32Transport::_finishStop(uint32_t highUs,
                                          uint64_t deadlineUs) {
  _setLine(true);
  if (!_delayWithinDeadline(highUs, deadlineUs)) {
    return TransportCode::TIMEOUT;
  }
  return _readLine() ? TransportCode::OK : TransportCode::LINE_STUCK;
}

TransferResult Esp32Transport::_staleResult() {
  TransferResult result{};
  result.detail = BACKEND_NOT_INITIALIZED_DETAIL;
  return result;
}

Esp32Transport::Timing Esp32Transport::_timingFor(SpeedMode speed) {
  if (speed == SpeedMode::STANDARD_SPEED) {
    return Timing{64000, 32000, 6000, 6000, 7000, 650};
  }
  return Timing{};
}

bool Esp32Transport::_pinNumbersInRange(
    const Esp32TransportConfig& config,
    int pinCount) {
  if (pinCount <= 0 || config.sioPin < 0 || config.sioPin >= pinCount) {
    return false;
  }
  return config.presencePin == -1 ||
         (config.presencePin >= 0 && config.presencePin < pinCount &&
          config.presencePin != config.sioPin);
}

}  // namespace AT21CS

#undef AT21CS_ESP32_IRAM_ATTR
#undef AT21CS_HAS_ESP32_PLATFORM
