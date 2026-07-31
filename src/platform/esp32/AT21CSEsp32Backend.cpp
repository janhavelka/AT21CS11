/// @file AT21CSEsp32Backend.cpp
/// @brief Private built-in single-wire GPIO/timing backend for ESP32 and Arduino.

#include "AT21CS/AT21CS.h"

#ifndef AT21CS_ENABLE_ESP32_COMPAT_BACKEND
#define AT21CS_ENABLE_ESP32_COMPAT_BACKEND 1
#endif

#if AT21CS_ENABLE_ESP32_COMPAT_BACKEND && \
    (defined(ARDUINO_ARCH_ESP32) || defined(AT21CS_PLATFORM_IDF) || defined(ESP_PLATFORM))
#define AT21CS_BACKEND_HAS_ESP32 1
#else
#define AT21CS_BACKEND_HAS_ESP32 0
#endif

#if AT21CS_ENABLE_ESP32_COMPAT_BACKEND && (defined(ARDUINO) || defined(ARDUINO_ARCH_ESP32))
#define AT21CS_BACKEND_HAS_ARDUINO 1
#else
#define AT21CS_BACKEND_HAS_ARDUINO 0
#endif

#if AT21CS_BACKEND_HAS_ARDUINO
#include <Arduino.h>
#endif

#if AT21CS_BACKEND_HAS_ESP32
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#if __has_include(<esp_cpu.h>)
#include <esp_cpu.h>
#endif
#define AT21CS_IRAM IRAM_ATTR
#else
#define AT21CS_IRAM
#endif

namespace {

#if AT21CS_BACKEND_HAS_ESP32
portMUX_TYPE gTimingMux = portMUX_INITIALIZER_UNLOCKED;
#endif

}  // namespace

namespace AT21CS {

Status Driver::_configurePins() {
  if (_hasTransport()) {
    if (_config.transport->begin != nullptr) {
      return _config.transport->begin(_config.transport->user);
    }
    return Status::Ok();
  }

#if AT21CS_BACKEND_HAS_ESP32
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

  const uint8_t pin = static_cast<uint8_t>(_config.sioPin);
  if (pin < 32) {
    _backend.lineSetReg = static_cast<uintptr_t>(GPIO_OUT_W1TS_REG);
    _backend.lineClrReg = static_cast<uintptr_t>(GPIO_OUT_W1TC_REG);
    _backend.lineInReg = static_cast<uintptr_t>(GPIO_IN_REG);
    _backend.lineMask = (1U << pin);
  } else {
    _backend.lineSetReg = static_cast<uintptr_t>(GPIO_OUT1_W1TS_REG);
    _backend.lineClrReg = static_cast<uintptr_t>(GPIO_OUT1_W1TC_REG);
    _backend.lineInReg = static_cast<uintptr_t>(GPIO_IN1_REG);
    _backend.lineMask = (1U << (pin - 32));
  }

#if AT21CS_BACKEND_HAS_ARDUINO
  _backend.cyclesPerUs = static_cast<uint32_t>(getCpuFrequencyMhz());
#endif

  if (_config.presencePin >= 0) {
    gpio_config_t presenceCfg{};
    presenceCfg.pin_bit_mask = (1ULL << static_cast<uint8_t>(_config.presencePin));
    presenceCfg.mode = GPIO_MODE_INPUT;
    presenceCfg.pull_up_en = GPIO_PULLUP_DISABLE;
    presenceCfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    presenceCfg.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&presenceCfg) != ESP_OK) {
      return Status::Error(Err::INVALID_CONFIG, "Failed to configure presencePin",
                           _config.presencePin);
    }
  }
#elif AT21CS_BACKEND_HAS_ARDUINO
  pinMode(static_cast<uint8_t>(_config.sioPin), OUTPUT_OPEN_DRAIN);
  digitalWrite(static_cast<uint8_t>(_config.sioPin), HIGH);
  if (_hasPresenceIndicator()) {
    pinMode(static_cast<uint8_t>(_config.presencePin), INPUT);
  }
#else
  return Status::Error(Err::INVALID_CONFIG, "No GPIO backend for this platform");
