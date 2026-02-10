#include <Arduino.h>

#include "AT21CS/AT21CS.h"
#include "../common/At21Example.h"
#include "../common/BoardConfig.h"

AT21CS::Driver gDevice;

void printBytes(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    Serial.printf("%02X ", data[i]);
  }
  Serial.println();
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  help                                 - show commands");
  Serial.println("  current                              - current address read");
  Serial.println("  e_read <addr> <len>                  - EEPROM read");
  Serial.println("  e_write <addr> <value>               - EEPROM byte write");
  Serial.println("  e_page <addr> <v0> [..v7]            - EEPROM page write");
  Serial.println("  s_read <addr> <len>                  - Security read");
  Serial.println("  s_write <addr> <value>               - Security user byte write");
  Serial.println("  s_page <addr> <v0> [..v7]            - Security user page write");
  Serial.println("  s_locked                             - Check security lock");
  Serial.println("  s_lock                               - Lock security register");
  Serial.println("  serial                               - Read serial + CRC");
  Serial.println("  wait [timeout_ms]                    - waitReady polling");
  Serial.println("  health                               - print health counters/state");
}

void setup() {
  board::initSerial();
  delay(200);

  Serial.println("\n=== 02_memory_security_cli ===");

  AT21CS::Config cfg;
  cfg.sioPin = board::SIO_PRIMARY;
  cfg.presencePin = board::PRESENCE_PRIMARY;
  cfg.addressBits = board::ADDRESS_BITS_PRIMARY;

  ex::printStatus(gDevice.begin(cfg));

  printHelp();
  Serial.print("> ");
}

void loop() {
  gDevice.tick(millis());

  String line;
  if (!ex::readLine(line)) {
    return;
  }

  String tokens[16];
  const int argc = ex::splitTokens(line, tokens, 16);
  if (argc == 0) {
    Serial.print("> ");
    return;
  }

  if (tokens[0] == "help") {
    printHelp();
  } else if (tokens[0] == "current") {
    uint8_t value = 0;
    ex::printStatus(gDevice.readCurrentAddress(value));
    Serial.printf("current=0x%02X\n", value);
  } else if (tokens[0] == "e_read" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t len = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], len) || len == 0 || len > 32) {
      Serial.println("Usage: e_read <addr> <len 1..32>");
    } else {
      uint8_t data[32] = {0};
      ex::printStatus(gDevice.readEeprom(addr, data, len));
      Serial.print("data=");
      printBytes(data, len);
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
  } else if (tokens[0] == "s_read" && argc >= 3) {
    uint8_t addr = 0;
    uint8_t len = 0;
    if (!ex::parseU8(tokens[1], addr) || !ex::parseU8(tokens[2], len) || len == 0 || len > 32) {
      Serial.println("Usage: s_read <addr> <len 1..32>");
    } else {
      uint8_t data[32] = {0};
      ex::printStatus(gDevice.readSecurity(addr, data, len));
      Serial.print("data=");
      printBytes(data, len);
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
    Serial.printf("Unknown command: %s\n", line.c_str());
  }

  Serial.print("> ");
}
