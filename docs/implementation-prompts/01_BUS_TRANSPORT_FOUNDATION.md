# Prompt 01 — shared Bus and frame-transport foundation

## Outcome

Perform the one-time architectural cutover from a platform-bearing,
byte-callback `Driver` to the exact v2 types, shared physical `Bus`, whole-frame
transport, and externally owned ESP32 backend declared in
`00_SHARED_V2_CONTRACT.md`.

This stage owns names and ownership. Later stages correct feature semantics and
qualify physical timing; they must not redesign this boundary.

Keep the core and transport interface framework-independent. Arduino through
the exact PioArduino pin frozen by the shared contract is the only current
firmware framework; this stage does not install or build a platform framework.
Do not install/select standalone ESP-IDF, add a native-IDF path, or perform
physical HIL. Prompt 04 owns the Arduino PHY software build, and Prompt 08 alone
owns physical qualification.

## Required working method

Read `AGENTS.md`, `00_SHARED_V2_CONTRACT.md`, and
`FINDINGS_REGISTRY.md`. Inspect the public Config/Status/lifecycle/transport
boundaries in `../MB85RC`, `../PCA9555`, `../TCA9548A`, and `../INA228`, then
apply only the sibling patterns explicitly accepted by the README comparison
boundary. Inspect `git status` and preserve unrelated changes.

Spawn at least three subagents:

1. public-contract reviewer;
2. Bus/transport implementation and fake-test reviewer;
3. platform-boundary/compile reviewer.

Use them read-only until the integrator confirms the exact contract. Keep one
integrator responsible for shared headers and source. Reuse correct backend
and protocol mechanics while refactoring as far as needed for simplicity,
robustness, and readability. Retain no obsolete public contract, compatibility
wrapper, migration architecture, parallel path, or band-aid. Reuse one correct
validation/address/status helper instead of copying logic, and simplify the
resulting code before adding surface area. Do not modify the protected
complete-driver report. Follow the packet README's saga checkpoint policy; do
not tag, release, publish, or upload.

## Sole owned findings

Close the architectural root of:

- P-01 bus-global write-high reservation;
- P-02 whole-frame atomic transport;
- P-10 typed transport outcome;
- A-01 shared Bus;
- A-03 external backend ownership;
- A-06 non-copyability;
- A-07 deterministic Status;
- A-12 scalar snapshots;
- A-14 bus Reset generation;
- A-17 callback-result shape validation;
- A-18 binding epoch and hold-preserving teardown;
- A-19 checked core deadline arithmetic;
- A-20 ambiguous current write-byte acceptance evidence;
- A-21 independent physical-Bus instance isolation;
- A-22 unique per-Bus address ownership;
- Q-16 contradictory ready-polling repository policy.

Stages 2–4 add behavior and physical proof.

## Exact `AGENTS.md` policy correction

Before implementing the write path, replace only this current Protocol Rules
line:

```text
During t_WR busy cycle, keep SI/O high and use bounded ready polling.
```

with:

```text
During t_WR busy cycle, keep SI/O released high continuously for the fixed
10 ms Bus write-high interval; do not ACK-poll before that deadline.
```

This closes Q-16 and makes the repository instruction agree with the frozen Bus
contract. Do not otherwise edit `AGENTS.md` in this stage; the separate
example-count reconciliation belongs only to Prompt 07.

## File scope

Create/refactor:

```text
include/AT21CS/Types.h
include/AT21CS/Status.h
include/AT21CS/Transport.h
include/AT21CS/Bus.h
include/AT21CS/Config.h
include/AT21CS/AT21CS.h
include/AT21CS/platform/esp32/Esp32Transport.h
src/Bus.cpp
src/AT21CS.cpp
src/platform/esp32/Esp32Transport.cpp
test/support/ScriptedTransport.h
test/test_main.cpp
test/test_bus_contract.cpp
AGENTS.md (only the exact Q-16 line replacement above)
```

Delete:

```text
include/AT21CS/Core.h
src/platform/esp32/AT21CSEsp32Backend.cpp
test/test_basic.cpp
test/stubs/Arduino.h
test/stubs/Wire.h
```

Do not retain both backend filenames or both transport contracts. Remove the
obsolete native-test stub include paths from the native environment in this
stage, so the post-cutover tree compiles without waiting for Prompt 05.

