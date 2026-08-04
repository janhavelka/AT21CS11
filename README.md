# AT21CS01 / AT21CS11 synchronous driver

`AT21CS01_AT21CS11` is a fixed-state C++ driver for the Microchip AT21CS01 and
AT21CS11 1-Kbit single-wire EEPROMs. Version `2.0.0-rc.1` is a release
candidate: its software gates are exercised on the host and on pinned
ESP32-S2/S3 Arduino builds, but physical qualification remains pending.

The core API is framework-neutral. The shipped hardware Backend supports
Arduino on ESP32-S2 and ESP32-S3 through PioArduino
`platform-espressif32` 55.03.311 (Arduino-ESP32 3.3.11). No second firmware
framework is shipped or supported.

The authoritative protocol source is Microchip DS20005857I:

- [AT21CS01/AT21CS11 datasheet](https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/AT21CS01-AT21CS11-1-Kbit-Serial-EEPROM-Data-Sheet-DS20005857.pdf)
- verified local size: `2247216` bytes
- verified local SHA-256:
  `704577264C3B6C60B2D14BE83A229F34C86433CC8951516641FB1DE9EC5DB1A5`

## Operating model

Every library call is synchronous: it validates its complete input, performs
bounded work and returns a `Status` plus any documented result evidence. Once a
call starts, firmware cannot asynchronously cancel it.

The library creates no task, queue, scheduler, application-facing mutex, retry
loop, hot-plug poller, logger or persistence service. The ESP32 Backend uses
private bounded timing-critical facilities only to preserve physical bit
timing. Firmware owns serialization, scheduling, retries, backoff, identity
association, logging, persistence and safety policy.

A page write consists of one bounded frame followed by the Bus-owned fixed
10 ms released-high software hold. The Bus does not ACK-poll. Firmware that
needs predictable scheduling should prefer `Driver::writeEepromPage()` over a
multi-page write and schedule other work after the call returns.

## Object ownership

One physical SI/O wire has exactly one Backend and one Bus. One to eight
Drivers with unique three-bit addresses may share that Bus:

```text
SI/O pin -> Esp32Transport -> Bus -> Driver address 0
                                -> Driver address 1
```

Two physical pins use two independent tuples and may both use address zero:

```text
pin A -> Backend A -> Bus A -> Driver A (address 0)
pin B -> Backend B -> Bus B -> Driver B (address 0)
```

Reset generation, address claims and the 10 ms write hold are shared only by
Drivers on the same Bus. State never leaks between independent Buses. A tuple
may be called a *wire instance* in application documentation; there is no
public channel abstraction.

Objects are externally owned, non-copyable and non-movable. Start them in this
order and shut them down in reverse order:

1. `Esp32Transport::begin()`
2. `Bus::bind()`
3. `Driver::begin()` or `Driver::bind()` followed by `Driver::initialize()`
4. `Driver::end()`
5. successful `Bus::end()`
6. `Esp32Transport::end()`

Keep the Backend alive if `Bus::end()` fails, then retry the Bus shutdown
explicitly.

## Public API at a glance

- Backend lifecycle: `Esp32Transport::begin()`, `Esp32Transport::end()`,
  `Esp32Transport::isInitialized()` and `Esp32Transport::descriptor()`.
- Bus lifecycle/diagnostics: `Bus::bind()`, `Bus::end()`,
  `Bus::hasPresenceIndicator()`, `Bus::readPresenceIndicator()`,
  `Bus::generation()` and `Bus::snapshot()`.
- Driver lifecycle: `Driver::bind()`, `Driver::initialize()`,
  `Driver::begin()`, `Driver::recover()`, `Driver::end()` and
  `Driver::probe()`.
- EEPROM: `Driver::readEeprom()`, `Driver::writeEepromPage()` and
  `Driver::writeEeprom()`.
- Security: `Driver::readSecurity()`, `Driver::writeSecurityUserPage()`,
  `Driver::writeSecurityUser()`, `Driver::readSecurityLockState()` and
  `Driver::permanentlyLockSecurity()`.
