# AGENTS.md - AT21CS01/AT21CS11 Production Embedded Guidelines

## CI and local verification

- Work with CI in mind from the start: inspect the relevant workflows and checks
  before implementation, reuse their commands, and keep changes compatible with
  their platforms, pinned dependencies, generated files and test registration.
- Run focused local checks for each change. Reserve remote CI dispatch and waiting
  for major changes: substantial behavior/API changes, cross-module integration,
  persistence/security changes, build/toolchain/dependency/workflow changes, and
  release or major integration milestones. Judge impact, not just diff size.
- Small documentation, comment, formatting and low-risk isolated edits need
  appropriate local verification, not a CI run after every commit or push. Batch
  related work before requesting CI; do not repeatedly dispatch unchanged suites.
- For major changes, use existing CI and verify the exact final commit through
  terminal results; dispatch it if needed. Fix confirmed failures without weakening
  checks. Report checks not run, blocked or failed honestly; never claim an older
  green run verifies a new commit. Honor required merge/release checks and existing
  hooks; do not change workflow triggers or bypass checks to avoid CI.

## Git workflow

- Before editing, fetch and inspect the current branch and upstream; fast-forward
  only when safe.
- Keep the whole promptset on its existing branch. Do not create a branch or
  worktree per prompt or agent unless the user asks.
- Assume concurrent agents. Preserve unrelated changes; never stash, discard,
  overwrite, or commit them. A dirty worktree alone is not a blocker.
- After relevant checks pass, stage only your own files or hunks, then commit
  and push each meaningful completed chunk, including multiple times within
  one prompt and before finishing. No further permission is needed.
- When using subagents, the lead owns Git mutations for their shared work.
- Confirm upstream synchronization. Follow the CI and local verification policy
  above; report conflicts or sync failures and never force-push to resolve them.

## PlatformIO

On Windows, use `.\scripts\pio.cmd <arguments>`; it selects the current user's
VS Code-managed installation. Never install another PlatformIO Core; if the
wrapper cannot find it, stop and report the missing installation.

## Role and Target
You are a professional embedded software engineer building a production-grade driver for Microchip AT21CS01 and AT21CS11 single-wire EEPROMs.

- Target: ESP32-S2 / ESP32-S3, Arduino framework, PlatformIO.
- Goals: deterministic behavior, robust error handling, maintainable API, predictable field behavior.

## Repository Model (Single Library)

```
include/AT21CS/       - Public API headers only
  CommandTable.h
  Status.h
  Config.h
  Types.h
  Transport.h
  Bus.h
  AT21CS.h
  Version.h           - Auto-generated (do not edit)
  platform/esp32/
    Esp32Transport.h  - Arduino-ESP32 Backend API
src/                  - Implementation (.cpp)
examples/
  01_*/ 02_*/         - Interactive demos
  common/             - Example-only helpers
platformio.ini
library.json
README.md
CHANGELOG.md
AGENTS.md
```

Rules:
- `examples/common/` is not part of the library.
- Keep example set minimal: one full single-device CLI and one multi-device CLI.
- Public headers must stay in `include/AT21CS/`.
- No board-specific pin defaults in library code.

## Core Engineering Rules

- Deterministic behavior: no unbounded loops; every wait must have a timeout/deadline.
- No dynamic allocation in steady-state library operation.
- No logging in library code; examples may log.
- No exceptions.
- No `delay()` in library code except bounded protocol timing/bit windows.
- The library is synchronous and creates no task, queue, scheduler, or
  application-facing mutex. A Backend may use private bounded critical
  facilities for physical timing.
- The safe firmware default is one task or loop owning all AT21CS instances and
  calling them sequentially. Multiple tasks may own separate physical-wire
  instances only after that Backend's simultaneous cross-instance timing has
  been qualified; Drivers sharing one Bus always share one owner.
