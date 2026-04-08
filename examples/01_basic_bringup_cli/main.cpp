#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <driver/gpio.h>
#include <esp_timer.h>
#if __has_include(<esp_cpu.h>)
#include <esp_cpu.h>
#endif
#if !defined(esp_cpu_get_cycle_count)
#define esp_cpu_get_cycle_count() ((uint32_t)esp_cpu_get_ccount())
#endif
#include <soc/gpio_reg.h>
#endif

#include "AT21CS/AT21CS.h"
#include "../common/At21Example.h"
#include "../common/BusDiag.h"
#include "../common/BoardConfig.h"
#include "../common/LoadCellMap.h"

AT21CS::Driver gDevice;
bool gVerbose = false;

const char* goodIfZeroColor(uint32_t value) {
  return (value == 0U) ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

const char* goodIfNonZeroColor(uint32_t value) {
  return (value > 0U) ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW;
}

const char* onOffColor(bool enabled) {
  return enabled ? LOG_COLOR_GREEN : LOG_COLOR_RESET;
}

const char* skipCountColor(uint32_t value) {
  return (value > 0U) ? LOG_COLOR_YELLOW : LOG_COLOR_RESET;
}

const char* successRateColor(float pct) {
  if (pct >= 99.9f) return LOG_COLOR_GREEN;
  if (pct >= 80.0f) return LOG_COLOR_YELLOW;
  return LOG_COLOR_RED;
}

void printBytes(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    Serial.printf("%02X ", data[i]);
  }
  Serial.println();
}

// ---------------------------------------------------------------------------
// Hex dump (MB85RC-style: address column + 16 hex bytes + |ASCII|)
// ---------------------------------------------------------------------------
void printHexDump(uint8_t startAddr, const uint8_t* data, size_t len) {
  for (size_t offset = 0; offset < len;) {
    const uint8_t lineAddr = static_cast<uint8_t>((startAddr + offset) & 0x7F);
    size_t lineLen = len - offset;
    if (lineLen > 16) lineLen = 16;
    const size_t untilWrap = static_cast<size_t>(AT21CS::cmd::EEPROM_SIZE - lineAddr);
    if (lineLen > untilWrap) lineLen = untilWrap;

    Serial.printf("  %02X: ", lineAddr);
    for (size_t i = 0; i < lineLen; ++i) {
      Serial.printf("%02X ", data[offset + i]);
    }
    for (size_t i = lineLen; i < 16; ++i) {
      Serial.print("   ");
    }
    Serial.print(" |");
    for (size_t i = 0; i < lineLen; ++i) {
      const char c = static_cast<char>(data[offset + i]);
      Serial.print((c >= 0x20 && c < 0x7F) ? c : '.');
    }
    Serial.println("|");
    offset += lineLen;
  }
}

// ---------------------------------------------------------------------------
// Escaped text view (MB85RC-style)
// ---------------------------------------------------------------------------
void printEscapedByte(uint8_t value) {
  switch (value) {
    case '\\': Serial.print("\\\\"); return;
    case '"':  Serial.print("\\\""); return;
    case '\r': Serial.print("\\r");  return;
    case '\n': Serial.print("\\n");  return;
    case '\t': Serial.print("\\t");  return;
    case '\0': Serial.print("\\0");  return;
    default: break;
  }
  if (value >= 0x20U && value <= 0x7EU) {
    Serial.write(static_cast<char>(value));
  } else {
    Serial.printf("\\x%02X", value);
  }
}

// ---------------------------------------------------------------------------
// CRC-32 (IEEE 802.3) over a memory region
// ---------------------------------------------------------------------------
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint32_t>(data[i]);
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      if ((crc & 1U) != 0U) {
        crc = (crc >> 1) ^ 0xEDB88320UL;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

// ---------------------------------------------------------------------------
// Stress stats (MB85RC-style summary block)
// ---------------------------------------------------------------------------
struct StressStats {
  bool active = false;
  uint32_t startMs = 0;
  uint32_t endMs = 0;
  int target = 0;
  int attempts = 0;
  int success = 0;
  uint32_t errors = 0;
  AT21CS::Status lastError = AT21CS::Status::Ok();
};

StressStats gStressStats;

void resetStressStats(int target) {
  gStressStats.active = true;
  gStressStats.startMs = millis();
  gStressStats.endMs = 0;
  gStressStats.target = target;
  gStressStats.attempts = 0;
  gStressStats.success = 0;
  gStressStats.errors = 0;
  gStressStats.lastError = AT21CS::Status::Ok();
}

void finishStressStats() {
  gStressStats.active = false;
  gStressStats.endMs = millis();
  const uint32_t durationMs = gStressStats.endMs - gStressStats.startMs;

  Serial.println("=== Stress Summary ===");
  Serial.printf("  Target: %d\n", gStressStats.target);
  Serial.printf("  Attempts: %d\n", gStressStats.attempts);
  Serial.printf("  Success: %s%d%s\n",
                goodIfNonZeroColor(static_cast<uint32_t>(gStressStats.success)),
                gStressStats.success,
                LOG_COLOR_RESET);
  Serial.printf("  Errors: %s%lu%s\n",
                goodIfZeroColor(gStressStats.errors),
                static_cast<unsigned long>(gStressStats.errors),
                LOG_COLOR_RESET);
  Serial.printf("  Duration: %lu ms\n", static_cast<unsigned long>(durationMs));
  if (durationMs > 0) {
    const float rate = 1000.0f * static_cast<float>(gStressStats.attempts) /
                       static_cast<float>(durationMs);
    Serial.printf("  Rate: %.2f ops/s\n", rate);
  }
  if (!gStressStats.lastError.ok()) {
    Serial.printf("  Last error: %s\n", ex::errToStr(gStressStats.lastError.code));
    if (gStressStats.lastError.msg && gStressStats.lastError.msg[0]) {
      Serial.printf("  Message: %s\n", gStressStats.lastError.msg);
    }
  }
}

// ---------------------------------------------------------------------------
// Write-verify stress (write byte -> waitReady -> read back -> compare)
// ---------------------------------------------------------------------------
void runWriteVerifyStress(int count) {
  resetStressStats(count);
  const uint32_t succBefore = gDevice.totalSuccess();
  const uint32_t failBefore = gDevice.totalFailures();

  for (int i = 0; i < count; ++i) {
    gStressStats.attempts++;
    const uint8_t addr = static_cast<uint8_t>(i % AT21CS::cmd::EEPROM_SIZE);
    const uint8_t pattern = static_cast<uint8_t>(i & 0xFF);

    AT21CS::Status st = gDevice.writeEepromByte(addr, pattern);
    if (!st.ok()) {
      gStressStats.errors++;
      gStressStats.lastError = st;
      if (gVerbose) {
        Serial.printf("  [%d] write 0x%02X@0x%02X failed: %s\n", i, pattern, addr, ex::errToStr(st.code));
      }
      continue;
    }

    st = gDevice.waitReady(10);
    if (!st.ok()) {
      gStressStats.errors++;
      gStressStats.lastError = st;
      if (gVerbose) {
        Serial.printf("  [%d] waitReady failed: %s\n", i, ex::errToStr(st.code));
      }
      continue;
    }

    uint8_t readBack = 0;
    st = gDevice.readEeprom(addr, &readBack, 1);
    if (!st.ok()) {
      gStressStats.errors++;
      gStressStats.lastError = st;
      if (gVerbose) {
        Serial.printf("  [%d] read 0x%02X failed: %s\n", i, addr, ex::errToStr(st.code));
      }
      continue;
    }

    if (readBack != pattern) {
      gStressStats.errors++;
      gStressStats.lastError = AT21CS::Status::Error(AT21CS::Err::IO_ERROR, "data mismatch");
      if (gVerbose) {
        Serial.printf("  [%d] mismatch @ 0x%02X: wrote 0x%02X read 0x%02X\n", i, addr, pattern, readBack);
      }
      continue;
    }

    gStressStats.success++;
    gDevice.tick(millis());
  }

  const uint32_t successDelta = gDevice.totalSuccess() - succBefore;
  const uint32_t failDelta = gDevice.totalFailures() - failBefore;

  finishStressStats();
  Serial.printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
                goodIfNonZeroColor(successDelta),
                static_cast<unsigned long>(successDelta),
                LOG_COLOR_RESET,
                goodIfZeroColor(failDelta),
                static_cast<unsigned long>(failDelta),
                LOG_COLOR_RESET);
}

// ---------------------------------------------------------------------------
// Speed test - per-operation timing (min/max/avg µs)
// ---------------------------------------------------------------------------
struct OpTiming {
  const char* name;
  uint32_t minUs;
  uint32_t maxUs;
  uint64_t totalUs;
  uint32_t count;
  uint32_t errors;
};

