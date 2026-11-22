#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace at21cs_example {

class CliOutput {
 public:
  // Fits the longest status line, including full-width counters and state names.
  static constexpr size_t FORMAT_BYTES = 256;

  virtual void write(const char* bytes, size_t length) = 0;

  void print(const char* text) { write(text, std::strlen(text)); }

  void println() { write("\r\n", 2); }

  void println(const char* text) {
    print(text);
    println();
  }

  bool printf(const char* format, ...) {
    char buffer[FORMAT_BYTES];
    va_list args;
    va_start(args, format);
    const int length = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (length < 0) {
      println("output formatting failed");
      return false;
    }
    if (static_cast<size_t>(length) >= sizeof(buffer)) {
      println("output truncated");
      return false;
    }
    write(buffer, static_cast<size_t>(length));
    return true;
  }

 protected:
  ~CliOutput() = default;
};

}  // namespace at21cs_example
