# Prompt 07 — documentation, clean packaging, CI, and release metadata

## Outcome

Make the completed v2 library reproducibly consumable from clean Arduino
projects while proving that its core boundary remains framework-independent,
synchronize all documentation/version metadata as a release candidate, and
turn CI into a release gate.

Do not claim production-ready hardware status or set stable `2.0.0` metadata
until Prompt 08 HIL passes and the maintainer authorizes finalization.

Arduino through the exact PioArduino pin frozen by the shared contract is the
only supported firmware framework in documentation, packaging, consumers, and
CI. Keep core framework-independent, but do not install/select standalone ESP-IDF
or retain a native-IDF build/support path. This stage may validate HIL
evidence structure only; Prompt 08 alone runs physical HIL.

## Required working method

Read all contracts, the completed implementation, current documentation,
packaging metadata, and CI. Inspect `git status`. Preserve unrelated changes.

Spawn subagents for:

1. public API/docs/migration consistency;
2. package archive and clean-consumer builds;
3. deterministic version/release metadata;
4. CI/tool pinning and build matrix.

Keep one integrator for metadata and workflow files. Do not modify the technical
content of the protected complete-driver report. Reuse the Stage 4/6 consumer
fixtures and one checker per contract; refactor or delete stale documentation
and build paths rather than documenting aliases or adding packaging band-aids.
Simplify the release surface before adding gates. Follow the packet README's
saga checkpoint policy; do not tag, release, publish, or upload.

## Sole owned findings

Close:

- P-18;
- Q-06;
- Q-10;
- Q-11;
- Q-12;
- Q-14;
- Q-17;
- Q-19.

Prepare, but do not falsely close, Q-03.

## Version decision

This stage prepares the breaking release candidate:

```text
2.0.0-rc.1
```

`library.json` remains the sole version source. Keep the changelog work under
`[Unreleased]` or an explicitly marked `2.0.0-rc.1` heading; do not date or
close a stable `2.0.0` release in this stage.

Set `library.json.frameworks` to Arduino only and keep the ESP32 platform claim
limited to the tested S2/S3 PioArduino environments. Framework-neutral core
design is an architectural property, not permission to advertise an unbuilt
framework.

Synchronize:

```text
library.json
include/AT21CS/Version.h
CHANGELOG.md release-candidate/unreleased heading
Doxygen project version if present
```

Remove unsupported native-IDF metadata/build artifacts rather than assigning
them release-candidate versions. Do not silently publish stable `2.0.0` as a
workaround for any packaging-tool limitation.

## Deterministic Version.h

Refactor `scripts/generate_version.py`:

- input is `library.json`;
- output contains SemVer numeric/string constants only;
- no wall-clock time;
- no dirty flag;
- no maintainer checkout commit;
- no environment-dependent absolute path;
- write only when content differs;
- add `--check` that performs no write and exits nonzero on mismatch.

Two generation runs must be byte-identical and leave `git status` unchanged.

Add:

```text
python scripts/generate_version.py --check
python tools/check_version_consistency.py
```

Do not hand-edit `Version.h`.

## Documentation

Update:

```text
README.md
CHANGELOG.md
CONTRIBUTING.md
SECURITY.md
AGENTS.md repository tree and example-count rule
docs/MIGRATION.md
remove or archive `docs/IDF_PORT.md` and `docs/IDF_PORT_IMPLEMENTATION.md` as
unsupported historical material
docs/ARCHITECTURE_SPLIT_PLAN.md or archive it as resolved
Doxyfile
```

Required content:

- exact Backend -> Bus -> Driver ownership;
- shared-wire topology: one Backend -> one Bus -> one to eight uniquely
  addressed Drivers;
- separate-wire topology: one complete Backend -> Bus -> Driver tuple per
  load-cell/peripheral connector, with the same `addressBits` (normally zero)
  valid on every independent Bus;
- one live Driver claim per address per Bus and no mutable global device state;
- per-Bus external serialization, with one firmware owner serializing all
  AT21CS channels by default because simultaneous ESP32 frame execution is not
  part of the qualified v2 contract;
- no internal mutex/task/retry/recovery policy;
- binding and absent-at-boot recovery;
- `probe()` is liveness only; reconnect/power-up uses `recover()`, followed by
  serial-number reconciliation before application calibration is reused;
- a presence input is a connector hint, not chip identity or per-address
  presence proof;
- connector/pin maps, request queues/deadlines, calibration records and units,
  expected-serial association, replacement policy, and retry/backoff remain
  upper-firmware responsibilities;
