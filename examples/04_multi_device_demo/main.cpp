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

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  help                                   - show commands");
  Serial.println("  list                                   - list all device states");
  Serial.println("  present <idx>                          - run isPresent for device index");
  Serial.println("  read_id <idx>                          - read manufacturer ID");
  Serial.println("  read_byte <idx> <addr>                 - read one EEPROM byte");
  Serial.println("  write_byte <idx> <addr> <value>        - write one EEPROM byte");
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

  Serial.println("\n=== multi_device_demo ===");

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

  String tokens[8];
  const int argc = ex::splitTokens(line, tokens, 8);
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
  } else if (tokens[0] == "read_id" && argc >= 2) {
    uint8_t idx = 0;
    if (!parseIndex(tokens[1], idx)) {
      Serial.println("Invalid index");
    } else {
      uint32_t id = 0;
      const AT21CS::Status st = gDevices[idx].readManufacturerId(id);
      ex::printStatus(st);
      Serial.printf("device[%u] id=0x%06lX\n", idx, static_cast<unsigned long>(id));
    }
  } else if (tokens[0] == "read_byte" && argc >= 3) {
    uint8_t idx = 0;
    uint8_t addr = 0;
    if (!parseIndex(tokens[1], idx) || !ex::parseU8(tokens[2], addr)) {
      Serial.println("Invalid args: read_byte <idx> <addr>");
    } else {
      uint8_t value = 0;
      const AT21CS::Status st = gDevices[idx].readEeprom(addr, &value, 1);
      ex::printStatus(st);
      Serial.printf("device[%u] eeprom[0x%02X]=0x%02X\n", idx, addr, value);
    }
  } else if (tokens[0] == "write_byte" && argc >= 4) {
    uint8_t idx = 0;
    uint8_t addr = 0;
    uint8_t value = 0;
    if (!parseIndex(tokens[1], idx) || !ex::parseU8(tokens[2], addr) ||
        !ex::parseU8(tokens[3], value)) {
      Serial.println("Invalid args: write_byte <idx> <addr> <value>");
    } else {
      const AT21CS::Status st = gDevices[idx].writeEepromByte(addr, value);
      ex::printStatus(st);
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
