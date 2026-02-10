#include <Arduino.h>

#include "AT21CS/AT21CS.h"
#include "../common/At21Example.h"
#include "../common/BoardConfig.h"

static constexpr size_t DEVICE_COUNT = 3;
AT21CS::Driver gDevices[DEVICE_COUNT];

struct DeviceCfg {
  int sioPin;
  int presencePin;
  uint8_t addressBits;
};

DeviceCfg gCfg[DEVICE_COUNT] = {
    {board::SIO_PRIMARY, board::PRESENCE_PRIMARY, board::ADDRESS_BITS_PRIMARY},
    {board::SIO_SECONDARY, board::PRESENCE_SECONDARY, board::ADDRESS_BITS_SECONDARY},
    {board::SIO_TERTIARY, board::PRESENCE_TERTIARY, board::ADDRESS_BITS_TERTIARY},
};

void printBytes(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    Serial.printf("%02X ", data[i]);
  }
  Serial.println();
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  help                                   - show commands");
  Serial.println("  list                                   - list all device states");
  Serial.println("  present <idx>                          - run isPresent for index");
  Serial.println("  probe <idx>                            - probe / detect for index");
  Serial.println("  recover <idx>                          - recover indexed driver");
  Serial.println("  id <idx>                               - read manufacturer ID");
  Serial.println("  part <idx>                             - print cached part");
  Serial.println("  high <idx>                             - set high speed");
  Serial.println("  std <idx>                              - set standard speed");
  Serial.println("  e_read <idx> <addr> <len>              - EEPROM read");
  Serial.println("  e_write <idx> <addr> <value>           - EEPROM byte write");
  Serial.println("  s_read <idx> <addr> <len>              - Security read");
  Serial.println("  serial <idx>                           - read serial + CRC");
  Serial.println("  health <idx>                           - print health counters");
}

bool parseIndex(const String& token, uint8_t& index) {
  if (!ex::parseU8(token, index)) {
    return false;
  }
  return index < DEVICE_COUNT;
}

void printList() {
  for (size_t i = 0; i < DEVICE_COUNT; ++i) {
    Serial.printf("[%u] sio=%d presentPin=%d addrBits=%u state=%s part=%s speed=%s\n",
                  static_cast<unsigned>(i), gCfg[i].sioPin, gCfg[i].presencePin,
                  gCfg[i].addressBits, ex::stateToStr(gDevices[i].state()),
                  ex::partToStr(gDevices[i].detectedPart()),
                  ex::speedToStr(gDevices[i].speedMode()));
  }
}

void setup() {
  board::initSerial();
  delay(200);

  Serial.println("\n=== 02_multi_device_demo ===");

  for (size_t i = 0; i < DEVICE_COUNT; ++i) {
    AT21CS::Config cfg;
    cfg.sioPin = gCfg[i].sioPin;
    cfg.presencePin = gCfg[i].presencePin;
    cfg.addressBits = gCfg[i].addressBits;

    Serial.printf("begin[%u] SI/O=%d presence=%d addrBits=%u\n",
                  static_cast<unsigned>(i), cfg.sioPin, cfg.presencePin, cfg.addressBits);
    ex::printStatus(gDevices[i].begin(cfg));
  }

  printHelp();
  printList();
  Serial.print("> ");
}