#endif

  return Status::Ok();
}

bool Driver::_presencePinReportsPresent() const {
  if (_hasTransport()) {
    if (_config.transport->presencePresent != nullptr) {
      return _config.transport->presencePresent(_config.transport->user);
    }
    return true;
  }

  if (_config.presencePin < 0) {
    return true;
  }
#if AT21CS_BACKEND_HAS_ESP32
  const int level = gpio_get_level(static_cast<gpio_num_t>(_config.presencePin));
#elif AT21CS_BACKEND_HAS_ARDUINO
  const int level = digitalRead(static_cast<uint8_t>(_config.presencePin));
#else
  const int level = 0;
#endif
  return _config.presenceActiveHigh ? (level != 0) : (level == 0);
}

AT21CS_IRAM void Driver::_releaseLine() {
  if (_hasTransport()) {
    _config.transport->releaseLine(_config.transport->user);
    return;
  }

#if AT21CS_BACKEND_HAS_ESP32
  if (_backend.lineSetReg == 0U) {
    return;
  }
  *reinterpret_cast<volatile uint32_t*>(_backend.lineSetReg) = _backend.lineMask;
#elif AT21CS_BACKEND_HAS_ARDUINO
  digitalWrite(static_cast<uint8_t>(_config.sioPin), HIGH);
#endif
}

AT21CS_IRAM void Driver::_lineLow() {
  if (_hasTransport()) {
    if (_config.transport->driveLowForUs != nullptr) {
      _config.transport->driveLowForUs(0U, _config.transport->user);
    }
    return;
  }

#if AT21CS_BACKEND_HAS_ESP32
  if (_backend.lineClrReg == 0U) {
    return;
  }
  *reinterpret_cast<volatile uint32_t*>(_backend.lineClrReg) = _backend.lineMask;
#elif AT21CS_BACKEND_HAS_ARDUINO
  digitalWrite(static_cast<uint8_t>(_config.sioPin), LOW);
#endif
}

AT21CS_IRAM bool Driver::_readLine() const {
  if (_hasTransport()) {
    if (_config.transport->readLine != nullptr) {
      return _config.transport->readLine(_config.transport->user);
    }
    return true;
  }

#if AT21CS_BACKEND_HAS_ESP32
  if (_backend.lineInReg == 0U) {
    return true;
  }
  return (*reinterpret_cast<volatile uint32_t*>(_backend.lineInReg) & _backend.lineMask) != 0;
#elif AT21CS_BACKEND_HAS_ARDUINO
  return digitalRead(static_cast<uint8_t>(_config.sioPin)) != 0;
#else
  return true;
#endif
}

AT21CS_IRAM void Driver::driveLow(uint32_t lowUs) {
  _lineLow();
  _sleepUs(lowUs);
}

AT21CS_IRAM void Driver::releaseLine() {
  _releaseLine();
}

AT21CS_IRAM bool Driver::readLine() {
  return _readLine();
}

AT21CS_IRAM void Driver::txBit0() {
  _lineLow();
  _sleepUs(_timing.low0Us);
  _releaseLine();

  if (_timing.bitUs > _timing.low0Us) {
    _sleepUs(static_cast<uint32_t>(_timing.bitUs - _timing.low0Us));
  }
}

AT21CS_IRAM void Driver::txBit1() {
  _lineLow();
  _sleepUs(_timing.low1Us);
  _releaseLine();

  if (_timing.bitUs > _timing.low1Us) {
    _sleepUs(static_cast<uint32_t>(_timing.bitUs - _timing.low1Us));
  }
}

AT21CS_IRAM bool Driver::rxBit() {
  _lineLow();
  _sleepUs(_timing.readLowUs);
  _releaseLine();

  if (_timing.readSampleUs > 0) {
    _sleepUs(_timing.readSampleUs);
  }

  const bool bit = _readLine();
  const uint16_t elapsed = static_cast<uint16_t>(_timing.readLowUs + _timing.readSampleUs);
  if (_timing.bitUs > elapsed) {
    _sleepUs(static_cast<uint32_t>(_timing.bitUs - elapsed));
  }

  return bit;
}

