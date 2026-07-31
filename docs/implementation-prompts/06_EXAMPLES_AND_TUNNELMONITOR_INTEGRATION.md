# Prompt 06 — safe examples and TunnelMonitor-style integration

## Outcome

Replace the oversized/unsafe example surface with minimal examples that
demonstrate the final Bus/Driver ownership model, bounded parsing, exact error
reporting, and safe irreversible-operation interlocks.

This stage does not add application-specific features to the library.

## Required working method

Read all contracts and completed Stages 1–5. Inspect sibling example patterns
in `../MB85RC`, `../PCA9555`, `../TCA9548A`, and `../INA228`; inspect
`../TunnelMonitor-node` owner-operation budgets, static construction, error
propagation, and shutdown rules. Use the README comparison boundary to reject
I2C-only behavior. Preserve unrelated changes.

Spawn subagents for:

1. bounded CLI/parser and destructive-command review;
2. multi-device/shared-Bus review;
3. native ESP-IDF example/contract review;
4. TunnelMonitor consumer-shape review.

Keep one integrator for common example helpers. Reuse helpers; do not copy the
single-device dispatcher into the multi-device example. Refactor and delete
obsolete example infrastructure rather than wrapping it; keep the final
examples smaller than the current set and add no compatibility band-aid. Do not
perform actual irreversible operations during automated tests. Do not modify
the protected report. Do not commit or publish.

## Sole owned findings

Close:

- Q-04 example half;
- Q-05;
- Q-07;
- Q-08 implementation half;
- Q-09;
- Q-13 example interlocks.

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

Use one backend, one Bus, and at least two Driver objects with separate
`addressBits`.

Show:

- independent snapshots/health;
- address selection without reconstructing global state;
- write-high interval shared by the Bus;
- recovery/reset generation effect;
- Bus replacement epoch invalidation without stale-Driver traffic;
- no copied Driver objects;
- serialized access from one loop/owner.
- the same Driver(s) -> Bus -> Backend shutdown rule, including a retained-hold
  error path.

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

## TunnelMonitor-shaped consumer fixture

Add a small compile/test fixture, not product code:

```cpp
struct CachedStatus {
  bool initialized = false;
  AT21CS::DriverState state = AT21CS::DriverState::UNINIT;
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
};

struct PageWriteResult {
  AT21CS::Err code = AT21CS::Err::OK;
  int32_t detail = 0;
  size_t bytesCommitted = 0;
  size_t bytesAccepted = 0;
  AT21CS::WriteEffect effect = AT21CS::WriteEffect::NOT_ATTEMPTED;
};

class At21csModule {
 public:
  AT21CS::Status bind(const ModuleConfig& config);  // copy/validate, zero I/O
  AT21CS::Status initialize();
  AT21CS::Status recover();
  void copyStatus(CachedStatus& out) const;         // cache-only
  AT21CS::Status readPage(
      uint8_t address, uint8_t* data, size_t length);
  PageWriteResult writePage(
      uint8_t address, const uint8_t* data, size_t length);
  AT21CS::Status shutdown();

 private:
  ModuleConfig _config{};
  AT21CS::Esp32Transport _backend{};
  AT21CS::Bus _bus{};
  AT21CS::Driver _device{};
  CachedStatus _cached{};
  bool _backendStarted = false;
  bool _busBound = false;
  bool _deviceBound = false;
};
```

Module lifecycle is exact:

1. `ModuleConfig` contains scalar pins/device policy only.
   `bind()` validates/copies it with no borrowed library object, callback, or
   hardware action.
2. First `initialize()` starts Backend, binds Bus, then begins Driver. If the
   device is absent, Backend/Bus and the valid Driver binding remain owned and a
   later owner action calls `recover()`; it does not reconstruct objects.
3. `shutdown()` ends Driver locally, then calls fallible `Bus::end()`. On a
   retained-hold failure it leaves Backend alive/released and returns the exact
   error so the owner can retry later. Only `Bus::end()==OK` permits
   `Esp32Transport::end()`.
4. If a product needs multiple addressed parts on one wire, this same module
   owns one Backend, one Bus, and a fixed array of Drivers. It never creates one
   Backend/Bus per address.

Prove:

- static ownership;
- only one owner invokes hardware;
- status consumers receive scalar copies; neither `AT21CS::Status::msg` nor any
  callback/context/Bus pointer enters `CachedStatus`;
- no library object/pointer escapes;
- one page write is one bounded owner operation: the fixed 9 ms frame deadline
  plus 10 ms high-only hold is at most 19 ms of library wait budget;
- after a failed/early high hold, the module admits no next Driver call until
  its owner clock has reached cached `writeHighUntilUs`; this prevents a later
  call from combining an old retained wait with a new page operation;
- product retry/backoff/reconciliation remains outside Driver;
- exact error and WriteResult translate into firmware-owned result fields.

Do not add TunnelMonitor-specific types to the AT21CS library.
The ESP32 compile fixture consumes the platform adapter qualified by Prompt 04.
Prompt 07 owns packed clean-consumer construction; this stage does not add a
second transport or package-only wrapper.

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
```

Native IDF S2/S3 builds remain required.

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
- The ownership shape can be wrapped by TunnelMonitor without modifying the
  library.
