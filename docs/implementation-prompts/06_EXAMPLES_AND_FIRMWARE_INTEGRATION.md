# Prompt 06 — safe synchronous examples and RTOS integration guidance

## Outcome

Replace the obsolete example surface with exactly two small Arduino examples
that use the current synchronous API:

1. one complete single-device CLI;
2. one concise two-device/two-pin CLI.

Demonstrate explicit hot-plug recovery and the ownership pattern suitable for a
firmware-owned RTOS task. Do not implement that task, its queue, or an
asynchronous wrapper in this repository.

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

It does not change Backend/Bus/Driver protocol behavior or public API. It does
not implement Prompt 07 packaging/CI or Prompt 08 HIL.

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
strict unsigned/hex parsing. It must:

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

Do not retain floating-point parsing merely because the old load-cell example
used it. The EEPROM examples do not need it.

Add native tests that include the real header and cover empty input, CR/LF,
exact buffer boundaries, overlong discard, maximum argument count, excess
arguments, zero, maximum accepted values, overflow, minus, and trailing text.

## Command contract and safety

`CommandContract.h` contains one authoritative fixed command-spec catalog for
name, usage, and risk. Each example has one registration table selecting its
catalog subset. Every registration references exactly one catalog entry and one
handler; no selected command lacks a handler and no handler lacks a catalog
entry. The two examples need not select the same subset.

The full CLI may contain only these classes of command:

- inspection: `help`, `status`, `presence`, `probe`, `manufacturer`, `serial`;
- bounded reads: EEPROM, Security, Security Lock state, and ROM-zone state;
- lifecycle/configuration: `recover`, supported speed selection, and `shutdown`;
- one bounded EEPROM page-write command.

The multi-device CLI uses a concise subset with an explicit instance selector.

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
tokens. Native tests include the real dispatcher with fake action counters to
prove every registered safe handler is meaningful and malformed/destructive
rejection performs zero action calls. Automated tests never perform real
destructive or irreversible hardware operations.

## Example-only wire instance

`WireInstance.h` is a small example-only aggregate/helper around:

```cpp
AT21CS::Esp32Transport backend;
AT21CS::Bus bus;
AT21CS::Driver driver;
```

It may centralize start, cached serial comparison, and ordered shutdown for the
examples. It is not a compatibility facade or public library API. It creates no
task, queue, mutex, scheduler, or retry loop.

Start order is Backend -> Bus -> Driver. An absent Driver is not a reason to
destroy a successfully started Backend/Bus or discard its Driver binding.

Shutdown order is Driver -> fallible `Bus::end()` -> Backend. If Bus end fails,
keep that Backend alive and report the exact failure.

## Single-device CLI

Use one statically owned wire instance and an explicit expected part. Show:

- Backend/Bus/Driver startup and exact startup status;
- initialized/offline/hot-plug state through copied snapshots;
- raw Manufacturer ID, detected part, revision, and known/unknown speed;
- optional presence indication clearly distinguished from protocol `probe()`;
- serial and bounded EEPROM/Security/ROM reads into fixed caller buffers;
- one bounded confirmed EEPROM page write with exact effect evidence;
- explicit `recover()` after an absent-at-boot or reattached device;
- serial read/comparison after successful recovery;
- ordered shutdown.

If `begin()` reports absence as `NOT_PRESENT` or an identity-phase
`NACK_DEVICE_ADDRESS`, leave the bound CLI usable so a later `recover` command
can succeed. Do not reconstruct the objects, mask an address silently, or hide
a retry.

## Multi-device CLI

Use two statically owned independent instances:

```text
configured pin A -> Backend A -> Bus A -> Driver A, addressBits 0
configured pin B -> Backend B -> Bus B -> Driver B, addressBits 0
```

Configure an explicit expected part for both Drivers; do not rely on
`PartType::UNKNOWN` in production-oriented examples.

Use the existing example pin choices unless the board configuration already
defines better values; do not invent an electrical qualification claim.

One Arduino `loop()` dispatches one complete synchronous command at a time. Show:

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
- RTOS guidance recommends one external synchronous owner without supplying an
  owner framework.
- No task, queue, mailbox, scheduler, retry, or application DTO has entered the
  library, public API, or example surface.