void runSpeedTest(int iterations) {
  OpTiming ops[] = {
    {"probe",         UINT32_MAX, 0, 0, 0, 0},
    {"readCurrent",   UINT32_MAX, 0, 0, 0, 0},
    {"readEeprom(1)", UINT32_MAX, 0, 0, 0, 0},
    {"readEeprom(8)", UINT32_MAX, 0, 0, 0, 0},
    {"writeByte+wait",UINT32_MAX, 0, 0, 0, 0},
    {"readMfgId",     UINT32_MAX, 0, 0, 0, 0},
    {"readSerial",    UINT32_MAX, 0, 0, 0, 0},
    {"waitReady",     UINT32_MAX, 0, 0, 0, 0},
  };
  const int opCount = static_cast<int>(sizeof(ops) / sizeof(ops[0]));

  Serial.printf("=== Speed Test (%d iterations per operation) ===\n", iterations);

  for (int iter = 0; iter < iterations; ++iter) {
    for (int op = 0; op < opCount; ++op) {
      const uint32_t t0 = micros();
      AT21CS::Status st = AT21CS::Status::Ok();

      switch (op) {
        case 0:
          st = gDevice.probe();
          break;
        case 1: {
          uint8_t val = 0;
          st = gDevice.readCurrentAddress(val);
          break;
        }
        case 2: {
          uint8_t buf[1];
          st = gDevice.readEeprom(0x00, buf, 1);
          break;
        }
        case 3: {
          uint8_t buf[8];
          st = gDevice.readEeprom(0x00, buf, 8);
          break;
        }
        case 4: {
          st = gDevice.writeEepromByte(0x7F, static_cast<uint8_t>(iter & 0xFF));
          if (st.ok()) {
            st = gDevice.waitReady(10);
          }
          break;
        }
        case 5: {
          uint32_t id = 0;
          st = gDevice.readManufacturerId(id);
          break;
        }
        case 6: {
          AT21CS::SerialNumberInfo sn = {};
          st = gDevice.readSerialNumber(sn);
          break;
        }
        case 7:
          st = gDevice.waitReady(10);
          break;
        default:
          break;
      }

      const uint32_t elapsed = micros() - t0;

      if (st.ok()) {
        if (elapsed < ops[op].minUs) ops[op].minUs = elapsed;
        if (elapsed > ops[op].maxUs) ops[op].maxUs = elapsed;
        ops[op].totalUs += elapsed;
        ops[op].count++;
      } else {
        ops[op].errors++;
      }
    }
    gDevice.tick(millis());
  }

  Serial.printf("  %-18s %8s %8s %8s %6s %6s\n", "Operation", "Min(us)", "Max(us)", "Avg(us)", "OK", "Err");
  Serial.printf("  %-18s %8s %8s %8s %6s %6s\n", "------------------", "--------", "--------", "--------", "------", "------");
  for (int i = 0; i < opCount; ++i) {
    const uint32_t avgUs = (ops[i].count > 0) ? static_cast<uint32_t>(ops[i].totalUs / ops[i].count) : 0;
    Serial.printf("  %-18s %s%8lu%s %s%8lu%s %s%8lu%s %s%6lu%s %s%6lu%s\n",
                  ops[i].name,
                  LOG_COLOR_GREEN,
                  ops[i].count > 0 ? static_cast<unsigned long>(ops[i].minUs) : 0UL,
                  LOG_COLOR_RESET,
                  LOG_COLOR_YELLOW,
                  static_cast<unsigned long>(ops[i].maxUs),
                  LOG_COLOR_RESET,
                  LOG_COLOR_CYAN,
                  static_cast<unsigned long>(avgUs),
                  LOG_COLOR_RESET,
                  goodIfNonZeroColor(ops[i].count),
                  static_cast<unsigned long>(ops[i].count),
                  LOG_COLOR_RESET,
                  goodIfZeroColor(ops[i].errors),
                  static_cast<unsigned long>(ops[i].errors),
                  LOG_COLOR_RESET);
  }
}

const char* sourceToStr(lcmap::CalibrationSource source) {
  switch (source) {
    case lcmap::CalibrationSource::MASTER:
      return "MASTER";
    case lcmap::CalibrationSource::MIRROR:
      return "MIRROR";
    default:
      return "NONE";
  }
}

void printLoadCellLayout() {
  Serial.println("LoadCellMap layout:");
  Serial.printf("  Security identity: addr=0x%02X size=%u\n", lcmap::SECURITY_IDENTITY_ADDR,
                static_cast<unsigned>(sizeof(lcmap::SecurityIdentityV1)));
  Serial.printf("  Calibration master: addr=0x%02X size=%u\n", lcmap::CALIBRATION_MASTER_ADDR,
                static_cast<unsigned>(sizeof(lcmap::CalibrationBlockV1)));
  Serial.printf("  Calibration mirror: addr=0x%02X size=%u\n", lcmap::CALIBRATION_MIRROR_ADDR,
                static_cast<unsigned>(sizeof(lcmap::CalibrationBlockV1)));
  Serial.printf("  Runtime block:      addr=0x%02X size=%u\n", lcmap::RUNTIME_ADDR,
                static_cast<unsigned>(sizeof(lcmap::RuntimeBlockV1)));
  Serial.printf("  Counter block:      addr=0x%02X size=%u\n", lcmap::COUNTERS_ADDR,
                static_cast<unsigned>(sizeof(lcmap::CounterBlockV1)));

  Serial.println("Key field addresses:");
  Serial.printf("  capacityGrams      @ 0x%02X\n", lcmap::field::CAPACITY_GRAMS);
  Serial.printf("  zeroBalanceRaw     @ 0x%02X\n", lcmap::field::ZERO_BALANCE_RAW);
  Serial.printf("  spanRawAtCapacity  @ 0x%02X\n", lcmap::field::SPAN_RAW_AT_CAPACITY);
  Serial.printf("  installTareRaw     @ 0x%02X\n", lcmap::field::INSTALL_TARE_RAW);
  Serial.printf("  overloadCount      @ 0x%02X\n", lcmap::field::OVERLOAD_COUNT);
}

void printLoadCellRecords() {
  lcmap::SecurityIdentityV1 identity = {};
  bool identityValid = false;
  ex::printStatus(lcmap::readSecurityIdentity(gDevice, identity, identityValid));
  Serial.printf(
      "securityIdentity valid=%s hwRev=%u modelId=%u moduleSerial=%lu batch=%u flags=0x%04X\n",
      identityValid ? "true" : "false", static_cast<unsigned>(identity.hwRevision),
      static_cast<unsigned>(identity.modelId), static_cast<unsigned long>(identity.moduleSerial),
      static_cast<unsigned>(identity.batchCode), static_cast<unsigned>(identity.flags));

  lcmap::CalibrationBlockV1 calibration = {};
  lcmap::CalibrationSource source = lcmap::CalibrationSource::NONE;
  bool calibrationValid = false;
  ex::printStatus(lcmap::readCalibrationBest(gDevice, calibration, source, calibrationValid));
  Serial.printf(
      "calibration valid=%s source=%s capacityGrams=%lu zeroRaw=%ld spanRaw=%ld sens=%ld "
      "tempCoeff=%d linearity=%d flags=0x%04X\n",
      calibrationValid ? "true" : "false", sourceToStr(source),
      static_cast<unsigned long>(calibration.capacityGrams),
      static_cast<long>(calibration.zeroBalanceRaw),
      static_cast<long>(calibration.spanRawAtCapacity),
      static_cast<long>(calibration.sensitivityNvPerV), static_cast<int>(calibration.tempCoeffPpmPerC),
      static_cast<int>(calibration.linearityPpm), static_cast<unsigned>(calibration.flags));

  lcmap::RuntimeBlockV1 runtime = {};
  bool runtimeValid = false;
  ex::printStatus(lcmap::readRuntime(gDevice, runtime, runtimeValid));
  Serial.printf(
      "runtime valid=%s seq=%lu tare=%ld zeroTrim=%ld spanTrimPpm=%ld filter=%u diag=%u "
      "flags=0x%04X\n",
      runtimeValid ? "true" : "false", static_cast<unsigned long>(runtime.seq),
      static_cast<long>(runtime.installTareRaw), static_cast<long>(runtime.userZeroTrimRaw),
      static_cast<long>(runtime.userSpanTrimPpm), static_cast<unsigned>(runtime.filterProfile),
      static_cast<unsigned>(runtime.diagnosticsMode), static_cast<unsigned>(runtime.flags));

  lcmap::CounterBlockV1 counters = {};
  bool countersValid = false;
  ex::printStatus(lcmap::readCounters(gDevice, counters, countersValid));
  Serial.printf(
      "counters valid=%s seq=%lu overload=%lu overTemp=%lu powerCycles=%lu saturation=%lu "
      "flags=0x%04X\n",
      countersValid ? "true" : "false", static_cast<unsigned long>(counters.seq),
      static_cast<unsigned long>(counters.overloadCount),
      static_cast<unsigned long>(counters.overTempCount),
      static_cast<unsigned long>(counters.powerCycleCount),
      static_cast<unsigned long>(counters.saturationCount), static_cast<unsigned>(counters.flags));
}

