# Migrating from v1 to v2

Version 2 is intentionally API-breaking. It has one synchronous production
path and no compatibility facade. Refactor ownership once instead of wrapping
the removed API.

## Ownership

The v1 Driver owned pins and transport behavior. Version 2 separates these
responsibilities:

```text
Esp32Transport -> Bus -> Driver(s)
```

Create one `Esp32Transport` and one `Bus` for each physical SI/O pin. Bind one
to eight uniquely addressed Drivers to that Bus. Separate pins need separate
tuples and may reuse address zero.

Move pin settings from the old Driver configuration to
`Esp32TransportConfig`. The v2 `Config` contains only per-device address, part,
speed and health settings.

```cpp
AT21CS::Esp32Transport backend;
AT21CS::Bus bus;
AT21CS::Driver driver;

AT21CS::Esp32TransportConfig backendConfig{};
backendConfig.sioPin = 6;
backendConfig.presencePin = -1;

AT21CS::Status status = backend.begin(backendConfig);
if (status.ok()) {
  AT21CS::BusConfig busConfig{};
  busConfig.transport = backend.descriptor();
  status = bus.bind(busConfig);
}
if (status.ok()) {
  AT21CS::Config deviceConfig{};
  deviceConfig.addressBits = 0;
  deviceConfig.expectedPart = AT21CS::PartType::AT21CS11;
  status = driver.begin(bus, deviceConfig);
}
```

Shutdown is `Driver::end()`, successful `Bus::end()`, then
`Esp32Transport::end()`. Keep the Backend alive while Bus shutdown is pending.

## Removed behavior

There is no `tick()`, current-address read, ready poll, scan, hidden discovery
retry, internal reconnect loop or pin-owning Driver constructor in v2. Reads
are address-explicit. Writes return only after the bounded frame and Bus-owned
fixed 10 ms released-high hold.

Use `Driver::probe()` for one explicit liveness check and `Driver::recover()`
for one explicit Reset/Discovery recovery attempt. Firmware owns their cadence
and any retry/backoff.

## Hot-plug

An initialization absence retains valid bindings and the address claim.

- With a detect signal, set `Esp32TransportConfig::presencePin` and
  `presenceActiveHigh`, debounce `Bus::readPresenceIndicator()` in firmware and
  call `Driver::recover()` after stable attachment.
- Without a detect signal, leave `presencePin == -1`. At each bounded polling
  event, call `probe()` once while initialized/online or `recover()` once while
  uninitialized/offline. The shipped examples use 1,000 ms.

The detect signal is a raw Bus-wide hint, not identity. The library creates no
poller. After recovery, compare `Driver::readSerialNumber()` with
application-owned identity before restoring data associated with the old
device.

## RTOS applications

Use one firmware task or cooperative loop as the default owner of every AT21CS
object. Drivers sharing a Bus must share the same owner. Application messages,
queues, deadlines and result routing remain application code; the library
ships no asynchronous wrapper.

Do not assume separate ESP32 Backends can be called simultaneously from
multiple tasks. That timing arrangement is outside current qualification.

## Status and mutation handling

All fallible APIs return `Status`. Keep validation failures, NACK phase and
transport failures distinct. Do not infer success from state alone.

Page and range writes take `WriteResult`; irreversible APIs take
`MutationResult`. If evidence says an operation may have committed, do not
replay it automatically.

RC limitation A-23 applies to reads longer than one eight-byte frame: a later
frame failure may leave an earlier completed prefix in the output buffer. Treat
every failed read buffer as invalid until the final-audit fix lands.

## Supported integration

The shipped adapter supports Arduino on ESP32-S2/S3 using the pinned PioArduino
platform. Core headers remain framework-neutral so a future Backend can be
implemented without changing Bus or Driver, but no other adapter is currently
implemented, packaged or qualified.
