# Migration Notes

## Unreleased Transport And Backend Boundary

This staged hardening pass keeps the compatibility pin fields for the current
major version. Existing Arduino and ESP-IDF consumers that use
`AT21CS::Driver` with `Config::sioPin` and optional `presencePin` can continue
using the built-in ESP32/Arduino backend.

New code may include `AT21CS/Core.h` or `AT21CS/Transport.h` when it needs the
clean core API or the explicit single-wire backend contract. `Config::transport`
is optional. When null, the existing built-in platform backend is used.

When `Config::transport` is set, the injected backend must provide byte-level
timing primitives:

- `writeByteReadAck`
- `readByteSendAck`
- `resetAndDiscover`
- `releaseLine`
- a microsecond wait source through `Config::sleepUs` or `SingleWireTransport::sleepUs`

The backend is responsible for preserving AT21CS timing at the SI/O pin,
including critical sections, interrupt masking, and fast line access. The core
does not add platform critical sections around injected byte primitives.

The built-in ESP32/Arduino compatibility backend has moved out of
`src/AT21CS.cpp` into `src/platform/esp32/AT21CSEsp32Backend.cpp`. The core
protocol source no longer includes Arduino, ESP-IDF, FreeRTOS, ESP32 GPIO, CPU,
timer, ROM-delay, or SoC headers. This is a source/backend split only; the
existing `Config::sioPin` compatibility path remains available when no
transport is injected.

For this release candidate the built-in ESP32 compatibility backend remains
enabled by default. That is intentional for the current major version because
the public Arduino and ESP-IDF examples still demonstrate the compatibility
`Config::sioPin` path. ESP-IDF applications that provide a complete
`SingleWireTransport` can configure the component with
`AT21CS_ENABLE_ESP32_COMPAT_BACKEND=OFF`; in that transport-only mode the
component omits the ESP32 GPIO/timer/FreeRTOS dependencies and any attempt to
use the pin-based path fails with `INVALID_CONFIG`.

`Config::sioPin`, `presencePin`, and `presenceActiveHigh` are now explicitly
defined as built-in backend compatibility config. They are used only when
`Config::transport == nullptr`. When `Config::transport` is set, leave these
fields unset (`sioPin = -1`, `presencePin = -1`) and provide presence checks
through `SingleWireTransport::presencePresent` if needed. Mixed
transport-plus-pin configuration now fails fast with `INVALID_CONFIG` so the
core never silently ignores pin policy.

## Compatibility

No required application code changes are introduced for callers using the
built-in compatibility backend. The existing pin fields remain supported, and
`begin(const Config&)` is unchanged.

Applications that adopted the unreleased injected transport path must avoid
also setting `sioPin` or `presencePin`. Move any presence policy into the
transport's `presencePresent` callback.

The ESP-IDF component no longer publishes `AT21CS_PLATFORM_IDF` to consumers.
Applications should not depend on that macro from public AT21CS headers.

Native tests use the injected `SingleWireTransport` path for no-hardware
coverage instead of depending on Arduino/Wire stubs for built-in GPIO behavior.
The fake transport now covers reset/discovery, byte read/write sequencing,
device-address, memory-address, and data NACKs, reset error propagation, and
serial-number CRC handling. Phase 6 adds fake-transport coverage for
`presencePresent` blocking `begin()` before protocol I/O and for bounded
`waitReady()` timeout/health behavior.

## Remaining Planned Breaking Work

The planned major-version split can still move `sioPin`, `presencePin`, and
presence polarity into backend-specific configuration and replace the current
compatibility `Config` with a protocol-only core config.
