/**
 * @file IdfArduinoCompat.h
 * @brief ESP-IDF compatibility layer for the Arduino-shaped AT21CS CLI example.
 *
 * This is example glue only. It implements the small Arduino surface used by
 * examples/01_basic_bringup_cli so the ESP-IDF example can share the same
 * command implementation without linking the Arduino framework.
 */

#pragma once

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <driver/gpio.h>
#include <esp_clk_tree.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef AT21CS_EXAMPLE_PLATFORM_IDF
#define AT21CS_EXAMPLE_PLATFORM_IDF 1
#endif

static constexpr uint8_t LOW = 0U;
static constexpr uint8_t HIGH = 1U;
static constexpr uint8_t INPUT = 0U;
static constexpr uint8_t OUTPUT = 1U;
static constexpr uint8_t OUTPUT_OPEN_DRAIN = 2U;
static constexpr uint8_t INPUT_PULLUP = 3U;

inline uint32_t millis() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

inline uint32_t micros() {
  return static_cast<uint32_t>(esp_timer_get_time());
}

inline uint32_t getCpuFrequencyMhz() {
  return static_cast<uint32_t>(esp_clk_cpu_freq() / 1000000U);
}

inline TickType_t idfExampleDelayTicks(uint32_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  if (ticks == 0 && ms > 0U) {
    ticks = 1;
  }
  return ticks;
}

inline void delay(uint32_t ms) {
  vTaskDelay(idfExampleDelayTicks(ms));
}

inline void delayMicroseconds(uint32_t us) {
  esp_rom_delay_us(us);
}

inline void yield() {
  vTaskDelay(1);
}

inline void pinMode(uint8_t pin, uint8_t mode) {
  const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  if (mode == OUTPUT_OPEN_DRAIN) {
    (void)gpio_set_direction(gpio, GPIO_MODE_INPUT_OUTPUT_OD);
    (void)gpio_set_level(gpio, 1);
  } else if (mode == OUTPUT) {
    (void)gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
  } else if (mode == INPUT_PULLUP) {
    (void)gpio_set_direction(gpio, GPIO_MODE_INPUT);
    (void)gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);
  } else {
    (void)gpio_set_direction(gpio, GPIO_MODE_INPUT);
  }
}

inline void digitalWrite(uint8_t pin, uint8_t value) {
  (void)gpio_set_level(static_cast<gpio_num_t>(pin), value == HIGH ? 1 : 0);
}

inline int digitalRead(uint8_t pin) {
  return gpio_get_level(static_cast<gpio_num_t>(pin));
}

class String {
 public:
  String() { clear(); }
  String(const char* text) { assign(text); }
  String(const String& other) { assign(other.c_str()); }

  String& operator=(const char* text) {
    assign(text);
    return *this;
  }

  String& operator=(const String& other) {
    if (this != &other) {
      assign(other.c_str());
    }
    return *this;
  }

  size_t length() const {
    return _len;
  }

  const char* c_str() const {
    return _buf;
  }

  char operator[](size_t index) const {
    return index < _len ? _buf[index] : '\0';
  }

  void trim() {
    size_t first = 0U;
    while (first < _len &&
           std::isspace(static_cast<unsigned char>(_buf[first])) != 0) {
      ++first;
    }
    size_t last = _len;
    while (last > first &&
           std::isspace(static_cast<unsigned char>(_buf[last - 1U])) != 0) {
      --last;
    }
    const size_t newLen = last - first;
    if (first > 0U && newLen > 0U) {
      std::memmove(_buf, _buf + first, newLen);
    }
    _len = newLen;
    _buf[_len] = '\0';
  }

  String substring(size_t start, size_t end) const {
    String out;
    if (start >= _len || end <= start) {
      return out;
    }
    if (end > _len) {
      end = _len;
    }
    const size_t copyLen = end - start;
    std::memcpy(out._buf, _buf + start, copyLen);
    out._buf[copyLen] = '\0';
    out._len = copyLen;
    return out;
  }

  long toInt() const {
    return std::strtol(_buf, nullptr, 0);
  }

  String& operator+=(char c) {
    if (_len < (kCapacity - 1U)) {
      _buf[_len++] = c;
      _buf[_len] = '\0';
    }
    return *this;
  }

  bool operator==(const char* rhs) const {
    return std::strcmp(_buf, rhs != nullptr ? rhs : "") == 0;
  }

  bool operator!=(const char* rhs) const {
    return !(*this == rhs);
  }

 private:
  static constexpr size_t kCapacity = 192U;

  void clear() {
    _buf[0] = '\0';
    _len = 0U;
  }

  void assign(const char* text) {
    if (text == nullptr) {
      clear();
      return;
    }
    std::strncpy(_buf, text, kCapacity - 1U);
    _buf[kCapacity - 1U] = '\0';
    _len = std::strlen(_buf);
  }

  char _buf[kCapacity] = {};
  size_t _len = 0U;
};

class IdfConsole {
 public:
  void begin(unsigned long) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) {
      (void)fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
  }

  explicit operator bool() const {
    return true;
  }

  int available() {
    pollInput();
    return static_cast<int>(_count);
  }

  int read() {
    pollInput();
    if (_count == 0U) {
      return -1;
    }
    const uint8_t value = _rx[_tail];
    _tail = (_tail + 1U) % kRxCapacity;
    --_count;
    return static_cast<int>(value);
  }

  size_t write(uint8_t value) {
    return fwrite(&value, 1U, 1U, stdout);
  }

  int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int written = vfprintf(stdout, fmt, args);
    va_end(args);
    return written;
  }

  void print(const char* value) {
    if (value != nullptr) {
      fputs(value, stdout);
    }
  }

  void print(char value) {
    (void)write(static_cast<uint8_t>(value));
  }

  void print(int value) {
    (void)printf("%d", value);
  }

  void print(unsigned int value) {
    (void)printf("%u", value);
  }

  void print(long value) {
    (void)printf("%ld", value);
  }

  void print(unsigned long value) {
    (void)printf("%lu", value);
  }

  void println() {
    (void)write(static_cast<uint8_t>('\n'));
  }

  template <typename T>
  void println(T value) {
    print(value);
    println();
  }

  void flush() {
    fflush(stdout);
  }

 private:
  static constexpr size_t kRxCapacity = 256U;

  void pollInput() {
    while (_count < kRxCapacity) {
      uint8_t value = 0U;
      const ssize_t readCount = ::read(STDIN_FILENO, &value, 1U);
      if (readCount == 1) {
        _rx[_head] = value;
        _head = (_head + 1U) % kRxCapacity;
        ++_count;
        continue;
      }
      if (readCount < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return;
      }
      return;
    }
  }

  uint8_t _rx[kRxCapacity] = {};
  size_t _head = 0U;
  size_t _tail = 0U;
  size_t _count = 0U;
};

extern IdfConsole Serial;
