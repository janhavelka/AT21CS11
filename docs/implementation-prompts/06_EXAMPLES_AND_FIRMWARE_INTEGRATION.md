# Prompt 06 — safe synchronous examples and RTOS integration guidance

## Outcome

Replace the obsolete example surface with exactly two small Arduino examples
that use the current synchronous API:

1. one complete single-device CLI;
2. one concise two-device/two-pin CLI.

Demonstrate hot-plug recovery with either an optional polarity-selectable detect
input or bounded firmware polling when that input does not exist. Also show the
ownership pattern suitable for a firmware-owned RTOS task. Do not implement
that task, its queue, or an asynchronous wrapper in this repository.

## Baseline and scope

Stages 01-05 are completed. Read `AGENTS.md`, the packet README, the shared
contract, the registry, and this prompt. Inspect current public headers and
tests before editing. Use the verified datasheet only if a protocol question is
encountered.

This stage owns:

- Q-04 example include/build cleanup;
- Q-05 meaningful command checking;
- Q-07 bounded parsing and removal of unsafe scan behavior;
- Q-08 removal of the product-specific `LoadCellMap` and duplicated paging;
- Q-09 the missing multi-device example;
- Q-13 safe destructive-operation handling;
- Q-17 removal of stale native-IDF example/checker artifacts;
- Q-18 simple synchronous multi-instance and RTOS integration guidance.

It does not change Backend/Bus/Driver protocol behavior or public API; the
required detect fields and Bus methods already exist. It does not implement
Prompt 07 packaging/CI or Prompt 08 HIL.

## Final example layout

Keep exactly:

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
    WireInstance.h
```

All common includes are local, such as `#include "BoardConfig.h"`. Remove every
other current example/helper, including `LoadCellMap.h`, raw-PHY helpers,
placeholder abstractions, and `examples/espidf_basic/`.

Remove the obsolete `tools/check_idf_example_contract.py`. Rewrite
`tools/check_cli_contract.py` for the two supported Arduino examples. Prompt 07
will verify that final documentation and package contents contain no native-IDF
claim.

## Bounded CLI

`BoundedCli.h` provides fixed character storage, in-place tokenization, and
strict unsigned/hex parsing. Freeze `LINE_BYTES = 128` including the terminating
NUL and `MAX_ARGS = 8` including the command name. It must:

- use no Arduino `String`, `std::string`, heap allocation, recursion, or
  unbounded line growth;
- define fixed line and argument limits;
- reject rather than truncate an overlong line or excess argument;
- discard an overlong line through its newline and report it once;
- require complete token consumption;
- clear and check `errno` where standard conversion functions are used;
- reject a leading minus, overflow, empty tokens, and trailing text;
- leave caller outputs unchanged when parsing fails;
- process at most the fixed line capacity per Arduino poll, allowing overlong
  discard to continue across later polls rather than monopolizing `loop()`.

Do not retain unused floating-point parsing. The EEPROM examples do not need
it.

Add native tests that include the real header and cover empty input, CR/LF,
exact buffer boundaries, overlong discard, maximum argument count, excess
arguments, zero, maximum accepted values, overflow, minus, and trailing text.

## Command contract and safety

`CommandContract.h` contains one authoritative fixed command-spec catalog for
name, usage, and risk. Each example has one registration table selecting its
catalog subset. Every registration references exactly one catalog entry and one
handler; no selected command lacks a handler and no handler lacks a catalog
entry. The two examples need not select the same subset.

Freeze the full single-device command surface and arities:

```text
help
status
presence
probe
manufacturer
serial
read-eeprom <address> <length>
read-security <address> <length>
security-locked
rom-zone <0..3>
speed <high|standard>
recover
write-page <address> <2..16 hexadecimal digits> CONFIRM_EEPROM_OVERWRITE
shutdown
```

Freeze the concise multi-device surface. `<wire>` is exactly `A` or `B`:

```text
help
status <wire>
presence <wire>
probe <wire>
serial <wire>
read-eeprom <wire> <address> <length>
recover <wire>
shutdown <wire|all>
```

`presence` first distinguishes lifecycle from configuration. If the instance is
shut down or its Bus is unbound, print `inactive` and perform no Bus call. On a
bound Bus, print `disabled` without a Bus call only when
`hasPresenceIndicator()` is false. Otherwise perform exactly one
`readPresenceIndicator()` sample through the same debounce observation path and
print the logical value or exact error.

For read commands, `address` accepts decimal or an explicit `0x` hexadecimal
prefix, `length` is decimal, and `CommandContract.h` defines
`READ_BUFFER_BYTES = 32`. Require length
`1..32` and validate the complete address-plus-length range against
`cmd::EEPROM_SIZE` or `cmd::SECURITY_SIZE` before Driver I/O. Page-write address
uses the same numeric rule. Do not accept implicit octal.

Do not expose stress, full erase, raw PHY, address scan, arbitrary chip command,
Security write, permanent Security Lock, permanent ROM-zone enable, or ROM
Freeze in either shipped example. The irreversible public APIs remain available
for separately authorized service software, but a general interactive example
must not make them easy to invoke.

Freeze this simple EEPROM page-write syntax:

```text
write-page <address> <2..16 hexadecimal digits> CONFIRM_EEPROM_OVERWRITE
```

Each byte is one hex pair, so length is derived as one to eight bytes. Reject
odd-length data, non-hex characters, EEPROM range overflow, page crossing,
missing/prefixed/differently cased confirmation, and extra input before Driver
I/O. Print the exact `Status` and full `WriteResult`, including ambiguous
effects.

`check_cli_contract.py` verifies the exact file layout, catalog uniqueness,
registration completeness, risks, usage, confirmation, and forbidden obsolete
tokens. It also verifies the BoardConfig defaults/override names and that the
multi-device registration contains no destructive handler. Native tests include
the real dispatcher with fake action counters to prove every registered safe
handler is meaningful and malformed/destructive rejection performs zero action
calls. Automated tests never perform real destructive or irreversible hardware
operations.

## Example board configuration

`BoardConfig.h` contains only example/firmware choices, not library defaults or
electrical qualification claims. Freeze these names and initial values:

```cpp
static constexpr int SIO_PRIMARY = 6;
static constexpr int PRESENCE_PRIMARY = -1;
static constexpr bool PRESENCE_PRIMARY_ACTIVE_HIGH = true;
static constexpr uint8_t ADDRESS_BITS_PRIMARY = 0;
static constexpr uint8_t OFFLINE_THRESHOLD_PRIMARY = 5;
static constexpr AT21CS::PartType EXPECTED_PART_PRIMARY =
    AT21CS::PartType::AT21CS11;

static constexpr int SIO_SECONDARY = 10;
static constexpr int PRESENCE_SECONDARY = -1;
static constexpr bool PRESENCE_SECONDARY_ACTIVE_HIGH = true;
static constexpr uint8_t ADDRESS_BITS_SECONDARY = 0;
static constexpr uint8_t OFFLINE_THRESHOLD_SECONDARY = 5;
static constexpr AT21CS::PartType EXPECTED_PART_SECONDARY =
    AT21CS::PartType::AT21CS11;

static constexpr uint32_t DETECT_SAMPLE_MS = 20;
static constexpr uint32_t DETECT_DEBOUNCE_MS = 100;
static constexpr uint32_t HOTPLUG_POLL_MS = 1000;
```

Both detect inputs default to disabled so the examples do not claim an
arbitrary board pin. A user may set either to a real externally biased input and
select its polarity. All three periods are nonzero, below `0x80000000`, and are
example scheduling defaults, not chip timing requirements.

