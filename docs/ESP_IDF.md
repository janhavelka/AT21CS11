# Native ESP-IDF integration

The repository root is an ESP-IDF component for the AT21CS01 and AT21CS11.
The packaged component requires ESP-IDF 6.0.1 or later. Builds cover version
6.0.1 on ESP32-S2 and ESP32-S3.

`library.json` lists `espidf` because the package ships the ESP-IDF component and native example; PlatformIO `framework = espidf` builds are not tested (only `idf.py` 6.0.1 for esp32s3/esp32s2).

Example 01's command engine is framework-neutral. Its shell provides a
millisecond clock, a non-blocking character reader and a bounded output sink.

## Native example

`examples/esp_idf/basic/` runs example 01's 14 commands. Start in the source
checkout or extracted package root in an ESP-IDF 6.0.1 shell. Use one build
directory and one sdkconfig per target so switching boards preserves each
configuration.

```sh
cd examples/esp_idf/basic
idf.py -B ../../../build-example-s2 -D SDKCONFIG=../../../sdkconfig.esp32s2 set-target esp32s2 build
idf.py -B ../../../build-example-s3 -D SDKCONFIG=../../../sdkconfig.esp32s3 set-target esp32s3 build
```

ESP32-S2 uses the native USB CDC console. ESP32-S3 uses USB Serial/JTAG.
Before starting the CLI, the S2 shell initializes the CDC read lock and
preserves any byte already waiting. The S3 shell installs the USB Serial/JTAG
driver so non-blocking reads can see bytes in its receive buffer.
Both use LF passthrough for input and output, so the engine's explicit CRLF
and LF reach the console unchanged. The startup banner identifies the
library version, source commit and status, SDK version and target.
The CPU0 main task owns the wire and yields one tick after each input and
service pass. The project uses a 240 MHz CPU, a 1000 Hz tick, `-Os`, 4 MB
flash and an 8192-byte main-task stack with power management disabled.
The engine stores its 128-byte input line in static storage. The formatter
uses a 256-byte stack buffer and reads use up to 32 bytes of payload.
The runtime stack high-water mark has not been measured.

Provide an external SI/O pull-up. After release, allow at most 0.60
microseconds in High Speed or 1.00 microsecond in Standard Speed for the
line to rise. At 3.3 V the approximate High-Speed resistance limits are
5.8 kOhm at 100 pF, 1.7 kOhm at 330 pF and 0.58 kOhm at 1000 pF. Hardware
wiring, board overrides and first-session commands are in the packaged
`examples/esp_idf/basic/README.md`. A native build is not hardware validation.

## Build evidence

| ESP-IDF 6.0.1 target | Smoke, PM disabled | Smoke, `CONFIG_PM_ENABLE=y` | CLI from checkout | CLI from package |
|---|---|---|---|---|
| ESP32-S2 | Image linked | Image linked | Image linked | Image linked |
| ESP32-S3 | Image linked | Image linked | Image linked | Image linked |

These results are build evidence only. The Backend objects have IRAM timing
symbols and reference the GPIO, timer, ROM delay and FreeRTOS scheduler APIs.
Only the PM-enabled objects reference the PM lock calls. Hardware and light
sleep were not run. PlatformIO's ESP-IDF framework, other targets and other
SDK versions are untested.

`esp-idf-example` builds the checked-out CLI and checks its Backend object
for IRAM placement. `esp-idf-smoke` checks the default and PM-enabled smoke
images and their SDK references. `package-payload` validates the archive's
exact file list and exports it with its SHA-256 checksum. `package-idf`
verifies that checksum and builds the packaged CLI in a fresh `consumer/`
directory with no checkout. A build from an extracted archive reports
`unknown` for both the source commit and working-tree status.

## Add the component

Place the extracted package or checkout in your project's `components/AT21CS01_AT21CS11/`.
A component that uses the library privately can declare
`PRIV_REQUIRES AT21CS01_AT21CS11` in its own `idf_component_register`.
If the component lives elsewhere, create a fixed-name wrapper at
`components/AT21CS01_AT21CS11/CMakeLists.txt` containing one line.
Adjust the relative path to its root.

```cmake
include("${CMAKE_CURRENT_LIST_DIR}/../../path/to/checkout/CMakeLists.txt")
```

The wrapper supplies the component name even when the checkout directory is
renamed. The root CMake file resolves its sources and public include directory
relative to itself. It requires C++17 or later and declares
`PRIV_REQUIRES esp_driver_gpio esp_hal_gpio esp_pm esp_timer`. The common IDF
components supply the public header's IRAM attribute and FreeRTOS types.
The Backend selects the same hardware implementation in both builds with
`defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)`.

## Timing and task ownership

Use one task pinned to one core to own every Backend, Bus and Driver. The
final released-high wait compares per-core cycle counts with interrupts
enabled. A task that migrates between reads could report a false timeout.
The ESP-IDF main task is pinned to CPU0 by default. Call the library from
task context only. ISR use is unsupported.

Each frame carries at most 8 payload bytes. The Bus allows 9 ms for a
High-Speed transfer, 24 ms for a Standard-Speed transfer and 9 ms for a
presence check. The Backend accepts a released-high wait of at most 10 ms.
It suspends the scheduler per frame and masks interrupts during the timed
segments. An AT21CS01 Standard-Speed segment can mask interrupts for up to
about 5.8 ms. Check the board's pull-up and total bus capacitance against the
[ESP32 pull-up and rise-time envelope](../README.md#esp32-pull-up-and-rise-time-envelope).

Use the smoke test settings of 240 MHz CPU, size optimization (`-Os`) and a
1000 Hz FreeRTOS tick. With `CONFIG_PM_ENABLE=y`, `Esp32Transport::begin()`
creates an `ESP_PM_CPU_FREQ_MAX` lock. The Backend holds it across the timing
segments of a frame or discovery and around the final cycle-count wait.
`tools/check_esp32_iram_sections.py` checks IRAM placement on each compiled
Backend object, including emitted transfer lambdas.

With PM enabled, the compiler emits calls to `_acquireTimingLock()` and
`_releaseTimingLock()` from the IRAM timing code. The initial ESP32-S3 link
placed these wrappers in flash. The component maps both wrappers into IRAM.
For each PM build, pass `--elf` followed by the linked `at21cs_phy_smoke.elf`
to the object checker. It verifies both wrappers reside in `.iram0.text` in
the final ELF.

## Reproduce the smoke builds

In an ESP-IDF 6.0.1 shell, run the following from the source checkout. The
smoke consumer at `test/consumer/phy_smoke/idf` defaults to SI/O pin `-1`.
It can be linked without assigning a GPIO. Each configuration gets its own
build directory and sdkconfig.

```sh
cd test/consumer/phy_smoke/idf
idf.py -B build-s2 -D SDKCONFIG=sdkconfig.esp32s2 set-target esp32s2 build
idf.py -B build-s3 -D SDKCONFIG=sdkconfig.esp32s3 set-target esp32s3 build
idf.py -B build-pm-s2 -D SDKCONFIG=sdkconfig.esp32s2.pm -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.pm" set-target esp32s2 build
idf.py -B build-pm-s3 -D SDKCONFIG=sdkconfig.esp32s3.pm -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.pm" set-target esp32s3 build
```