void writeLoadCellDemoData() {
  lcmap::SecurityIdentityV1 identity = {};
  identity.hwRevision = 1;
  identity.modelId = 1101;
  identity.moduleSerial = 1000001;
  identity.batchCode = 2602;
  identity.flags = 0x0001;
  ex::printStatus(lcmap::writeSecurityIdentity(gDevice, identity));

  lcmap::CalibrationBlockV1 calibration = {};
  calibration.flags = 0x0001;
  calibration.capacityGrams = 50000;
  calibration.zeroBalanceRaw = -17320;
  calibration.spanRawAtCapacity = 947112;
  calibration.sensitivityNvPerV = 2000000;
  calibration.tempCoeffPpmPerC = -35;
  calibration.linearityPpm = 120;
  ex::printStatus(lcmap::writeCalibrationBoth(gDevice, calibration));

  lcmap::RuntimeBlockV1 runtime = {};
  runtime.flags = 0x0001;
  runtime.seq = 1;
  runtime.installTareRaw = -120;
  runtime.userZeroTrimRaw = 0;
  runtime.userSpanTrimPpm = 0;
  runtime.filterProfile = 2;
  runtime.diagnosticsMode = 0;
  ex::printStatus(lcmap::writeRuntime(gDevice, runtime));

  lcmap::CounterBlockV1 counters = {};
  counters.flags = 0x0001;
  counters.seq = 1;
  counters.overloadCount = 0;
  counters.overTempCount = 0;
  counters.powerCycleCount = 1;
  counters.saturationCount = 0;
  ex::printStatus(lcmap::writeCounters(gDevice, counters));
}

void runStressMix(int count) {
  struct OpStats {
    const char* name;
    uint32_t ok;
    uint32_t fail;
  };
  OpStats ops[] = {
      {"probe", 0, 0},
      {"isPresent", 0, 0},
      {"readCurrent", 0, 0},
      {"readId", 0, 0},
      {"readSerial", 0, 0},
      {"waitReady", 0, 0},
      {"isLocked", 0, 0},
      {"isHighSpeed", 0, 0},
  };
  const int opCount = static_cast<int>(sizeof(ops) / sizeof(ops[0]));

  const uint32_t succBefore = gDevice.totalSuccess();
  const uint32_t failBefore = gDevice.totalFailures();
  const uint32_t startMs = millis();

  for (int i = 0; i < count; ++i) {
    const int op = i % opCount;
    AT21CS::Status st = AT21CS::Status::Ok();

    switch (op) {
      case 0:
        st = gDevice.probe();
        break;
      case 1: {
        bool present = false;
        st = gDevice.isPresent(present);
        if (st.ok() && !present) {
          st = AT21CS::Status::Error(AT21CS::Err::NOT_PRESENT, "Device not present");
        }
        break;
      }
      case 2: {
        uint8_t value = 0;
        st = gDevice.readCurrentAddress(value);
        break;
      }
      case 3: {
        uint32_t id = 0;
        st = gDevice.readManufacturerId(id);
        if (st.ok() && id == 0) {
          st = AT21CS::Status::Error(AT21CS::Err::IO_ERROR, "manufacturer ID is zero");
        }
        break;
      }
      case 4: {
        AT21CS::SerialNumberInfo sn = {};
        st = gDevice.readSerialNumber(sn);
        if (st.ok() && (!sn.crcOk || !sn.productIdOk)) {
          st = AT21CS::Status::Error(AT21CS::Err::CRC_MISMATCH, "serial validation failed");
        }
        break;
      }
      case 5:
        st = gDevice.waitReady(8);
        break;
      case 6: {
        bool locked = false;
        st = gDevice.isSecurityLocked(locked);
        break;
      }
      case 7: {
        bool high = false;
        st = gDevice.isHighSpeed(high);
        break;
      }
      default:
        break;
    }

    if (st.ok()) {
      ops[op].ok++;
    } else {
      ops[op].fail++;
      if (gVerbose) {
        Serial.printf("  [%d] %s failed: %s\n", i, ops[op].name, ex::errToStr(st.code));
      }
    }
    gDevice.tick(millis());
  }

  const uint32_t elapsed = millis() - startMs;
  uint32_t totalOk = 0;
  uint32_t totalFail = 0;
  for (int i = 0; i < opCount; ++i) {
    totalOk += ops[i].ok;
    totalFail += ops[i].fail;
  }

  Serial.println("=== stress_mix summary ===");
  Serial.printf("  Target: %d\n", count);
  Serial.printf("  Attempts: %d\n", count);
  Serial.printf("  Success: %s%lu%s\n",
                goodIfNonZeroColor(totalOk),
                static_cast<unsigned long>(totalOk),
                LOG_COLOR_RESET);
  Serial.printf("  Errors: %s%lu%s\n",
                goodIfZeroColor(totalFail),
                static_cast<unsigned long>(totalFail),
                LOG_COLOR_RESET);
  Serial.printf("  Duration: %lu ms\n", static_cast<unsigned long>(elapsed));
  if (elapsed > 0) {
    Serial.printf("  Rate: %.2f ops/s\n", (1000.0f * static_cast<float>(count)) / elapsed);
  }
  Serial.println("  Per-operation breakdown:");
  for (int i = 0; i < opCount; ++i) {
    Serial.printf("    %-11s %sok=%lu%s %sfail=%lu%s\n",
                  ops[i].name,
                  goodIfNonZeroColor(ops[i].ok),
                  static_cast<unsigned long>(ops[i].ok),
                  LOG_COLOR_RESET,
                  goodIfZeroColor(ops[i].fail),
                  static_cast<unsigned long>(ops[i].fail),
                  LOG_COLOR_RESET);
  }
  const uint32_t successDelta = gDevice.totalSuccess() - succBefore;
  const uint32_t failDelta = gDevice.totalFailures() - failBefore;
  Serial.printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
                goodIfNonZeroColor(successDelta),
                static_cast<unsigned long>(successDelta),
                LOG_COLOR_RESET,
                goodIfZeroColor(failDelta),
                static_cast<unsigned long>(failDelta),
                LOG_COLOR_RESET);
}