The example defaults target AT21CS11. A consumer using AT21CS01 changes the
corresponding `EXPECTED_PART_*`; it must not use `PartType::UNKNOWN` to avoid
deciding which part is actually attached.

Make wiring and part selection reproducible without editing committed files.
`BoardConfig.h` uses `#ifndef` defaults for these build-time overrides and then
defines the constants above from them:

```text
AT21CS_EXAMPLE_PRIMARY_SIO_PIN
AT21CS_EXAMPLE_PRIMARY_PRESENCE_PIN
AT21CS_EXAMPLE_PRIMARY_PRESENCE_ACTIVE_HIGH   # 0 or 1
AT21CS_EXAMPLE_PRIMARY_ADDRESS_BITS
AT21CS_EXAMPLE_PRIMARY_PART                   # 1 or 11
AT21CS_EXAMPLE_SECONDARY_SIO_PIN
AT21CS_EXAMPLE_SECONDARY_PRESENCE_PIN
AT21CS_EXAMPLE_SECONDARY_PRESENCE_ACTIVE_HIGH # 0 or 1
AT21CS_EXAMPLE_SECONDARY_ADDRESS_BITS
AT21CS_EXAMPLE_SECONDARY_PART                 # 1 or 11
```

Reject invalid part/polarity selections at compile time. The normal production
configuration validation still rejects target-invalid pins and a detect/SI/O
collision within one Backend. Add example-level compile-time checks that the two
SI/O pins differ and that every enabled detect pin is distinct from both SI/O
pins and from the other enabled detect pin. Prompt 08 records the exact override
set for each HIL firmware build.

Map these settings directly to `Esp32TransportConfig::presencePin` and
`presenceActiveHigh`. Do not read GPIO directly or duplicate polarity logic.
An enabled detect input has no internal pull; the connected board must provide
a stable level. Exactly `-1` disables it.

For each Driver set `Config::addressBits`, `offlineThreshold`, and
`expectedPart` from its matching constants and set `startupSpeed` explicitly to
`HIGH_SPEED`. Do not rely on partially implicit per-device configuration.

## Example-only wire instance and hot-plug policy

`WireInstance.h` is a small example-only aggregate/helper around:

```cpp
AT21CS::Esp32Transport backend;
AT21CS::Bus bus;
AT21CS::Driver driver;
```

It may centralize start, cached serial comparison, ordered shutdown, and the
small fixed-state caller-owned hot-plug policy below. It is not a compatibility
facade or public library API. It creates no task, timer, queue, mutex, scheduler,
callback service, interrupt, or internal/unbounded retry loop.

Keep only the state needed to implement that policy: started/shut-down state;
detect raw/candidate/debounced known/value flags; recovery and serial-read
pending flags plus one terminal automatic-recovery-blocked flag; valid flags
plus the three millisecond reference times; last valid serial plus
known/current/comparison state; and one last policy status/action for display.
Do not add request IDs, attachment generations, application records, dynamic
strings, or a generic event framework.
Read `Driver::isInitialized()`/`state()` when deciding; do not cache or mirror
the Driver lifecycle/health state.

Start order is Backend -> Bus -> Driver. An absent Driver is not a reason to
destroy a successfully started Backend/Bus or discard its Driver binding.

Shutdown order is Driver -> fallible `Bus::end()` -> Backend. If Bus end fails,
keep that Backend alive and report the exact failure.
Any shutdown request disables hot-plug service before teardown; it must not be
treated as an absent device and automatically restarted. If `Bus::end()` fails,
keep the Backend alive and allow a later explicit shutdown retry, but do not
resume polling/recovery.

### Scheduling and wrap handling

Put the small host-testable cadence/debounce state in `WireInstance.h`; do not
add another framework. Use `uint32_t` Arduino-millisecond values and only
wrap-safe elapsed subtraction:

```cpp
static_cast<uint32_t>(nowMs - sinceMs) >= intervalMs
```