## Exact implementation instructions

### 1. Install the frozen public vocabulary

Implement the exact enums, structs, defaults, method names, and deleted API list
from `00_SHARED_V2_CONTRACT.md`.

Specific non-negotiable details:

- `Status{}` is OK, detail zero, message `"OK"`.
- Remove `Status::inProgress()`.
- Add total `toString()` overloads for every public enum.
- Use `makeProtocolDetail()`, `protocolDetailPhase()`, and
  `protocolDetailIndex()` exactly as specified.
- Store `SingleWireTransport` by value in `BusConfig`/`Bus`.
- Only callback `user` targets are non-owning.
- Delete copy and move for `Bus`, `Driver`, and `Esp32Transport`.
- Destructors perform no callback. `Driver::end()` is physical-bus-silent but
  releases its in-memory Bus address claim;
  fallible `Bus::end()` owns retained high-hold completion.
- Snapshots contain no pointers or platform types.

### 2. Implement `Bus`

Private Bus state must contain only:

```cpp
SingleWireTransport _transport{};
bool _bound = false;
bool _bindingEpochValid = true;
uint64_t _bindingEpoch = 0;
uint64_t _generation = 0;
uint8_t _claimedAddressMask = 0;
bool _resetEstablishedHighSpeed = false;
uint64_t _writeHighUntilUs = 0;
TransferResult _previousTransfer{};
TransferResult _lastTransfer{};
WriteCycleResult _lastWriteCycle{};
```

Use the fixed `TRANSFER_TIMEOUT_US=9000` and `RESET_TIMEOUT_US=5000` constants
from the shared contract; do not add user-configurable deadlines.

Use `uint64_t` generation in the implementation and snapshot. Before an
operation that would increment `UINT64_MAX`, return `INVALID_STATE` with zero
wire activity; do not wrap or silently lose Reset invalidation.

Implement these private helpers, with `Driver` as friend:

```cpp
Status _execute(
    const SingleWireTransfer& transfer,
    TransferResult& result);

Status _executeWrite(
    const SingleWireTransfer& transfer,
    WriteCycleResult& result);

Status _resetAndDiscover(
    bool& present,
    TransferResult& result);

Status _completeWriteHighHold(
    TransferResult& result);

Status _readPresence(
    bool& present,
    TransferResult& result);

Status _claimAddress(uint8_t addressBits);
void _releaseAddress(uint8_t addressBits);

Status _mapTransferFailure(
    const TransferResult& result) const;
```

Rules:

- Validate the required descriptor callbacks (`nowUs`, `transfer`,
  `resetAndDiscover`, and `waitUntilUs`) before replacing an existing binding.
  `readPresence` and `user` are explicitly optional.
- All Bus state is per instance. Do not add a mutable global/current Bus,
  transport, generation, deadline, diagnostic, or callback target.
- One live transport descriptor/physical wire may be bound to exactly one Bus.
  Bus cannot safely infer descriptor aliasing, so the upper owner/configuration
  layer must reject sharing one descriptor between Bus objects.
- `bind()` calls no callback. Quiescent `end()` calls no callback; with a
  retained high deadline it calls `_completeWriteHighHold()` once and clears
  nothing unless that bounded completion succeeds.
- `_claimAddress()` rejects a bit already set in `_claimedAddressMask` with
  `INVALID_CONFIG`, requested address in detail, and zero callback.
  `_releaseAddress()` clears only that bit and performs no callback.
- `Bus::end()` first requires `_claimedAddressMask==0`. Otherwise it returns
  `BUSY`, places the mask in detail, and performs zero callback, including no
  retained-hold wait. Replacement `bind()` preserves claims.
- Advance/invalidate `bindingEpoch` exactly as specified by the shared
  contract. A valid replacement binding invalidates every previously bound
  Driver even though `bind()` itself performs zero I/O.
- Replacement `bind()` returns `BUSY` with zero callbacks while a retained
  deadline exists and preserves the old descriptor/state. Failed/early
  `end()` preserves the same state and requires the backend to remain alive.
- `_execute()` validates every `SingleWireTransfer` invariant before I/O.
- Validate every callback's returned phase, ACK fields, byte count, and Stop
  evidence against the exact callback-result shapes in the shared contract.
  Never turn a short/incomplete `OK` result into logical success.