void runSelfTest() {
  struct Result {
    uint32_t pass = 0;
    uint32_t fail = 0;
    uint32_t skip = 0;
  } result;

  enum class SelftestOutcome : uint8_t { PASS, FAIL, SKIP };
  auto report = [&](const char* name, SelftestOutcome outcome, const char* note) {
    const bool ok = (outcome == SelftestOutcome::PASS);
    const bool skip = (outcome == SelftestOutcome::SKIP);
    const char* color = skip ? LOG_COLOR_YELLOW : LOG_COLOR_RESULT(ok);
    const char* tag = skip ? "SKIP" : (ok ? "PASS" : "FAIL");
    Serial.printf("  [%s%s%s] %s", color, tag, LOG_COLOR_RESET, name);
    if (note && note[0]) {
      Serial.printf(" - %s", note);
    }
    Serial.println();
    if (skip) {
      result.skip++;
    } else if (ok) {
      result.pass++;
    } else {
      result.fail++;
    }
  };
  auto reportCheck = [&](const char* name, bool ok, const char* note) {
    report(name, ok ? SelftestOutcome::PASS : SelftestOutcome::FAIL, note);
  };
  auto reportSkip = [&](const char* name, const char* note) {
    report(name, SelftestOutcome::SKIP, note);
  };

  Serial.println("=== AT21CS selftest (safe commands) ===");

  const uint32_t succBefore = gDevice.totalSuccess();
  const uint32_t failBefore = gDevice.totalFailures();
  const uint8_t consBefore = gDevice.consecutiveFailures();

  AT21CS::Status st = gDevice.probe();
  if (st.code == AT21CS::Err::NOT_INITIALIZED) {
    reportSkip("probe responds", "driver not initialized");
    reportSkip("remaining checks", "selftest aborted");
    Serial.printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                  goodIfNonZeroColor(result.pass), static_cast<unsigned long>(result.pass), LOG_COLOR_RESET,
                  goodIfZeroColor(result.fail), static_cast<unsigned long>(result.fail), LOG_COLOR_RESET,
                  skipCountColor(result.skip), static_cast<unsigned long>(result.skip), LOG_COLOR_RESET);
    return;
  }
  reportCheck("probe responds", st.ok(), st.ok() ? "" : ex::errToStr(st.code));
  const bool probeNoTrack = gDevice.totalSuccess() == succBefore &&
                            gDevice.totalFailures() == failBefore &&
                            gDevice.consecutiveFailures() == consBefore;
  reportCheck("probe no-health-side-effects", probeNoTrack, "");

  bool present = false;
  st = gDevice.isPresent(present);
  reportCheck("isPresent", st.ok(), st.ok() ? "" : ex::errToStr(st.code));
  reportCheck("present=true", st.ok() && present, "");

  AT21CS::PartType detected = AT21CS::PartType::UNKNOWN;
  st = gDevice.detectPart(detected);
  reportCheck("detectPart", st.ok(), st.ok() ? "" : ex::errToStr(st.code));
  const bool partKnown = st.ok() &&
                         (detected == AT21CS::PartType::AT21CS01 ||
                          detected == AT21CS::PartType::AT21CS11);
  reportCheck("detectPart known", partKnown, "");
  reportCheck("detectPart matches runtime", st.ok() && detected == gDevice.detectedPart(), "");

  uint8_t cur = 0;
  st = gDevice.readCurrentAddress(cur);
  reportCheck("readCurrentAddress", st.ok(), st.ok() ? "" : ex::errToStr(st.code));

  uint32_t id = 0;
  st = gDevice.readManufacturerId(id);
  reportCheck("readManufacturerId", st.ok(), st.ok() ? "" : ex::errToStr(st.code));
  reportCheck("manufacturerId non-zero", st.ok() && id != 0, "");
  const bool manufacturerKnown =
      st.ok() && (id == AT21CS::cmd::MANUFACTURER_ID_AT21CS01 ||
                  id == AT21CS::cmd::MANUFACTURER_ID_AT21CS11);
  reportCheck("manufacturerId known", manufacturerKnown, "");

  AT21CS::SerialNumberInfo sn = {};
  st = gDevice.readSerialNumber(sn);
  reportCheck("readSerialNumber", st.ok(), st.ok() ? "" : ex::errToStr(st.code));
  reportCheck("serial CRC valid", st.ok() && sn.crcOk, "");
  reportCheck("serial product ID valid", st.ok() && sn.productIdOk, "");

  uint8_t eepromBytes[4] = {0};
  st = gDevice.readEeprom(0x00, eepromBytes, sizeof(eepromBytes));
  reportCheck("readEeprom[0..3]", st.ok(), st.ok() ? "" : ex::errToStr(st.code));

  uint8_t securityBytes[4] = {0};
  st = gDevice.readSecurity(0x00, securityBytes, sizeof(securityBytes));
  reportCheck("readSecurity[0..3]", st.ok(), st.ok() ? "" : ex::errToStr(st.code));

  for (uint8_t zone = 0; zone < AT21CS::cmd::ROM_ZONE_REGISTER_COUNT; ++zone) {
    char checkName[40];
    snprintf(checkName, sizeof(checkName), "readRomZoneRegister[%u]", zone);
    uint8_t zoneReg = 0;
    st = gDevice.readRomZoneRegister(zone, zoneReg);
    reportCheck(checkName, st.ok(), st.ok() ? "" : ex::errToStr(st.code));

    snprintf(checkName, sizeof(checkName), "isZoneRom[%u]", zone);
    bool isRom = false;
    st = gDevice.isZoneRom(zone, isRom);
    reportCheck(checkName, st.ok(), st.ok() ? "" : ex::errToStr(st.code));
  }

  bool frozen = false;
  st = gDevice.areRomZonesFrozen(frozen);
  reportCheck("areRomZonesFrozen", st.ok(), st.ok() ? "" : ex::errToStr(st.code));

  const uint8_t crcVector[] = {0x01, 0x02, 0x03};
  const uint8_t crc = AT21CS::Driver::crc8_31(crcVector, sizeof(crcVector));
  reportCheck("crc8_31 known vector", crc == 0xD8, "");

  st = gDevice.waitReady(8);
  reportCheck("waitReady", st.ok(), st.ok() ? "" : ex::errToStr(st.code));

  bool locked = false;
  st = gDevice.isSecurityLocked(locked);
  reportCheck("isSecurityLocked", st.ok(), st.ok() ? "" : ex::errToStr(st.code));

  st = gDevice.setHighSpeed();
  reportCheck("setHighSpeed", st.ok(), st.ok() ? "" : ex::errToStr(st.code));

  bool high = false;
  st = gDevice.isHighSpeed(high);
  reportCheck("isHighSpeed", st.ok(), st.ok() ? "" : ex::errToStr(st.code));
  reportCheck("highSpeed enabled", st.ok() && high, "");

  if (gDevice.detectedPart() == AT21CS::PartType::AT21CS01) {
    st = gDevice.setStandardSpeed();
    reportCheck("setStandardSpeed", st.ok(), st.ok() ? "" : ex::errToStr(st.code));
    bool std = false;
    st = gDevice.isStandardSpeed(std);
    reportCheck("isStandardSpeed", st.ok() && std, st.ok() ? "" : ex::errToStr(st.code));
    st = gDevice.setHighSpeed();
    reportCheck("restore setHighSpeed", st.ok(), st.ok() ? "" : ex::errToStr(st.code));
  } else {
    reportSkip("standard-speed checks", "AT21CS11");
  }

  st = gDevice.resetAndDiscover();
  reportCheck("resetAndDiscover", st.ok(), st.ok() ? "" : ex::errToStr(st.code));
  reportCheck("detectedPart after reset", gDevice.detectedPart() != AT21CS::PartType::UNKNOWN, "");

  st = gDevice.recover();
  reportCheck("recover", st.ok(), st.ok() ? "" : ex::errToStr(st.code));
  reportCheck("isOnline", gDevice.isOnline(), "");

  Serial.printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                goodIfNonZeroColor(result.pass), static_cast<unsigned long>(result.pass), LOG_COLOR_RESET,
                goodIfZeroColor(result.fail), static_cast<unsigned long>(result.fail), LOG_COLOR_RESET,
                skipCountColor(result.skip), static_cast<unsigned long>(result.skip), LOG_COLOR_RESET);
}

void printHelp() {
  auto helpSection = [](const char* title) {
    Serial.printf("\n%s[%s]%s\n", LOG_COLOR_GREEN, title, LOG_COLOR_RESET);
  };
  auto helpItem = [](const char* cmd, const char* desc) {
    Serial.printf("  %s%-32s%s - %s\n", LOG_COLOR_CYAN, cmd, LOG_COLOR_RESET, desc);
  };

  Serial.println();
  Serial.printf("%s=== AT21CS11 CLI Help ===%s\n", LOG_COLOR_CYAN, LOG_COLOR_RESET);

  helpSection("Common");
  helpItem("help / ?", "Show this help");
  helpItem("version / ver", "Print firmware and library version info");
  helpItem("init [addr]", "Re-run begin() with address (default: 0)");
  helpItem("wire", "Low-level GPIO wire test");
  helpItem("rawtx [byte]", "Raw bit-bang: reset+discovery+send byte");
  helpItem("timing", "Measure actual delayMicroseconds accuracy");
  helpItem("addrscan", "Try all 8 A2:A0 addresses");
  helpItem("scan", "Bus scan helper");
  helpItem("probe", "Probe and detect device");
  helpItem("recover", "Manual recovery");
  helpItem("drv / health", "Print health counters and state");
  helpItem("read", "Read current address byte");
  helpItem("cfg / settings", "Show active config snapshot");
  helpItem("verbose [0|1]", "Set or get verbose mode");

  helpSection("Device");
  helpItem("present", "Run presence check");
  helpItem("reset", "Reset and discovery");
  helpItem("high", "Set high speed");
  helpItem("std", "Set standard speed (AT21CS01 only)");
  helpItem("is_high", "Check high speed mode");
  helpItem("is_std", "Check standard speed mode");
  helpItem("part", "Print detected part");
  helpItem("detect", "Run detectPart()");
  helpItem("id", "Read manufacturer ID");
  helpItem("crc8 <b0> [b1..bN]", "Compute CRC8-0x31 over bytes");
  helpItem("current", "Current address read");
  helpItem("wait [timeout_ms]", "waitReady polling");

  helpSection("EEPROM And Security");
  helpItem("e_read <addr> <len>", "EEPROM read (raw hex bytes)");
  helpItem("e_dump <addr> <len>", "EEPROM hex+ASCII dump");
  helpItem("e_text <addr> <len>", "EEPROM escaped text view");
  helpItem("e_write <addr> <value>", "EEPROM byte write");
  helpItem("e_page <addr> <v0> [..v7]", "EEPROM page write (up to 8 bytes)");
  helpItem("e_fill <addr> <value> <len>", "Fill EEPROM region with a byte");
  helpItem("e_verify <addr> <v0> [..vN]", "Verify EEPROM matches expected bytes");
  helpItem("e_crc <addr> <len>", "CRC-32 over EEPROM region");
  helpItem("e_strings [addr] [len] [min]", "Scan printable ASCII strings in EEPROM");
  helpItem("s_read <addr> <len>", "Security read");
  helpItem("s_dump <addr> <len>", "Security hex+ASCII dump");
  helpItem("s_write <addr> <value>", "Security user byte write");
  helpItem("s_page <addr> <v0> [..v7]", "Security user page write");
  helpItem("s_locked", "Check security lock");
  helpItem("s_lock", "Lock security register");
  helpItem("serial", "Read serial and CRC");
  helpItem("read_zone <0..3>", "Read ROM zone register");
  helpItem("is_rom <0..3>", "Check zone ROM/EEPROM state");
  helpItem("set_rom <0..3>", "Set zone to ROM");
  helpItem("frozen", "Check if ROM zones are frozen");
  helpItem("freeze", "Freeze ROM zone configuration");

  helpSection("Diagnostics");
  helpItem("selftest", "Safe command self-test report");
  helpItem("stress [N]", "Repeat probe N times (read-only)");
  helpItem("stress_mix [N]", "Mixed safe operations (read-only)");
  helpItem("stress_rw [N]", "Write-verify stress (write+read+compare)");
  helpItem("speed [N]", "Per-operation speed test (min/max/avg µs)");

  helpSection("Load Cell Map");
  helpItem("lc_layout", "Print full load-cell map layout");
  helpItem("lc_write_demo", "Write demo LoadCellMap records");
  helpItem("lc_read", "Read and validate LoadCellMap records");
  helpItem("lc_set_tare <signed_raw>", "Update runtime tare field");
  helpItem("lc_inc_overload [count]", "Increment overload counter");
  helpItem("lc_fwrite <addr> <float>", "Write float32 via map helper");
  helpItem("lc_fread <addr>", "Read float32 via map helper");
}