AT21CS_IRAM bool Driver::txByte(uint8_t value) {
  if (_hasTransport()) {
    return _config.transport->writeByteReadAck(value, _timing, _config.transport->user);
  }

#if AT21CS_BACKEND_HAS_ESP32
  portENTER_CRITICAL(&gTimingMux);
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

#if AT21CS_BACKEND_HAS_ESP32
  portEXIT_CRITICAL(&gTimingMux);
#endif

  return ack;
}

AT21CS_IRAM uint8_t Driver::rxByte(bool ack) {
  if (_hasTransport()) {
    return _config.transport->readByteSendAck(ack, _timing, _config.transport->user);
  }

#if AT21CS_BACKEND_HAS_ESP32
  portENTER_CRITICAL(&gTimingMux);
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

#if AT21CS_BACKEND_HAS_ESP32
  portEXIT_CRITICAL(&gTimingMux);
#endif

  return value;
}

AT21CS_IRAM void Driver::_sendStart() {
  _releaseLine();
  _sleepUs(_timing.htssUs);
}

AT21CS_IRAM void Driver::_sendStop() {
  _releaseLine();
  _sleepUs(_timing.htssUs);
}

Status Driver::_resetAndDiscoverRaw() {
  if (_hasTransport()) {
    const Status st = _config.transport->resetAndDiscover(HIGH_SPEED_TIMING,
                                                           _config.transport->user);
    if (st.ok()) {
      _setSpeedMode(SpeedMode::HIGH_SPEED);
    }
    return st;
  }

  driveLow(DISCHARGE_LOW_US);
  releaseLine();
  _sleepUs(RESET_RECOVERY_US);

#if AT21CS_BACKEND_HAS_ESP32
  portENTER_CRITICAL(&gTimingMux);
#endif

  _lineLow();
  _sleepUs(DISCOVERY_REQUEST_US);
  _releaseLine();

  _sleepUs(DISCOVERY_STROBE_DELAY_US);

  _lineLow();
  _sleepUs(DISCOVERY_STROBE_US);
  _releaseLine();

  _sleepUs(DISCOVERY_SAMPLE_DELAY_US);
  const bool present = !_readLine();

#if AT21CS_BACKEND_HAS_ESP32
  portEXIT_CRITICAL(&gTimingMux);
#endif

  _sleepUs(HIGH_SPEED_TIMING.htssUs);

  if (!present) {
    return Status::Error(Err::DISCOVERY_FAILED, "Discovery response not detected");
  }

  _setSpeedMode(SpeedMode::HIGH_SPEED);
  return Status::Ok();
}

uint32_t Driver::_nowMs() const {
  if (_config.nowMs != nullptr) {
    return _config.nowMs(_config.timeUser);
  }
  if (_hasTransport() && _config.transport->nowMs != nullptr) {
    return _config.transport->nowMs(_config.transport->user);
  }
#if AT21CS_BACKEND_HAS_ARDUINO
  return millis();
#elif AT21CS_BACKEND_HAS_ESP32
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
#else
  return 0U;
#endif
}

AT21CS_IRAM void Driver::_sleepUs(uint32_t us) const {
  if (us == 0U) {
    return;
  }
  if (_config.sleepUs != nullptr) {
    _config.sleepUs(us, _config.timeUser);
    return;
  }
  if (_hasTransport() && _config.transport->sleepUs != nullptr) {
    _config.transport->sleepUs(us, _config.transport->user);
    return;
  }
#if AT21CS_BACKEND_HAS_ESP32 && defined(ARDUINO_ARCH_ESP32)
  const uint32_t target = us * _backend.cyclesPerUs;
  const uint32_t start = esp_cpu_get_cycle_count();
  while ((esp_cpu_get_cycle_count() - start) < target) {}
#elif AT21CS_BACKEND_HAS_ESP32
  esp_rom_delay_us(us);
#elif AT21CS_BACKEND_HAS_ARDUINO
  delayMicroseconds(us);
#endif
}

}  // namespace AT21CS
