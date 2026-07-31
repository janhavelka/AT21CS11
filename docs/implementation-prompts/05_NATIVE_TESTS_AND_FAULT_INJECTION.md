# Prompt 05 — exhaustive native tests and fault injection

## Outcome

Turn the Prompt 01 scripted transport into an independent protocol oracle and
prove every public API, state transition, failure phase, write effect, and
multi-device bus interaction.

This stage primarily tests. It may change production code only when a new test
demonstrates a root defect. It must not weaken a test to preserve existing code.

## Required working method

Read the shared contract, finding registry, completed Stages 1–4, all existing
tests, and the Stage 4 v2 smoke consumers under
`test/consumer/phy_smoke/arduino/` and `test/consumer/phy_smoke/idf/`.
Preserve unrelated changes.

Spawn subagents for:

1. public API/validation matrix;
2. protocol event-trace and NACK/fault matrix;
3. state/health/multi-device matrix;
4. sanitizer/build-review matrix.

Require agents to design expected outcomes independently from implementation.
Keep one integrator for test-support code. Reuse one fixed-capacity scripted
transport and shared assertion helpers; refactor duplicate tests instead of
adding per-bug harnesses. If a test exposes a production defect, fix its owning
root contract rather than band-aiding the test. Do not modify the protected
report. Do not commit or publish.

## Sole owned findings

Close:

- P-14;
- Q-02;
- regression verification for every P/A finding.

## Test layout

Replace monolithic `test/test_basic.cpp` with focused files:

```text
test/
  support/
    ScriptedTransport.h
    ScriptedTransport.cpp
    ExpectedFrames.h
    TestBuilders.h
    TestAccess.h
  test_main.cpp
  test_status_and_contract.cpp
  test_bus.cpp
  test_lifecycle.cpp
  test_reads_identity_speed.cpp
  test_eeprom_writes.cpp
  test_security.cpp
  test_rom_and_freeze.cpp
  test_health_state.cpp
  test_multi_device.cpp
```

Delete:

```text
test/stubs/Arduino.h
test/stubs/Wire.h
```

Remove their include paths from PlatformIO configuration.

No test may calculate an expected frame, CRC, address, or state by calling the
production helper being tested.

`test_main.cpp` is the single Unity/native runner and registers the focused test
functions. Do not add one competing `main()` per source file.

When a saturation or otherwise unreachable invariant needs private-state setup,
use exactly one `AT21CS_TESTING`-guarded friend named `TestAccess`, implemented
in `test/support/TestAccess.h`. Define `AT21CS_TESTING` only in native test
environments. It must add no data member, virtual function, public method, or
installed test header. Do not use `#define private public`, linker tricks, or
test behavior in production builds.

## Scripted transport requirements

The fake must:

- use fixed-capacity arrays only;
- fail explicitly on script/event overflow;
- compare every requested transfer field;
- record timestamps and event order;
- inject one terminal result at any TransferPhase/index;
- control Stop completion;
- control `dataBytesTransferred`;
- control `currentWriteByteMayBeAccepted` independently, including illegal
  combinations used to test Bus validation;
- return present/absent;
- advance, stall, or jump `uint64_t nowUs`;
- verify `waitUntilUs` deadlines;
- detect a transfer attempted during write-high hold;
- count Reset/Discovery globally;
- expose no Arduino/Wire types.

Expected frame builders are test-owned literals. For example, EEPROM random
read expected raw addresses are computed in test code from documented bit
layout, not `_deviceAddress()`.

## Mandatory API test matrix

For every fallible public API, cover:

1. success;
2. every invalid enum/scalar;
3. null pointer where applicable;
4. lower and upper valid boundary;
5. just-below/just-above range;
6. zero I/O for validation/precondition failure;
7. transport TIMEOUT where the valid call invokes a transport callback;
8. LINE_STUCK where the valid call invokes a transport callback;
9. IO_ERROR where the valid call invokes a transport callback;
10. every applicable NACK phase;
11. output initialization/preservation;
12. final Driver state;
13. lastStatus/lastError;
14. exact success/failure counter delta.

Public APIs:

```text
Bus::bind
Bus::end
Bus::readPresenceIndicator
Driver::bind
Driver::initialize
Driver::begin
Driver::recover
Driver::probe
Driver::readEeprom
Driver::writeEepromPage
Driver::writeEeprom
Driver::readSecurity
Driver::writeSecurityUserPage
Driver::writeSecurityUser
Driver::readSecurityLockState
Driver::permanentlyLockSecurity
Driver::readSerialNumber
Driver::readManufacturerId
Driver::readRomZoneState
Driver::permanentlyEnableRomZone
Driver::permanentlyFreezeRomZones
Driver::setSpeedMode
```

Cached getters must be proven bus-silent.

`Bus::bind`, `Driver::bind`, and any other valid zero-I/O operation are not
given fabricated transport failures. Prove their validation, state, output, and
zero-I/O behavior instead. NACK cases likewise apply only to frames containing
the named ACK phase.

