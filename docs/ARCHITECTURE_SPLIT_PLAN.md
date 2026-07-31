# AT21CS Architecture Split Plan

This is a planning document plus staged cleanup log. It records resolved and
remaining framework-boundary work for splitting the AT21CS01/AT21CS11 driver
into a framework-neutral protocol core plus timing-critical ESP32 backends. Do
not modify the static chip reference document as part of this plan.

## Current Leaks And Evidence

- Resolved in the staged transport-boundary pass: the public header no longer selects an ESP32 platform at include time, includes ESP32/FreeRTOS implementation headers, exposes `portMUX_TYPE`, or exposes named direct GPIO register pointers.
- Resolved in the staged transport-boundary pass: `Config.h` timing callback comments no longer encode Arduino/ESP-IDF fallback policy; fallbacks are an implementation/backend detail.
- Resolved in Phase 4: Arduino, ESP-IDF, FreeRTOS, CPU, ROM-delay, timer, and
  GPIO includes were moved out of `src/AT21CS.cpp` into the private
  `src/platform/esp32/AT21CSEsp32Backend.cpp` backend source.
- Resolved in Phase 4: built-in pin setup, presence-pin reads, direct GPIO
  register access, ESP32 critical sections, reset/discovery waveform timing,
  and default framework time/delay fallbacks are implemented in the private
  backend source instead of the core protocol source.
- Partially resolved in Phase 4: the ESP-IDF component still requires ESP-IDF
  driver packages for the built-in compatibility backend, but
  `AT21CS_PLATFORM_IDF=1` is private to the component target and the dependency
  no longer leaks through public headers or core source.
- Clarified in Phase 5: `Config::sioPin`, `presencePin`, and
  `presenceActiveHigh` remain for this major version as compatibility backend
  config only. They are valid only when `Config::transport == nullptr`; mixed
  transport-plus-pin configuration is rejected with `INVALID_CONFIG`.
- Clarified in Phase 6: the built-in ESP32 compatibility backend remains the
  default component path for this major version, but ESP-IDF transport-only
  consumers can set `AT21CS_ENABLE_ESP32_COMPAT_BACKEND=OFF` to remove ESP32
  GPIO/timer/FreeRTOS component dependencies. Making the backend default-off is
  deferred until explicit backend config replaces compatibility pin fields.

## Staged Transport Boundary Implemented

- Added `include/AT21CS/Transport.h` with a framework-neutral `SingleWireTransport` contract.
- Added `include/AT21CS/Core.h` as the clean public core include entry point.
- Added optional `Config::transport`; existing pin-based configuration remains supported for compatibility.
- Injected transports are required to provide byte-level `writeByteReadAck`, `readByteSendAck`, and `resetAndDiscover` primitives so high-speed AT21CS timing remains owned by the backend rather than by callback-heavy bit loops.
- The legacy built-in ESP32/Arduino backend is now source-separated into
  `src/platform/esp32/AT21CSEsp32Backend.cpp`; this stage does not claim a
  public backend-object API or hardware timing validation.
- `tools/check_core_timing_guard.py` now rejects framework/platform tokens in clean public core headers.
- Phase 5 host tests exercise the injected byte-level transport path with a
  deterministic fake backend for reset/discovery, byte read/write sequencing,
  ACK/NACK phase errors, reset error propagation, and serial CRC validation.
- Phase 6 host tests add injected presence-policy coverage and bounded
  `waitReady()` timeout/health coverage.

## Proposed Core / Backend / API Layout

Target layout:

```text
include/AT21CS/
  Core.h                   - framework-neutral driver and protocol config
  Transport.h              - single-wire PHY/line callback contract
  Status.h
  Config.h                 - protocol-only config
  CommandTable.h
  Version.h                - generated
  backends/
    ArduinoEsp32Backend.h  - ARDUINO_ARCH_ESP32 timing/GPIO backend
    IdfEsp32Backend.h      - ESP_PLATFORM timing/GPIO backend
  AT21CS.h                 - compatibility umbrella/wrapper only
src/
  core/
    AT21CSCore.cpp
  platform/esp32/
    AT21CSEsp32Backend.cpp
  platform/arduino/
    AT21CSArduinoEsp32Backend.cpp
  platform/esp_idf/
    AT21CSIdfEsp32Backend.cpp
```

