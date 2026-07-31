# Prompt 02 — Driver lifecycle, state, reads, identity, and speed

## Outcome

Complete the non-mutating half of the v2 Driver on top of Prompt 01's frozen Bus
and frame transport. Remove all hidden Reset/Discovery behavior, make
absent-at-boot recovery work, and implement exact state/health semantics.

Do not redesign Bus, transport structs, write evidence, or platform timing.

## Required working method

Read `AGENTS.md`, `00_SHARED_V2_CONTRACT.md`,
`FINDINGS_REGISTRY.md`, and the completed Prompt 01 diff. Preserve unrelated
changes.

Spawn subagents for:

1. lifecycle/state transition audit;
2. read/identity/speed frame audit against DS20005857I;
3. fault-injection test review.

Keep one integrator for `AT21CS.h` and `AT21CS.cpp`. Reuse Prompt 01's raw frame
helper; do not create feature-specific transport paths. Fix each finding by
refactoring its root, delete superseded paths, and simplify shared lifecycle
logic instead of stacking conditions or compatibility band-aids. Do not modify
the protected report. Do not commit or publish.

## Sole owned findings

Close:

- P-03, P-04, P-05 core half, P-09, P-11, P-16, P-19;
- A-02, A-04, A-08, A-09, A-10, A-11, A-13, A-14 Driver half,
  A-15, A-16.

## File scope

Primary:

```text
include/AT21CS/AT21CS.h
include/AT21CS/Config.h
src/AT21CS.cpp
test/test_driver_lifecycle.cpp
test/test_driver_reads.cpp
```

Touch `Bus`/transport only if a failing test proves Prompt 01 violated its frozen
contract. Report such a change explicitly.

## Exact implementation instructions

### 1. Lifecycle API

Implement exactly:

```cpp
Status bind(Bus& bus, const Config& config);
Status initialize();
Status begin(Bus& bus, const Config& config);
Status recover();
void end();
```

`bind()`:

- validates `bus.isBound()`;
- validates `addressBits <= 7`;
- validates enum values;
- rejects Standard Speed unless `expectedPart==AT21CS01`; in particular,
  `UNKNOWN+STANDARD_SPEED` is invalid;
- validates the complete replacement before changing the current binding;
- transactionally claims the requested A2:A0 through Bus. If another Driver on
  that Bus owns it, return `INVALID_CONFIG` with the address in detail and
  preserve the old binding/claim;
- permits the same Driver to rebind its already claimed Bus/address;
- acquires any new claim before releasing its previous claim;
- performs zero transport callback/physical I/O;
- copies scalar Config and stores non-owning Bus pointer;
- caches the Bus's current valid binding epoch and Reset generation;
- resets device-local health/cache;
- sets bound true, initialized false, state UNINIT.

`begin()`:

- validates/binds, then calls `initialize()`;
- if initialize fails, retains the new valid binding and exact error;
- must not call `end()` on a valid old binding before replacement validation.

`initialize()`:

1. requires bound, uninitialized `UNINIT`; every other state is an untracked
   `INVALID_STATE` with zero I/O;
2. checks optional presence indicator once; false means NOT_PRESENT/OFFLINE;
3. sets PROBING;
4. performs exactly one Bus Reset+Discovery;
5. if present, sets INIT_CONFIG;
6. reads Manufacturer ID;
7. classifies the part after masking revision bits D2:D0, caches the complete
   raw ID and silicon revision, and validates expected part;
8. applies configured Standard Speed only for AT21CS01;
9. sets initialized/READY only after all steps succeed.

No internal retry or backoff.
A Manufacturer-ID first-device-address NACK during initialize preserves exact
NACK code/detail but finishes uninitialized/OFFLINE immediately.

`recover()`:

- allowed whenever bound and state is UNINIT, READY, DEGRADED, or OFFLINE;
- sets RECOVERING;
- clears initialized before the Reset attempt and restores it only after the
  complete sequence succeeds;