## Mandatory value/boundary matrix

Lengths:

```text
0, 1, 2, 7, 8, 9, 15, 16, 31, 32, 127, 128, 129,
0x10000, SIZE_MAX
```

Addresses:

```text
EEPROM: 0x00, 0x01, 0x07, 0x08, 0x77, 0x78, 0x7E, 0x7F
Security: 0x00, 0x0F, 0x10, 0x17, 0x18, 0x1E, 0x1F, 0x20
Device address bits: 0, 1, 7, 8, 0xFF
Zone index: 0, 1, 2, 3, 4, 0xFF
```

## Exact protocol oracle cases

### Discovery and lifecycle

- present/absent;
- failure at Reset low/recovery/request/sample;
- success reports exact `DISCOVERY_RELEASE` phase after the separate release
  check; line still low reports `LINE_STUCK` at `DISCOVERY_RELEASE`;
- one attempt only;
- binding survives absence/error;
- later recovery success;
- checked transfer and Reset deadline addition near `UINT64_MAX`; overflow
  returns `CLOCK_STALLED` before the corresponding wire callback;
- part IDs `00D200`, `00D380`, unknown, expected mismatch;
- no Standard opcode for AT21CS11;
- Driver/backend end is idempotent and bus-silent when called in the permitted
  owner order; quiescent `Bus::end()` is callback-free, while a retained
  deadline causes one bounded high-only completion attempt. A failed/early
  completion preserves the binding, epoch, descriptor, and deadline.

For every callback type, inject malformed nominal-success shapes and require
`IO_ERROR` while preserving the raw result in Bus diagnostics. Include:

- transfer `OK` with phase other than `STOP`, missing required address ACK,
  short/excess byte count, absent Stop, or ACK/evidence for a phase not present
  in the request;
- `currentWriteByteMayBeAccepted=true` on success, NACK, read, non-data phase,
  exhausted payload, or without the required preceding address ACKs;
- Reset/Discovery `OK` with phase other than `DISCOVERY_RELEASE` or nonzero
  byte/ACK/Stop evidence;
- wait `OK` with phase other than `WAIT_HIGH` or before the verified deadline;
- presence `OK` with phase other than `PRESENCE` or nonzero byte/ACK/Stop
  evidence;
- `NACK` returned by Reset, wait, or presence callbacks.

### Reads

- raw address values for every opcode/addressBits/RW combination used;
- random-read repeated Start;
- host ACK intermediate bytes and NACK final byte;
- frame chunks <=8;
- no Reset before any ordinary read;
- first and repeated device-address NACK distinguished;
- reads use a fixed eight-byte scratch buffer per frame and copy into caller
  memory only after the whole frame succeeds;
- after a later chunk failure, earlier successful chunks remain committed while
  bytes belonging to the failed chunk and all later chunks are untouched.

### CRC/serial

Independent known vectors:

```text
empty -> 0x00
"123456789" -> 0xA1
01 02 03 -> 0xD8
```

Cover byte-0 product mismatch and byte-7 CRC mismatch without an intermediate
health success. Assert exact details: product mismatch stores the observed byte;
CRC mismatch stores `(computedCrc << 8) | storedCrc`.

### Speed

- HS->SS and SS->HS raw opcode/address;
- current speed used for command bits;
- 650 us post-command high request;
- cached speed changes only after success;
- requesting the already-active valid mode returns `OK`, emits zero frames, and
  performs zero health/state mutation;
- AT21CS11 Standard zero I/O;
- address NACK preserves the known active/configured speed;
- first device-address ACK followed by Stop, post-high, timeout, line-stuck, or
  other transport failure sets `speedKnown=false`, preserves configured speed,
  and blocks normal I/O until explicit recovery;
- bus generation resynchronization across two devices.

### Writes

- page splitting at every offset;
- NACK at every data index;
- accepted prefix plus Stop ambiguity;
- non-NACK failure during every data-byte ACK sample, including the first byte:
  the proven count excludes the uncertain byte, the explicit flag is true, a
  high-only hold is armed, the effect is `MAY_HAVE_COMMITTED`, and the frame is
  never replayed;
- wait callback error;
- early-return/stalled time;
- exact 10 ms deadline;
- checked deadline addition immediately below/at/above the safe
  `UINT64_MAX` boundary, with pre-frame overflow causing zero line activity and
  mutating preflight reserving both transfer and hold intervals, and
  post-acceptance hold overflow failing closed at `UINT64_MAX`;
- no SI/O transfer or Reset/Discovery event inside hold; an optional
  input-only presence read neither drives SI/O nor touches/clears the retained
  deadline;
- no automatic replay;
- bytesCommitted and last-page evidence at every failure point.

### Lock/ROM/Freeze

- exact Check Lock memory-address NACK semantics;
- no opcode `2h/R`;
- precheck/already-applied paths;
- mandatory verification;
- all ROM register literals and values 00/FF;
- invalid ROM read value;
- exact Freeze `1h/W,55,AA`;
- no opcode `1h/R` anywhere;
- exact private Freeze observation `1h/W` address-only frame followed by early
  Stop on address ACK, yielding confirmed not-frozen without a mutation;