Never construct an absolute `nowMs + intervalMs` deadline. Track whether each
timestamp is initialized so the first requested action may be immediate. After
an attempt, set its reference time to the current `nowMs`; a delayed service
call performs one action and never catches up with a burst.

Advance the hot-plug poll/retry reference only after a no-detect
`probe()`/`recover()` poll action or a detect-driven recovery attempt. Detect
samples and deferred serial reads use their own state and must not postpone the
1,000 ms liveness/recovery cadence.

All intervals are below half the `uint32_t` range. Reinitialize the example
policy after any suspension longer than `0x7fffffff` ms; normal active firmware
must service it much more often than that.

Each `serviceHotPlug(nowMs)` call performs at most one fallible or I/O-performing
library operation: one detect sample, `probe()`, `recover()`, or
`readSerialNumber()`. Public scalar accessors/snapshots used to decide do not
count. A successful recovery schedules the serial read for a later service call.
There is no loop around a library operation.

Use this priority so periodic detect sampling cannot starve useful work:

1. no action after any shutdown request;
2. one pending serial read;
3. with detect enabled, one needed/due recovery when debounced present and not
   blocked, otherwise one due detect sample;
4. with detect disabled, one due `probe()`/`recover()` decision described below.

Both examples ingest at most the bounded CLI input and perform at most one
fallible/I/O library operation per Arduino loop. After dispatching a completed
command, the next loop gives one due automatic action priority before another
command; if none is due, dispatch the next command. Thus continuous commands
cannot starve hot-plug work.

Do not build a second error or lifecycle state machine. Automatic recovery is
needed only when the started/bound Driver did not initialize, its production
state is `OFFLINE`, or a debounced detach/reattach is pending. `DEGRADED` alone
does not trigger recovery. Parser errors, presence-callback errors, shutdown
errors, and individual Driver status codes are displayed but are not
reclassified by the helper.

Call production `probe()`/`recover()` and report the exact result; production
remains the authority for admission, resynchronization, detect preflight,
Reset/Discovery, identity, Bus state, and health. Recovery success clears
recovery-needed state and schedules one serial read. If either automatic call
returns `NOT_BOUND` or `INVALID_STATE`, or the Driver enters `FAULT`, stop
automatic attempts and require explicit firmware/user action. Otherwise a
still-uninitialized/`OFFLINE` Driver remains eligible at the next fixed period.
Do not inspect private state or duplicate Bus-poison logic.
An explicit successful manual recover or a full explicit restart re-enables the
policy.

### Enabled detect input

When `Bus::hasPresenceIndicator()` is true:

Recovery-needed state remains pending but cannot run until logical presence is
debounced true.

1. Sample `Bus::readPresenceIndicator()` no faster than every
   `DETECT_SAMPLE_MS`.
2. Treat an error as an unknown sample and report its exact `Status`; never turn
   it into logical absence. Do not print repeated unchanged samples or the same
   unchanged error on every poll.
3. Start a candidate when the logical raw value changes. Commit it only after
   the same value has remained stable for `DETECT_DEBOUNCE_MS`.
4. On stable absent, keep every binding and the last serial, mark the cached
   identity not current, set detach/reattach pending, and issue no protocol
   recovery while absence remains.
5. On the transition to stable present, request one immediate recovery. An
   initial stable present after a successful startup only establishes the
   baseline; it must not redundantly reset a healthy Driver.
6. If recovery fails while stable present and remains eligible under the simple
   rule above, wait `HOTPLUG_POLL_MS` before the next single attempt.

The Backend already applies active-high/active-low mapping. Presence is a
Bus-wide connector hint. It does not identify an addressed chip, and a recovery
that reaches Reset changes that Bus generation.

### No detect input

When `presencePin == -1`, never call `readPresenceIndicator()`. At each
`HOTPLUG_POLL_MS` event perform exactly one action:

1. If the bound Driver is initialized and `READY`/`DEGRADED`, call `probe()`
   once. This is liveness traffic, not Reset/recovery.
