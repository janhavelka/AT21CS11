# Shared AT21CS v2 contract

This contract describes the library implemented at the completed Stage-05
checkpoint. Public headers are the source of truth for exact declarations; this
file freezes the behavioral and ownership rules used by the remaining prompts.

## 1. Scope

The library drives Microchip AT21CS01 and AT21CS11 single-wire EEPROMs. It is a
synchronous, deterministic component. It exposes raw EEPROM/Security access,
identity, device state, diagnostics, and conservative mutation evidence.

It does not implement:

- an RTOS task, queue, scheduler, mutex, request/result DTO, or callback service;
- automatic retry, reconnect, presence debounce, or hot-plug polling;
- logging, telemetry, persistence, calibration, connector policy, or machine
  control;
- a native ESP-IDF integration path.

The public/core headers remain framework-neutral. The supported hardware
adapter is Arduino on ESP32-S2/S3 through the pinned PioArduino platform.

## 2. Public/source layout

```text
include/AT21CS/
  AT21CS.h
  Bus.h
  CommandTable.h
  Config.h
  Status.h
  Transport.h
  Types.h
  Version.h
  platform/esp32/Esp32Transport.h
src/
  AT21CS.cpp
  Bus.cpp
  TransferValidation.h
  platform/esp32/Esp32Transport.cpp
```

Do not add a compatibility facade, duplicate transport, owner class, channel
class, task API, or product-specific type to the library.

## 3. Synchronous call model

Public Bus/Driver operations and adapter lifecycle methods return
`AT21CS::Status`; Backend protocol callbacks return typed `TransferResult`
evidence. A synchronous public operation:

1. validates its complete input before device I/O;
2. performs its documented bounded synchronous work;
3. updates only the relevant Bus/Driver instance;
4. returns exact status and conservative write or mutation evidence. NACK
   phase/index is encoded in `Status.detail`; raw transport phase/evidence
   remains available in the Bus snapshots.

Once a call begins, it is not asynchronously cancelled. Firmware decides when
to call, whether to retry, and what to do with the result.

Ownership objects (`Esp32Transport`, `Bus`, and `Driver`) are non-copyable,
non-movable, and not thread-safe. Value types such as configuration, status,
descriptors, results, and snapshots remain copyable. The library allocates no
steady-state heap memory and creates no application-facing concurrency
primitive. A hardware Backend may use private low-level critical facilities
solely to protect bounded physical timing.

## 4. Object ownership and multiple devices

One physical SI/O wire owns exactly one external Backend and one Bus:

```text
Backend (pin and timing) -> Bus (wire-wide state) -> Driver(s) (devices)
```

`Esp32Transport` owns its pin configuration and physical timing. `Bus` owns
wire-wide binding, address claims, Reset generation, transfer diagnostics, and
the post-write released-high deadline. Each `Driver` owns one address, device
lifecycle, identity, configured/active speed, state, and health.

One to eight Drivers with unique `addressBits` may share one Bus. Devices on
separate pins use separate Backend/Bus tuples and may reuse the same address,
including address zero.

Examples:

```cpp
// Two devices sharing one physical wire.
AT21CS::Esp32Transport backend;
AT21CS::Bus bus;
AT21CS::Driver device0;
AT21CS::Driver device1;

// Two independent wires. Address zero may be used on both.
struct WireInstance {
  AT21CS::Esp32Transport backend;
  AT21CS::Bus bus;
  AT21CS::Driver driver;
};

WireInstance wireA;
WireInstance wireB;
```

There is no library-level “channel.” Documentation may say “wire instance” as
plain language for one statically owned tuple.

## 5. Serialization and RTOS use

The supported safe default is one firmware task or cooperative loop owning all
AT21CS objects and issuing one library call at a time. This works for multiple
devices on one Bus and for multiple independent Buses.

Other application tasks may communicate with that owner using application-owned
fixed-size messages. Queue capacity, priorities, deadlines, result routing, and
shutdown are not library contracts.

Multiple tasks may each own a different physical-wire tuple only when the
selected Backend explicitly qualifies simultaneous cross-instance execution.
The current reference ESP32 adapter is qualified for serialized calls, not for
simultaneous timing-critical transfers from separate tasks. Drivers sharing one
Bus always use the same firmware owner.

Backend use of short guarded Arduino-ESP32/FreeRTOS critical facilities to
execute a frame does not make the library asynchronous and does not authorize a
library-created task.

## 6. Lifecycle and hot-plug

Construction order is:

1. `Esp32Transport::begin()`;
2. `Bus::bind()` using the Backend descriptor;
3. `Driver::bind()` plus `Driver::initialize()`, or `Driver::begin()`.