- Preserve and validate `currentWriteByteMayBeAccepted` exactly. It names the
  one fully delivered payload byte whose ACK outcome is unknown; it is not an
  extra proven byte and is legal only for the shared-contract
  `DATA_WRITE` failure shape.
- `_execute()` checks/completes any retained write-high deadline first.
- Derive absolute transfer/reset/presence/write deadlines with strict checked
  `uint64_t` addition. A result equal to the reserved `UINT64_MAX` poison
  sentinel or arithmetic overflow is `CLOCK_STALLED`; apply the fail-closed
  post-acceptance rule from the shared contract. Avoid loops in core.
- `result.code == TransportCode::NACK` maps by phase:
  - device write/read -> `NACK_DEVICE_ADDRESS` with encoded exact phase;
  - memory address -> `NACK_MEMORY_ADDRESS`;
  - data write -> `NACK_DATA` with encoded payload index.
- `TIMEOUT`, `LINE_STUCK`, and `IO_ERROR` remain distinct.
- Preserve the entire physical result in `_lastTransfer`, shifting its previous
  value into `_previousTransfer` before every new transfer/reset/presence
  result. This is a fixed two-result diagnostic history, not a dynamic trace.
- `_resetAndDiscover()` increments generation before invoking the callback,
  because a failed attempt may still have driven a Reset pulse.
- It sets `_resetEstablishedHighSpeed=true` only after transport OK and
  `present=true`.
- Reset/Discovery success must terminate at `DISCOVERY_RELEASE`, after the
  distinct 25 us release check. `LINE_STUCK` at that phase remains distinct
  from the earlier `DISCOVERY_SAMPLE` observation.
- Genuine `present=false` maps to `NOT_PRESENT`; a timeout never maps to
  absence.
- `_readPresence()` initializes `present=false` before every exit. A callback
  `NACK` or malformed success is `IO_ERROR`, never absence.

### 3. Implement write-high reservation in Bus, not Driver

All mutating commands use `_executeWrite()`; `_execute()` rejects a descriptor
with `txLength > 0`. `_executeWrite()`:

1. stores the complete frame result in `WriteCycleResult::frame`;
2. computes `holdRequired` from the raw fail-closed predicate:
   `dataBytesTransferred > 0`, or
   `currentWriteByteMayBeAccepted && phase == DATA_WRITE &&
   dataBytesTransferred < txLength`;
3. if `holdRequired=false`, performs no wait and preserves the frame Status;
4. otherwise samples `nowUs` after the pre-frame checked-arithmetic guard;
5. for a sum below `UINT64_MAX`, sets
   `_writeHighUntilUs = now + WRITE_HIGH_HOLD_US`;
6. invokes `waitUntilUs(_writeHighUntilUs)` only for that finite deadline;
7. stores that callback result separately in `WriteCycleResult::hold`;
8. verifies a second `nowUs()` is at or beyond the deadline;
9. sets `holdCompleted=true` and clears the deadline only after that proof.

If the frame failed after proven acceptance or after a fully delivered byte
whose ACK is unknown, complete any finite hold and return the original mapped
frame Status. If the frame succeeded but the finite hold failed, return the
mapped hold Status. This preserves the primary protocol failure while retaining
both physical results for `WriteResult`. A malformed frame still maps to
`IO_ERROR`, but plausible programming evidence still arms the hold. Reserved
sentinel equality/overflow remains the fail-closed exception described below.

If the callback fails or returns early:

- return mapped transport error or `CLOCK_STALLED`;
- retain the finite `_writeHighUntilUs`;
- reject every later transfer/Reset until time has actually reached that finite
  deadline.

`UINT64_MAX` is not a finite deadline. If post-acceptance addition equals that
reserved value or overflows, store `UINT64_MAX`, skip `waitUntilUs`, return
`CLOCK_STALLED` with ambiguous write evidence, and permanently reject protocol
traffic, replacement bind, and successful `Bus::end()` on that Bus object.

No Bus/Driver method may ACK-poll during this interval. The wait callback must
not drive SI/O low. This stage establishes the mechanism; Stage 3 decides
exactly which command outcomes arm it.

