/// @file Arduino.h
/// @brief Minimal Arduino stub for native testing
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <cstdlib>

// Basic types
using byte = uint8_t;

// Timing stubs
inline uint32_t millis() { return 0; }
inline uint32_t micros() { return 0; }
inline void delay(uint32_t ms) { (void)ms; }
inline void delayMicroseconds(uint32_t us) { (void)us; }

// Pin mode / GPIO stubs
static constexpr int INPUT = 0;
static constexpr int OUTPUT = 1;
static constexpr int OUTPUT_OPEN_DRAIN = 2;
static constexpr int INPUT_PULLUP = 3;
static constexpr int HIGH = 1;
static constexpr int LOW = 0;

inline void pinMode(uint8_t pin, uint8_t mode) {
  (void)pin;
  (void)mode;
}

inline void digitalWrite(uint8_t pin, uint8_t value) {
  (void)pin;
  (void)value;
}

inline int digitalRead(uint8_t pin) {
  (void)pin;
  return HIGH;
}

// Serial stub
class SerialClass {
public:
  void begin(uint32_t baud) { (void)baud; }
  void print(const char* s) { (void)s; }
  void println(const char* s = "") { (void)s; }
  void printf(const char* fmt, ...) { (void)fmt; }
  int available() { return 0; }
  int read() { return -1; }
  operator bool() { return true; }
};

extern SerialClass Serial;

// String class (minimal stub)
class String {
public:
  String() = default;
  String(const char* s) : _data(s ? s : "") {}
  const char* c_str() const { return _data.c_str(); }
  size_t length() const { return _data.length(); }
  void trim() {}
  bool startsWith(const char* prefix) const {
    return _data.find(prefix) == 0;
  }
  String substring(size_t start) const {
    return String(_data.substr(start).c_str());
  }
  int toInt() const { return std::stoi(_data); }
  String& operator+=(char c) { _data += c; return *this; }
private:
  std::string _data;
};
