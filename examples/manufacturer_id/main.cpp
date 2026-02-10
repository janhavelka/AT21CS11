#include <Arduino.h>

#include "AT21CS/AT21CS.h"
#include "../common/At21Example.h"
#include "../common/BoardConfig.h"

AT21CS::Driver gDevice;

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  help                  - show commands");
  Serial.println("  read                  - read 24-bit manufacturer ID");
  Serial.println("  detect                - run detectPart() from ID");
  Serial.println("  std                   - try setStandardSpeed() (AT21CS11 should fail)");
  Serial.println("  high                  - setHighSpeed()");
  Serial.println("  health                - print health counters/state");
}

void setup() {
  board::initSerial();
  delay(200);

  Serial.println("\n=== manufacturer_id ===");

  AT21CS::Config cfg;
  cfg.sioPin = board::SIO_PRIMARY;
  cfg.presencePin = board::PRESENCE_PRIMARY;
  cfg.addressBits = board::ADDRESS_BITS_PRIMARY;

  const AT21CS::Status st = gDevice.begin(cfg);
  ex::printStatus(st);
  Serial.printf("detectedPart=%s\n", ex::partToStr(gDevice.detectedPart()));

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
    uint32_t id = 0;
    const AT21CS::Status st = gDevice.readManufacturerId(id);
    ex::printStatus(st);
    Serial.printf("manufacturer_id=0x%06lX\n", static_cast<unsigned long>(id));
  } else if (line == "detect") {
    AT21CS::PartType part = AT21CS::PartType::UNKNOWN;
    const AT21CS::Status st = gDevice.detectPart(part);
    ex::printStatus(st);
    Serial.printf("part=%s\n", ex::partToStr(part));
  } else if (line == "std") {
    const AT21CS::Status st = gDevice.setStandardSpeed();
    ex::printStatus(st);
    Serial.printf("speed=%s\n", ex::speedToStr(gDevice.speedMode()));
  } else if (line == "high") {
    const AT21CS::Status st = gDevice.setHighSpeed();
    ex::printStatus(st);
    Serial.printf("speed=%s\n", ex::speedToStr(gDevice.speedMode()));
  } else if (line == "health") {
    ex::printHealth(gDevice);
  } else {
    Serial.printf("Unknown command: %s\n", line.c_str());
  }

  Serial.print("> ");
}
