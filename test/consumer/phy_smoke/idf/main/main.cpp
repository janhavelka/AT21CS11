#include <cstdint>

#include "AT21CS/AT21CS.h"
#include "AT21CS/platform/esp32/Esp32Transport.h"

extern "C" void app_main() {
  AT21CS::Esp32Transport backend;
  AT21CS::Esp32TransportConfig pins{};
  // begin() rejects the default SI/O pin (-1) before touching GPIO. Calling it
  // keeps the hardware path and its SDK references in the linked smoke image.
  if (!backend.begin(pins).ok()) {
    return;
  }

  AT21CS::Bus bus;
  AT21CS::BusConfig busConfig{};
  busConfig.transport = backend.descriptor();
  if (!bus.bind(busConfig).ok()) {
    backend.end();
    return;
  }

  AT21CS::Driver driver;
  uint32_t manufacturerId = 0;
  uint8_t readBuffer[1]{};
  if (driver.bind(bus, AT21CS::Config{}).ok() &&
      driver.initialize().ok() &&
      driver.readManufacturerId(manufacturerId).ok()) {
    (void)driver.readEeprom(0, readBuffer, sizeof(readBuffer));
  }

  driver.end();
  if (bus.end().ok()) {
    backend.end();
  }
}