- Identity and speed: `Driver::readSerialNumber()`,
  `Driver::readManufacturerId()` and `Driver::setSpeedMode()`.
- ROM protection: `Driver::readRomZoneState()`,
  `Driver::permanentlyEnableRomZone()` and
  `Driver::permanentlyFreezeRomZones()`.
- Cached inspection: `Driver::isBound()`, `Driver::isInitialized()`,
  `Driver::isOnline()`, `Driver::state()`, `Driver::lastStatus()`,
  `Driver::lastError()` and `Driver::snapshot()`.

See the installed headers under `include/AT21CS/` for exact signatures and
value types.

## Minimal Arduino setup

```cpp
#include <Arduino.h>
#include "AT21CS/AT21CS.h"
#include "AT21CS/platform/esp32/Esp32Transport.h"

AT21CS::Esp32Transport backend;
AT21CS::Bus bus;
AT21CS::Driver device;

void setup() {
  AT21CS::Esp32TransportConfig backendConfig{};
  backendConfig.sioPin = 6;
  backendConfig.presencePin = -1;

  AT21CS::Status status = backend.begin(backendConfig);
  if (!status.ok()) return;

  AT21CS::BusConfig busConfig{};
  busConfig.transport = backend.descriptor();
  status = bus.bind(busConfig);
  if (!status.ok()) {
    backend.end();
    return;
  }

  AT21CS::Config deviceConfig{};
  deviceConfig.addressBits = 0;
  deviceConfig.expectedPart = AT21CS::PartType::AT21CS11;
  status = device.begin(bus, deviceConfig);
}

void loop() {
  // Call synchronous operations here. Firmware decides the cadence.
}
```

The pin in this snippet is an application choice, not a library default or a
board guarantee.

## Hot-plug behavior

An absent device does not destroy valid Backend, Bus or Driver bindings. Later
recovery can reuse the same objects and device configuration.

### Optional detect input

`Esp32TransportConfig::presencePin == -1` disables detection. An enabled valid
GPIO must differ from SI/O. `presenceActiveHigh` selects whether high or low
means logically present. The Backend configures this as a plain input with no
internal pull; board hardware must provide a stable external bias.

`Bus::readPresenceIndicator()` takes one bounded raw logical Bus-wide sample.
When disabled it returns `UNSUPPORTED_COMMAND`; logical false means absent;
callback faults remain errors rather than being converted to absence. The
sample is a connector/Bus hint, not proof of a chip or address.

Firmware should debounce the input and call `Driver::recover()` once after a
stable attachment. The examples sample every 20 ms and require 100 ms of
stability. A recovery Reset affects every Driver sharing the Bus, and a single
detect input cannot distinguish their addresses.

### No detect input

At each bounded polling event firmware may make exactly one liveness action:

- call `Driver::probe()` while initialized in `READY` or `DEGRADED`;
- call `Driver::recover()` while uninitialized or `OFFLINE`.

The examples use a configurable 1,000 ms default. `probe()` checks liveness; it
does not replace Reset/Discovery recovery after power-up. Transport failures
may leave a Driver `DEGRADED` until its configured `offlineThreshold` is
reached.

Without a detect signal, idle removal is unknowable until an explicit operation
or scheduled probe fails. Removal and replacement entirely between polls may be
missed. A replacement that returns before the Driver reaches `OFFLINE` may also
be unobservable. After successful recovery, firmware may read and compare the
serial number before using application-owned data associated with the previous
device.

The library never wakes itself, debounces, retries, tracks attachment
generations or decides replacement policy.

## RTOS integration

The safe default is one firmware task or cooperative loop owning all AT21CS
objects and calling them sequentially. Drivers sharing one Bus must always have
the same owner.

Other application tasks may exchange copied application-defined messages with
that owner. Queue capacity, priorities, deadlines, backoff and result routing
remain firmware policy. The library ships no RTOS wrapper or owner framework.

Simultaneous calls from multiple tasks, even to separate ESP32 Backend
instances, are outside the current qualification. Separate tasks should be used
only after that exact Backend and hardware arrangement has been independently
qualified for concurrent timing.

