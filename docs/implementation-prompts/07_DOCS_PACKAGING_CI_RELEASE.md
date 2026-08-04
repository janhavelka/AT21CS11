# Prompt 07 — documentation, clean packaging, CI, and RC metadata

## Outcome

Make the synchronous v2 library understandable and consumable from a clean
checkout/package. Documentation must match the current API, hot-plug behavior,
multiple-device ownership, and external RTOS responsibility boundary.

This stage creates a `2.0.0-rc.1` candidate only. It does not run HIL, publish a
package, create a tag, or claim hardware qualification.

## Baseline and owned findings

Stages 01-06 must be complete. This stage owns:

- P-18 stale non-protected reference material;
- Q-06 deterministic version generation;
- Q-10 documentation/API consistency;
- Q-11 curated package contents;
- Q-12 CI coverage;
- Q-14 stale non-protected technical claims;
- Q-19 consumer ownership/hot-plug/RTOS responsibility documentation.

It verifies the Stage-06 closure of Q-17 by rejecting native-IDF artifacts and
claims. It does not create an RTOS owner fixture.

## Release metadata

Set the source-of-truth version in `library.json` to `2.0.0-rc.1`. Generate
`include/AT21CS/Version.h` deterministically from it. The generator must support
a read-only `--check` mode and two consecutive generations must be byte-identical.

Do not set stable `2.0.0`; Prompt 08 and an explicit maintainer decision own any
later stable finalization.

## Documentation

Update the non-protected consumer documentation that exists or is required by
the final package, including:

```text
README.md
CHANGELOG.md
CONTRIBUTING.md
SECURITY.md
AGENTS.md repository layout if needed
docs/MIGRATION.md
Doxyfile
```

Remove or clearly archive obsolete native-IDF and resolved migration-plan
documents. Do not modify
`docs/AT21CS01_AT21CS11_complete_driver_report.md`.

Documentation must explain in plain language:

### Synchronous operation

- every library call completes synchronously and returns exact status/evidence;
- the library creates no task, queue, application-facing mutex, scheduler, retry
  loop, logger, or persistence service; private bounded Backend
  timing-critical facilities remain allowed;
- once a call begins, firmware cannot asynchronously cancel it;
- a page write includes its bounded frame and fixed 10 ms released-high hold.

### Multiple devices

- one physical wire uses one Backend and one Bus;
- one to eight uniquely addressed Drivers may share that Bus;
- separate pins use independent Backend/Bus/Driver tuples and may reuse address
  zero;
- Reset and write hold are shared only by Drivers on the same Bus;
- independent Bus state does not leak between pins.

Avoid defining a public “channel.” If plain-language grouping is needed, call it
a wire instance and show the actual Backend/Bus/Driver objects.

### RTOS integration

- safe default: one firmware task/loop owns all AT21CS objects and calls them
  sequentially;
- Drivers sharing one Bus always share the same owner;
- application tasks may exchange copied application-defined messages with that
  task, but message layouts, queues, priorities, deadlines, backoff, and result
  routing are firmware policy;
- multiple tasks simultaneously calling separate ESP32 Backend instances are
  not part of the current qualification;
- no RTOS wrapper or owner framework ships with the library.

### Hot-plug

- absence during initialization retains valid bindings;
- `Esp32TransportConfig::presencePin == -1` disables the optional detect input;
  an enabled valid pin uses `presenceActiveHigh` for active-high/active-low
  mapping and requires stable external bias because internal pulls are off;
- `Bus::readPresenceIndicator()` is one raw logical Bus-wide sample; disabled
  returns `UNSUPPORTED_COMMAND`, false is absence, and callback faults remain
  errors;
- firmware debounces an enabled detect input and calls explicit `recover()`
  after stable attachment;
- without a detect input, firmware may make one liveness `probe()` per bounded
  polling event while online and one `recover()` attempt per event while
  uninitialized/offline; document the examples' configurable 1,000 ms default;
- transport failures may remain `DEGRADED` until `offlineThreshold`; a
  replacement that returns before `OFFLINE`, or entirely between polls, may be
  unobservable without a detect signal;
- the example policy follows public Driver initialization/state and does not
  reinterpret every error or duplicate the Driver lifecycle;
- document the exact `AT21CS_EXAMPLE_*` build-time overrides for SI/O pin,
  optional detect pin, polarity, address, and AT21CS01/AT21CS11 selection; the
  committed defaults keep detect disabled;
- `probe()` is liveness-only and does not replace recovery after power-up;
- a presence input is a connector/Bus hint, not chip or address identity;
- after recovery, firmware may read/compare the serial before using
  application-owned data associated with the previous device;
- the library does not wake itself, debounce, retry automatically, track
  attachment generations, or own replacement policy;
- on a shared Bus, recovery Reset affects every Driver and one detect input
  cannot distinguish addresses; independent Buses remain independent;