void printVersionInfo() {
  Serial.println("=== Version Info ===");
  Serial.printf("  Example firmware build: %s %s\n", __DATE__, __TIME__);
  Serial.printf("  AT21CS library version: %s\n", AT21CS::VERSION);
  Serial.printf("  AT21CS library full: %s\n", AT21CS::VERSION_FULL);
  Serial.printf("  AT21CS library build: %s\n", AT21CS::BUILD_TIMESTAMP);
  Serial.printf("  AT21CS library commit: %s (%s)\n", AT21CS::GIT_COMMIT, AT21CS::GIT_STATUS);
}

void setup() {
  board::initSerial();
  delay(200);

  Serial.println("\n=== 01_basic_bringup_cli ===");
  Serial.printf("SI/O=%d presencePin=%d A2:A0=%u\n", board::SIO_PRIMARY,
                board::PRESENCE_PRIMARY, board::ADDRESS_BITS_PRIMARY);

  AT21CS::Config cfg;
  cfg.sioPin = board::SIO_PRIMARY;
  cfg.presencePin = board::PRESENCE_PRIMARY;
  cfg.addressBits = board::ADDRESS_BITS_PRIMARY;

  const AT21CS::Status st = gDevice.begin(cfg);
  ex::printStatus(st);
  Serial.printf("detectedPart=%s speed=%s\n", ex::partToStr(gDevice.detectedPart()),
                ex::speedToStr(gDevice.speedMode()));

  printHelp();
  Serial.print("> ");
}