void loop() {
  const uint32_t nowMs = millis();
  for (size_t i = 0; i < DEVICE_COUNT; ++i) {
    gDevices[i].tick(nowMs);
  }

  String line;
  if (!ex::readLine(line)) {
    return;
  }

  String tokens[10];
  const int argc = ex::splitTokens(line, tokens, 10);
  if (argc == 0) {
    Serial.print("> ");
    return;
  }

  if (tokens[0] == "help") {
    printHelp();
  } else if (tokens[0] == "list") {
    printList();
  } else if (tokens[0] == "present" && argc >= 2) {
    uint8_t idx = 0;
    if (!parseIndex(tokens[1], idx)) {
      Serial.println("Invalid index");
    } else {
      bool present = false;
      const AT21CS::Status st = gDevices[idx].isPresent(present);
      ex::printStatus(st);
      Serial.printf("device[%u] present=%s\n", idx, present ? "true" : "false");
    }
  } else if (tokens[0] == "probe" && argc >= 2) {
    uint8_t idx = 0;
    if (!parseIndex(tokens[1], idx)) {
      Serial.println("Invalid index");
    } else {
      ex::printStatus(gDevices[idx].probe());
      Serial.printf("device[%u] part=%s speed=%s\n", idx,
                    ex::partToStr(gDevices[idx].detectedPart()),
                    ex::speedToStr(gDevices[idx].speedMode()));
    }
  } else if (tokens[0] == "recover" && argc >= 2) {
    uint8_t idx = 0;
    if (!parseIndex(tokens[1], idx)) {
      Serial.println("Invalid index");
    } else {
      ex::printStatus(gDevices[idx].recover());
    }
  } else if (tokens[0] == "id" && argc >= 2) {
    uint8_t idx = 0;
    if (!parseIndex(tokens[1], idx)) {
      Serial.println("Invalid index");
    } else {
      uint32_t id = 0;
      const AT21CS::Status st = gDevices[idx].readManufacturerId(id);
      ex::printStatus(st);
      Serial.printf("device[%u] id=0x%06lX\n", idx, static_cast<unsigned long>(id));
    }
  } else if (tokens[0] == "part" && argc >= 2) {
    uint8_t idx = 0;
    if (!parseIndex(tokens[1], idx)) {
      Serial.println("Invalid index");
    } else {
      Serial.printf("device[%u] part=%s speed=%s\n", idx,
                    ex::partToStr(gDevices[idx].detectedPart()),
                    ex::speedToStr(gDevices[idx].speedMode()));
    }
  } else if (tokens[0] == "high" && argc >= 2) {
    uint8_t idx = 0;
    if (!parseIndex(tokens[1], idx)) {
      Serial.println("Invalid index");
    } else {
      ex::printStatus(gDevices[idx].setHighSpeed());
      Serial.printf("device[%u] speed=%s\n", idx, ex::speedToStr(gDevices[idx].speedMode()));
    }
  } else if (tokens[0] == "std" && argc >= 2) {
    uint8_t idx = 0;
    if (!parseIndex(tokens[1], idx)) {
      Serial.println("Invalid index");
    } else {
      ex::printStatus(gDevices[idx].setStandardSpeed());
      Serial.printf("device[%u] speed=%s\n", idx, ex::speedToStr(gDevices[idx].speedMode()));
    }
  } else if (tokens[0] == "e_read" && argc >= 4) {
    uint8_t idx = 0;
    uint8_t addr = 0;
    uint8_t len = 0;
    if (!parseIndex(tokens[1], idx) || !ex::parseU8(tokens[2], addr) ||
        !ex::parseU8(tokens[3], len) || len == 0 || len > 32) {
      Serial.println("Usage: e_read <idx> <addr> <len 1..32>");
    } else {
      uint8_t data[32] = {0};
      ex::printStatus(gDevices[idx].readEeprom(addr, data, len));
      Serial.printf("device[%u] data=", idx);
      printBytes(data, len);
    }
  } else if (tokens[0] == "e_write" && argc >= 4) {
    uint8_t idx = 0;
    uint8_t addr = 0;
    uint8_t value = 0;
    if (!parseIndex(tokens[1], idx) || !ex::parseU8(tokens[2], addr) ||
        !ex::parseU8(tokens[3], value)) {
      Serial.println("Usage: e_write <idx> <addr> <value>");
    } else {
      const AT21CS::Status st = gDevices[idx].writeEepromByte(addr, value);
      ex::printStatus(st);
    }
  } else if (tokens[0] == "s_read" && argc >= 4) {
    uint8_t idx = 0;
    uint8_t addr = 0;
    uint8_t len = 0;
    if (!parseIndex(tokens[1], idx) || !ex::parseU8(tokens[2], addr) ||
        !ex::parseU8(tokens[3], len) || len == 0 || len > 32) {
      Serial.println("Usage: s_read <idx> <addr> <len 1..32>");
    } else {
      uint8_t data[32] = {0};
      ex::printStatus(gDevices[idx].readSecurity(addr, data, len));
      Serial.printf("device[%u] data=", idx);
      printBytes(data, len);
    }
  } else if (tokens[0] == "serial" && argc >= 2) {
    uint8_t idx = 0;
    if (!parseIndex(tokens[1], idx)) {
      Serial.println("Invalid index");
    } else {
      AT21CS::SerialNumberInfo sn = {};
      ex::printStatus(gDevices[idx].readSerialNumber(sn));
      Serial.printf("device[%u] serial=", idx);
      printBytes(sn.bytes, AT21CS::cmd::SECURITY_SERIAL_SIZE);
      Serial.printf("device[%u] productIdOk=%s crcOk=%s\n", idx,
                    sn.productIdOk ? "true" : "false", sn.crcOk ? "true" : "false");
    }
  } else if (tokens[0] == "health" && argc >= 2) {
    uint8_t idx = 0;
    if (!parseIndex(tokens[1], idx)) {
      Serial.println("Invalid index");
    } else {
      ex::printHealth(gDevices[idx]);
    }
  } else {
    Serial.printf("Unknown command: %s\n", line.c_str());
  }

  Serial.print("> ");
}