Core responsibilities:

- Own device state, address handling, command sequencing, ACK/NACK phase mapping, retries, memory/security-zone validation, health tracking, and AT21CS01/AT21CS11 policy.
- Preserve exact `Status` granularity for discovery, device-address NACK, memory-address NACK, data NACK, busy timeout, CRC mismatch, part mismatch, and unsupported command.
- Depend only on core headers and a transport/PHY contract. No Arduino, ESP-IDF, FreeRTOS, `soc/gpio_reg.h`, or IRAM attributes in core headers.

Backend responsibilities:

- Own SI/O GPIO configuration, optional presence GPIO, open-drain drive/release/read operations, timing-critical bit or byte primitives, critical sections, and microsecond timing.
- Provide optimized ESP32 paths where callback overhead would violate AT21CS timing windows.
- Map platform setup failures to `Status` without exposing raw framework errors in the core.

Transport/PHY contract:

- At minimum, expose reset/discovery, write-byte-with-ACK, read-byte-with-ACK/NACK, line release, presence read, monotonic time, and bounded microsecond wait primitives.
- Prefer byte-level primitives for ESP32 so sub-microsecond bit timing and critical sections stay in backend code.
- Allow a portable line-level fallback only when timing is proven by tests and hardware validation.

## Intentional Breaking Changes

- `Config::sioPin`, `presencePin`, and `presenceActiveHigh` should move to backend config; core config should describe protocol policy, not MCU pins.
- Until that major-version move, these fields are explicitly compatibility
  backend config and must not be combined with `Config::transport`.
- Null timing callbacks should not trigger framework fallbacks in the core. The backend supplies timing, or the caller supplies a transport with timing.
- Done: public `AT21CS.h` no longer defines `AT21CS_PLATFORM_ESP32` or includes
  ESP32/FreeRTOS headers.
- Done: `AT21CS_IRAM` and direct GPIO register access moved to backend
  implementation source, not core public headers.
- `begin(const Config&)` should become either `begin(const CoreConfig&, AT21CSTransport&)` or a `BeginConfig` that contains a transport pointer/reference.
- Done: ESP-IDF component registration keeps `AT21CS_PLATFORM_IDF=1` private to
  the component target.

## Migration Path

- Introduce the new core and backend headers beside the current `AT21CS::Driver`.
- Keep current Arduino sketch behavior through a compatibility wrapper only if the wrapper privately owns an Arduino ESP32 backend and forwards to the core.
- Provide explicit backend construction examples:
  - Arduino ESP32: configure SI/O/presence pins in `AT21CSArduinoEsp32BackendConfig`, then pass its transport to the core driver.
  - ESP-IDF: configure SI/O/presence GPIOs in `AT21CSIdfEsp32BackendConfig`, then pass its transport to the core driver from `app_main`.
- Update examples to stop relying on framework fallbacks in core config.
- Keep `Status` return shape stable where possible; config and begin signatures are the main expected breaking changes.
- Do not modify `docs/AT21CS01_AT21CS11_complete_driver_report.md` unless separately requested.

## Compatibility Wrappers Only If Clean

Compatibility wrappers are acceptable only when they:

- Keep Arduino/ESP-IDF/FreeRTOS includes out of core headers.
- Own backend state privately without heap allocation in steady-state operation.
- Preserve all current `Status` codes and ACK/NACK phase detail.
- Do not expose ESP32 critical-section or direct-register fields through public core class layout.

If the current `AT21CS::Driver` name cannot be kept without reintroducing platform fields in the public header, introduce a new core driver type and mark the old include/API as a major-version compatibility wrapper.

