#include <Arduino.h>

#include "AT21CS/AT21CS.h"
#include "../common/At21Example.h"
#include "../common/BoardConfig.h"

AT21CS::Driver gDevice;

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  help                  - show commands");
  Serial.println("  read                  - read serial bytes + product ID + CRC check");
  Serial.println("  raw                   - read raw security bytes 0x00..0x07");
  Serial.println("  health                - print health counters/state");
}

void printSerial(const AT21CS::SerialNumberInfo& serial) {
  Serial.print("serial=");
  for (size_t i = 0; i < AT21CS::cmd::SECURITY_SERIAL_SIZE; ++i) {
    Serial.printf("%02X ", serial.bytes[i]);
  }
  Serial.println();
  Serial.printf("productIdOk=%s crcOk=%s\n",
                serial.productIdOk ? "true" : "false",
                serial.crcOk ? "true" : "false");
}

void setup() {
  board::initSerial();
  delay(200);

  Serial.println("\n=== serial_number_and_crc ===");

  AT21CS::Config cfg;
  cfg.sioPin = board::SIO_PRIMARY;
  cfg.presencePin = board::PRESENCE_PRIMARY;
  cfg.addressBits = board::ADDRESS_BITS_PRIMARY;

  const AT21CS::Status st = gDevice.begin(cfg);
  ex::printStatus(st);

  printHelp();
  Serial.print("> ");
}

void loop() {
  gDevice.tick(millis());

  String line;
  if (!ex::readLine(line)) {
    return;
  }

  if (line == "help") {
    printHelp();
  } else if (line == "read") {
    AT21CS::SerialNumberInfo serial = {};
    const AT21CS::Status st = gDevice.readSerialNumber(serial);
    ex::printStatus(st);
    printSerial(serial);
  } else if (line == "raw") {
    uint8_t raw[AT21CS::cmd::SECURITY_SERIAL_SIZE] = {0};
    const AT21CS::Status st =
        gDevice.readSecurity(AT21CS::cmd::SECURITY_SERIAL_START, raw, AT21CS::cmd::SECURITY_SERIAL_SIZE);
    ex::printStatus(st);

    Serial.print("raw=");
    for (size_t i = 0; i < AT21CS::cmd::SECURITY_SERIAL_SIZE; ++i) {
      Serial.printf("%02X ", raw[i]);
    }
    Serial.println();

    const uint8_t computed = gDevice.crc8_31(raw, AT21CS::cmd::SECURITY_SERIAL_SIZE - 1U);
    Serial.printf("computed_crc=0x%02X stored_crc=0x%02X\n", computed,
                  raw[AT21CS::cmd::SECURITY_SERIAL_SIZE - 1U]);
  } else if (line == "health") {
    ex::printHealth(gDevice);
  } else {
    Serial.printf("Unknown command: %s\n", line.c_str());
  }

  Serial.print("> ");
}