- Hot-plug is explicit. `Esp32TransportConfig::presencePin == -1` disables the
  optional detect input; an enabled input uses `presenceActiveHigh` to select
  active-high or active-low logic. It is a raw Bus-wide hint, not chip identity.
- Retain valid Backend/Bus/Driver bindings while a device is absent. Firmware
  may debounce an enabled detect input or periodically call `probe()`/`recover()`
  when no detect input exists. The library itself creates no poller, timer,
  retry loop, interrupt handler, or background reconnect work.

## Protocol Rules (AT21CS)

- Single-wire timed bit protocol, MSb-first.
- Discovery handshake required after reset/power-up.
- Preserve ACK/NACK phase granularity in errors.
- During t_WR busy cycle, keep SI/O released high continuously for the fixed
  10 ms Bus write-high interval; do not ACK-poll before that deadline.
- AT21CS11 is High-Speed only; Standard Speed requests must fail cleanly.

## Irreversible Provisioning

- Keep `permanentlyLockSecurity()`, `permanentlyEnableRomZone()`, and
  `permanentlyFreezeRomZones()` out of ordinary runtime command paths.
- Security Lock protects only Security-user bytes. ROM-zone enable protects one
  32-byte EEPROM zone. ROM Freeze locks only the current zone configuration;
  an unenabled zone remains writable forever after Freeze.
- Provisioning must verify device identity and data first, enable and verify the
  complete desired zone map, and Freeze only with explicit authorization.
- Never automatically replay `MAY_HAVE_COMMITTED` or accepted-but-unverified
  mutation evidence. See `docs/IRREVERSIBLE_OPERATIONS.md`.

## Driver State Model

Use explicit state machine in code and docs:

- `UNINIT`, `PROBING`, `INIT_CONFIG`, `READY`, `BUSY`, `DEGRADED`, `OFFLINE`, `RECOVERING`, `SLEEPING`, `FAULT`

Transitions must remain explicit in lifecycle and recovery paths.

## Status / Error Handling

All fallible APIs return `Status`:

```cpp
struct Status {
  Err code;
  int32_t detail;
  const char* msg;
};
```

- No silent failures.
- Validation/precondition errors must not be disguised as transport failures.

## Documentation Guardrail

`docs/AT21CS01_AT21CS11_complete_driver_report.md` is a static chip reference.
Do not modify its technical content unless explicitly requested by the maintainer.

## Versioning and Releases

- Source of truth: `library.json`
- `Version.h` is generated by `scripts/generate_version.py`
- SemVer policy:
  - MAJOR: API breaking changes
  - MINOR: backward-compatible features
  - PATCH: fixes/docs/refactors

## Naming Conventions

- Member variables: `_camelCase`
- Methods/functions: `camelCase`
- Constants: `CAPS_CASE`
- Locals/params: `camelCase`
- Config fields: `camelCase`

---

## Framework Boundary And PioArduino

- The currently supported firmware framework is Arduino on ESP32-S2/S3 through
  this exact PioArduino platform pin:
  `https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip`.
- Public/core code must stay framework-neutral. A future framework may provide
  another Backend without changing Bus or Driver, but native ESP-IDF support is
  not implemented, built, tested, packaged, advertised, or required now.
- Core code must not depend on Arduino runtime APIs, ESP-IDF, FreeRTOS, ESP32
  GPIO headers, board pins, or scheduler policy.
- The explicit ESP32 Arduino transport may use guarded Arduino-ESP32 low-level
  SoC/FreeRTOS facilities supplied by PioArduino. It must compile only in
  `framework = arduino` environments and must not create a parallel
  `framework = espidf` path.
- Do not install or select a standalone ESP-IDF SDK, download a PlatformIO
  `framework-espidf` package, invoke `idf.py`, ship an ESP-IDF example/component,
  or add native-IDF CI/package gates.
- Keep exactly two shipped Arduino examples: one full single-device CLI and one
  concise multi-device CLI.