## Staged Sequence

1. Add a core header boundary test that includes core headers with no Arduino, ESP-IDF, FreeRTOS, or ESP32 SoC headers.
2. Define `AT21CSTransport` / PHY contract and fake backend tests before moving protocol logic.
3. Move command sequencing, validation, health tracking, serial CRC, memory/security-zone logic, and busy-poll deadlines into `src/core/`.
4. Done in Phase 4: move ESP32 GPIO/timing, critical sections,
   cycle-counter delay, and direct-register access into platform backend
   source.
5. Phase 5 compatibility decision complete: keep pin fields for this major
   version, document them as built-in backend config, and reject mixed
   transport-plus-pin setup.
6. Convert Arduino and ESP-IDF examples to explicit backend construction.
7. Phase 6 partial: ESP-IDF component registration now has an
   `AT21CS_ENABLE_ESP32_COMPAT_BACKEND` cache option. It defaults ON for
   compatibility, but can be disabled for transport-only applications. The
   future major split should still move to explicit backend construction and
   make the backend dependency opt-in by API, not only by build flag.
8. Update CLI/IDF contract tools, README, IDF docs, and changelog.
9. Deprecate legacy config fields and remove them only in the planned major release.

## Tests Required

- Header-boundary test for `Core.h`, `Config.h`, `Status.h`, `Transport.h`, and `CommandTable.h` without platform headers.
- Fake PHY tests for discovery success/failure, device-address NACK, memory-address NACK, data NACK, write busy timeout, read/write bounds, page-write boundaries, serial CRC, manufacturer ID, security-zone lock state, and AT21CS11 Standard Speed rejection.
- Health-state tests for success reset, consecutive failures, offline threshold, manual recovery, sleeping/fault preconditions, and validation errors not counted as transport failures.
- Timing-contract tests for backend primitives where host tests can simulate call order and bounded waits.
- Existing native tests currently represented by `test/test_basic.cpp`, updated
  to use fake transport instead of Arduino/Wire stubs for core coverage. Phase
  5 covers reset/discovery success and failure, byte read/write sequencing,
  device-address NACK, memory-address NACK, data NACK, reset transport error
  propagation, and serial CRC pass/fail paths. Phase 6 adds presence callback
  preflight and `waitReady()` busy-timeout coverage.
- Arduino PlatformIO builds for ESP32-S2 and ESP32-S3 CLI examples.
- ESP-IDF native example builds and `tools/check_idf_example_contract.py`.
- CLI contract tests using `tools/check_cli_contract.py`.

## Hardware Validation Sequence

1. ESP32-S3 Arduino with AT21CS11: discovery, high-speed mode, serial-number read/CRC, manufacturer ID, current-address read, EEPROM read/write/page write, and `waitReady()` timeout bounds.
2. ESP32-S2 Arduino: repeat smoke path and run timing measurement commands.
3. ESP32-S3 ESP-IDF native example: repeat discovery, read/write, serial CRC, and busy-poll validation from `app_main`.
4. ESP32-S2 ESP-IDF native example: repeat IDF smoke path.
5. AT21CS01-specific validation: Standard Speed activation, high-speed transition, security-zone operations, and supported ROM/lock operations.
6. AT21CS11-specific validation: confirm Standard Speed requests fail cleanly with `UNSUPPORTED_COMMAND`.
7. Presence-pin validation with present, absent, active-high, and active-low configurations.
8. Fault/recovery validation: held SI/O low, missing pull-up, missing device, NACK at each phase, write busy timeout, power-cycle during t_WR, and manual `recover()`.
9. Stress loops for mixed reads/writes/security operations with health counter deltas and no unbounded waits.

## Generated Version.h Caveat

`include/AT21CS/Version.h` is generated from `library.json`; `include/AT21CS/Version.h:5-6` explicitly says not to edit it manually. Any split release that changes config or begin signatures should update `library.json`, regenerate `Version.h`, and document the SemVer impact in `CHANGELOG.md`.
