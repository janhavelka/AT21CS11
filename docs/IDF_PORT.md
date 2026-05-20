# AT21CS11 ESP-IDF Port Notes

Date: 2026-05-19.

## Boundary Contract

- Public headers must not force Arduino include paths in consumers.
- The single-wire AT21CS driver currently has a documented ESP32 timing/GPIO
  implementation path because the protocol requires microsecond bit timing.
  Keep that coupling explicit in `CMakeLists.txt` and do not add Arduino
  runtime requirements to ESP-IDF builds.
- Arduino examples may use Arduino APIs.
- ESP-IDF examples must be native IDF programs using `app_main`,
  `driver/gpio.h`, `esp_timer`, `esp_rom_delay_us`, `vTaskDelay`, and fixed C
  buffers. They must not include Arduino CLI source or Arduino-style facades.

## Native ESP-IDF Example

`examples/espidf_basic` is a standalone native IDF CLI. It creates its own
fixed command buffer, calls the AT21CS driver directly, and exposes the same
bring-up workflows expected from the Arduino CLI: help, begin, scan, probe,
recover, reset, driver status, config, EEPROM read/write, security read,
serial-number read, raw/chip workflow notes, selftest, stress, and verbose.

## Build And Checks

```bash
python tools/check_core_timing_guard.py
python tools/check_idf_example_contract.py
python -m platformio test -e native
python -m platformio run -e ex_cli_s3
python -m platformio run -e ex_cli_s2
idf.py -C examples/espidf_basic set-target esp32s3
idf.py -C examples/espidf_basic build
git diff --check
```

## Hardware Validation

Hardware validation remains required for SI/O timing margins, open-drain line
release, pull-up sizing, presence-pin polarity, standard/high-speed switching,
write-cycle polling, and disconnected-device behavior.
