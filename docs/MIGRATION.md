# Migration

## From v2.0.0 to Unreleased

- **Arduino.** Keep the existing PioArduino platform and Backend, Bus and
  Driver bindings. Both Arduino examples retain their commands and output.
- **Native ESP-IDF.** The component and single-device CLI use the same
  ESP32 Backend. The manifest requires ESP-IDF 6.0.1 or later. Builds cover
  6.0.1 on ESP32-S2 and ESP32-S3. Pin the owner task to one core.
  [ESP-IDF integration guide](ESP_IDF.md)
- **AT21CS01 Standard Speed.** Give each Standard-Speed device an exclusive
  Bus claim on its physical wire. Budget up to about 5.8 ms of interrupt
  masking for one segment and check that the external pull-up meets the
  [rise-time limits](../README.md#esp32-pull-up-and-rise-time-envelope).

The [Unreleased changelog](../CHANGELOG.md) lists the fixes since v2.0.0.

## From v1 to v2.0.0

Version 2 is intentionally API-breaking. It has one synchronous production
path and no compatibility facade. Refactor ownership once instead of wrapping
the removed API.

### Ownership

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

### Removed behavior

There is no `tick()`, current-address read, ready poll, scan, hidden discovery
retry, internal reconnect loop or pin-owning Driver constructor in v2. Reads
are address-explicit. If a device accepted or may have accepted a payload byte,
the Bus holds SI/O released high for 10 ms. If the hold fails, the Bus retains
it and must complete it before the next transfer.

Use `Driver::probe()` for one explicit liveness check and `Driver::recover()`
for one explicit Reset/Discovery recovery attempt. Firmware owns their cadence
and any retry/backoff.

### Hot-plug

An initialization absence retains valid bindings and the address claim. Leave
`presencePin == -1` for a fixed device or when no detect signal exists. Polling
is optional firmware policy and is needed only when the application wants to
detect hot-plug without a separate signal. See the README's hot-plug section
for detect polarity, debounce and explicit `probe()`/`recover()` guidance.

### RTOS applications

Use one firmware task or cooperative loop as the default owner of every AT21CS
object. Drivers sharing a Bus must share the same owner. The README describes
the supported synchronous ownership model; the library ships no asynchronous
wrapper.

### Status and mutation handling

All fallible APIs return `Status`. Keep validation failures, NACK phase and
transport failures distinct. Do not infer success from state alone.

Page and range writes take `WriteResult`; irreversible APIs take
`MutationResult`. If evidence says an operation may have committed, do not
replay it automatically. Before using Security Lock, ROM-zone enable or ROM
Freeze, follow [the irreversible-operation guide](IRREVERSIBLE_OPERATIONS.md).

EEPROM and Security reads are whole-call transactional. They use fixed-size
scratch storage and leave the caller buffer unchanged if any frame fails.
