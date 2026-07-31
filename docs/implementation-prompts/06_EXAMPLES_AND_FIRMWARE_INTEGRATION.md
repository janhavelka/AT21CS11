# Prompt 06 — safe examples and generic firmware integration

## Outcome

Replace the oversized/unsafe example surface with minimal examples that
demonstrate the final Bus/Driver ownership model, bounded parsing, exact error
reporting, safe irreversible-operation interlocks, and both supported physical
topologies. The primary firmware fixture is a generic fixed-size
multi-channel owner for independently wired removable peripherals.

This stage does not add application-specific features to the library.

## Required working method

Read all contracts and completed Stages 1–5. Inspect sibling example patterns
in `../MB85RC`, `../PCA9555`, `../TCA9548A`, and `../INA228`; inspect
`../TunnelMonitor-node` owner-operation budgets, static construction, error
propagation, and shutdown rules as one reference profile, not as public API
authority. Use the README comparison boundary to reject I2C-only behavior.
Preserve unrelated changes.

Spawn subagents for:

1. bounded CLI/parser and destructive-command review;
2. separate-wire and shared-wire ownership review;
3. native ESP-IDF example/contract review;
4. generic firmware-owner, hot-plug, and cross-task DTO review.

Keep one integrator for common example helpers. Reuse helpers; do not copy the
single-device dispatcher into the multi-device example. Refactor and delete
obsolete example infrastructure rather than wrapping it; keep the final
examples smaller than the current set and add no compatibility band-aid. Do not
perform actual irreversible operations during automated tests. Do not modify
the protected report. Follow the packet README's saga checkpoint policy; do not
tag, release, publish, or upload.

## Sole owned findings

Close:

- Q-04 example half;
- Q-05;
- Q-07;
- Q-08 implementation half;
- Q-09;
- Q-13 example interlocks;
- Q-18 generic multi-channel firmware integration.

## Final example layout

Keep:

```text
examples/
  01_basic_bringup_cli/
    main.cpp
  02_multi_device_cli/
    main.cpp
  common/
    BoardConfig.h
    BoundedCli.h
    CommandContract.h
    StatusText.h
    ExampleTransport.h
  espidf_basic/
    CMakeLists.txt
    main/
      CMakeLists.txt
      main.cpp
```

Remove obsolete/raw/duplicated helpers, including `LoadCellMap.h` unless the
maintainer explicitly chooses to keep and fully test an honest fixed-record
example. Do not retain a false journal/wear-leveling claim.

All includes inside `examples/common` are local, for example:

```cpp
#include "BoardConfig.h"
```

Never use repository-root-qualified `examples/common/...` includes.

## Shared bounded CLI

Implement in `BoundedCli.h` with fixed storage:

```cpp
static constexpr size_t CLI_LINE_CAPACITY = 128;
static constexpr size_t CLI_MAX_ARGS = 8;

struct ParsedCommand {
  size_t argc = 0;
  char* argv[CLI_MAX_ARGS] = {};
};
```

No Arduino `String`, `std::string`, heap allocation, unbounded line growth, or
recursive parser.

Parsing helpers:

```cpp
bool parseUint32(const char* text, uint32_t& value);
bool parseUint8(const char* text, uint8_t& value);
bool parseFiniteFloat(const char* text, float& value);
```

Rules:

- clear `errno`;
- require complete token consumption;
- reject leading minus for unsigned;
- reject overflow/underflow;
- reject NaN and Infinity;
- leave output unchanged on failure;
- overlong line is discarded until newline and reported once.

Add host tests for parser behavior.

## Command contract

Define:

```cpp
enum class CommandRisk : uint8_t {
  READ_ONLY = 0,
  MUTATING,
  DESTRUCTIVE,
  IRREVERSIBLE
};

struct CommandSpec {
  const char* name;
  CommandRisk risk;
  const char* usage;
  const char* confirmation;
};
```

One repo-local manifest is authoritative for Arduino and IDF command names/risk
classes. Framework-specific handlers remain native as required by `AGENTS.md`.

Required risk/confirmation examples:

```text
write/stress/full erase:
  DESTRUCTIVE
  CONFIRM_EEPROM_OVERWRITE

permanentlyLockSecurity:
  IRREVERSIBLE
  CONFIRM_PERMANENT_SECURITY_LOCK

permanentlyEnableRomZone <n>:
  IRREVERSIBLE
  CONFIRM_PERMANENT_ROM_ZONE

permanentlyFreezeRomZones:
  IRREVERSIBLE
  CONFIRM_PERMANENT_ROM_FREEZE
```

The exact final argument must equal the confirmation string. A prefix,
case-insensitive match, missing argument, or extra token fails with zero Driver
I/O.

Automated example tests must compile irreversible handlers but invoke only their
rejection/dry-run paths.

## Single-device CLI

Demonstrate:

- explicit backend begin;
- Bus bind;
- Driver begin with exact expected part;
- cached snapshot/status;
- raw Manufacturer ID, masked part classification, and silicon revision;
- known/unknown speed state;
- Bus-level presence indicator versus Driver protocol probe;
- serial/manufacturer reads;
- bounded EEPROM read/page write;
- Security/ROM reads;
- explicit recover;
- conservative WriteResult/MutationResult printing.
- ordered shutdown that keeps Backend alive until fallible `Bus::end()` returns
  OK.

Remove:

- duplicated raw PHY commands or raw bit implementation;
- redundant `waitReady()` after synchronous writes;
- hidden address masking;
- placeholder/no-op commands;
- load-cell application schema;
- unbounded stress loops.
- address scanning. Reset/Discovery is Bus-wide and a temporary Driver scan
  would repeatedly reset every configured device. Examples instantiate only
  explicit configured addresses and use `probe()`/`recover()` on those objects.

Every stress command requires:

- explicit finite iteration count with a hard maximum;
- explicit affected range;
- destructive confirmation;
- stop on first failure;
- final readback summary.

## Multi-device CLI

Demonstrate the intended removable-peripheral topology with:

```text
SI/O pin A -> Backend A -> Bus A -> Driver A, addressBits 0
SI/O pin B -> Backend B -> Bus B -> Driver B, addressBits 0
```

Use two distinct board-configured SI/O pins. Reusing address zero is deliberate
and proves that addresses are scoped to a Bus. The less common topology with
multiple A2:A0 devices sharing one wire remains fully supported and is proved
by the shared contract and Prompt 05 tests. A firmware owner may mix channel
cardinalities: every physical wire is exactly one Backend/Bus channel, and that
channel may contain one to eight uniquely addressed Drivers. Never share a Bus
across pins or create a Bus per address on a shared wire. This concise CLI
itself remains scoped to two separate one-device wires.

Show:

- independent snapshots/health;
- a Reset, retained write hold, rebind, absence, or shutdown on A does not
  change B;
- channel selection without reconstructing global state;
- explicit `recover()` after simulated attachment/power-up;
- serial-number comparison after recovery;
- no copied Driver objects;
- serialized access from one loop/owner;
- independent Driver -> Bus -> Backend shutdown for each channel. The code path
  must keep A's Backend alive if A Bus end fails and leave B usable; do not
  inject that fault in the CLI. Prompt 05's host oracle proves it.

Reuse common parser/command/status helpers. Keep this example concise; do not
copy the full single-device command set.

## Native ESP-IDF example

Use:

- `app_main`;
- native ESP-IDF GPIO/timer/task APIs;
- `vTaskDelay`;
- fixed C buffers;
- explicit backend/Bus/Driver construction.

Do not use Arduino headers, `String`, `Serial`, `Wire`, compatibility facades,
or Arduino CLI source.

Either implement each claimed command meaningfully or remove it from the
manifest. Specifically, no placeholder `raw`, `chip`, unused `verbose`, or
CRC-only “selftest” may satisfy parity.

Invalid arguments must produce a visible error and zero Driver I/O.

## Generic fixed-size firmware-owner fixture

Create under:

```text
test/consumer/firmware_owner/
  platformio.ini
  src/FirmwareOwnerPolicy.h
  src/main.cpp

tools/run_firmware_owner_fixture.py
```

`platformio.ini` must have no checkout-relative library path. Declare exactly
one dependency using:

```ini
lib_deps =
  ${sysenv.AT21CS_FIXTURE_LIB_SPEC}
```

`run_firmware_owner_fixture.py` accepts required `--library-root`, optional
`--fixture-root` (default `test/consumer/firmware_owner`), and one or more
`--environment`. It resolves the library root, requires its `library.json`,
constructs `AT21CS-under-test=<Path.as_uri()>` (a PlatformIO `file://` local
package), sets `AT21CS_FIXTURE_LIB_SPEC` only in the child process, and invokes
`python -m platformio run -v`. It accepts repeatable `--forbid-root`; any
compiler/linker input or include path under a forbidden root fails the run.
There is no fallback dependency, repository `lib_extra_dirs`, or hardcoded
`../../../` path.

This is compile/test consumer code, never public library API. Use these exact
fixture constants:

```cpp
static constexpr size_t MAX_CHANNELS = 4;
static constexpr size_t OWNER_DATA_BYTES = 8;
static constexpr size_t REQUEST_QUEUE_DEPTH = 8;
static constexpr size_t RESULT_QUEUE_DEPTH = 8;
static constexpr size_t MAX_OUTSTANDING_REQUESTS = 8;

static constexpr uint64_t PRESENCE_DEBOUNCE_US = 50000;
static constexpr uint64_t RECOVERY_INITIAL_BACKOFF_US = 100000;
static constexpr uint64_t RECOVERY_MAX_BACKOFF_US = 5000000;

static constexpr uint64_t SINGLE_FRAME_BUDGET_US = 10000;
static constexpr uint64_t PAGE_WRITE_BUDGET_US = 24000;
static constexpr uint64_t RECOVERY_AND_IDENTITY_BUDGET_US = 52000;

static_assert(MAX_OUTSTANDING_REQUESTS <= RESULT_QUEUE_DEPTH);
```

The combined recovery budget covers an optional 9 ms presence callback, the
5 ms Reset callback deadline, up to three 9 ms frames for Manufacturer ID,
AT21CS01 Standard-speed restoration, and serial read, plus fixed owner
overhead. Do not reduce it from observed typical timing.

The page budget covers Prompt 04's 22 ms maximum fault-path fresh page call plus
2 ms for the exact owner admission/result-construction phase. Measure the full
second-admission-check-through-phase-return path in Prompt 08 firmware-owner
HIL and fail that release row if it exceeds 24 ms; do not consume the margin
with logging or callbacks.

Define only firmware-local DTOs and owner errors:

```cpp
struct BoardConfig {
  uint64_t sioOutputCapableMask = 0;
  uint64_t inputCapableMask = 0;
  uint64_t reservedPinMask = 0;
};

struct ChannelConfig {
  bool enabled = false;
  int sioPin = -1;
  int presencePin = -1;
  bool presenceActiveHigh = true;
  uint8_t addressBits = 0;
  AT21CS::PartType expectedPart = AT21CS::PartType::UNKNOWN;
  AT21CS::SpeedMode startupSpeed = AT21CS::SpeedMode::HIGH_SPEED;
  uint8_t offlineThreshold = 1;
};

enum class ChannelOperation : uint8_t {
  READ_EEPROM_CHUNK = 0,
  WRITE_EEPROM_PAGE,
  READ_SERIAL,
  READ_MANUFACTURER,
  PROBE,
  RECOVER
};

enum class OwnerResultCode : uint8_t {
  OK = 0,
  INVALID_REQUEST,
  DUPLICATE_REQUEST_ID,
  REQUEST_QUEUE_FULL,
  OUTSTANDING_LIMIT,
  DEADLINE_EXPIRED,
  DEADLINE_TOO_SHORT,
  IDENTITY_NOT_READY,
  CHANNEL_ABSENT,
  STOPPING,
  CLOCK_OVERFLOW,
  GENERATION_EXHAUSTED,
  LIBRARY_ERROR,
  INTERNAL_ERROR
};

struct ChannelRequest {
  uint32_t requestId = 0;
  uint8_t channelIndex = 0;
  ChannelOperation operation = ChannelOperation::READ_EEPROM_CHUNK;
  uint8_t address = 0;
  uint8_t length = 0;
  uint8_t data[OWNER_DATA_BYTES] = {};
  uint64_t expectedAttachmentGeneration = 0;
  uint64_t notAfterUs = 0;
};

struct ChannelResult {
  uint32_t requestId = 0;
  uint8_t channelIndex = 0;
  OwnerResultCode ownerCode = OwnerResultCode::OK;
  bool libraryInvoked = false;
  AT21CS::Err libraryCode = AT21CS::Err::OK;
  int32_t libraryDetail = 0;
  uint8_t length = 0;
  uint8_t data[OWNER_DATA_BYTES] = {};
  AT21CS::WriteEffect writeEffect =
      AT21CS::WriteEffect::NOT_ATTEMPTED;
  size_t bytesCommitted = 0;
  size_t bytesAccepted = 0;
  uint64_t attachmentGeneration = 0;
  uint64_t replacementGeneration = 0;
};

struct CachedChannelStatus {
  bool backendStarted = false;
  bool busBound = false;
  bool driverBound = false;
  bool initialized = false;
  AT21CS::DriverState state = AT21CS::DriverState::UNINIT;
  bool physicalPresenceKnown = false;
  bool physicalPresent = false;
  bool serialEverConfirmed = false;
  bool identityValid = false;
  uint8_t serial[8] = {};
  uint64_t attachmentGeneration = 0;
  uint64_t replacementGeneration = 0;
  AT21CS::PartType part = AT21CS::PartType::UNKNOWN;
  uint32_t manufacturerId = 0;
  uint8_t siliconRevision = 0;
  bool speedKnown = false;
  AT21CS::SpeedMode activeSpeed = AT21CS::SpeedMode::HIGH_SPEED;
  AT21CS::Err lastCode = AT21CS::Err::OK;
  int32_t lastDetail = 0;
  uint64_t bindingEpoch = 0;
  uint64_t resetGeneration = 0;
  uint64_t writeHighUntilUs = 0;
  uint64_t nextRecoveryUs = 0;
  uint8_t recoveryAttempts = 0;  // saturating
};
```

Define the fixture owner surface exactly:

```cpp
enum class OwnerState : uint8_t {
  UNCONFIGURED = 0,
  CONFIGURED,
  RUNNING,
  STOPPING,
  STOPPED,
  FAULT
};

template <size_t ChannelCount>
class At21csOwner {
 public:
  OwnerResultCode configure(
      const BoardConfig& board,
      const ChannelConfig (&channels)[ChannelCount]);
  OwnerResultCode start();

  // Cross-task-safe, task-context-only producer API.
  OwnerResultCode submit(
      const ChannelRequest& request,
      uint64_t nowUs);

  // Called by exactly one designated result-dispatcher task.
  bool receive(ChannelResult& result);

  // Cross-task-safe read-only publication API.
  bool readPublishedStatus(
      uint8_t channelIndex,
      CachedChannelStatus& status);

  // Called only by the single AT21CS owner task/loop.
  void serviceOnce(uint64_t nowUs);
  OwnerResultCode stopChannel(uint8_t channelIndex);
  void stopAll();
  bool channelStopped(uint8_t channelIndex) const;
  bool allStopped() const;
  OwnerState state() const;
};

using FirmwareAt21csOwner = At21csOwner<MAX_CHANNELS>;
```

Add total fixture-local `toString(OwnerResultCode)` and
`toString(OwnerState)` switches with an `"UNKNOWN"` fallback; do not expose
them from the AT21CS namespace.

