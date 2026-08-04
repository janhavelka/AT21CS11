# Prompt 04 — completed Arduino ESP32-S2/S3 Backend

## Status

Completed and checkpointed. This file records the supported Backend boundary.

## Established result

- Supported firmware framework: Arduino on ESP32-S2/S3 through
  `https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip`.
- Core remains framework-neutral; native ESP-IDF is not supported or claimed.
- `Esp32Transport` owns pin configuration and precise single-wire timing.
- Its optional detect input is disabled only by `presencePin == -1`. An enabled
  valid input distinct from SI/O is sampled once per callback and mapped by
  `presenceActiveHigh`; internal pulls, interrupts, debounce, and polling remain
  disabled/absent.
- Each callback executes one bounded whole frame with correct open-drain GPIO,
  MSb order, ACK sampling, Reset/Discovery, and released-high cleanup.
- Timing-critical backend sections may use guarded Arduino-ESP32/FreeRTOS
  facilities, but the library creates no task, queue, or public lock.
- Multiple Backend instances have independent state and descriptors.
- The qualified concurrency contract is serialized calls, normally from one
  firmware owner task/loop. Simultaneous timing-critical calls from separate
  tasks are not claimed.
- Stage-local S2/S3 physical-layer smoke consumers live under
  `test/consumer/phy_smoke/arduino/`.

Host/backend regressions are covered by `test/test_esp32_transport.cpp` and the
static timing/IRAM checks.

## Regression rule

Do not add native-IDF examples, components, packages, or build gates. Prompt 06
removes stale example/checker artifacts; Prompt 07 verifies the final package.
Physical waveform qualification belongs only to Prompt 08.