- performs the same single Reset+Discovery, identity, and speed sequence;
- success -> READY/initialized;
- definite absence -> OFFLINE;
- a Manufacturer-ID first-device-address NACK preserves its exact
  `NACK_DEVICE_ADDRESS` Status/detail but is definite addressed-target absence
  and enters OFFLINE immediately;
- failed recovery entered from OFFLINE remains OFFLINE;
- part mismatch -> FAULT;
- successful recovery adopts the Bus binding epoch and Reset generation;
- retains the exact returned Status and transport detail.

`end()`:

- releases its Bus address claim, then clears Driver binding/cache/state;
- performs no transport/backend callback or physical I/O;
- is idempotent.

### 2. Central state helpers

Add and use exactly these state-ownership helpers from the shared contract:

```cpp
bool _canUseNormalIo() const;
Status _requireBound() const;
Status _requireInitializedForIo() const;
void _setState(DriverState state, bool initialized);
void _enterOperation(DriverState transient);
void _finishOperation(
    const Status& status,
    OperationKind kind,
    DriverState entryState);
void _resetLocalState();
```

Normal reads/speed writes are admitted only in READY or DEGRADED. `probe()` is
also admitted from initialized OFFLINE. BUSY/PROBING/INIT_CONFIG/RECOVERING/
SLEEPING/FAULT reject with zero I/O unless the lifecycle method explicitly owns
that transition.

Do not scatter direct `_state =` assignments outside lifecycle helpers.
Validation/precondition/no-op exits occur before `_enterOperation()` and never
call `_finishOperation()`. Each operation that reaches a device-facing
callback/frame finishes exactly once. Initialization/recovery clear initialized
on failure; normal/mutation failures retain it. Capture `entryState` before
entering a transient and apply the exact `OperationKind` table from the shared
contract. Do not invent per-call Boolean policy flags or infer the prior stable
state from `PROBING`, `RECOVERING`, or `BUSY`.

### 3. Health contract

Implement exactly the shared contract:

- one health update per public logical operation that reaches a device-facing
  callback/frame;
- raw helpers never track;
- counters saturate;
- `lastStatus` is the most recent tracked result;
- `lastError` persists across later success;
- validation/precondition/unsupported-without-I/O is untracked;
- CRC/product ID/verify mismatches are tracked logical failures;
- `offlineThreshold==0` disables threshold classification;
- NOT_PRESENT enters OFFLINE immediately.

Use `uint64_t nowUs()` from Bus; do not add a second timing callback to Config.
`isOnline()` additionally requires a bound/current Bus epoch and known speed.

### 4. Bus binding/generation synchronization

Before each admitted normal command:

```cpp
Status _synchronizeBusState(bool restoreConfiguredSpeed);
```

Behavior:

- invalid/currently unbound Bus or invalid binding epoch: NOT_BOUND/INVALID_STATE
  with zero frames;
- changed binding epoch:
  - set `speedKnown=false`;
  - return INVALID_STATE with zero frames;
  - only explicit `recover()` may adopt the current epoch;
- same generation: no action;
- changed generation plus Bus known High-Speed:
  - set active speed HIGH_SPEED;
  - set `speedKnown=true`;
  - adopt generation;
  - apply configured STANDARD_SPEED before the intended command when this
    device is a detected AT21CS01 and `restoreConfiguredSpeed=true`;
- changed generation plus unknown Bus speed:
  - set `speedKnown=false`;
  - return INVALID_STATE without a frame; caller must recover;
- no normal command is admitted while `speedKnown=false`.

This synchronization must never Reset.

Initialization/recovery, which explicitly own Reset, adopt the current Bus
epoch/generation immediately after a successful Reset+Discovery and establish
known High-Speed before the identity read. `setSpeedMode()` passes
`restoreConfiguredSpeed=false`, so a request for High-Speed after another
Driver's Reset does not wastefully restore the old Standard setting first.

### 5. Read/probe frame helpers

Use a small set of raw untracked helpers:

```cpp
Status _readRandomRaw(
    uint8_t opcode, uint8_t address,
    uint8_t* data, size_t length);

Status _readDirectRaw(
    uint8_t opcode,
    uint8_t* data, size_t length);

Status _readManufacturerIdRaw(uint32_t& manufacturerId);
Status _classifyManufacturerIdRaw(
    uint32_t manufacturerId,
    PartType& part,
    uint8_t& siliconRevision);
Status _setSpeedModeRaw(
    SpeedMode mode, TransferResult& transferResult);
```

All feature reads reuse these helpers.
Each raw read receives into one local eight-byte scratch array and copies into
its destination only after the complete frame succeeds. Do not pass caller
buffers directly to transport.

`readEeprom()`:

- validates non-null, length >0, and entire `size_t` range before I/O;
- address range 0x00..0x7F;
- chunks into random-read frames of at most 8 bytes;
- final byte of every frame is host NACK;
- returns the first failure; complete earlier chunks remain copied, while the
  failed chunk and all later bytes remain untouched.

`readSecurity()`:

- same behavior for 0x00..0x1F.

`readManufacturerId()`:

- direct opcode C read of exactly three bytes;
- assembles big-endian 24-bit result;
- output is zeroed before work and remains zero on failure.

Manufacturer classification:

- use `(manufacturerId & 0x00FFFFF8u)`, not whole-value equality;
- `0x00D200` classifies AT21CS01 and `0x00D380` classifies AT21CS11;
- cache the complete raw 24-bit ID and
  `siliconRevision = manufacturerId & 0x07u`;
- an unknown masked code returns PART_MISMATCH with the complete raw ID in
  detail;
- a known part that differs from explicit `expectedPart` returns PART_MISMATCH
  and enters FAULT during initialize/recover;
- `probe()` does not silently change a previously detected part. A different
  known part is a tracked PART_MISMATCH; explicit recover/rebind owns identity
  replacement.

`readSerialNumber()`:

- zeroes the output;
- calls raw Security read once through the public logical operation, not nested
  tracked `readSecurity()`;
- requires byte 0 `0xA0`; mismatch returns `PART_MISMATCH` with the observed
  byte in `Status::detail`;
- validates CRC-8/Maxim of bytes 0..6; mismatch returns `CRC_MISMATCH` with
  `detail = (computedCrc << 8) | storedCrc`;
- tracks one final public result.

All scalar outputs are initialized before validation/state checks. This includes
zeroing Manufacturer ID and setting every boolean output owned by later stages
to false; the shared contract is authoritative for their exact policy.

`probe()`:

- performs a non-destructive Manufacturer ID read using cached/known speed;
- performs no Reset or retry;
- validates the masked detected/expected part and records raw ID/revision only
  after successful classification;
- is tracked and may restore OFFLINE to READY on success;
- a failed probe entered from OFFLINE remains OFFLINE;
- Manufacturer-ID first-device-address NACK moves any admitted probe to OFFLINE
  immediately while preserving NACK code/detail;
- never changes configured speed.

`probe()` is a liveness/identity check only. It is not a reconnect operation:
after known/possible removal, SI/O power loss, a new attachment, or a rising
connector-presence indication, the device may have powered up in High-Speed and
requires the mandatory Reset/Discovery handshake. Upper firmware calls explicit
`recover()` in those cases, then reads the serial number and reconciles
attachment identity. Do not make `probe()` silently fall back to Reset.

`Bus::readPresenceIndicator()` is the only public presence API:

- returns UNSUPPORTED_COMMAND with zero I/O if Bus transport has no callback;
- otherwise reports exact callback status;
- does not treat `present=false` as transport failure;
- does not substitute for Driver protocol `probe()`;
- never changes Driver health or state.

Presence is only a physical connector/module hint and is not per-address proof.
Outside lifecycle methods, observing `present=false` does not automatically
change any Driver state; upper firmware decides whether and when to stop
admitting work and schedule `recover()`.

### 6. Speed API

Only expose:

```cpp
Status setSpeedMode(SpeedMode mode);
SpeedMode speedMode() const;
```

Rules:

- validate enum before I/O;
- known AT21CS11 + Standard -> UNSUPPORTED_COMMAND, zero frames, no health change;
- first synchronize Bus epoch/generation with
  `restoreConfiguredSpeed=false`;
- if speed is unknown -> INVALID_STATE, zero frames, explicit recover required;
- if requested mode already equals known active mode -> OK, zero frames, commit
  that mode to `_config.startupSpeed`, and do not mutate health/state;
- otherwise send one address-only opcode D or E at current active timing;
- require device ACK;
- request `minimumPostTransferHighUs=SPEED_CHANGE_HOLD_US`;
- only a validated address NACK, or a transport failure at START before the
  address byte, preserves known active/configured speed; transport error during
  the address phase is ambiguous;
- malformed/contradictory ACK evidence makes speed unknown;
- first-device address ACK followed by any Stop/post-high/transport failure
  sets `speedKnown=false`, preserves `_config.startupSpeed`, and forces explicit
  recover before further normal I/O;
- commit cached mode, `speedKnown=true`, and `_config.startupSpeed` only after
  the complete transfer and post-high interval succeed;
- Reset is forbidden;
- remove all speed query APIs.

Initialization/Recovery knows actual HS after successful Reset and only then
applies configured Standard mode.

### 7. Remove obsolete public surface

Confirm absence with `rg`:

```text
_activateDevice
readCurrentAddress
tick
_lastTickMs
discoveryRetries
getConfig
driverState
detectPart
setHighSpeed
isHighSpeed
setStandardSpeed
isStandardSpeed
resetAndDiscover
isPresent
```

Do not leave deprecated wrappers.

## Required tests

Add named tests for:

1. bind performs zero I/O;
2. invalid rebind preserves working binding;
3. Standard startup requires explicit expected AT21CS01;
4. begin absence retains binding and exact NOT_PRESENT;
5. later recover succeeds without config resupply;
6. every initialization failure phase preserves exact error/detail;
   identity address NACK preserves NACK while entering OFFLINE;
7. initialize is UNINIT-only and no ordinary API increments reset count;
8. failed OFFLINE recovery remains OFFLINE and uninitialized;
9. every legal/illegal state admission and one centralized finish;
10. Bus replacement binding epoch blocks stale Driver traffic until recover;
11. second Driver Reset generation invalidates first Driver speed cache without
   causing a Reset loop;
12. AT21CS01 Standard is restored lazily after another Driver resets Bus;
13. set High after that Reset skips redundant Standard restoration;
14. speed address NACK preserves known state, while address ACK plus later
    failure or malformed ACK evidence makes speed unknown and blocks all normal
    I/O;
15. AT21CS11 Standard rejection performs zero I/O and no health mutation;
16. Manufacturer revision values 0..7 classify by masked part code and expose
    raw ID/revision; unknown/different parts fail exactly;
17. probe is non-destructive/tracked and failed OFFLINE probe remains OFFLINE;
18. EEPROM/Security boundary lengths, including `SIZE_MAX`;
19. two address phases of random read map separately;
20. the eight-byte scratch policy preserves the failed read chunk;
21. independent CRC vectors, product-byte mismatch with the observed-byte
    detail, and CRC mismatch with exact computed/stored packed detail;
22. scalar outputs initialize before every validation/state failure;
23. one health update per composite operation;
24. saturating counters and persistent lastError;
25. `Driver::end()` is idempotent, releases exactly its claim, and performs no
    physical I/O;
26. duplicate same-Bus address bind fails transactionally, while equal
    addresses on different Bus objects succeed;
27. Bus replacement preserves claims and stale Drivers can recover without an
    alias taking their address.

## Verification

```text
python tools/check_core_timing_guard.py
python -m platformio test -e native
git diff --check
git status --short
```

Run strict C++17 warnings on core sources. Include a trace assertion that no
ordinary read/probe/speed call emits `RESET_DISCOVER`.

## Exit criteria

- All non-write public methods match the shared contract.
- Device absence at boot is recoverable.
- No normal operation hides Reset or retry.
- State and health changes have one central implementation.
- Prompt 03 can implement writes without revisiting lifecycle.