- each independent SI/O wire needs its own electrically qualified pull-up and
  harness; product cable/connector/protection claims are limited to Prompt 08
  profiles actually tested;
- shutdown order is Driver(s) -> fallible `Bus::end()` -> Backend, independently
  for every physical wire;
- exact state semantics;
- page write as bounded owner scheduling unit;
- fixed 10 ms bus-global high-only software policy, clearly labeled as requiring
  board/part/temperature qualification rather than a universal beyond-25 C
  datasheet guarantee unless current vendor evidence explicitly supplies one;
- WriteResult/MutationResult ambiguity;
- no current-address API;
- no Freeze-state query;
- AT21CS11 High-Speed only;
- exact Arduino S2/S3 support matrix actually built and an explicit statement
  that the core interface is framework-independent while no other framework is
  currently implemented or supported;
- no board pin defaults in library;
- HIL status and limitations;
- no I2C-style address scan command; A2:A0 selection is explicit;
- migration table from every removed v1 API to v2 equivalent.

Verify the maintainer-updated `AGENTS.md` example rule remains:

```text
Keep the shipped example set minimal: one full Arduino single-device CLI and
one concise Arduino multi-device CLI. Do not ship a native-IDF example.
```

Remove any native-IDF example, checker, component, fixture, CMake-only support
path, or documentation claim. This closes Q-17 while preserving the Stage
1-corrected no-ACK-poll `tWR` rule and every other governing constraint.

Correct the non-protected datasheet reference:

- CRC-8/Maxim is reflected;
- `tMRS` is absolute from falling edge;
- Check Lock frame;
- no opcode `1h/R` Freeze query.

For the similarly named checked-in PDF fingerprint recorded in this prompt
pack, either replace it with the exact verified DS20005857I artifact or remove
the duplicate and keep the immutable official URL/hash instructions. Never
retain it under an authoritative-current label merely because its filename
matches.

Never change the protected complete-driver report.

Remove/qualify:

- “production-grade” before Prompt 08 passes;
- false command parity;
- false journal/wear-leveling claims;
- incorrect `capacityGramsDiv10` schema;
- nonexistent PlatformIO environments;
- obsolete 1.x support statements;
- stale AI implementation prompt presented as current guidance.

Keep this prompt pack as implementation history but exclude it from installed
API/Doxygen output if it is not consumer documentation.

## Documentation checks

Create `tools/check_docs.py` to verify:

- local Markdown links;
- README API symbol list against installed headers;
- PlatformIO environment names;
- version references;
- package-local referenced docs exist;
- protected report has exact SHA-256
  `4B39CBD8437A6DC33EF1E0764FA3A4652F57E41ADC2174F420D1897006F96255`,
  independent of the current Git diff;
- the authoritative DS20005857I URL, size, and SHA-256 in this prompt pack
  match `README.md`;
- Doxygen inputs exclude obsolete prompt/planning material.

Configure Doxygen warnings as errors.

## Explicit package contents

Use `library.json` export/include settings so the archive includes only consumer
material:

```text
include/AT21CS/**
src/**
library.json
LICENSE
README.md
CHANGELOG.md
docs/MIGRATION.md
supported examples and required common helpers
```

Exclude:

```text
.git/**
.github/**
.pio/**
build/**
test/**
tools/**
scripts/** except when intentionally shipped
internal audit/prompt/planning docs
Doxyfile
platformio.ini
idf_component.yml
native-IDF-only CMake/component files
temporary captures
```

If packaged README links to a document, include it or change the link to a
stable repository URL.

## Clean consumer fixtures

Create:

```text
test/consumer/arduino/
  platformio.ini
  src/main.cpp

test/consumer/core_only/
  CMakeLists.txt
  main.cpp

test/consumer/phy_smoke/
  arduino/

test/consumer/firmware_owner/
  platformio.ini
  src/FirmwareOwnerPolicy.h
  src/main.cpp
```

Rules:

- consume a packed/unpacked library, not repository `src/**`;
- include only installed public headers;
- use no repository-root include path;
- Arduino builds S2/S3;
- core-only build proves platform-neutral Bus/Driver without ESP32 backend;
- reuse the Stage 4 `phy_smoke` consumers rather than creating replacement PHY
  code;
