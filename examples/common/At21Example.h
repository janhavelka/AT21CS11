/**
 * @file At21Example.h
 * @brief Shared helpers used by AT21CS examples.
 */

#pragma once

#include <Arduino.h>
#include <cerrno>
#include <climits>
#include <cstdlib>

#include "Log.h"
#include "AT21CS/AT21CS.h"

namespace ex {

inline const char* errToStr(AT21CS::Err err) {
  using AT21CS::Err;
  switch (err) {
    case Err::OK:
      return "OK";
    case Err::NOT_INITIALIZED:
      return "NOT_INITIALIZED";
    case Err::INVALID_CONFIG:
      return "INVALID_CONFIG";
    case Err::INVALID_PARAM:
      return "INVALID_PARAM";
    case Err::NOT_PRESENT:
      return "NOT_PRESENT";
    case Err::DISCOVERY_FAILED:
      return "DISCOVERY_FAILED";
    case Err::NACK_DEVICE_ADDRESS:
      return "NACK_DEVICE_ADDRESS";
    case Err::NACK_MEMORY_ADDRESS:
      return "NACK_MEMORY_ADDRESS";
    case Err::NACK_DATA:
      return "NACK_DATA";
    case Err::BUSY_TIMEOUT:
      return "BUSY_TIMEOUT";
    case Err::UNSUPPORTED_COMMAND:
      return "UNSUPPORTED_COMMAND";
    case Err::CRC_MISMATCH:
      return "CRC_MISMATCH";
    case Err::PART_MISMATCH:
      return "PART_MISMATCH";
    case Err::IO_ERROR:
      return "IO_ERROR";
    default:
      return "UNKNOWN";
  }
}

inline const char* stateToStr(AT21CS::DriverState state) {
  using AT21CS::DriverState;
  switch (state) {
    case DriverState::UNINIT:
      return "UNINIT";
    case DriverState::PROBING:
      return "PROBING";
    case DriverState::INIT_CONFIG:
      return "INIT_CONFIG";
    case DriverState::READY:
      return "READY";
    case DriverState::BUSY:
      return "BUSY";
    case DriverState::DEGRADED:
      return "DEGRADED";
    case DriverState::OFFLINE:
      return "OFFLINE";
    case DriverState::RECOVERING:
      return "RECOVERING";
    case DriverState::SLEEPING:
      return "SLEEPING";
    case DriverState::FAULT:
      return "FAULT";
    default:
      return "UNKNOWN";
  }
}

inline const char* partToStr(AT21CS::PartType part) {
  using AT21CS::PartType;
  switch (part) {
    case PartType::AT21CS01:
      return "AT21CS01";
    case PartType::AT21CS11:
      return "AT21CS11";
    default:
      return "UNKNOWN";
  }
}

inline const char* speedToStr(AT21CS::SpeedMode speed) {
  return speed == AT21CS::SpeedMode::STANDARD_SPEED ? "STANDARD" : "HIGH";
}

inline void printStatus(const AT21CS::Status& st) {
  Serial.printf("status=%s%s%s code=%u detail=%ld msg=%s\n",
                LOG_COLOR_RESULT(st.ok()),
                errToStr(st.code),
                LOG_COLOR_RESET,
                static_cast<unsigned>(st.code),
                static_cast<long>(st.detail),
                (st.msg != nullptr) ? st.msg : "");
}

inline void printHealth(const AT21CS::Driver& driver) {
  const uint32_t now = millis();
  const uint32_t totalOk = driver.totalSuccess();
  const uint32_t totalFail = driver.totalFailures();
  const uint32_t total = totalOk + totalFail;
  const float successRate = (total > 0U)
                                ? (100.0f * static_cast<float>(totalOk) / static_cast<float>(total))
                                : 0.0f;
  const bool online = driver.isOnline();
  const AT21CS::DriverState state = driver.state();
  const char* color =
      (state == AT21CS::DriverState::UNINIT)
          ? LOG_COLOR_RESET
          : LOG_COLOR_STATE(online, driver.consecutiveFailures());

  Serial.println("=== Driver Health ===");
  Serial.printf("state=%s%s%s online=%s%s%s\n",
                color,
                stateToStr(state),
                LOG_COLOR_RESET,
                online ? LOG_COLOR_GREEN : LOG_COLOR_RED,
                log_bool_str(online),
                LOG_COLOR_RESET);
  const char* failColor = (driver.consecutiveFailures() == 0U) ? LOG_COLOR_GREEN : LOG_COLOR_RED;
  const char* totalFailColor = (totalFail == 0U) ? LOG_COLOR_GREEN : LOG_COLOR_RED;
  const char* totalOkColor = (totalOk > 0U) ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW;
  const char* successRateColor = (successRate >= 99.9f) ? LOG_COLOR_GREEN :
                                 (successRate >= 80.0f) ? LOG_COLOR_YELLOW : LOG_COLOR_RED;
  Serial.printf("failures=%s%u%s totalFail=%s%lu%s totalOk=%s%lu%s successRate=%s%.1f%%%s\n",
                failColor,
                driver.consecutiveFailures(),
                LOG_COLOR_RESET,
                totalFailColor,
                static_cast<unsigned long>(totalFail),
                LOG_COLOR_RESET,
                totalOkColor,
                static_cast<unsigned long>(totalOk),
                LOG_COLOR_RESET,
                successRateColor,
                successRate,
                LOG_COLOR_RESET);

  const uint32_t lastOkMs = driver.lastOkMs();
  const uint32_t lastErrorMs = driver.lastErrorMs();
  if (lastOkMs > 0U) {
    Serial.printf("lastOk=%lu ms ago (at %lu ms)\n",
                  static_cast<unsigned long>(now - lastOkMs),
                  static_cast<unsigned long>(lastOkMs));
  } else {
    Serial.println("lastOk=never");
  }
  if (lastErrorMs > 0U) {
    Serial.printf("lastError=%lu ms ago (at %lu ms)\n",
                  static_cast<unsigned long>(now - lastErrorMs),
                  static_cast<unsigned long>(lastErrorMs));
  } else {
    Serial.println("lastError=never");
  }

  const AT21CS::Status last = driver.lastError();
  if (!last.ok()) {
    Serial.printf("lastErrorCode=%s%s%s detail=%ld msg=%s\n",
                  LOG_COLOR_RED,
                  errToStr(last.code),
                  LOG_COLOR_RESET,
                  static_cast<long>(last.detail),
                  (last.msg != nullptr) ? last.msg : "");
  }
}

inline bool readLine(String& outLine) {
  static String buffer;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      if (buffer.length() == 0) {
        continue;
      }
      outLine = buffer;
      buffer = "";
      outLine.trim();
      return true;
    }
    buffer += c;
  }
  return false;
}

