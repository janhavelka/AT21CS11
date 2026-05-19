# AT21CS11 ESP-IDF Port Implementation Notes

Date: 2026-05-19.
Branch: `feature/at21cs-idf-port`.

## Scope

- Kept `include/AT21CS/` and `src/AT21CS.cpp` as one driver core with the
  existing public `Config` contract.
- Removed pure-IDF blockers by replacing unconditional Arduino includes and
  timing calls with explicit platform paths.
- Added ESP-IDF component metadata and a native IDF entry point for the full
  bring-up CLI.
- Added an example-only ESP-IDF compatibility layer so the Arduino and ESP-IDF
  examples share one command implementation.

## Files Added

- `CMakeLists.txt`
- `idf_component.yml`
- `examples/common/IdfArduinoCompat.h`
- `examples/espidf_basic/CMakeLists.txt`
- `examples/espidf_basic/main/CMakeLists.txt`
- `examples/espidf_basic/main/main.cpp`

## Core Platform Resolution

- `AT21CS_PLATFORM_IDF=1` selects the ESP-IDF component path.
- `ARDUINO_ARCH_ESP32` keeps the Arduino-ESP32 fast path.
- `AT21CS_PLATFORM_ESP32` covers both Arduino-on-ESP32 and pure ESP-IDF for
  GPIO register access and critical sections.
- `_nowMs()` uses `Config::nowMs` when supplied, Arduino `millis()` in Arduino
  builds, and `esp_timer_get_time() / 1000` in ESP-IDF builds.
- `_sleepUs()` uses `Config::sleepUs` when supplied, the existing Arduino-ESP32
  cycle-counter spin wait in Arduino builds, and `esp_rom_delay_us()` in pure
  ESP-IDF builds.
- Presence-pin reads use `gpio_get_level()` in ESP-IDF instead of Arduino
  `digitalRead()`.

## Example Strategy

The Arduino CLI remains the source of truth. The ESP-IDF example sets
`AT21CS_EXAMPLE_PLATFORM_IDF=1`, includes `examples/common/IdfArduinoCompat.h`,
defines `Serial`, then includes `examples/01_basic_bringup_cli/main.cpp`.

The compatibility shim provides:

- `millis`, `micros`, `delay`, `delayMicroseconds`, and `yield`
- GPIO helpers used by diagnostics
- a fixed-capacity `String` subset used by the CLI parser
- a nonblocking stdin/stdout `Serial` replacement

The shim is example-only and is not part of the public driver API.

## Remaining Hardware Checks

- Build the IDF example for `esp32s3` and `esp32s2` with a real ESP-IDF v6.0.1
  toolchain.
- Run reset/discovery, manufacturer ID, serial number, EEPROM read/write,
  security register, ROM-zone, speed-switch, presence-pin, and recovery flows
  on target hardware.
- Verify high-speed bit timing with the selected ESP-IDF clock configuration.

## Verification

Completed locally:

- `python -m platformio test -e native`: passed, 14 tests.
- `python -m platformio run -e ex_cli_s3`: passed.
- `python -m platformio run -e ex_cli_s2`: passed.
- `python tools/check_cli_contract.py`: passed.
- `python tools/check_core_timing_guard.py`: passed.
- `python scripts/generate_version.py check`: completed, then generated
  `Version.h` timestamp churn was restored before commit.
- `doxygen Doxyfile`: completed.
- `git diff --check`: passed before the final verification pass.

Pending:

- `idf.py build` for `examples/espidf_basic`. `idf.py` was not available on
  PATH in this shell.
