# AT21CS11 ESP-IDF Port Notes

Date: 2026-05-19.
Branch: `feature/at21cs-idf-port`.

## Current Status

- ESP-IDF support is implemented for the current port branch.
- The core protocol code builds for Arduino and ESP-IDF through platform guards
  and callback/time hooks.
- Root `CMakeLists.txt` and `idf_component.yml` expose the driver as an
  ESP-IDF component.
- `examples/espidf_basic` reuses the same colored bring-up CLI source as the
  Arduino example, so command structure, help depth, diagnostics, and output
  style stay aligned across frameworks.

## Architecture

- The library remains application-owned for board policy:
  - the configured SIO pin belongs to `Config`;
  - optional presence-pin policy remains in `Config`;
  - the driver does not create tasks or own global board resources.
- Timing is injected through `Config::nowMs`, `Config::sleepUs`, and
  `timeUser`; platform fallbacks are guarded.
- The ESP-IDF implementation uses IDF GPIO/timing APIs where the Arduino build
  uses Arduino GPIO/timing APIs.
- The ESP-IDF example provides only example glue and calls the same
  `setup()` / `loop()` command implementation as Arduino.

## Public Boundary Note

The fast ESP32 GPIO path still exposes ESP32/FreeRTOS implementation members in
the public class layout under platform guards. This is compatible with the
current ESP-IDF component build, but a future cleanup can move those details
behind a private platform helper if a stricter framework-neutral public header
is required.

## ESP-IDF Example

- Entry point: `examples/espidf_basic/main/main.cpp`
- Shared CLI source: `examples/01_basic_bringup_cli/main.cpp`
- Example compatibility layer: `examples/common/IdfArduinoCompat.h`

The shared CLI covers the same flows in both frameworks:

- version/help/settings output;
- probe/recover/health diagnostics;
- ROM and device information reads;
- EEPROM read/write/verify/fill-style flows exposed by the example;
- stress/self-test style bring-up checks.

## Validation

Completed during the implementation pass:

- Arduino PlatformIO example builds.
- Native/unit tests where present.
- Static shared-CLI contract checks.

Still required before production hardware release:

- `idf.py build` for `examples/espidf_basic` on the target ESP-IDF version.
- Hardware timing validation on ESP32-S2 and ESP32-S3.
- EEPROM read/write smoke tests on real AT21CS devices.
