# AT21CS11 ESP-IDF Port Implementation Notes

Date: 2026-05-19.

## Implemented

- Replaced the previous ESP-IDF entry point with a native `app_main` CLI.
- Updated the IDF contract check to reject Arduino headers, Arduino CLI source
  inclusion, Arduino serial/string types, and Arduino-style facades in the IDF
  example.
- Preserved command coverage through a repo-local native command contract.
- Removed ESP32/FreeRTOS implementation headers and platform macro selection
  from the public AT21CS core headers.
- Added `AT21CS/Transport.h` with a byte-level single-wire backend contract and
  optional `Config::transport` injection.
- Split the built-in ESP32 GPIO/timing implementation into the private
  `src/platform/esp32/AT21CSEsp32Backend.cpp` source. The core protocol source
  no longer includes ESP-IDF, FreeRTOS, ROM-delay, timer, CPU, or GPIO headers.
- Kept `AT21CS_PLATFORM_IDF` private to the ESP-IDF component target.
- Kept the built-in ESP32 compatibility backend enabled by default for this
  major version, but added the private CMake cache option
  `AT21CS_ENABLE_ESP32_COMPAT_BACKEND`. Set it to `OFF` only for applications
  that inject a complete `SingleWireTransport`; this transport-only build path
  omits the ESP32 GPIO/timer/FreeRTOS component dependencies.
- Kept `Config::sioPin`, `presencePin`, and `presenceActiveHigh` for this
  major version as built-in backend compatibility config. ESP-IDF applications
  using `Config::transport` must leave those fields unset and implement
  presence through `SingleWireTransport::presencePresent` if required.
- Tightened `begin()` validation so mixed injected-transport plus compatibility
  pin configuration returns `INVALID_CONFIG` instead of silently ignoring pin
  policy.

## Validation Status

- Static IDF contract: passed in Phase 4.
- Core timing guard: passed in Phase 4.
- PlatformIO native tests: expanded in Phase 6 and passed with
  `python -m platformio test -e native`.
- Arduino example builds: pending until `arduino-cli` or PlatformIO Arduino
  toolchains are available.
- Native `idf.py` build: pending unless an ESP-IDF shell is available.
- Hardware: pending real single-wire validation for timing, pull-up, presence,
  speed switching, write polling, and disconnected-device behavior.

## Remaining Architecture Work

- The built-in backend is now source-separated, but its exact timing still
  needs ESP32-S2/S3 hardware validation and an ESP-IDF build in a configured
  IDF shell.
- Safe next phase: wrap the built-in compatibility backend behind a formal
  backend object and make backend construction explicit in Arduino and ESP-IDF
  examples. The current build flag is a release-candidate opt-out for
  transport-only consumers, not the final backend-object API. Removing
  compatibility pin fields remains future major-version work.