- the generic firmware-owner fixture implements Prompt 06's exact
  `BoardConfig`, `ChannelConfig`, `ChannelOperation`, `OwnerResultCode`,
  `ChannelRequest`, `ChannelResult`, `CachedChannelStatus`, `OwnerState`,
  `At21csChannel`, and `At21csOwner<MAX_CHANNELS>` contracts rather than
  product-specific facades;
- it statically owns two complete Backend -> Bus -> Driver tuples on distinct
  SI/O pins, both with `addressBits=0`, plus fixed buffers/rings and one explicit
  owner context;
- it builds for both S2 and S3 from the unpacked archive, stays under
  `test/consumer/` so it is neither a shipped example nor package content, and
  contains no address scan, dynamic allocation, unsynchronized cross-task
  snapshot read, or hidden library recovery loop;
- its `platformio.ini` takes only `AT21CS_FIXTURE_LIB_SPEC`; reuse
  `tools/run_firmware_owner_fixture.py` and never add a relative checkout path;
- either add a documented standalone core CMake target, or make the core-only
  fixture compile exactly the unpacked `src/Bus.cpp` and `src/AT21CS.cpp`;
  never reach back into the repository checkout;
- no clean consumer uses `framework = espidf`, provisions
  `framework-espidf`, or treats framework independence as a native-IDF support
  claim.

## Package checker

Create `tools/check_package.py`:

1. run `pio pkg pack`;
2. inspect explicit allowlist/denylist;
3. unpack into a new temporary directory;
4. build platform-neutral and Arduino consumers against that archive;
5. build supported examples as consumers;
6. copy the generic firmware-owner fixture into the checker's temporary root,
   invoke `run_firmware_owner_fixture.py` with the unpacked package as
   `--library-root`, the copied fixture as `--fixture-root`, and the repository
   checkout as `--forbid-root`; build S2 and S3 and reject any compiler/linker
   input that reaches the checkout;
7. build the Stage 4 Arduino PHY smoke fixtures;
8. reject repository-root includes;
9. reject referenced-but-missing docs;
10. leave repository clean.

Use safe temporary-directory APIs. Never delete a computed path without
verifying it lies inside the created temporary root. Resolve both paths and
require the package/fixture temporary root to be outside the repository before
using the repository as `--forbid-root`.

The checker must not clone, download, install, select, or mutate a standalone
ESP-IDF SDK or a PlatformIO `framework-espidf` package. PlatformIO may provision
only the Arduino packages declared by the exact PioArduino pin. Give the
checker separate modes:

```text
python tools/check_package.py --inspect
python tools/check_package.py --build-platform-neutral
python tools/check_package.py --build-arduino
```

`--build-arduino` builds the S2/S3 examples, firmware-owner fixture, and Stage
04 PHY smoke consumer through PioArduino 55.03.311 with
`framework = arduino`. `--build-platform-neutral` proves the core boundary
without claiming another supported firmware framework.

## PlatformIO cleanup

Stop compiling the whole repository as application source.

Create explicit environments:

```text
native
native_sanitize
ex_cli_s2
ex_cli_s3
ex_multi_s2
ex_multi_s3
```

Examples must resolve the library through normal package/library semantics.
Remove unused Arduino/Wire test include paths.

## HIL evidence infrastructure

Create the release-evidence infrastructure now so CI can validate its shape
before hardware is run:

```text
tools/check_hil_evidence.py
docs/validation/HIL_MATRIX.md
docs/validation/RUN_RECORD_SCHEMA.md
docs/validation/runs/RUN_RECORD_TEMPLATE.md
docs/validation/captures/README.md
```

Stage 7 owns only the checker, schema, empty/template records, and an
`HIL_MATRIX.md` containing the exact HIL-01 through HIL-09 rows specified by
Prompt 08. Stage 8 fills reviewed measurements and capture references.

The checker must:

- provide `--structure-only` without requiring completed hardware results;
- provide `--require-release-matrix` for Stage 8;
- validate required fields, explicit row IDs, relative links, SHA-256 syntax,
  evidence-size policy, and tested-source/firmware identifiers;
- never fabricate, normalize, or edit measurement data;
- accept evidence-only commits after the immutable tested release-candidate
  commit before finalization, while rejecting intervening changes to code,
  public headers, build files, manifests, packaged docs/examples, or package
  contents;
- after maintainer-authorized stable finalization, accept only a parsed,
  exact-field allowlist: the version scalar in `library.json`, generated SemVer
  constants in `Version.h`, Doxygen project version, changelog version/date
  heading, README qualification status, and evidence references. Reject any
  other change even when hidden in one of those files. Record both RC and final
  source/package digests.