- with no detect input, idle removal is unknowable until an explicit operation
  or scheduled probe fails, and a remove/replacement entirely between polls can
  be missed; do not hide that physical limitation.

### Protocol and safety

- no I2C-style scan or current-address API;
- AT21CS11 is High-Speed only;
- exact NACK phase versus transport failure;
- fixed 10 ms Bus-wide released-high software policy with no ACK polling;
- transactional reads and conservative `WriteResult`/`MutationResult` evidence;
- no automatic replay of a possibly committed write;
- irreversible operations are service/provisioning actions and are not exposed
  by shipped examples;
- Arduino ESP32-S2/S3 is the only supported firmware integration, while core
  interfaces remain framework-neutral.

Do not advertise a generic remote harness, temperature range, or physical
production qualification not proven by Prompt 08 evidence.

## Documentation checks

Provide a focused `tools/check_docs.py` that checks:

- local Markdown links;
- documented public symbols against installed headers;
- example and PlatformIO environment names;
- version consistency;
- absence of obsolete v1/native-IDF/RTOS-owner claims;
- consistency of optional detect-pin defaults/polarity and the example polling
  constants with the shared contract and Prompt 06;
- protected report SHA-256 without modifying it;
- authoritative datasheet URL/hash/size consistency.

Configure Doxygen so current public inputs build without warnings treated as
success. Exclude internal prompt/planning files from generated API docs.

## Package contents

Configure `library.json` export rules so the archive contains only consumer
material:

```text
include/AT21CS/**
src/**
library.json
LICENSE
README.md
CHANGELOG.md
docs/MIGRATION.md
examples/01_basic_bringup_cli/**
examples/02_multi_device_cli/**
examples/common/**
```

Exclude repository control files, tests, tools, prompts/plans, root PlatformIO
configuration, native-IDF files, captures, and temporary build output. Package
links must resolve inside the package or point to stable repository URLs.

## Clean consumers

Use the packaged examples as the Arduino clean consumers. Keep or create only
one additional clean consumer:

```text
test/consumer/core_only/     # platform-neutral Bus/Driver compilation
```

Do not create `test/consumer/firmware_owner/`. The packaged single-device and
multi-device examples are the synchronous integration consumers.

The clean core consumer and packaged examples use only installed public headers
and the unpacked package, with no repository-root include path or
checkout-relative library dependency. No consumer uses `framework = espidf`.
The existing Stage-04 `test/consumer/phy_smoke/arduino/` remains a separate
checkout regression fixture with its existing symlink dependency; it is not a
clean-package consumer and is not copied into the package checker.

## Package checker

`tools/check_package.py` must use a safe temporary directory to:

1. create the PlatformIO package archive;
2. compare contents with an explicit allowlist/denylist;
3. unpack outside the repository;
4. build the platform-neutral consumer against the unpacked source;
5. build both examples for S2 and S3 against the unpacked package;
6. reject compiler/include inputs that reach back into the repository;
7. leave the repository clean.

Provide separate inspect, platform-neutral build, and Arduino build modes so CI
failures are easy to diagnose. Never install or invoke native ESP-IDF.

## CI

Use pinned, separated jobs for:

- native tests and strict warnings;
- native sanitizer tests where supported;
- static timing/IRAM/placeholder checks;
- CLI and documentation checks;
- deterministic version verification;
- S2/S3 clean-package builds for both examples;
- separate checkout builds for the existing S2/S3 physical-layer smoke
  consumer;
- package inspection and clean consumer builds.

CI performs no physical HIL and no irreversible mutation.

## Required validation

Run at least:

```text
.\scripts\pio.cmd test -e native
.\scripts\pio.cmd test -e native_sanitize
python tools/check_cli_contract.py
python tools/check_docs.py
python scripts/generate_version.py --check
python tools/check_package.py --inspect
python tools/check_package.py --build-platform-neutral
python tools/check_package.py --build-arduino
.\scripts\pio.cmd run -e ex_cli_s3
.\scripts\pio.cmd run -e ex_cli_s2
.\scripts\pio.cmd run -e ex_multi_s3
.\scripts\pio.cmd run -e ex_multi_s2
.\scripts\pio.cmd run -d test/consumer/phy_smoke/arduino -e phy_smoke_s3
.\scripts\pio.cmd run -d test/consumer/phy_smoke/arduino -e phy_smoke_s2
git diff --check
git status --short
```

Run any repository formatting/static commands introduced by this stage. Claim
only successful commands actually executed.

## Exit criteria

- Public docs match current synchronous API and both hot-plug paths.
- One-task ownership is recommended without supplying an RTOS framework.
- Exactly two Arduino examples build for S2/S3 from the clean package.
- No owner fixture, mailbox DTO, native-IDF artifact, v1 API, or product schema
  is shipped or advertised.
- Package contents are explicit and clean consumers cannot reach the checkout.
- Version generation and CI gates are deterministic.
- Metadata remains `2.0.0-rc.1` and hardware qualification remains clearly
  pending Prompt 08.