### 4. Cut `Driver` over without retaining platform state

`Driver` contains:

```cpp
Bus* _bus = nullptr;
Config _config{};
bool _bound = false;
bool _initialized = false;
DriverState _state = DriverState::UNINIT;
PartType _detectedPart = PartType::UNKNOWN;
uint32_t _manufacturerId = 0;
uint8_t _siliconRevision = 0;
SpeedMode _activeSpeed = SpeedMode::HIGH_SPEED;
bool _speedKnown = false;
bool _seenBusBindingEpochValid = false;
uint64_t _seenBusBindingEpoch = 0;
uint64_t _seenBusGeneration = 0;
// scalar health/cache fields only
```

Remove from Driver:

- pins and GPIO register addresses;
- transport pointer;
- timing profiles;
- bit/byte/start/stop/reset functions;
- critical-section/platform macros;
- callback begin/end ownership;
- `_activateDevice()`;
- `_lastTickMs`.

All raw protocol operations must construct `SingleWireTransfer` and call Bus.
There must be one internal device-address helper:

```cpp
uint8_t _deviceAddress(uint8_t opcode, bool read) const;
```

Do not duplicate address construction in individual methods.

Stage 2 owns final lifecycle/read semantics and Stage 3 owns final write
semantics. Do not add temporary public methods, legacy adapters, or
`UNSUPPORTED_COMMAND` stubs that could survive the migration. Later-owned
Driver methods may remain declared but unreferenced until their owning stage;
the Stage 1 native runner exercises only completed Stage 1 contracts. Prompt 04
owns backend consumer builds and Prompt 07 owns final clean-package consumers;
do not preserve old examples as an alternate API merely to make them compile
early.

### 5. Establish the explicit platform adapter boundary

Create:

```cpp
struct Esp32TransportConfig {
  int sioPin = -1;
  int presencePin = -1;
  bool presenceActiveHigh = true;
};

class Esp32Transport {
 public:
  Status begin(const Esp32TransportConfig& config);
  void end();
  bool isInitialized() const;
  SingleWireTransport descriptor();
  // deleted copy/move
};
```

The descriptor's context points to the backend. `Driver` and `Bus` never call
backend `begin()`/`end()`.

`descriptor()` returns a zero-initialized `SingleWireTransport{}` before a
successful backend `begin()` and after backend `end()`. Define
`BACKEND_NOT_INITIALIZED_DETAIL=-1` once in `Transport.h`. Every callback also
checks backend initialization before touching GPIO. `transfer`,
`resetAndDiscover`, `waitUntilUs`, and optional `readPresence` invoked through
a stale copied descriptor after `end()` return `TransferResult` with
`TransportCode::IO_ERROR`, `TransferPhase::NONE`, that exact detail, and every
other field defaulted. Stale `nowUs` returns `0`. All five stale paths perform
zero GPIO/timer activity. This guard does not authorize ending/restarting a
backend while a Bus remains bound.

The backend must outlive every bound Bus. Application shutdown is Driver
local-end, successful fallible `Bus::end()`, then backend end. Never call
backend end after a write failure while Bus still retains a high-only deadline.

Move current platform code structurally into this class and make `transfer()`
own a complete frame rather than calling public byte callbacks. It is acceptable
for the timing constants to remain explicitly unqualified until Prompt 04, but:

- do not preserve the extra discovery low pulse;
- do not preserve immediate write ACK polling;
- do not expose per-byte callbacks;
- do not claim the backend production-ready;
- keep all platform code out of core headers/sources.
- keep every mutable pin/timing/lifecycle/callback target in the backend
  instance; no singleton callback context or file-static active backend.

### 6. Add a reusable event-tracing fake

`test/support/ScriptedTransport.h` must be fixed-capacity and allocation-free.
It records:

```cpp
enum class FakeEventKind : uint8_t {
  NOW_US,
  PRESENCE,
  RESET_DISCOVER,
  TRANSFER_BEGIN,
  DEVICE_ADDRESS,
  MEMORY_ADDRESS,
  RESTART,
  TX_DATA,
  RX_DATA,
  STOP,
  WAIT_UNTIL,
  TRANSFER_END
};

struct FakeEvent {
  FakeEventKind kind;
  uint64_t atUs;
  uint32_t value;
  size_t index;
};
```

