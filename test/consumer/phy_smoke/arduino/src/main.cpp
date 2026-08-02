#include <Arduino.h>

#include <cstdint>

#include "AT21CS/AT21CS.h"
#include "AT21CS/platform/esp32/Esp32Transport.h"

namespace {

AT21CS::Esp32Transport backend;
AT21CS::Bus bus;
AT21CS::Driver driver;
uint8_t readBuffer[1]{};
uint32_t manufacturerId = 0;

void shutDown() {
  driver.end();
  if (bus.end().ok()) {
    backend.end();
  }
}

}  // namespace

void setup() {
  if (AT21CS_SMOKE_SIO_PIN < 0) {
    return;
  }

  AT21CS::Esp32TransportConfig backendConfig{};
  backendConfig.sioPin = AT21CS_SMOKE_SIO_PIN;
  backendConfig.presencePin = AT21CS_SMOKE_PRESENCE_PIN;
  if (!backend.begin(backendConfig).ok()) {
    return;
  }

  AT21CS::BusConfig busConfig{};
  busConfig.transport = backend.descriptor();
  if (!bus.bind(busConfig).ok()) {
    backend.end();
    return;
  }

  AT21CS::Config driverConfig{};
  if (!driver.bind(bus, driverConfig).ok() ||
      !driver.initialize().ok() ||
      !driver.readManufacturerId(manufacturerId).ok() ||
      !driver.readEeprom(0, readBuffer, sizeof(readBuffer)).ok()) {
    shutDown();
  }
}

void loop() {
  delay(1000);
}