Initialization failure caused by an absent device retains a valid Bus/Driver
binding and the address claim. Firmware does not need to reconstruct objects or
resupply configuration when the device is attached later.

Hot-plug recovery is explicit:

1. Firmware observes a connector hint or reaches its own bounded retry event.
2. Firmware calls `Driver::recover()` once.
3. `recover()` performs the required Reset/Discovery and initialization.
4. On success, firmware may call `readSerialNumber()` and compare the returned
   bytes with its application-owned previous identity before reusing associated
   application data.

`probe()` is a nondestructive liveness check, not power-up recovery. An optional
presence pin is an instantaneous diagnostic hint; it does not prove chip
identity and is not debounced by the library.

Ordinary operations contain no hidden Reset, Discovery, retry, or recovery.
Failure on one independent Bus does not alter another. On a shared Bus, a
physical Reset necessarily affects every attached Driver through the shared
Reset generation.

Shutdown order is Driver(s), fallible `Bus::end()`, then Backend. Firmware must
keep the Backend alive unless `Bus::end()` succeeds.

## 7. Status, state, and diagnostics

Use the exact enums, structures, defaults, and methods declared in current
public headers. Preserve these behavioral rules:

- validation/precondition errors remain distinct from protocol NACK and
  transport failure;
- NACK detail identifies the exact protocol phase and byte index;
- optional-presence or Reset/Discovery absence reports `NOT_PRESENT`; an
  identity address NACK remains `NACK_DEVICE_ADDRESS`; both retain the binding
  and fail offline rather than erasing configuration;
- failed recovery does not falsely report the Driver online;
- public outputs are initialized transactionally and are not partially exposed
  unless their result type explicitly records a committed prefix;
- each logical Driver call updates health at most once;
- counters saturate and successful calls do not erase the persistent last error;
- snapshots contain copied scalar state, never callback/context pointers.

## 8. Transport and protocol invariants

The authoritative protocol source is the verified DS20005857I artifact recorded
in the packet README.

The Backend executes one whole uninterrupted frame per transfer callback. Bytes
are MSb-first. Read frames host-NACK their final byte. Reset/Discovery uses the
documented request, response sample, and release-high verification. ACK/NACK
phases, timeout, stalled clock, line-stuck, and I/O failure remain distinct.

All deadline-bearing transfer/reset/wait/presence operations receive checked
finite deadlines and use bounded waits; `nowUs` is the Backend's monotonic clock
source rather than a deadline-bearing callback. GPIO is open-drain/released-high
safe on every exit. Core code contains no Arduino, ESP-IDF, FreeRTOS, GPIO,
board-pin, or logging dependency.

AT21CS11 is High-Speed only. An invalid Standard-Speed request fails before
device I/O. All ordinary reads are address-explicit random reads; no public
current-address API or I2C-style scan exists.

## 9. Writes and irreversible operations

EEPROM page size is eight bytes. Page APIs reject page crossing. Range APIs use
one shared bounded page-splitting implementation and stop on the first failure.

After a write may have been accepted, Bus keeps SI/O continuously released high
for the fixed 10 ms policy interval. It does not ACK-poll. The hold is Bus-wide
for shared-wire devices and does not affect an independent Bus.

Checked deadline arithmetic must not wrap or pass the reserved terminal
sentinel to a Backend. The existing `BusSnapshot::writeHighUntilUs` encoding is:

- `0`: no retained hold;
- `1..UINT64_MAX-1`: finite Backend-clock deadline;
- `UINT64_MAX`: permanent fail-closed post-acceptance poison.

Firmware normally does not interpret this field; Bus enforces it. It must never
compare the value with a different clock source or treat poison as a scheduling
timestamp.

`WriteResult` and `MutationResult` preserve committed-prefix, accepted-byte,
and ambiguous-effect evidence. A possibly committed operation is never replayed
automatically.

Lock, ROM-zone enable, and ROM Freeze APIs retain their explicit `permanently`
names and conservative verification semantics. Examples require strong exact
confirmation. Automated tests never execute a real irreversible mutation.

## 10. Determinism and responsibility boundary

Core and Backend remain fixed-size, bounded, synchronous, and free of hidden
policy. Firmware owns:

- tasking, queues, mutexes, serialization, and deadlines outside a call;
- retry/backoff and hot-plug detection cadence;
- identity association, persistence, calibration schema, and replacement
  policy;
- connector pin maps, pull-ups, level shifting, cabling, and electrical
  qualification;
- logging, telemetry, machine control, and safety response.

The library reports what happened. Firmware decides what happens next.