void loop() {
  gDevice.tick(millis());

  String line;
  if (!ex::readLine(line)) {
    return;
  }

  String tokens[20];
  const int argc = ex::splitTokens(line, tokens, 20);
  if (argc == 0) {
    Serial.print("> ");
    return;
  }

  if (tokens[0] == "help" || tokens[0] == "?") {
    printHelp();
  } else if (tokens[0] == "init") {
    AT21CS::Config cfg;
    cfg.sioPin = board::SIO_PRIMARY;
    cfg.presencePin = board::PRESENCE_PRIMARY;
    cfg.addressBits = board::ADDRESS_BITS_PRIMARY;
    if (argc >= 2) {
      cfg.addressBits = static_cast<uint8_t>(tokens[1].toInt() & 0x07);
    }
    Serial.printf("Trying A2:A0=%u...\n", cfg.addressBits);
    const AT21CS::Status st = gDevice.begin(cfg);
    ex::printStatus(st);
    Serial.printf("detectedPart=%s speed=%s\n", ex::partToStr(gDevice.detectedPart()),
                  ex::speedToStr(gDevice.speedMode()));
  } else if (tokens[0] == "addrscan") {
    Serial.println("=== Address Scan (A2:A0 = 0..7) ===");
    for (uint8_t addr = 0; addr < 8; ++addr) {
      AT21CS::Config cfg;
      cfg.sioPin = board::SIO_PRIMARY;
      cfg.presencePin = board::PRESENCE_PRIMARY;
      cfg.addressBits = addr;
      const AT21CS::Status st = gDevice.begin(cfg);
      Serial.printf("  A2:A0=%u: %s (code=%d)\n", addr,
                    st.ok() ? "ACK - FOUND" : ex::errToStr(st.code),
                    static_cast<int>(st.code));
      if (st.ok()) {
        Serial.printf("  -> detectedPart=%s speed=%s\n",
                      ex::partToStr(gDevice.detectedPart()),
                      ex::speedToStr(gDevice.speedMode()));
      }
    }
  } else if (tokens[0] == "version" || tokens[0] == "ver") {
    printVersionInfo();
  } else if (tokens[0] == "scan") {
    bus_diag::scan();
  } else if (tokens[0] == "drv") {
    ex::printHealth(gDevice);
  } else if (tokens[0] == "read") {
    uint8_t value = 0;
    ex::printStatus(gDevice.readCurrentAddress(value));
    Serial.printf("read=0x%02X\n", value);
  } else if (tokens[0] == "cfg" || tokens[0] == "settings") {
    Serial.printf("cfg.sioPin=%d cfg.presencePin=%d cfg.addressBits=%u\n",
                  board::SIO_PRIMARY, board::PRESENCE_PRIMARY, board::ADDRESS_BITS_PRIMARY);
    const bool online = gDevice.isOnline();
    const char* stateColor = (gDevice.state() == AT21CS::DriverState::UNINIT)
                                 ? LOG_COLOR_GRAY
                                 : LOG_COLOR_STATE(online, gDevice.consecutiveFailures());
    Serial.printf("state=%s%s%s part=%s speed=%s verbose=%s%s%s\n",
                  stateColor, ex::stateToStr(gDevice.state()), LOG_COLOR_RESET,
                  ex::partToStr(gDevice.detectedPart()),
                  ex::speedToStr(gDevice.speedMode()),
                  onOffColor(gVerbose), gVerbose ? "true" : "false", LOG_COLOR_RESET);
  } else if (tokens[0] == "verbose") {
    if (argc >= 2) {
      gVerbose = (tokens[1].toInt() != 0);
    }
    Serial.printf("verbose=%s%s%s\n", onOffColor(gVerbose), gVerbose ? "true" : "false", LOG_COLOR_RESET);
  } else if (tokens[0] == "stress") {
    int count = 100;
    if (argc >= 2) {
      count = tokens[1].toInt();
      if (count <= 0) count = 100;
    }
    resetStressStats(count);
    const uint32_t succBefore = gDevice.totalSuccess();
    const uint32_t failBefore = gDevice.totalFailures();

    for (int i = 0; i < count; ++i) {
      gStressStats.attempts++;
      const AT21CS::Status st = gDevice.probe();
      if (st.ok()) {
        gStressStats.success++;
      } else {
        gStressStats.errors++;
        gStressStats.lastError = st;
        if (gVerbose) {
          Serial.printf("  [%d] probe failed: %s\n", i, ex::errToStr(st.code));
        }
      }
      gDevice.tick(millis());
    }

    const uint32_t successDelta = gDevice.totalSuccess() - succBefore;
    const uint32_t failDelta = gDevice.totalFailures() - failBefore;
    finishStressStats();
    Serial.printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
                  goodIfNonZeroColor(successDelta),
                  static_cast<unsigned long>(successDelta),
                  LOG_COLOR_RESET,
                  goodIfZeroColor(failDelta),
                  static_cast<unsigned long>(failDelta),
                  LOG_COLOR_RESET);
  } else if (tokens[0] == "stress_mix") {
    int count = 100;
    if (argc >= 2) {
      count = tokens[1].toInt();
      if (count <= 0) count = 100;
    }
    runStressMix(count);
  } else if (tokens[0] == "wire") {
    Serial.println("=== Wire Test ===");
    const gpio_num_t pin = static_cast<gpio_num_t>(board::SIO_PRIMARY);
    // 1. Idle state (pull-up should hold HIGH)
    bool idle = gpio_get_level(pin) != 0;
    Serial.printf("1. Idle:     %s (expect HIGH)\n", idle ? "HIGH" : "LOW");
    // 2. Drive low, verify master can pull line down
    gpio_set_level(pin, 0);
    delayMicroseconds(10);
    bool driven = gpio_get_level(pin) != 0;
    Serial.printf("2. Driven:   %s (expect LOW)\n", driven ? "HIGH" : "LOW");
    // 3. Release and let pull-up restore
    gpio_set_level(pin, 1);
    delayMicroseconds(50);
    bool released = gpio_get_level(pin) != 0;
    Serial.printf("3. Released: %s (expect HIGH)\n", released ? "HIGH" : "LOW");
    if (idle && !driven && released) {
      Serial.println("PASS: GPIO and pull-up verified");
    } else {
      Serial.println("FAIL: check wiring, pull-up, and pin config");
    }
  } else if (tokens[0] == "timing") {
    // Measure actual delay accuracy for critical timing values
    Serial.println("=== Timing Measurement ===");
    volatile uint32_t* setReg;
    volatile uint32_t* clrReg;
    uint32_t mask;
    constexpr int sioPin = board::SIO_PRIMARY;
    if constexpr (sioPin < 32) {
      setReg = reinterpret_cast<volatile uint32_t*>(GPIO_OUT_W1TS_REG);
      clrReg = reinterpret_cast<volatile uint32_t*>(GPIO_OUT_W1TC_REG);
      mask = (1U << sioPin);
    } else {
      setReg = reinterpret_cast<volatile uint32_t*>(GPIO_OUT1_W1TS_REG);
      clrReg = reinterpret_cast<volatile uint32_t*>(GPIO_OUT1_W1TC_REG);
      mask = (1U << (sioPin - 32));
    }
    Serial.println("  -- delayMicroseconds (Arduino) --");
    const uint32_t targets[] = {1, 2, 3, 5, 8, 12, 150};
    for (size_t i = 0; i < sizeof(targets)/sizeof(targets[0]); ++i) {
      portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
      portENTER_CRITICAL(&mux);
      int64_t t0 = esp_timer_get_time();
      *clrReg = mask;
      delayMicroseconds(targets[i]);
      *setReg = mask;
      int64_t t1 = esp_timer_get_time();
      portEXIT_CRITICAL(&mux);
      Serial.printf("    target=%lu actual=%lld us\n",
                    (unsigned long)targets[i], (long long)(t1 - t0));
      delayMicroseconds(200);
    }
    Serial.println("  -- esp_timer busy-wait --");
    for (size_t i = 0; i < sizeof(targets)/sizeof(targets[0]); ++i) {
      portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
      portENTER_CRITICAL(&mux);
      *clrReg = mask;
      int64_t t0 = esp_timer_get_time();
      int64_t deadline = t0 + static_cast<int64_t>(targets[i]);
      while (esp_timer_get_time() < deadline) {}
      *setReg = mask;
      int64_t t1 = esp_timer_get_time();
      portEXIT_CRITICAL(&mux);
      Serial.printf("    target=%lu actual=%lld us\n",
                    (unsigned long)targets[i], (long long)(t1 - t0));
      delayMicroseconds(200);
    }
    Serial.println("  -- CCOUNT cycle counter (driver uses this) --");
    const uint32_t cpuMhz = static_cast<uint32_t>(getCpuFrequencyMhz());
    Serial.printf("    CPU freq: %lu MHz\n", (unsigned long)cpuMhz);
    for (size_t i = 0; i < sizeof(targets)/sizeof(targets[0]); ++i) {
      portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
      portENTER_CRITICAL(&mux);
      *clrReg = mask;
      int64_t t0 = esp_timer_get_time();
      uint32_t cyc = targets[i] * cpuMhz;
      uint32_t start = esp_cpu_get_cycle_count();
      while ((esp_cpu_get_cycle_count() - start) < cyc) {}
      *setReg = mask;
      int64_t t1 = esp_timer_get_time();
      portEXIT_CRITICAL(&mux);
      Serial.printf("    target=%lu actual=%lld us\n",
                    (unsigned long)targets[i], (long long)(t1 - t0));
      delayMicroseconds(200);
    }
  } else if (tokens[0] == "rawtx") {
    // Raw bit-bang diagnostic: bypass driver, do reset+discovery+send byte
    const gpio_num_t pin = static_cast<gpio_num_t>(board::SIO_PRIMARY);
    uint8_t txByte = 0xC1; // manufacturer ID read, addr=0
    if (argc >= 2) {
      txByte = static_cast<uint8_t>(strtoul(tokens[1].c_str(), nullptr, 0));
    }
    Serial.printf("=== Raw TX: 0x%02X (binary: ", txByte);
    for (int8_t b = 7; b >= 0; --b) {
      Serial.print((txByte >> b) & 1);
    }
    Serial.println(") ===");

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

    // 1. Discharge reset (200µs low)
    gpio_set_level(pin, 0);
    delayMicroseconds(200);
    gpio_set_level(pin, 1);
    delayMicroseconds(20);  // t_RRT (min 8µs, generous)

    // 2. Discovery: DRR low 1µs, release, wait, strobe, sample
    portENTER_CRITICAL(&mux);
    gpio_set_level(pin, 0);
    delayMicroseconds(1);   // t_DRR
    gpio_set_level(pin, 1);
    delayMicroseconds(2);   // wait before strobe
    gpio_set_level(pin, 0);
    delayMicroseconds(2);   // t_MSDR strobe
    gpio_set_level(pin, 1);
    delayMicroseconds(1);   // sample delay
    bool present = (gpio_get_level(pin) == 0);
    portEXIT_CRITICAL(&mux);

    Serial.printf("Discovery: %s\n", present ? "PRESENT" : "NOT PRESENT");
    if (!present) {
      Serial.println("Aborting - no device");
    } else {
      // 3. Start condition: hold high for t_HTSS
      delayMicroseconds(200);

      // 4. Send byte MSb first with clear timing margins
      // Using t_LOW0=10µs for '0', t_LOW1=1µs for '1', t_BIT=20µs
      portENTER_CRITICAL(&mux);
      int64_t bitTimes[8];
      for (int8_t bit = 7; bit >= 0; --bit) {
        bool one = ((txByte >> bit) & 1) != 0;
        int64_t bt0 = esp_timer_get_time();
        gpio_set_level(pin, 0);
        if (one) {
          // Logic 1: very short low (~1µs)
          // Inline NOP-based sub-microsecond delay
          for (int k = 0; k < 60; k++) { __asm__ __volatile__("nop"); }
        } else {
          // Logic 0: long low (10µs)
          delayMicroseconds(10);
        }
        gpio_set_level(pin, 1);
        int64_t bt1 = esp_timer_get_time();
        bitTimes[7 - bit] = bt1 - bt0;
        // Pad to 20µs bit frame
        while ((esp_timer_get_time() - bt0) < 20) {}
      }

      // 5. Read ACK bit (9th bit)
      gpio_set_level(pin, 0);
      for (int k = 0; k < 60; k++) { __asm__ __volatile__("nop"); }
      gpio_set_level(pin, 1);
      delayMicroseconds(2);
      bool ackBit = (gpio_get_level(pin) == 0);  // LOW = ACK
      portEXIT_CRITICAL(&mux);

      // Stop
      gpio_set_level(pin, 1);
      delayMicroseconds(200);

      Serial.println("Bit low-pulse durations:");
      for (int i = 0; i < 8; i++) {
        bool one = ((txByte >> (7 - i)) & 1) != 0;
        Serial.printf("  bit%d (%c): ~%lld us\n", i, one ? '1' : '0', (long long)bitTimes[i]);
      }
      Serial.printf("ACK bit: %s\n", ackBit ? "ACK (device responded)" : "NACK (no response)");
    }
  } else if (tokens[0] == "selftest") {
    runSelfTest();
  } else if (tokens[0] == "stress_rw") {
    int count = 20;
    if (argc >= 2) {
      count = tokens[1].toInt();
      if (count <= 0) count = 20;
    }
    runWriteVerifyStress(count);
  } else if (tokens[0] == "speed") {
    int iterations = 10;
    if (argc >= 2) {
      iterations = tokens[1].toInt();
      if (iterations <= 0) iterations = 10;
    }
    runSpeedTest(iterations);
  } else if (tokens[0] == "present") {
    bool present = false;
    ex::printStatus(gDevice.isPresent(present));
    Serial.printf("present=%s\n", present ? "true" : "false");
  } else if (tokens[0] == "reset") {
    ex::printStatus(gDevice.resetAndDiscover());
  } else if (tokens[0] == "probe") {
    ex::printStatus(gDevice.probe());
  } else if (tokens[0] == "recover") {
    ex::printStatus(gDevice.recover());
  } else if (tokens[0] == "high") {
    ex::printStatus(gDevice.setHighSpeed());
    Serial.printf("speed=%s\n", ex::speedToStr(gDevice.speedMode()));
  } else if (tokens[0] == "std") {
    ex::printStatus(gDevice.setStandardSpeed());
    Serial.printf("speed=%s\n", ex::speedToStr(gDevice.speedMode()));
  } else if (tokens[0] == "is_high") {
    bool enabled = false;
    ex::printStatus(gDevice.isHighSpeed(enabled));
    Serial.printf("isHighSpeed=%s\n", enabled ? "true" : "false");
  } else if (tokens[0] == "is_std") {
    bool enabled = false;
    ex::printStatus(gDevice.isStandardSpeed(enabled));
    Serial.printf("isStandardSpeed=%s\n", enabled ? "true" : "false");
  } else if (tokens[0] == "part") {
    Serial.printf("part=%s\n", ex::partToStr(gDevice.detectedPart()));
  } else if (tokens[0] == "detect") {
    AT21CS::PartType part = AT21CS::PartType::UNKNOWN;
    ex::printStatus(gDevice.detectPart(part));
    Serial.printf("detected=%s\n", ex::partToStr(part));
  } else if (tokens[0] == "id") {
    uint32_t id = 0;
    ex::printStatus(gDevice.readManufacturerId(id));
    Serial.printf("manufacturerId=0x%06lX\n", static_cast<unsigned long>(id));
  } else if (tokens[0] == "crc8") {
    if (argc < 2) {
      Serial.println("Usage: crc8 <b0> [b1..bN]");
    } else {
      uint8_t bytes[64] = {0};
      size_t len = 0;
      bool ok = true;
      for (int i = 1; i < argc && len < 64; ++i) {
        uint8_t b = 0;
        if (!ex::parseU8(tokens[i], b)) {
          ok = false;
          break;
        }
        bytes[len++] = b;
      }
      if (!ok || len == 0) {
        Serial.println("Invalid byte list");
      } else {
        const uint8_t crc = AT21CS::Driver::crc8_31(bytes, len);
        Serial.printf("crc8=0x%02X\n", crc);
      }
    }
  } else if (tokens[0] == "current") {
    uint8_t value = 0;
    ex::printStatus(gDevice.readCurrentAddress(value));
    Serial.printf("current=0x%02X\n", value);
  } else if (tokens[0] == "e_read" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t len = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], len) || len == 0 || len > 128) {
      Serial.println("Usage: e_read <addr> <len 1..128>");
    } else {
      uint8_t data[128] = {0};
      const AT21CS::Status st = gDevice.readEeprom(addr, data, len);
      ex::printStatus(st);
      if (st.ok()) {
        Serial.print("  data=");
        printBytes(data, len);
      }
    }
  } else if (tokens[0] == "e_dump" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t len = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], len) || len == 0 || len > 128) {
      Serial.println("Usage: e_dump <addr> <len 1..128>");
    } else {
      uint8_t data[128] = {0};
      const AT21CS::Status st = gDevice.readEeprom(addr, data, len);
      ex::printStatus(st);
      if (st.ok()) {
        printHexDump(addr, data, len);
      }
    }
  } else if (tokens[0] == "e_text" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t len = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], len) || len == 0 || len > 128) {
      Serial.println("Usage: e_text <addr> <len 1..128>");
    } else {
      uint8_t data[128] = {0};
      const AT21CS::Status st = gDevice.readEeprom(addr, data, len);
      ex::printStatus(st);
      if (st.ok()) {
        for (size_t offset = 0; offset < len;) {
          const uint8_t lineAddr = static_cast<uint8_t>((addr + offset) & 0x7F);
          size_t lineLen = len - offset;
          if (lineLen > 32U) lineLen = 32U;
          Serial.printf("  %02X: \"", lineAddr);
          for (size_t i = 0; i < lineLen; ++i) {
            printEscapedByte(data[offset + i]);
          }
          Serial.println("\"");
          offset += lineLen;
        }
      }
    }
  } else if (tokens[0] == "e_write" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t value = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], value)) {
      Serial.println("Usage: e_write <addr> <value>");
    } else {
      ex::printStatus(gDevice.writeEepromByte(addr, value));
    }
  } else if (tokens[0] == "e_page" && argc >= 3) {
    uint8_t addr = 0;
    if (!ex::parseU8(tokens[1], addr)) {
      Serial.println("Invalid address");
    } else {
      uint8_t data[8] = {0};
      size_t len = 0;
      for (int i = 2; i < argc && len < 8; ++i) {
        uint8_t value = 0;
        if (!ex::parseU8(tokens[i], value)) {
          len = 0;
          break;
        }
        data[len++] = value;
      }
      if (len == 0) {
        Serial.println("Usage: e_page <addr> <v0> [..v7]");
      } else {
        ex::printStatus(gDevice.writeEepromPage(addr, data, len));
      }
    }
  } else if (tokens[0] == "e_fill" && argc >= 4) {
    uint8_t addr = 0;
    uint8_t value = 0;
    uint8_t len = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], value) ||
        !ex::parseU8(tokens[3], len) || len == 0) {
      Serial.println("Usage: e_fill <addr> <value> <len>");
    } else {
      uint8_t buf[128];
      memset(buf, value, len);
      const AT21CS::Status st = gDevice.writeEeprom(addr, buf, len);
      ex::printStatus(st);
      if (st.ok()) {
        Serial.printf("  Filled %u bytes at 0x%02X with 0x%02X\n", len, addr, value);
      }
    }
  } else if (tokens[0] == "e_verify" && argc >= 3) {
    uint8_t addr = 0;
    if (!ex::parseU8(tokens[1], addr)) {
      Serial.println("Usage: e_verify <addr> <v0> [..vN]");
    } else {
      uint8_t expected[64] = {0};
      size_t len = 0;
      bool parseOk = true;
      for (int i = 2; i < argc && len < 64; ++i) {
        uint8_t v = 0;
        if (!ex::parseU8(tokens[i], v)) {
          parseOk = false;
          break;
        }
        expected[len++] = v;
      }
      if (!parseOk || len == 0) {
        Serial.println("Usage: e_verify <addr> <v0> [..vN]");
      } else {
        uint8_t actual[64] = {0};
        const AT21CS::Status st = gDevice.readEeprom(addr, actual, len);
        ex::printStatus(st);
        if (st.ok()) {
          bool match = true;
          for (size_t i = 0; i < len; ++i) {
            if (actual[i] != expected[i]) {
              Serial.printf("  %sMismatch%s at 0x%02X: expected=0x%02X actual=0x%02X\n",
                            LOG_COLOR_RED, LOG_COLOR_RESET,
                            static_cast<uint8_t>(addr + i), expected[i], actual[i]);
              match = false;
            }
          }
          if (match) {
            Serial.printf("  %sVerify OK%s: %u bytes at 0x%02X match\n",
                          LOG_COLOR_GREEN, LOG_COLOR_RESET,
                          static_cast<unsigned>(len), addr);
          }
        }
      }
    }
  } else if (tokens[0] == "e_crc" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t len = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], len) || len == 0 || len > 128) {
      Serial.println("Usage: e_crc <addr> <len 1..128>");
    } else {
      uint8_t data[128] = {0};
      const AT21CS::Status st = gDevice.readEeprom(addr, data, len);
      ex::printStatus(st);
      if (st.ok()) {
        uint32_t crc = crc32Update(0xFFFFFFFFUL, data, len);
        crc ^= 0xFFFFFFFFUL;
        Serial.printf("  CRC32[0x%02X + %u] = 0x%08lX\n", addr,
                      static_cast<unsigned>(len), static_cast<unsigned long>(crc));
      }
    }
  } else if (tokens[0] == "e_strings") {
    uint8_t addr = 0x00;
    uint8_t len = static_cast<uint8_t>(AT21CS::cmd::EEPROM_SIZE);
    uint8_t minLen = 4;
    if (argc >= 2 && !ex::parseU8(tokens[1], addr)) {
      Serial.println("Usage: e_strings [addr] [len] [minLen]");
    } else {
      if (argc >= 3) ex::parseU8(tokens[2], len);
      if (argc >= 4) ex::parseU8(tokens[3], minLen);
      if (len == 0 || len > 128) len = static_cast<uint8_t>(AT21CS::cmd::EEPROM_SIZE);
      if (minLen == 0) minLen = 1;
      uint8_t data[128] = {0};
      const AT21CS::Status st = gDevice.readEeprom(addr, data, len);
      ex::printStatus(st);
      if (st.ok()) {
        uint32_t matches = 0;
        uint8_t strStart = 0;
        uint8_t strLen = 0;
        for (uint8_t i = 0; i < len; ++i) {
          const uint8_t c = data[i];
          if (c >= 0x20U && c <= 0x7EU) {
            if (strLen == 0) strStart = static_cast<uint8_t>(addr + i);
            strLen++;
          } else {
            if (strLen >= minLen) {
              Serial.printf("  %02X len=%u \"", strStart, strLen);
              for (uint8_t j = static_cast<uint8_t>(i - strLen); j < i; ++j) {
                printEscapedByte(data[j]);
              }
              Serial.println("\"");
              matches++;
            }
            strLen = 0;
          }
        }
        // Flush trailing string
        if (strLen >= minLen) {
          Serial.printf("  %02X len=%u \"", strStart, strLen);
          for (uint8_t j = static_cast<uint8_t>(len - strLen); j < len; ++j) {
            printEscapedByte(data[j]);
          }
          Serial.println("\"");
          matches++;
        }
        if (matches == 0) {
          Serial.println("  No printable strings found.");
        } else {
          Serial.printf("  Found %lu printable string(s).\n", static_cast<unsigned long>(matches));
        }
      }
    }
  } else if (tokens[0] == "s_read" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t len = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], len) || len == 0 || len > 32) {
      Serial.println("Usage: s_read <addr> <len 1..32>");
    } else {
      uint8_t data[32] = {0};
      const AT21CS::Status st = gDevice.readSecurity(addr, data, len);
      ex::printStatus(st);
      if (st.ok()) {
        Serial.print("  data=");
        printBytes(data, len);
      }
    }
  } else if (tokens[0] == "s_dump" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t len = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], len) || len == 0 || len > 32) {
      Serial.println("Usage: s_dump <addr> <len 1..32>");
    } else {
      uint8_t data[32] = {0};
      const AT21CS::Status st = gDevice.readSecurity(addr, data, len);
      ex::printStatus(st);
      if (st.ok()) {
        printHexDump(addr, data, len);
      }
    }
  } else if (tokens[0] == "s_write" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t value = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], value)) {
      Serial.println("Usage: s_write <addr> <value>");
    } else {
      ex::printStatus(gDevice.writeSecurityUserByte(addr, value));
    }
  } else if (tokens[0] == "s_page" && argc >= 3) {
    uint8_t addr = 0;
    if (!ex::parseU8(tokens[1], addr)) {
      Serial.println("Invalid address");
    } else {
      uint8_t data[8] = {0};
      size_t len = 0;
      for (int i = 2; i < argc && len < 8; ++i) {
        uint8_t value = 0;
        if (!ex::parseU8(tokens[i], value)) {
          len = 0;
          break;
        }
        data[len++] = value;
      }
      if (len == 0) {
        Serial.println("Usage: s_page <addr> <v0> [..v7]");
      } else {
        ex::printStatus(gDevice.writeSecurityUserPage(addr, data, len));
      }
    }
  } else if (tokens[0] == "s_locked") {
    bool locked = false;
    ex::printStatus(gDevice.isSecurityLocked(locked));
    Serial.printf("locked=%s\n", locked ? "true" : "false");
  } else if (tokens[0] == "s_lock") {
    ex::printStatus(gDevice.lockSecurityRegister());
  } else if (tokens[0] == "serial") {
    AT21CS::SerialNumberInfo sn = {};
    ex::printStatus(gDevice.readSerialNumber(sn));
    Serial.print("serial=");
    printBytes(sn.bytes, AT21CS::cmd::SECURITY_SERIAL_SIZE);
    Serial.printf("productIdOk=%s crcOk=%s\n", sn.productIdOk ? "true" : "false",
                  sn.crcOk ? "true" : "false");
  } else if (tokens[0] == "read_zone" && argc >= 2) {
    uint8_t zone = 0;
    if (!ex::parseU8(tokens[1], zone)) {
      Serial.println("Usage: read_zone <0..3>");
    } else {
      uint8_t value = 0;
      ex::printStatus(gDevice.readRomZoneRegister(zone, value));
      Serial.printf("zone=%u register=0x%02X\n", zone, value);
    }
  } else if (tokens[0] == "is_rom" && argc >= 2) {
    uint8_t zone = 0;
    if (!ex::parseU8(tokens[1], zone)) {
      Serial.println("Usage: is_rom <0..3>");
    } else {
      bool isRom = false;
      ex::printStatus(gDevice.isZoneRom(zone, isRom));
      Serial.printf("zone=%u isRom=%s\n", zone, isRom ? "true" : "false");
    }
  } else if (tokens[0] == "set_rom" && argc >= 2) {
    uint8_t zone = 0;
    if (!ex::parseU8(tokens[1], zone)) {
      Serial.println("Usage: set_rom <0..3>");
    } else {
      ex::printStatus(gDevice.setZoneRom(zone));
    }
  } else if (tokens[0] == "frozen") {
    bool frozen = false;
    ex::printStatus(gDevice.areRomZonesFrozen(frozen));
    Serial.printf("frozen=%s\n", frozen ? "true" : "false");
  } else if (tokens[0] == "freeze") {
    ex::printStatus(gDevice.freezeRomZones());
  } else if (tokens[0] == "lc_layout") {
    printLoadCellLayout();
  } else if (tokens[0] == "lc_write_demo") {
    writeLoadCellDemoData();
  } else if (tokens[0] == "lc_read") {
    printLoadCellRecords();
  } else if (tokens[0] == "lc_set_tare" && argc >= 2) {
    int32_t tareRaw = 0;
    if (!ex::parseI32(tokens[1], tareRaw)) {
      Serial.println("Usage: lc_set_tare <signed_raw>");
    } else {
      lcmap::RuntimeBlockV1 runtime = {};
      bool valid = false;
      const AT21CS::Status readSt = lcmap::readRuntime(gDevice, runtime, valid);
      ex::printStatus(readSt);
      if (readSt.ok()) {
        if (!valid) {
          runtime = {};
          runtime.flags = 0x0001;
          runtime.filterProfile = 2;
        }
        runtime.seq += 1;
        runtime.installTareRaw = tareRaw;
        ex::printStatus(lcmap::writeRuntime(gDevice, runtime));
      }
    }
  } else if (tokens[0] == "lc_inc_overload") {
    uint32_t increment = 1;
    if (argc >= 2 && !ex::parseU32(tokens[1], increment)) {
      Serial.println("Usage: lc_inc_overload [count]");
    } else {
      lcmap::CounterBlockV1 counters = {};
      bool valid = false;
      const AT21CS::Status readSt = lcmap::readCounters(gDevice, counters, valid);
      ex::printStatus(readSt);
      if (readSt.ok()) {
        if (!valid) {
          counters = {};
          counters.flags = 0x0001;
        }
        counters.seq += 1;
        counters.overloadCount += increment;
        ex::printStatus(lcmap::writeCounters(gDevice, counters));
      }
    }
  } else if (tokens[0] == "lc_fwrite" && argc >= 3) {
    uint8_t addr = 0;
    float value = 0.0F;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseF32(tokens[2], value)) {
      Serial.println("Usage: lc_fwrite <addr> <float>");
    } else {
      ex::printStatus(lcmap::writeFloat32(gDevice, addr, value));
    }
  } else if (tokens[0] == "lc_fread" && argc >= 2) {
    uint8_t addr = 0;
    if (!ex::parseU8(tokens[1], addr)) {
      Serial.println("Usage: lc_fread <addr>");
    } else {
      float value = 0.0F;
      ex::printStatus(lcmap::readFloat32(gDevice, addr, value));
      Serial.printf("float@0x%02X = %.7f\n", addr, static_cast<double>(value));
    }
  } else if (tokens[0] == "wait") {
    uint32_t timeoutMs = 6;
    if (argc >= 2 && !ex::parseU32(tokens[1], timeoutMs)) {
      Serial.println("Usage: wait [timeout_ms]");
    } else {
      ex::printStatus(gDevice.waitReady(timeoutMs));
    }
  } else if (tokens[0] == "health") {
    ex::printHealth(gDevice);
  } else {
    Serial.printf("Unknown command: %s\n", tokens[0].c_str());
  }

  Serial.print("> ");
}