`At21csChannel` owns one `ChannelConfig`, `Esp32Transport`, `Bus`, `Driver`,
and `CachedChannelStatus`. `At21csOwner<MAX_CHANNELS>` default-owns a fixed
array of these single-device/separate-wire channels. This exact fixture is
deliberately scoped to the common one-chip-per-removable-connector case. The
library's shared-wire and mixed-topology support is covered by the shared
contract, multi-device CLI description, and Prompt 05 oracle; do not distort
this fixture into a second general library.

The owner also owns an exact statically allocated FreeRTOS mailbox:

- request and result queues are created with `xQueueCreateStatic()` using
  `REQUEST_QUEUE_DEPTH`/`RESULT_QUEUE_DEPTH` and fixed byte storage;
- no queue, semaphore, mutex, or storage is heap-created;
- a fixed `uint32_t _outstandingRequestIds[MAX_OUTSTANDING_REQUESTS]` and one
  private `portMUX_TYPE` protect admission metadata;
- `submit()` is safe for multiple producer tasks. Exactly one designated
  result-dispatcher task calls destructive FIFO `receive()` and routes results
  by `requestId`; no other task may consume that queue.
  `readPublishedStatus()` is safe for multiple reader tasks. All three are
  task-context-only; no ISR entry point exists;
- accepting a request first reserves one free outstanding-ID/result slot.
  `submit()` rejects zero/duplicate IDs, a full request queue, or no result
  reservation. If queue insertion fails, it rolls back the ID reservation;
- `receive()` removes the matching outstanding ID only after copying the
  terminal result to the caller. Thus queued + executing + completed-unread
  requests never exceed result capacity;
- the owner retains one fixed pending-result slot and performs no next Driver
  operation until that result is queued. No accepted request/result is dropped,
  overwritten, or converted into logging;
- the owner publishes a scalar `CachedChannelStatus[MAX_CHANNELS]` copy under
  the same short critical section after each channel transition/operation.
  Other tasks call only `readPublishedStatus()`; `copyStatus()` and direct live
  snapshot/cache access remain owner-context-only;
- replacement notification is the monotonic `attachmentGeneration` and
  `replacementGeneration` in published status, so a skipped intermediate poll
  cannot clear or lose the event.

Neither class/mailbox is added to `include/AT21CS/`.

Owner rules:

1. `configure()` is allowed only from `UNCONFIGURED` or fully `STOPPED`;
   `start()` is allowed only from `CONFIGURED`; neither silently restarts a
   channel. `configure()` validates/copies the complete channel table before hardware:
   every pin is 0..63 before shifting; every SI/O is allowed by
   `sioOutputCapableMask`; every enabled presence pin is allowed by
   `inputCapableMask`; `reservedPinMask` rejects board strap/flash/PSRAM/other
   owned pins; enabled SI/O pins are distinct; enabled presence pins are
   distinct and collide with no enabled SI/O; address is 0..7; `expectedPart`
   is explicitly AT21CS01 or AT21CS11, never UNKNOWN; enum values are valid;
   Standard Speed is accepted only with AT21CS01; and at least one channel is
   enabled. It performs zero hardware I/O and preserves the prior
   configuration on failure.
2. Each enabled channel starts Backend -> Bus -> Driver independently. Address
   zero may be reused on every separate Bus. Failure/absence on one channel
   does not abort initialization of the others. After each successful Driver
   initialize, reconcile the serial before marking that channel identity-valid.
   `start()` returns `OK` after attempting every enabled channel and entering
   `RUNNING`, even when individual channels are absent/offline; their exact
   failures are published per channel.
3. Only the owner context calls library methods or reads live library
   snapshots. Cross-task producers/consumers use only the exact mailbox APIs
   above; an unsynchronized concurrent cache read is forbidden.
4. Runtime requests are limited to the six `ChannelOperation` values above.
   Irreversible Lock/ROM/Freeze and bulk erase/stress are excluded from the
   ordinary runtime queue and belong to separately authorized service tools.
