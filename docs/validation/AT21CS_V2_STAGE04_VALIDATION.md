# AT21CS v2 Stage 04 validation record

Status: **COMPLETE — software implementation and independent audit passed;
physical qualification is HIL_ONLY in Prompt 08**

This record applies only to Prompt 04. It is software and build evidence, not
physical hardware-success evidence. Physical qualification is `HIL_ONLY` and
belongs exclusively to Prompt 08.

## Authoritative protocol source

- File: `docs/AT21CS01-AT21CS11-1-Kbit-Serial-EEPROM-Data-Sheet-DS20005857.pdf`
- Size: 2,247,216 bytes
- SHA-256: `704577264C3B6C60B2D14BE83A229F34C86433CC8951516641FB1DE9EC5DB1A5`

## Supported build boundary

- Firmware framework: Arduino on ESP32-S2 and ESP32-S3 only.
- Platform: exact PioArduino `platform-espressif32` 55.03.311 pin.
- Arduino core: 3.3.11.
- The supplied ESP32 transport rejects non-Arduino production compilation;
  `AT21CS_TESTING` is the sole host-test exception.
- No native-IDF fixture, component manifest, IDF-only root CMake build,
  `framework = espidf` environment, `ESP_PLATFORM` transport branch, standalone
  ESP-IDF SDK, or `idf.py` validation path is used.

PioArduino's Arduino framework contains its normal bundled ESP-IDF-derived
libraries. That internal Arduino implementation detail is not a native-IDF
support path and does not require installing a standalone ESP-IDF SDK.

## Frozen ESP32 reference-adapter bounds

```text
ESP32_MAX_CONTIGUOUS_IRQ_MASK_US      = 2000
ESP32_MAX_SUCCESS_FRESH_PAGE_CALL_US  = 20000
ESP32_MAX_FAULT_FRESH_PAGE_CALL_US    = 22000
ESP32_MAX_RETAINED_HOLD_PAGE_CALL_US  = 32000
```

The backend owns all physical timing. It suspends scheduling across a complete
frame callback, uses one continuous timing-critical burst in High-Speed mode,
and uses one nine-bit timing-critical section per Standard-Speed byte. No
application callback occurs inside bit timing.

When Arduino power management is enabled, `begin()` creates an
`ESP_PM_CPU_FREQ_MAX` lock and each Reset/Discovery, frame transfer, and final
deadline wait acquires it before cycle-dependent timing. When power management
is disabled, DFS is unavailable. Each atomic timing segment samples the current
CPU cycles-per-microsecond value immediately before emitting edges; no boot-time
frequency is cached. Host tests cover lock failure and balanced release.

These are implementation properties, not measured interrupt latency, waveform,
or call-duration results.

## Validation performed on 2026-08-02

- `python tools/check_core_timing_guard.py`: passed. The check rejects native-IDF
  build paths/metadata and a non-Arduino production ESP32 transport.
- `python -m platformio test -e native`: 90/90 passed through production core
  and ESP32 transport paths.
- Strict C++17 compilation of `src/AT21CS.cpp`, `src/Bus.cpp`, and
  `src/platform/esp32/Esp32Transport.cpp` with
  `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wundef`
  and `-Werror`: passed.
- Fresh `phy_smoke_s2` build: passed with PioArduino 55.03.311,
  Arduino-ESP32 3.3.11, and `framework = arduino`.
- Fresh `phy_smoke_s3` build: passed with PioArduino 55.03.311,
  Arduino-ESP32 3.3.11, and `framework = arduino`.
- S2 IRAM check passed on
  `test/consumer/phy_smoke/arduino/.pio/build/phy_smoke_s2/lib2ac/AT21CS11/platform/esp32/Esp32Transport.cpp.o`:
  all 17 required timing methods and all three emitted timing lambdas were in
  IRAM sections.
- S3 IRAM check passed on
  `test/consumer/phy_smoke/arduino/.pio/build/phy_smoke_s3/lib2ac/AT21CS11/platform/esp32/Esp32Transport.cpp.o`:
  all 17 required timing methods and all three emitted timing lambdas were in
  IRAM sections.
- `git diff --check`: passed at the final implementation review point.
- The protected complete-driver report had no diff.

The first Arduino build initialized PioArduino's new unified Xtensa 14.2
toolchain cache and exposed an incomplete package link. After PioArduino
finished converting that cache into its normal versioned PlatformIO package,
both clean Arduino builds passed. No native-IDF framework was selected or
built during this recovery.

## Prompt 08 `HIL_ONLY` work

No board, part, SI/O pin, presence pin, pull-up, instrument channel, rail, or
safe harness mapping was supplied. No hardware command was issued and no
hardware success is claimed.

Prompt 08 exclusively owns physical qualification of:

- Reset, Discovery request/sample/release, Start/repeated-Start/Stop, Standard
  and High-Speed bit shapes, ACK/NACK, and maximum read/write frames;
- analog rise time and actual ESP32 input thresholds for the released
  electrical/harness profile;
- 80/160/240 MHz where supported, DFS enabled/disabled, worst-case interrupt,
  Wi-Fi, and flash load;
- continuous interrupt-mask duration and success/fault/retained-hold page-call
  duration against the frozen bounds;
- independent-wire isolation and the authorized mutable/irreversible HIL
  matrix.

Missing physical evidence does not block the Stage 04 software implementation
or its later independent audit/checkpoint.