## Errors, reads and writes

Validation and state errors are distinct from a device NACK and from transport
timeout, stalled clock, stuck line or I/O failure. `Status.detail` preserves the
protocol phase and byte index where applicable. Callers should branch on the
returned status instead of guessing from Driver state.

Reads are address-explicit random reads; there is no current-address operation
and no I2C-style scan. A successful output is valid only when its `Status` is
OK. Each completed transport frame is copied transactionally.

Known RC limitation A-23: for `readEeprom()` or `readSecurity()` requests longer
than one eight-byte frame, a later-frame failure can leave an earlier completed
prefix in the caller buffer. Treat the entire buffer as invalid whenever the
call fails. The shipped examples display read data only on success. The final
audit owns removal of this limitation without changing the public API.

`WriteResult` records a proven committed prefix, the last page's accepted-byte
evidence and whether that page may have committed. `MutationResult` similarly
distinguishes not attempted, possibly committed, accepted and verified
outcomes. Firmware must never automatically replay a possibly committed
mutation.

AT21CS11 supports High-Speed only; a Standard-Speed request fails before device
I/O. Security Lock, ROM-zone enable and ROM Freeze are irreversible
service/provisioning actions. The shipped examples do not expose them.

The fixed 10 ms hold is the library's conservative software policy, not a claim
that every voltage, temperature, pull-up, board or waveform has been physically
qualified. Physical timing and electrical qualification belong to the final
HIL gate.

## Examples and configuration

The repository and package contain exactly two Arduino examples:

- [single-device CLI](examples/01_basic_bringup_cli/main.cpp)
- [two-wire/two-device CLI](examples/02_multi_device_cli/main.cpp)

Both use [shared bounded helpers](examples/common/BoardConfig.h). Their
build-time overrides are:

| Setting | Primary | Secondary | Committed default |
|---|---|---|---|
| SI/O GPIO | `AT21CS_EXAMPLE_PRIMARY_SIO_PIN` | `AT21CS_EXAMPLE_SECONDARY_SIO_PIN` | `6`, `10` |
| detect GPIO | `AT21CS_EXAMPLE_PRIMARY_PRESENCE_PIN` | `AT21CS_EXAMPLE_SECONDARY_PRESENCE_PIN` | `-1`, `-1` |
| detect active-high | `AT21CS_EXAMPLE_PRIMARY_PRESENCE_ACTIVE_HIGH` | `AT21CS_EXAMPLE_SECONDARY_PRESENCE_ACTIVE_HIGH` | `1`, `1` |
| address bits | `AT21CS_EXAMPLE_PRIMARY_ADDRESS_BITS` | `AT21CS_EXAMPLE_SECONDARY_ADDRESS_BITS` | `0`, `0` |
| expected part (`1` or `11`) | `AT21CS_EXAMPLE_PRIMARY_PART` | `AT21CS_EXAMPLE_SECONDARY_PART` | `11`, `11` |

Override these with PlatformIO `build_flags`. Enabled detect pins require the
external bias described above.

Build environments are `ex_cli_s3`, `ex_cli_s2`, `ex_multi_s3` and
`ex_multi_s2`.

## Package and development checks

`library.json` is the version source of truth. `Version.h` is generated
deterministically:

```text
python scripts/generate_version.py
python scripts/generate_version.py --check
```

The package export contains only public headers, sources, metadata, migration
notes and the two examples. The principal local gates are:

```text
.\scripts\pio.cmd test -e native
.\scripts\pio.cmd test -e native_sanitize
python tools/check_cli_contract.py
python tools/check_docs.py
python tools/check_package.py --inspect
python tools/check_package.py --build-platform-neutral
python tools/check_package.py --build-arduino
```

See [migration notes](docs/MIGRATION.md), [changelog](CHANGELOG.md), the
[contribution guide](https://github.com/janhavelka/AT21CS11/blob/main/CONTRIBUTING.md)
and [security policy](https://github.com/janhavelka/AT21CS11/blob/main/SECURITY.md).

## License

MIT; see [LICENSE](LICENSE).