5. `submit()` returns an immediate `OwnerResultCode`; `OK` means accepted and
   guarantees exactly one later `ChannelResult`. An immediately rejected
   submission returns no result and performs no library call. If an accepted
   request later fails owner admission/service checks, its result sets
   `libraryInvoked=false`, `libraryCode=OK`, and exact `ownerCode`. A Driver
   call sets `libraryInvoked=true`. Use `LIBRARY_ERROR` only when the returned
   AT21CS `Status` itself fails, preserving exact
   `libraryCode/libraryDetail`. If the AT21CS call succeeds but a later owner
   reconciliation step fails (for example generation exhaustion), retain the
   exact owner code with `libraryInvoked=true` and `libraryCode=OK`. Never label
   an owner deadline/queue/identity error as a transport error.
6. A page write uses only `writeEepromPage()`. A read turn transfers at most
   eight bytes. Exact `Status` plus `WriteResult` evidence is copied into the
   result; ambiguous writes are never replayed.
7. If channel A has a retained `writeHighUntilUs`, the scheduler may defer A to
   keep the next request's latency bounded and continue servicing B. Bus A
   remains responsible for protocol enforcement; the optimization is not a
   correctness dependency.

Submission validation precedence is fixed: owner/channel stopping state,
request ID/channel/enum/shape, deadline, duplicate ID, outstanding-result
capacity, then request-queue capacity. `stopChannel()` returns
`INVALID_REQUEST` for an invalid/disabled index and is idempotent `OK` for an
already stopping/stopped channel. `stopAll()` is idempotent.

Implement and reuse these exact fixture-private helpers:

```cpp
uint64_t operationBudgetUs(ChannelOperation operation);
OwnerResultCode validateRequestShape(const ChannelRequest& request);
OwnerResultCode admitDeadline(
    const ChannelRequest& request,
    uint64_t nowUs);
bool checkedAddUs(uint64_t baseUs, uint64_t deltaUs, uint64_t& resultUs);
bool advanceGeneration(uint64_t& generation);
uint64_t nextRecoveryDelayUs(uint8_t recoveryAttempts);
```

`operationBudgetUs()` maps read chunk, serial, Manufacturer ID, and probe to
`SINGLE_FRAME_BUDGET_US`; page write to `PAGE_WRITE_BUDGET_US`; and recover to
`RECOVERY_AND_IDENTITY_BUDGET_US`. `admitDeadline()` rejects
`notAfterUs==0`, tests `nowUs >= notAfterUs` before subtraction, then compares
`notAfterUs-nowUs` with the operation budget. Repeat this test immediately
before Driver I/O because a valid request may expire in the queue. A started
synchronous library call cannot be cancelled.

`notAfterUs` is the latest allowed completion time for the request's
synchronous library/recovery-and-identity phase. Each budget is measured from
the second admission check through return from that phase and includes its
fixed owner-side work. It does not promise that the already-built terminal
result has been consumed—or even queued under result backpressure—by that
timestamp. Result publication may occur later, but no later hardware call is
started while a terminal result is pending.

Request shapes are exact:

| Operation | Input contract | Success result |
|---|---|---|
| `READ_EEPROM_CHUNK` | address 0..127; length 1..8; range inside 128 bytes; input data all zero; nonzero `expectedAttachmentGeneration` exactly matches the channel | `length` requested bytes |
| `WRITE_EEPROM_PAGE` | address 0..127; length 1..8; range inside 128 bytes and one physical 8-byte page; data used; nonzero expected generation exactly matches | zero data length plus exact write evidence |
| `READ_SERIAL` | address/length/expected generation/data all zero | eight validated serial bytes |
| `READ_MANUFACTURER` | address/length/expected generation/data all zero | three big-endian Manufacturer-ID bytes |
| `PROBE` | address/length/expected generation/data all zero | zero data length |
| `RECOVER` | address/length/expected generation/data all zero | recovery plus serial reconciliation; eight serial bytes |

An EEPROM request received while `identityValid=false`, or whose expected
attachment generation is stale, returns `IDENTITY_NOT_READY` with zero library
I/O. An absent presence hint returns `CHANNEL_ABSENT`. The owner processes at
most one queued request or one automatic recovery action per `serviceOnce()`,
round-robin across channels.

Hot-plug policy is explicit upper-firmware behavior:

1. A configured presence pin is a real debounced connector/module input, never
   inferred from idle SI/O. On debounced false, set `identityValid=false` while
   retaining `serialEverConfirmed`, the last confirmed serial bytes, and both
   generations for later comparison. While false, schedule no protocol call.
   Apply the same identity invalidation after library `NOT_PRESENT`, or after a
   lifecycle Manufacturer-ID `NACK_DEVICE_ADDRESS` that leaves the Driver
   `OFFLINE`; both are definite absence for the owner.
2. On debounced rising presence, invoke explicit `recover()`. Automatic
   recovery is eligible only for an enabled, Backend-started, Bus-bound,
   Driver-bound, neither-stopping-nor-stopped channel with
   `identityValid=false`, no terminal clock/generation failure, no currently
   debounced-false presence indication, and Driver state `UNINIT`, `READY`,
   `DEGRADED`, or `OFFLINE` (the library's recover-admitted set). `FAULT` and
   transient states are never auto-recovered. A Backend/Bus/Driver setup
   failure, `FAULT`, `CLOCK_OVERFLOW`, or `GENERATION_EXHAUSTED` is published as
   a terminal channel/configuration problem and requires the explicit
   stop-all/reconfigure lifecycle. Without a presence pin, automatic recovery
   covers bound uninitialized/`OFFLINE` channels after definite absence or
   recovery failure. A healthy `READY`/`DEGRADED`, identity-valid channel is
   never periodically reset/recovered. A successful recovery cancels the
   backoff, sets
   `recoveryAttempts=0`, and clears `nextRecoveryUs`. Use 100 ms initial
   exponential backoff capped at 5 s. Perform at most one automatic recovery
   action per owner service turn, round-robin across enabled channels.
   `probe()` is not reconnect because power-up requires Discovery. Compute
   backoff by repeated capped doubling, never by an unchecked shift. Use
   `checkedAddUs()` for `nextRecoveryUs`; on overflow publish
   `CLOCK_OVERFLOW`. Reject explicit/automatic RECOVER on that channel until
   the terminal stop-all/reconfigure/start lifecycle supplies a valid clock
   epoch.
3. Retain Backend/Bus/Driver bindings while temporarily absent. Do not rebuild
   objects or hide retry inside the library.
4. A RECOVER operation is not successful at the owner layer until Driver
   recovery and a CRC-checked eight-byte serial read both succeed. Until then,
   `identityValid=false` and EEPROM requests are rejected.
5. On each successfully reconciled initialize/recover, advance
   `attachmentGeneration` with `advanceGeneration()` before admitting EEPROM
   work. The first confirmed identity sets `serialEverConfirmed=true` and does
   not count as replacement. A later different serial additionally advances
   `replacementGeneration`. A later same serial still advances attachment
   generation, invalidating requests queued before a disconnect. Neither
   generation wraps; exhaustion returns `GENERATION_EXHAUSTED`, leaves identity
   invalid, and stops that channel.
6. A direct `READ_SERIAL` normally leaves generations unchanged. If it observes
   a different valid serial without a preceding recorded attachment boundary,
   treat that as an implicit replacement: advance both generations and publish
   the new identity before returning.
7. Every EEPROM request carries the generation observed by its caller. This
   prevents an old queued calibration read/write from running against a newly
   attached cell. Calibration schema/version/units/CRC/generation,
   expected-serial policy, and association with a load-cell channel remain
   application-owned. The fixture never silently reuses old calibration and
   never writes automatically on attachment.

Shutdown has two distinct owner-context operations:

- `stopChannel(A)` rejects new A requests and publishes `STOPPING` terminal
  results for already queued A requests, but continues ordinary operation on B.
  It is terminal for A in the current configured owner lifetime; there is no
  `startChannel()`. Re-enabling A requires completing `stopAll()`, then a new
  `configure()`/`start()` lifecycle.
- `stopAll()` atomically closes all external admission, publishes `STOPPING`
  terminal results for all queued requests, and performs no further normal
  channel operation.

For either path, service teardown incrementally per channel: call Driver end
once to release its claim; call fallible Bus end on later owner turns as needed;
keep Backend initialized/released until Bus end succeeds; then call Backend
end. A teardown failure on A does not prevent teardown progress on B during
`stopAll()`, while channel-only `stopChannel(A)` deliberately leaves B
operational. `allStopped()` is true only after every enabled Backend ended.

Test the exact fixture with two enabled channels on distinct SI/O pins, both
using `addressBits=0`; do not add a Driver array to this fixture. Its adjacent
documentation must show the legal shared-wire/mixed-topology extension as a
different upper-firmware container: one physical-wire channel owns one
Backend/Bus and a fixed array of one to eight uniquely claimed Drivers. Never
create Backend/Bus per address on that wire.

Do not add firmware-product, load-cell, connector, scheduler, queue,
calibration-record, or reference-consumer types to AT21CS public/core code. The
fixture consumes the adapter qualified by Prompt 04. Prompt 07 owns packed
clean-consumer construction.

Keep the DTOs, constants, and pure validation/deadline/identity helpers in
`src/FirmwareOwnerPolicy.h`; it remains fixture-private and contains no
Arduino/FreeRTOS include. `main.cpp` owns the ESP32/FreeRTOS mailbox and
hardware objects. Add native policy tests that include this exact header rather
than copying its implementation:

1. every operation's exact valid and invalid shape, including page crossing;
2. deadline equality is expired, one microsecond below each budget is too
   short, exact budget is admitted, and subtraction/addition never wraps;
3. recovery delay sequence is 100 ms, 200 ms, 400 ms ... capped at 5 s, with
   saturating attempts and explicit clock-overflow behavior;
4. request ID zero/duplicate/full/outstanding-limit behavior; an ID remains
   reserved until its result is received;
5. every accepted request yields exactly one result even when result delivery
   is temporarily blocked; no next Driver call occurs while a result is
   pending;
6. owner errors have `libraryInvoked=false`; injected Driver errors preserve
   exact library code/detail and conservative write evidence;
7. known absence retains the serial but invalidates identity; first identity is
   not replacement; same-chip reconnect advances only attachment generation;
   changed chip advances both; stale-generation EEPROM requests perform zero
   library calls; generation exhaustion fails closed;
8. `stopChannel(A)` drains/rejects A while B remains operable;
   `stopAll()` rejects all new work and independently completes both teardowns;
9. two address-zero channels have independent cached state, library fakes, and
   teardown progress.

## Semantic command checker

Replace token-only checks with a checker that verifies:

- each manifest command has one registered handler;
- usage/risk/confirmation match;
- no handler is a placeholder/no-op;
- invalid input tests prove zero Driver calls;
- irreversible handlers require exact confirmation;
- Arduino and IDF claim only their real supported command intersection.

A deliberate no-op replacement must make the checker/test fail.

## Required tests/builds

Host:

```text
python -m platformio test -e native
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
```

Arduino:

```text
python -m platformio run -e ex_cli_s3 -e ex_cli_s2
python -m platformio run -e ex_multi_s3 -e ex_multi_s2
python tools/run_firmware_owner_fixture.py --library-root . --environment firmware_owner_s3 --environment firmware_owner_s2
```

The `firmware_owner_s2` and `firmware_owner_s3` environments must compile the
generic two-channel owner fixture described above, with both Drivers configured
for `addressBits=0` on distinct SI/O pins. Native IDF S2/S3 builds remain
required.

Also run:

```text
git diff --check
git status --short
```

## Exit criteria

- Exactly one full Arduino CLI and one concise Arduino multi-device CLI exist.
- Native IDF example is genuinely native.
- Parsing and command storage are bounded.
- Invalid/destructive input cannot silently mutate hardware.
- Irreversible commands require explicit confirmation.
- No raw PHY or paging implementation is duplicated in examples.
- Generic upper firmware can consume the library unchanged with either
  separate-wire channels or shared-wire addressed devices.
- Cross-task publication is synchronized, and one failed/absent/recovering
  channel cannot alter another channel's state, write hold, identity, or
  shutdown progress.