Use fixed arrays with explicit overflow failure. It must script:

- exact terminal result and phase;
- exact first-device, memory-address, and repeated-device ACK evidence;
- exact proven-byte count and current-byte-may-have-been-accepted evidence;
- NACK at each phase/index;
- present/absent;
- monotonic, wrapping-not-applicable `uint64_t` time;
- deliberately stalled time;
- reset and transfer counters.

Do not use Arduino/Wire stubs.

## Stage tests

Add named tests proving:

1. `Status{}` and all public structs have deterministic values.
2. Bus/Driver/backend are non-copyable and non-movable using `static_assert`.
3. Invalid replacement Bus config preserves the prior valid binding and does
   zero I/O.
4. Initial/replacement bind and successful `end()` advance or invalidate `bindingEpoch`;
   a stale Driver performs no frame.
5. Replacement bind during a retained hold returns `BUSY` with zero callbacks;
   failed `end()` preserves the binding/deadline and successful `end()` waits
   before invalidating it; repeated unbound `end()` is silent/idempotent.
6. One transfer callback represents the entire frame.
7. Every NACK phase and address-ACK field maps exactly.
8. Malformed `OK` results (short byte count, missing address ACK, missing Stop,
   or impossible phase/evidence) fail as `IO_ERROR`.
9. A failure during the data-byte ACK sample reports the proven prefix plus
   `currentWriteByteMayBeAccepted`; zero proven bytes still arm the hold, map to
   `MAY_HAVE_COMMITTED` downstream, and are never replayed. Illegal flag
   combinations map to `IO_ERROR`, while plausible acceptance evidence still
   arms the hold.
10. Transport timeout/line-stuck/I/O remain distinct.
11. Consecutive physical outcomes shift through
    `previousTransfer`/`lastTransfer` without allocation.
12. Checked deadline arithmetic rejects both reserved-sentinel equality and
   overflow before I/O, and post-acceptance equality/overflow fails closed as
   permanent poison without a wait callback.
13. Bus-level presence false is distinct from callback failure and does not
   touch SI/O.
14. `_executeWrite()` preserves separate frame/hold results and a write-high
   deadline blocks transfers from a second Driver sharing Bus.
15. Reset increments generation and is visible to every Driver.
16. Failed Reset leaves bus speed knowledge false.
17. Discovery sample-low presence and the later release check are separate;
    held-low at `DISCOVERY_RELEASE` maps to `LINE_STUCK`.
18. Core headers compile without Arduino/IDF/FreeRTOS/ESP32 includes.
19. No v1 byte callback or platform field remains.
20. Two independent transport/Bus pairs may both use Driver address zero.
    Reset, binding epoch, retained hold, diagnostics, presence, failure, and end
    on pair A do not change pair B. Distinct fake callback contexts prove no
    global target or descriptor leakage.
21. `descriptor()` is empty before begin/after end; stale result-returning
    callbacks produce the exact backend-not-initialized result, stale `nowUs`
    returns zero, and all stale paths have zero GPIO/timer activity.
22. Two Drivers cannot claim the same address on one Bus; the same address on
    independent Buses succeeds. Transactional Driver rebind preserves its old
    claim on validation/new-claim failure.
23. `Bus::end()` with live claims is callback-free `BUSY`; Driver end releases
    claims without physical I/O, after which Bus teardown follows the retained
    hold rules.

## Verification

Run:

```text
python tools/check_core_timing_guard.py
python -m platformio test -e native
git diff --check
git status --short
```

Also compile a translation unit that includes only:

```cpp
#include <AT21CS/AT21CS.h>
#include <AT21CS/Bus.h>
```

with strict C++17 warnings:

```text
-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
-Wshadow -Wundef -Werror
```

Physical timing qualification is not this stage's exit gate and is deferred to
Prompt 08. The framework-neutral boundary must remain syntactically integrated
for Prompt 04's Arduino transport.

## Exit criteria

- Exactly one frame transport contract exists.
- Exactly one Bus abstraction exists.
- No compatibility `DriverV2`, `LegacyDriver`, or old-method alias exists.
- Native Bus contract tests pass.
- All later prompts can refer to the exact types in the shared contract without
  reinterpretation.