CI runs only `--structure-only`. Hardware absence must never be rendered as a
green HIL result.

## CI jobs

Pin exact PlatformIO, Python, host compiler, clang-format, Doxygen, PioArduino
platform/framework packages, and action commit versions in the workflow or a
reviewed tool-version file. Do not use floating branches, moving `x` endpoints,
unpinned container tags, or mutable major-only action tags. Create separate
jobs:

1. `static-contracts`
   - core boundary guard;
   - semantic CLI contracts;
   - docs/link/API checks;
   - exact `python tools/check_format.py` format gate;
   - `python tools/check_no_production_placeholders.py`;
   - `git diff --check`.
2. `native-tests`
   - strict warnings;
   - normal tests;
   - ASan/UBSan.
3. `arduino-builds`
   - S2/S3 single-device and multi-device examples plus the generic
     firmware-owner fixture as clean consumers.
4. `package-consumers`
   - pack, inspect, and unpack once;
   - platform-neutral and Arduino S2/S3 builds;
   - fail if native-IDF artifacts or support claims enter the archive.
5. `docs`
   - Doxygen warnings fatal.
6. `release-metadata`
   - deterministic version;
   - synchronized metadata;
   - clean generated tree;
   - archive license/README/allowlist.
7. `hil-evidence-structure`
   - `python tools/check_hil_evidence.py --structure-only`;
   - never runs hardware or claims a qualified matrix.

Do not make HIL a fake green cloud job. CI verifies the structure and freshness
of maintainer-supplied HIL evidence; Prompt 08 owns actual hardware results.

## Required commands

```text
python scripts/generate_version.py --check
python tools/check_version_consistency.py
python tools/check_docs.py
python tools/check_cli_contract.py
python tools/check_format.py
python tools/check_hil_evidence.py --structure-only
python -m platformio test -e native
python -m platformio test -e native_sanitize
python -m platformio run -e ex_cli_s2 -e ex_cli_s3
python -m platformio run -e ex_multi_s2 -e ex_multi_s3
python tools/run_firmware_owner_fixture.py --library-root . --environment firmware_owner_s2 --environment firmware_owner_s3
python tools/check_package.py --inspect
python tools/check_package.py --build-platform-neutral
python tools/check_package.py --build-arduino
python tools/check_no_production_placeholders.py
doxygen Doxyfile
git diff --check
git status --short
```

All ESP32 clean-consumer builds use the exact PioArduino 55.03.311
`framework = arduino` environments. A missing S2/S3 Arduino environment or any
native-IDF package/build path is a release blocker. The production-source scan
must have no hit.

## Stable finalization after Prompt 08

Do not execute this subsection during the normal Stage 7 pass. After Stage 8
passes against an immutable `2.0.0-rc.1` candidate and the maintainer explicitly
authorizes stable finalization:

1. change `library.json` from `2.0.0-rc.1` to `2.0.0`;
2. regenerate `Version.h` and synchronize Doxygen;
3. convert the release-candidate/unreleased changelog entry into the dated
   stable `2.0.0` entry;
4. update README HIL status using the reviewed evidence row IDs;
5. rerun every required command in this prompt, the exact PioArduino Arduino
   S2/S3 matrix, `check_hil_evidence.py --require-release-matrix`, clean package
   consumers, documentation, and version consistency;
6. record the final source/package digest and verify that only authorized
   finalization fields and evidence documentation changed from the tested RC.

Any production, build-surface, example, public-header change outside the exact
generated SemVer constants, or package-content change outside the exact
metadata/documentation allowlist invalidates the RC HIL evidence and requires a
new release candidate plus affected HIL reruns. No tag, publication, upload, or
release creation is automatic.

## Exit criteria

- All installed metadata reports `2.0.0-rc.1` consistently.
- Version generation is reproducible.
- Clean consumers build from the archive.
- The generic firmware-owner fixture builds for S2/S3 with two independent
  address-zero channels and no public-library product/scheduler types.
- Package contents are intentional.
- Documentation matches actual headers and behavior.
- CI covers every software release gate.
- HIL checker/schema/templates exist and `--structure-only` passes without
  fabricating hardware success.
- `AGENTS.md` permits exactly the two Arduino CLIs and no native-IDF example.
- README still labels hardware qualification pending until Prompt 08 succeeds.
- Stable `2.0.0` remains an authorized post-HIL action, not a Stage 7 outcome.
