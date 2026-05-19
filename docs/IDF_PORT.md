# AT21CS11 ESP-IDF v6.0.1 Port Readiness Audit

Date: 2026-05-17.
Status update: the first ESP-IDF port implementation was added on
2026-05-19. Keep this audit as historical context and see
`docs/IDF_PORT_IMPLEMENTATION.md` for resolved items, validation, and remaining
hardware checks.

Original scope: documentation for a future ESP-IDF port only. Do not change
code, `library.json`, `README.md`, `CHANGELOG.md`, generated `Version.h`,
examples, or tests while applying this audit.

## Current State

- The library is an Arduino/PlatformIO component for AT21CS single-wire EEPROMs,
  not an I2C device. The bus is bit-banged through `Config::sioPin`; optional
  `Config::presencePin` is sampled separately.
- The public API already has production-style lifecycle and status reporting:
  `AT21CS::Driver::begin(const Config&)`, `tick(uint32_t)`, `end()`,
  `probe()`, `recover()`, read/write helpers, and health counters.
- `include/AT21CS/Config.h` already exposes timing hooks:
  `NowMsFn nowMs`, `SleepUsFn sleepUs`, and `timeUser`.
- ESP32 Arduino builds use direct GPIO register access and ESP timing helpers,
  but pure ESP-IDF builds are currently blocked by Arduino dependencies.
- `platformio.ini` and `library.json` are Arduino-only. There is no
  `CMakeLists.txt`, `idf_component.yml`, or IDF-native example.

## Blockers

- `src/AT21CS.cpp` includes `<Arduino.h>` unconditionally.
- `src/AT21CS.cpp` falls back to Arduino APIs in platform helpers:
  `pinMode`, `digitalWrite`, `digitalRead`, `millis()`, and
  `delayMicroseconds()`.
- `_presencePinReportsPresent()` calls `digitalRead()` unconditionally, so a
  pure IDF build cannot even use an IDF-configured presence pin.
- `include/AT21CS/AT21CS.h` exposes ESP32 implementation members under
  `ARDUINO_ARCH_ESP32`, not an IDF-capable platform macro.
- The ESP32 implementation path exposes `portMUX_TYPE` and FreeRTOS critical
  sections in the public header. If that path is made available to pure IDF,
  the component must declare the FreeRTOS dependency or hide those members
  behind an opaque/private platform layer.
- No component build file declares ESP-IDF dependencies for GPIO, `esp_timer`,
  ROM delay, or CPU cycle helpers.
- Examples are Arduino sketches and cannot validate a pure IDF build.

## Exact Files and APIs to Change

- `include/AT21CS/AT21CS.h`
  - Replace `ARDUINO_ARCH_ESP32` implementation guards with a platform guard
    that covers both Arduino-on-ESP32 and pure ESP-IDF, for example
    `AT21CS_PLATFORM_ESP32`.
  - Keep the public class API unchanged. Do not expose IDF driver types in the
    public API unless a new optional adapter struct is unavoidable.
- `include/AT21CS/Config.h`
  - Keep `nowMs`, `sleepUs`, and `timeUser`.
  - Add GPIO hook fields only if direct `<driver/gpio.h>` support cannot keep
    the existing `sioPin`/`presencePin` contract clean. If added, make them
    optional and default-compatible for Arduino sketches.
- `src/AT21CS.cpp`
  - Move Arduino-only includes and calls behind `#if defined(ARDUINO)`.
  - Add an IDF path using `<driver/gpio.h>`, `<esp_timer.h>`, and, if needed,
    `<esp_rom_sys.h>` / `esp_rom_delay_us()`.
  - Update `_configurePins()`, `_presencePinReportsPresent()`, `_releaseLine()`,
    `_lineLow()`, `_readLine()`, `_nowMs()`, and `_sleepUs()`.
  - Keep all timeout loops deadline bounded. Do not introduce `delay()` in
    library code.
- New component files, when implementation starts:
  - `CMakeLists.txt`
  - `idf_component.yml`
  - optional `examples/espidf_basic/`

## Compatibility Architecture

- Preserve the Arduino public API and current `Config` defaults.
- Add a small platform layer rather than forking the driver:
  - `AT21CS_PLATFORM_ARDUINO` selects Arduino GPIO/time calls.
  - `AT21CS_PLATFORM_IDF` selects ESP-IDF GPIO/time calls.
  - Shared protocol logic remains in `src/AT21CS.cpp`.
- The IDF path must not allocate heap in steady state, log from the library, or
  own any global resources outside the configured GPIO pins.
- The single-wire line should be configured as open-drain where hardware allows:
  drive low for `0`, release/high-Z for `1`, and read the line through
  `gpio_get_level()`.