2. If the bound Driver is uninitialized or `OFFLINE`, except `FAULT`, call
   `recover()` once.
3. If unbound, shut down, `FAULT`, or automatic recovery is blocked, do nothing
   automatically and report that state.

The next poll uses the production state left by the previous call. The helper
does not reinterpret a failed probe; a later recovery begins only after Driver
state itself becomes uninitialized/`OFFLINE`. A device removed while idle is
therefore noticed at the next poll rather than by background library work.
Transport faults may require up to the configured `offlineThreshold` failed
probes before `OFFLINE`; a device that reappears or is replaced first may pass a
later probe without a serial comparison. A remove-and-replace event entirely
between polls may also be unobservable. Without a separate detect signal no
software can guarantee observing either transition.

Use the initialized flag and explicit states above, not `isOnline()` as a
pre-gate: a Driver stale after another Driver's successful shared-Bus Reset can
legitimately resynchronize during `probe()`.

Manual `recover` always means one immediate explicit attempt. It updates the
same policy state so an automatic catch-up attempt cannot follow immediately.

### Identity after recovery

After successful startup or recovery, read the serial once in a later service
call. Keep fixed storage for the last valid serial. Report `first seen`, `same
device`, or `different device`; on change show old and new bytes before storing
the new value. A failed serial read preserves the old bytes, marks identity not
current, and reports the exact status without reclassifying it. If production
Driver state is then `OFFLINE`, the same simple recovery-needed rule applies;
otherwise no new recovery is invented. The example reports identity change but
makes no application-data decision.
The manual `serial` command uses this same comparison path; do not duplicate
identity logic in the two example mains.

## Single-device CLI

Use one statically owned wire instance and an explicit expected part. Show:

- Backend/Bus/Driver startup and exact startup status;
- initialized/offline/hot-plug state through copied snapshots;
- raw Manufacturer ID, detected part, revision, and known/unknown speed;
- optional presence indication clearly distinguished from protocol `probe()`;
- serial and bounded EEPROM/Security/ROM reads into fixed caller buffers;
- one bounded confirmed EEPROM page write with exact effect evidence;
- explicit `recover()` after an absent-at-boot or reattached device;
- the exact caller-owned detect/debounce or no-detect polling policy above;
- serial read/comparison after successful recovery;
- ordered shutdown.

If `begin()` reports absence as `NOT_PRESENT` or an identity-phase
`NACK_DEVICE_ADDRESS`, leave the bound CLI usable so a later `recover` command
can succeed. Do not reconstruct the objects, mask an address silently, or hide
an immediate/unbounded retry.

## Multi-device CLI

Use two statically owned independent instances:

```text
configured pin A -> Backend A -> Bus A -> Driver A, addressBits 0
configured pin B -> Backend B -> Bus B -> Driver B, addressBits 0
```

Configure an explicit expected part for both Drivers; do not rely on
`PartType::UNKNOWN` in production-oriented examples.

Use the common CLI/automatic-action arbitration above. Alternate A/B priority
after each automatic action so missing A cannot starve B. Show:

- explicit A/B selection without copying or reconstructing objects;
- independent status and serial identity;
- `recover A` or `recover B` after attachment;
- a missing/failing A does not alter B state;
- independent ordered shutdown;
- address zero legitimately reused because the physical wires are separate.

Document the shared-wire alternative in a short code comment: one Backend and
one Bus with a fixed set of uniquely addressed Drivers. Do not create a Bus per
address on the same wire.

## RTOS integration guidance

The example comments and Stage-07 documentation must state:

- library calls are synchronous and objects are not thread-safe;
- the safe default is one firmware task/loop owning all AT21CS wire instances
  and calling them sequentially;
- application tasks may send copied application-defined messages to that task;
- the library defines no mailbox, request ID, deadline, result dispatcher,
  automatic recovery, backoff, or attachment-generation scheme;