inline bool parseU32(const String& token, uint32_t& value) {
  if (token.length() == 0) {
    return false;
  }
  char* end = nullptr;
  value = static_cast<uint32_t>(strtoul(token.c_str(), &end, 0));
  return end != nullptr && *end == '\0';
}

inline bool parseU8(const String& token, uint8_t& value) {
  uint32_t parsed = 0;
  if (!parseU32(token, parsed) || parsed > 0xFFU) {
    return false;
  }
  value = static_cast<uint8_t>(parsed);
  return true;
}

inline bool parseI32(const String& token, int32_t& value) {
  if (token.length() == 0) {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const long parsed = strtol(token.c_str(), &end, 0);
  if (end == nullptr || *end != '\0' || errno != 0) {
    return false;
  }
  if (parsed < INT32_MIN || parsed > INT32_MAX) {
    return false;
  }
  value = static_cast<int32_t>(parsed);
  return true;
}

inline bool parseF32(const String& token, float& value) {
  if (token.length() == 0) {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const float parsed = strtof(token.c_str(), &end);
  if (end == nullptr || *end != '\0' || errno != 0) {
    return false;
  }
  value = parsed;
  return true;
}

inline int splitTokens(const String& line, String* tokens, int maxTokens) {
  if (tokens == nullptr || maxTokens <= 0) {
    return 0;
  }

  int count = 0;
  int start = 0;
  const int n = static_cast<int>(line.length());
  while (start < n && count < maxTokens) {
    while (start < n && line[start] == ' ') {
      ++start;
    }
    if (start >= n) {
      break;
    }

    int end = start;
    while (end < n && line[end] != ' ') {
      ++end;
    }
    tokens[count++] = line.substring(start, end);
    start = end + 1;
  }

  return count;
}

}  // namespace ex