- Keep critical timing sections short. If direct register access is retained,
  isolate it behind ESP32-family platform helpers and keep the portable IDF
  fallback based on `gpio_set_level()` / `gpio_set_direction()`.

## Adapter Contract

- Time:
  - `nowMs(user)` returns monotonic milliseconds. IDF adapter:
    `static_cast<uint32_t>(esp_timer_get_time() / 1000LL)`.
  - `sleepUs(user, us)` performs a bounded microsecond wait. IDF adapter:
    `esp_rom_delay_us(us)` or the existing cycle-count wait if dynamic frequency
    behavior is explicitly handled.
- GPIO:
  - Configure `sioPin` with `<driver/gpio.h>` as open-drain output with pull-up
    disabled unless board wiring requires otherwise.
  - Configure `presencePin` as input using `<driver/gpio.h>` and read with
    `gpio_get_level()`.
  - All GPIO errors must map to `AT21CS::Status`; do not leak `esp_err_t`
    through public APIs. Store the original `esp_err_t` in `Status::detail`.
- Timing:
  - No unbounded busy-waits. Any polling loop must use a deadline and the
    existing timeout constants.
  - Do not call FreeRTOS delay APIs inside bit-level timing paths.

## CMake and Component Plan

Minimal component:

```cmake
idf_component_register(
  SRCS "src/AT21CS.cpp"
  INCLUDE_DIRS "include"
  REQUIRES esp_driver_gpio esp_timer freertos
  PRIV_REQUIRES esp_hw_support
)
target_compile_definitions(${COMPONENT_LIB} PUBLIC AT21CS_PLATFORM_IDF=1)
```

Component metadata:

```yaml
version: "1.3.0"
description: "AT21CS single-wire EEPROM driver"
targets:
  - esp32s2
  - esp32s3
dependencies:
  idf: ">=6.0.1"
```

Do not compile Arduino-only examples in the IDF component. If a platform shim
adds separate source files, list only the IDF shim in the IDF component and keep
Arduino builds controlled by PlatformIO.

## Example Plan

- IDF example:
  - Add `examples/espidf_basic/main/main.cpp`.
  - Use `extern "C" void app_main()`.
  - Configure pins in a local example config struct, not in the library.
  - Call `driver.begin(config)`, `driver.tick(nowMs)`, `probe()`, read ROM, and
    read a small EEPROM range.
  - Use `vTaskDelay(pdMS_TO_TICKS(10))` in the example loop only.
- Arduino example:
  - Keep existing sketches compiling unchanged.
  - Add an Arduino build check that the new platform guards still select the
    Arduino path.

## Test And Validation Plan

- Host/native unit tests for CRC/address helpers, status handling, and timeout
  math where possible.
- IDF build test for `esp32s2` and `esp32s3`.
- Hardware smoke test:
  - `begin()` succeeds with a real device.
  - `begin()` returns a fallible error when no device is present.
  - read/write EEPROM round trip with bounded write wait.
  - `presencePin` active-high and active-low variants.
  - recovery path after forcing a bus failure.
- Timing test:
  - Verify bit timing remains within AT21CS tolerance at the selected CPU
    frequency and with dynamic frequency scaling settings documented.

## ESP-IDF v6.0.1 Hazards

- Use `<driver/gpio.h>` from `esp_driver_gpio`; do not rely on Arduino GPIO
  functions in pure IDF builds.
- `esp_timer_get_time()` returns `int64_t` microseconds. Convert to
  `uint32_t` milliseconds only at API boundaries where wrap-safe math is used.
- `esp_rom_delay_us()` is a busy wait. Keep calls short and deterministic.
- Direct GPIO register access is SoC-sensitive. If retained for speed, guard it
  by target and keep a `gpio_*` fallback for portability.
- CPU cycle based delays are sensitive to dynamic frequency scaling. Either use
  ROM delay or document that CPU frequency must be stable during AT21CS bus
  transactions.
- Do not use FreeRTOS delays or logging in the low-level bit protocol.

## Ordered Checklist

1. Add platform guard macros that distinguish Arduino and pure IDF builds.
2. Remove unconditional `<Arduino.h>` from library sources.
3. Implement IDF GPIO helpers with `<driver/gpio.h>`.
4. Implement IDF time helpers with `esp_timer_get_time()` and a bounded
   microsecond wait.
5. Map every IDF GPIO/timing failure to `Status`.
6. Add root `CMakeLists.txt` and `idf_component.yml`.
7. Add a minimal IDF example using `app_main()`.
8. Confirm existing Arduino examples still compile.
9. Build IDF examples for `esp32s2` and `esp32s3`.
10. Run hardware timing and EEPROM read/write smoke tests.
