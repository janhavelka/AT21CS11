#pragma once

#include <cstddef>
#include <cstdint>

namespace at21cs_example {

static constexpr size_t LINE_BYTES = 128;
static constexpr size_t MAX_ARGS = 8;

enum class LineEvent : uint8_t {
  NONE = 0,
  READY,
  TOO_LONG,
  TOO_MANY_ARGS
};

struct Arguments {
  size_t count = 0;
  const char* values[MAX_ARGS] = {};
};

class BoundedCli {
 public:
  LineEvent push(char value, Arguments& arguments) {
    arguments = {};

    if (value == '\n' && _ignoreLf) {
      _ignoreLf = false;
      return LineEvent::NONE;
    }
    _ignoreLf = false;

    if (value == '\r' || value == '\n') {
      _ignoreLf = value == '\r';
      if (_discarding) {
        _discarding = false;
        _length = 0;
        return LineEvent::TOO_LONG;
      }
      if (_length == 0) {
        return LineEvent::NONE;
      }

      _line[_length] = '\0';
      const LineEvent result = tokenize(arguments);
      _length = 0;
      return result;
    }

    if (_discarding) {
      return LineEvent::NONE;
    }

    if (_length >= LINE_BYTES - 1) {
      _discarding = true;
      _length = 0;
      return LineEvent::NONE;
    }

    _line[_length++] = value;
    return LineEvent::NONE;
  }

  void reset() {
    _length = 0;
    _discarding = false;
    _ignoreLf = false;
  }

 private:
  static bool separator(char value) {
    return value == ' ' || value == '\t';
  }

  LineEvent tokenize(Arguments& arguments) {
    char* cursor = _line;
    while (*cursor != '\0') {
      while (separator(*cursor)) {
        ++cursor;
      }
      if (*cursor == '\0') {
        break;
      }
      if (arguments.count == MAX_ARGS) {
        arguments = {};
        return LineEvent::TOO_MANY_ARGS;
      }

      arguments.values[arguments.count++] = cursor;
      while (*cursor != '\0' && !separator(*cursor)) {
        ++cursor;
      }
      if (*cursor != '\0') {
        *cursor++ = '\0';
      }
    }

    if (arguments.count == 0) {
      return LineEvent::NONE;
    }
    return LineEvent::READY;
  }

  char _line[LINE_BYTES] = {};
  size_t _length = 0;
  bool _discarding = false;
  bool _ignoreLf = false;
};

inline bool parseUnsigned(const char* text,
                          uint32_t maximum,
                          bool allowExplicitHex,
                          uint32_t& value) {
  if (text == nullptr || *text == '\0' || *text == '-') {
    return false;
  }

  unsigned base = 10;
  const char* cursor = text;
  if (allowExplicitHex && cursor[0] == '0' &&
      (cursor[1] == 'x' || cursor[1] == 'X')) {
    base = 16;
    cursor += 2;
    if (*cursor == '\0') {
      return false;
    }
  }

  uint32_t parsed = 0;
  size_t consumed = 0;
  while (*cursor != '\0' && consumed < LINE_BYTES) {
    unsigned digit = 0;
    if (*cursor >= '0' && *cursor <= '9') {
      digit = static_cast<unsigned>(*cursor - '0');
    } else if (base == 16 && *cursor >= 'a' && *cursor <= 'f') {
      digit = static_cast<unsigned>(*cursor - 'a') + 10U;
    } else if (base == 16 && *cursor >= 'A' && *cursor <= 'F') {
      digit = static_cast<unsigned>(*cursor - 'A') + 10U;
    } else {
      return false;
    }
    if (digit >= base || digit > maximum ||
        parsed > (maximum - digit) / base) {
      return false;
    }
    parsed = static_cast<uint32_t>(parsed * base + digit);
    ++cursor;
    ++consumed;
  }
  if (*cursor != '\0') {
    return false;
  }

  value = parsed;
  return true;
}

inline bool parseDecimal(const char* text, uint32_t maximum, uint32_t& value) {
  return parseUnsigned(text, maximum, false, value);
}

inline bool parseDecimalOrExplicitHex(const char* text,
                                      uint32_t maximum,
                                      uint32_t& value) {
  return parseUnsigned(text, maximum, true, value);
}

inline bool parseHexBytes(const char* text,
                          uint8_t* bytes,
                          size_t capacity,
                          size_t& length) {
  if (text == nullptr || bytes == nullptr) {
    return false;
  }
  const size_t byteCapacity = capacity < 8 ? capacity : 8;
  const size_t maximumChars = byteCapacity * 2;
  size_t chars = 0;
  while (chars <= maximumChars && text[chars] != '\0') {
    ++chars;
  }
  if (chars < 2 || chars > maximumChars || (chars & 1U) != 0) {
    return false;
  }

  uint8_t parsed[8] = {};
  const size_t parsedLength = chars / 2;
  for (size_t index = 0; index < parsedLength; ++index) {
    uint8_t byte = 0;
    for (size_t nibble = 0; nibble < 2; ++nibble) {
      const char value = text[index * 2 + nibble];
      uint8_t digit = 0;
      if (value >= '0' && value <= '9') {
        digit = static_cast<uint8_t>(value - '0');
      } else if (value >= 'a' && value <= 'f') {
        digit = static_cast<uint8_t>(value - 'a' + 10);
      } else if (value >= 'A' && value <= 'F') {
        digit = static_cast<uint8_t>(value - 'A' + 10);
      } else {
        return false;
      }
      byte = static_cast<uint8_t>((byte << 4U) | digit);
    }
    parsed[index] = byte;
  }

  for (size_t index = 0; index < parsedLength; ++index) {
    bytes[index] = parsed[index];
  }
  length = parsedLength;
  return true;
}

}  // namespace at21cs_example
