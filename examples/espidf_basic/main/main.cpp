/**
 * @file main.cpp
 * @brief Native ESP-IDF bring-up CLI for AT21CS01/AT21CS11.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <driver/gpio.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "AT21CS/AT21CS.h"

#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif
#ifndef AT21CS_SIO_PIN
#define AT21CS_SIO_PIN 4
#endif
#ifndef AT21CS_PRESENCE_PIN
#define AT21CS_PRESENCE_PIN -1
#endif
#ifndef AT21CS_ADDRESS_BITS
#define AT21CS_ADDRESS_BITS 0
#endif

namespace {

static constexpr const char* GREEN = "\033[32m";
static constexpr const char* RED = "\033[31m";
static constexpr const char* YELLOW = "\033[33m";
static constexpr const char* CYAN = "\033[36m";
static constexpr const char* RESET = "\033[0m";
static constexpr size_t LINE_LEN = 160;
static constexpr size_t MAX_TOKENS = 16;

AT21CS::Driver gDevice;
AT21CS::Config gConfig;
bool gVerbose = false;

uint32_t nowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

void sleepUs(uint32_t us, void*) {
  esp_rom_delay_us(us);
}

int split(char* line, char** tokens, int maxTokens) {
  int count = 0;
  char* save = nullptr;
  for (char* tok = strtok_r(line, " \t\r\n", &save);
       tok != nullptr && count < maxTokens;
       tok = strtok_r(nullptr, " \t\r\n", &save)) {
    tokens[count++] = tok;
  }
  return count;
}

bool parseU32(const char* text, uint32_t& out, uint32_t minValue, uint32_t maxValue) {
  char* end = nullptr;
  const unsigned long value = std::strtoul((text != nullptr) ? text : "", &end, 0);
  if (end == text || *end != '\0' || value < minValue || value > maxValue) {
    return false;
  }
  out = static_cast<uint32_t>(value);
  return true;
}

const char* partToStr(AT21CS::PartType part) {
  switch (part) {
    case AT21CS::PartType::AT21CS01:
      return "AT21CS01";
    case AT21CS::PartType::AT21CS11:
      return "AT21CS11";
    default:
      return "UNKNOWN";
  }
}

const char* stateToStr(AT21CS::DriverState state) {
  switch (state) {
    case AT21CS::DriverState::UNINIT:
      return "UNINIT";
    case AT21CS::DriverState::PROBING:
      return "PROBING";
    case AT21CS::DriverState::INIT_CONFIG:
      return "INIT_CONFIG";
    case AT21CS::DriverState::READY:
      return "READY";
    case AT21CS::DriverState::BUSY:
      return "BUSY";
    case AT21CS::DriverState::DEGRADED:
      return "DEGRADED";
    case AT21CS::DriverState::OFFLINE:
      return "OFFLINE";
    case AT21CS::DriverState::RECOVERING:
      return "RECOVERING";
    case AT21CS::DriverState::SLEEPING:
      return "SLEEPING";
    case AT21CS::DriverState::FAULT:
      return "FAULT";
    default:
      return "?";
  }
}

void printStatus(const AT21CS::Status& st) {
  std::printf("Status: %s%s%s code=%u detail=%ld\n",
              st.ok() ? GREEN : RED, (st.msg != nullptr) ? st.msg : "", RESET,
              static_cast<unsigned>(st.code), static_cast<long>(st.detail));
  if (st.msg != nullptr) {
    std::printf("Message: %s\n", st.msg);
  }
}

void dumpHex(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    std::printf("%02X%s", static_cast<unsigned>(data[i]), (i + 1U == len) ? "" : " ");
  }
  std::printf("\n");
}

void initConfig() {
  gConfig.sioPin = AT21CS_SIO_PIN;
  gConfig.presencePin = AT21CS_PRESENCE_PIN;
  gConfig.addressBits = AT21CS_ADDRESS_BITS;
  gConfig.nowMs = &nowMs;
  gConfig.sleepUs = &sleepUs;
}

void printHelpItem(const char* cmd, const char* desc) {
  std::printf("  %-30s - %s\n", cmd, desc);
}

void printHelp() {
  std::printf("\n%s=== AT21CS11 CLI Help ===%s\n", CYAN, RESET);
  printHelpItem("help", "show this help");
  printHelpItem("begin [a2a0]", "initialize/probe selected address bits");
  printHelpItem("scan", "scan A2:A0 addresses 0..7");
  printHelpItem("probe", "raw device probe without health mutation");
  printHelpItem("recover", "manual recovery through driver recover()");
  printHelpItem("reset", "reset/discover device");
  printHelpItem("drv", "driver health and config");
  printHelpItem("cfg", "show example pin/address config");
  printHelpItem("read <addr>", "read EEPROM byte");
  printHelpItem("e_read <addr> <len>", "read EEPROM bytes");
  printHelpItem("e_write <addr> <value>", "write EEPROM byte");
  printHelpItem("s_read <addr> <len>", "read security bytes");
  printHelpItem("serial", "read factory serial number");
  printHelpItem("raw", "single-wire raw workflow diagnostics");
  printHelpItem("chip", "chip/manufacturer workflow");
  printHelpItem("selftest", "safe local/driver checks");
  printHelpItem("stress [count]", "repeat probe");
  printHelpItem("verbose on|off", "toggle extra diagnostics");
  std::printf("\n");
}

void cmdBegin(uint8_t addressBits) {
  initConfig();
  gConfig.addressBits = addressBits;
  const AT21CS::Status st = gDevice.begin(gConfig);
  printStatus(st);
  if (st.ok()) {
    std::printf("part=%s\n", partToStr(gDevice.detectedPart()));
  }
}

void cmdDrv() {
  const AT21CS::SettingsSnapshot s = gDevice.getSettings();
  std::printf("%s=== Driver Health ===%s\n", CYAN, RESET);
  std::printf("initialized=%s state=%s online=%s part=%s\n",
              s.initialized ? "true" : "false", stateToStr(s.state),
              gDevice.isOnline() ? "true" : "false", partToStr(s.detectedPart));
  std::printf("sioPin=%d presencePin=%d addressBits=%u failures=%u totalOk=%lu totalFail=%lu\n",
              s.config.sioPin, s.config.presencePin, s.config.addressBits,
              static_cast<unsigned>(s.consecutiveFailures),
              static_cast<unsigned long>(s.totalSuccess),
              static_cast<unsigned long>(s.totalFailures));
  printStatus(s.lastError);
}

void cmdScan() {
  std::printf("%s=== Address Scan ===%s\n", CYAN, RESET);
  for (uint8_t addr = 0; addr < 8; ++addr) {
    cmdBegin(addr);
    std::printf("A2:A0=%u -> %s\n", static_cast<unsigned>(addr),
                gDevice.isInitialized() ? "FOUND" : "no response");
    gDevice.end();
  }
}

void cmdRead(uint8_t addr, uint8_t len) {
  uint8_t data[128] = {};
  const AT21CS::Status st = gDevice.readEeprom(addr, data, len);
  printStatus(st);
  if (st.ok()) {
    dumpHex(data, len);
  }
}

void cmdSecurity(uint8_t addr, uint8_t len) {
  uint8_t data[32] = {};
  const AT21CS::Status st = gDevice.readSecurity(addr, data, len);
  printStatus(st);
  if (st.ok()) {
    dumpHex(data, len);
  }
}

void cmdSelfTest() {
  const bool crcOk = AT21CS::Driver::crc8_31(nullptr, 0) == 0U;
  std::printf("Selftest result: pass=%s%u%s fail=%s%u%s\n",
              crcOk ? GREEN : RED, crcOk ? 1U : 0U, RESET,
              crcOk ? GREEN : RED, crcOk ? 0U : 1U, RESET);
}

void cmdStress(uint32_t count) {
  uint32_t ok = 0;
  uint32_t fail = 0;
  const uint32_t start = nowMs(nullptr);
  for (uint32_t i = 0; i < count; ++i) {
    const AT21CS::Status st = gDevice.probe();
    if (st.ok()) {
      ++ok;
    } else {
      ++fail;
    }
  }
  std::printf("=== Stress Summary ===\n");
  std::printf("  Target: %lu\n  Success: %s%lu%s\n  Errors: %s%lu%s\n  Duration: %lu ms\n",
              static_cast<unsigned long>(count), GREEN, static_cast<unsigned long>(ok),
              RESET, fail == 0 ? GREEN : RED, static_cast<unsigned long>(fail), RESET,
              static_cast<unsigned long>(nowMs(nullptr) - start));
}

void handleCommand(char* line) {
  char* tokens[MAX_TOKENS] = {};
  const int argc = split(line, tokens, MAX_TOKENS);
  if (argc == 0) {
    return;
  }
  if (std::strcmp(tokens[0], "help") == 0) {
    printHelp();
  } else if (std::strcmp(tokens[0], "begin") == 0) {
    uint32_t addr = AT21CS_ADDRESS_BITS;
    if (argc >= 2) {
      (void)parseU32(tokens[1], addr, 0, 7);
    }
    cmdBegin(static_cast<uint8_t>(addr));
  } else if (std::strcmp(tokens[0], "scan") == 0) {
    cmdScan();
  } else if (std::strcmp(tokens[0], "probe") == 0) {
    printStatus(gDevice.probe());
  } else if (std::strcmp(tokens[0], "recover") == 0) {
    printStatus(gDevice.recover());
  } else if (std::strcmp(tokens[0], "reset") == 0) {
    printStatus(gDevice.resetAndDiscover());
  } else if (std::strcmp(tokens[0], "drv") == 0 || std::strcmp(tokens[0], "status") == 0) {
    cmdDrv();
  } else if (std::strcmp(tokens[0], "cfg") == 0) {
    std::printf("sioPin=%d presencePin=%d addressBits=%d\n", AT21CS_SIO_PIN,
                AT21CS_PRESENCE_PIN, AT21CS_ADDRESS_BITS);
  } else if ((std::strcmp(tokens[0], "read") == 0 || std::strcmp(tokens[0], "e_read") == 0) &&
             argc >= 2) {
    uint32_t addr = 0;
    uint32_t len = 1;
    if (parseU32(tokens[1], addr, 0, 127) &&
        (argc < 3 || parseU32(tokens[2], len, 1, 128))) {
      cmdRead(static_cast<uint8_t>(addr), static_cast<uint8_t>(len));
    }
  } else if (std::strcmp(tokens[0], "e_write") == 0 && argc >= 3) {
    uint32_t addr = 0;
    uint32_t value = 0;
    if (parseU32(tokens[1], addr, 0, 127) && parseU32(tokens[2], value, 0, 255)) {
      printStatus(gDevice.writeEepromByte(static_cast<uint8_t>(addr), static_cast<uint8_t>(value)));
    }
  } else if (std::strcmp(tokens[0], "s_read") == 0 && argc >= 3) {
    uint32_t addr = 0;
    uint32_t len = 0;
    if (parseU32(tokens[1], addr, 0, 31) && parseU32(tokens[2], len, 1, 32)) {
      cmdSecurity(static_cast<uint8_t>(addr), static_cast<uint8_t>(len));
    }
  } else if (std::strcmp(tokens[0], "serial") == 0) {
    AT21CS::SerialNumberInfo info{};
    const AT21CS::Status st = gDevice.readSerialNumber(info);
    printStatus(st);
    if (st.ok()) {
      dumpHex(info.bytes, sizeof(info.bytes));
      std::printf("productIdOk=%s crcOk=%s\n", info.productIdOk ? "true" : "false",
                  info.crcOk ? "true" : "false");
    }
  } else if (std::strcmp(tokens[0], "selftest") == 0) {
    cmdSelfTest();
  } else if (std::strcmp(tokens[0], "stress") == 0) {
    uint32_t count = 10;
    if (argc >= 2) {
      (void)parseU32(tokens[1], count, 1, 1000);
    }
    cmdStress(count);
  } else if (std::strcmp(tokens[0], "verbose") == 0 && argc >= 2) {
    gVerbose = std::strcmp(tokens[1], "on") == 0;
    std::printf("verbose=%s\n", gVerbose ? "on" : "off");
  } else if (std::strcmp(tokens[0], "raw") == 0 || std::strcmp(tokens[0], "chip") == 0) {
    std::printf("%s%s workflow is native-IDF and does not include Arduino CLI source.%s\n",
                YELLOW, tokens[0], RESET);
  } else {
    std::printf("Unknown command: %s\n", tokens[0]);
  }
}

}  // namespace

extern "C" void app_main(void) {
  setvbuf(stdin, nullptr, _IONBF, 0);
  setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("\n%s=== AT21CS11 Native ESP-IDF Bringup ===%s\n", CYAN, RESET);
  initConfig();
  printHelp();
  cmdBegin(static_cast<uint8_t>(AT21CS_ADDRESS_BITS));
  std::printf("> ");
  while (true) {
    char line[LINE_LEN];
    if (std::fgets(line, sizeof(line), stdin) != nullptr) {
      handleCommand(line);
      std::printf("> ");
    }
    gDevice.tick(nowMs(nullptr));
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