- observation address NACK plus matching same-address Manufacturer ID liveness
  yields confirmed frozen;
- observation address NACK plus failed/mismatched liveness is
  `INDETERMINATE`;
- already-frozen precheck is `VERIFIED` and emits no mutation frame;
- mutation address NACK after confirmed-not-frozen precheck is
  `INDETERMINATE`;
- accepted mutation plus confirmed postcheck promotes to `VERIFIED`;
- accepted mutation plus postcheck mismatch/failure preserves `ACCEPTED`;
- no Freeze session cache exists before or after Bus generation changes.

## State transition oracle

Table-drive all allowed and rejected combinations for:

```text
UNINIT
PROBING
INIT_CONFIG
READY
BUSY
DEGRADED
OFFLINE
RECOVERING
SLEEPING
FAULT
```

Required proofs:

- normal I/O only READY/DEGRADED;
- probe additionally OFFLINE when initialized;
- failed OFFLINE recovery remains OFFLINE;
- validation failure never changes state;
- threshold zero disables threshold-based OFFLINE;
- NOT_PRESENT enters OFFLINE immediately;
- success restores READY where allowed;
- SLEEPING is unreachable via public API;
- FAULT requires a successful explicit `Driver::bind()` followed by
  `initialize()`; `Driver::end()` is optional/local and performs no Bus I/O.

Test counter saturation at `UINT8_MAX`, `UINT32_MAX`, and generation refusal at
`UINT64_MAX` through the single test-only `TestAccess` mechanism above.

Test Bus binding lifetime separately:

- successful initial and replacement bind advance `bindingEpoch`;
- replacement bind invalidates stale Drivers before any frame;
- successful `Bus::end()` invalidates the current epoch;
- replacement bind at `bindingEpoch==UINT64_MAX` fails `INVALID_STATE` and
  preserves the old binding;
- `Bus::end()` at `bindingEpoch==UINT64_MAX` sets
  `bindingEpochValid=false`, prevents rebinding that Bus object, and leaves
  stale Drivers bus-silent;
- failed/early `Bus::end()` during a retained hold preserves binding, epoch,
  descriptor, and deadline.

## Multi-device oracle

Use:

```text
one ScriptedTransport
one Bus
Driver address 0
Driver address 1
```

Prove:

- frames contain the correct separate address bits;
- health/state do not leak;
- write by address 0 blocks address 1 until high deadline;
- Reset by address 1 advances one shared generation;
- address 0 lazily recognizes known HS without another Reset;
- configured Standard is restored only for the affected addressed device;
- failed Reset makes physical mode unknown for both;
- no Driver owns or ends Bus/backend.

Also test two independent Bus/backend pairs to prove no global state leakage.

## Sanitizers and warnings

Add native environments:

```text
native
native_sanitize
```

`native_sanitize` should use ASan/UBSan where supported. Both use:

```text
-std=c++17
-Wall
-Wextra
-Wpedantic
-Wconversion
-Wsign-conversion
-Wshadow
-Wundef
-Werror
```

Do not suppress warnings globally. A narrowly justified platform-only
suppression must not affect core.

## Coverage manifest

Create `test/COVERAGE_MATRIX.md` mapping:

```text
requirement/finding -> named test -> source/helper under test
```

Every P/A finding in `FINDINGS_REGISTRY.md` must have at least one named native
test or be marked `HIL_ONLY` with the Stage 8 evidence item.

Also map each Stage 4 smoke consumer to its exact build configuration and the
physical-only findings it exercises. A Stage 4 item may be marked `HIL_ONLY`,
but the manifest must still name the smoke source/build and the Stage 8 capture
row that closes it. Do not copy the ESP32 backend into native test support.

Create `tools/check_no_production_placeholders.py`. It scans `include/` and
`src/` for `TODO`, `FIXME`, `placeholder`, and `not implemented`, prints exact
hits, exits nonzero on any hit, and exits zero only when none exist.

## Verification

```text
python -m platformio test -e native
python -m platformio test -e native_sanitize
python tools/check_core_timing_guard.py
python tools/check_no_production_placeholders.py
git diff --check
git status --short
```

If PlatformIO sanitizer integration is unavailable, run an equivalent pinned
host compiler command and document it. Do not silently skip.

The production-source scan must have no hit. If a pre-existing item cannot be
removed, leave the stage blocked; do not use a TODO as deferred stage
implementation.

## Exit criteria

- No Arduino/Wire stubs remain.
- Every public fallible API has matrix coverage.
- Every audit bug has a regression oracle.
- Fake capacity overflow is a test failure, not silent truncation.
- Tests validate behavior independently rather than mirroring implementation.
- The one test-only `TestAccess` exposes no installed public API and is disabled
  in every non-test build.
- No production TODO, FIXME, placeholder, or unimplemented stage path remains.