- multiple tasks owning separate wire instances are outside the current ESP32
  concurrency qualification unless the Backend is later tested and documented
  for simultaneous calls;
- Drivers sharing a Bus always share one owner;
- firmware calls `recover()` explicitly after attachment and may compare the
  serial before reusing application-owned data;
- a firmware-owned fixed cadence may request one recovery attempt while an
  instance needs recovery; this is caller policy, not hidden library work;
- `writeEepromPage()` is the preferred scheduling unit when a firmware task
  needs one bounded page write.

Do not create:

```text
test/consumer/firmware_owner/
tools/run_firmware_owner_fixture.py
FirmwareOwnerPolicy.h
At21csOwner
ChannelRequest / ChannelResult / CachedChannelStatus
```

## Builds and tests

Update root `platformio.ini` to provide exactly these example environments:

```text
ex_cli_s3
ex_cli_s2
ex_multi_s3
ex_multi_s2
```

All use the exact pinned PioArduino platform and `framework = arduino`.

Native tests must include the actual cadence/debounce code from
`WireInstance.h` and cover:

- first/immediate request, just-before/exactly-at period, delayed service with
  no catch-up burst, and `uint32_t` wrap;
- raw detect bounce, stable present/absent transitions, and a sample error that
  remains unknown rather than absence;
- no detect callback when disabled, no recovery while stable absent, one
  recovery on stable present, and 1,000 ms spacing after failure;
- no-detect 1,000 ms `probe()` while online and `recover()` only after the
  production Driver becomes uninitialized/`OFFLINE`;
- the simple decision rule: uninitialized/`OFFLINE` needs recovery,
  `READY`/`DEGRADED` does not, and `FAULT`/blocked state disables automatic
  attempts until explicit restart/successful manual recovery;
- terminal blocking after automatic `probe()` or `recover()` returns
  `NOT_BOUND`/`INVALID_STATE`;
- successful recovery scheduling one later serial read, same/different serial,
  and failed serial preserving the previous bytes;
- service-action priority and A/B round-robin progress with repeatedly absent
  A;
- continuous completed commands interleaved with due automatic work, proving
  neither side starves.

Keep the host-tested helper pure: cadence arithmetic, presence debounce, small
action selection, identity comparison, and fairness only. Use simple booleans
or action counters; do not inject callbacks or implement another lifecycle/fake
device protocol. Stage-05 production tests remain the authority for actual
Bus/Driver recovery and faults; Arduino example builds verify integration.

Run:

```text
.\scripts\pio.cmd test -e native
python tools/check_cli_contract.py
.\scripts\pio.cmd run -e ex_cli_s3
.\scripts\pio.cmd run -e ex_cli_s2
.\scripts\pio.cmd run -e ex_multi_s3
.\scripts\pio.cmd run -e ex_multi_s2
git diff --check
git status --short
```

Also run affected static/backend gates if shared headers or build filters change.
No physical HIL is performed in this stage.

## Exit criteria

- Exactly two shipped Arduino examples exist and both use production paths.
- Parsing and command storage are fixed-size and bounded.
- Invalid input produces a visible error and zero Driver I/O.
- The only shipped destructive command is a bounded page write protected by the
  exact confirmation token.
- No irreversible, stress, scan, raw-PHY, placeholder, product-schema, native-IDF,
  or duplicated paging implementation remains in examples.
- Two separate address-zero instances and explicit hot-plug recovery are clear.
- Optional active-high/active-low detect input and the no-detect 1,000 ms
  probe/recovery polling path are both documented and host-tested.
- RTOS guidance recommends one external synchronous owner without supplying an
  owner framework.
- No task, timer, queue, mailbox, scheduler, hidden/unbounded retry, or
  application DTO has entered the library or public API. Automatic behavior is
  limited to one visible example-owned action per due service call, with fixed
  spacing and no catch-up burst.
