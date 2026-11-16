# AT21CS01_AT21CS11 native ESP-IDF promptset

Prepared 2026-11-16 on `main` at `9dc5b144` (clean; equal to the local
`origin/main` ref, not re-fetched while writing).
**Status: NOT STARTED.** These files are executable instructions for a later
coder. Writing them changed no driver, Backend, example, package, checker or CI
behavior. The SHA is an evidence anchor, not an instruction to reset; record
the fetched `main` HEAD when a prompt starts and re-read anything that moved.

## Purpose and current state

On 2026-11-16 the owner elected pure ESP-IDF support with a native example for
this library, built the way the owner's other libraries do it. Today the
repository forbids that work:

- The core is framework-neutral. The only hardware Backend,
  `src/platform/esp32/Esp32Transport.cpp`, already uses only ESP-IDF, SoC and
  FreeRTOS facilities (`:9-28`: `driver/gpio.h`, `esp_cpu.h`, `esp_pm.h`,
  `esp_rom_sys.h`, `esp_timer.h`, `freertos/task.h`, `hal/gpio_ll.h`,
  `soc/gpio_struct.h`, `soc/soc_caps.h`) and no Arduino runtime API. It is
  compiled only when `ARDUINO_ARCH_ESP32` is defined
  (`include/AT21CS/platform/esp32/Esp32Transport.h:11-22,167-173`).
- There is no root `CMakeLists.txt` or `idf_component.yml`; both examples are
  Arduino-only; CI has no ESP-IDF job; `library.json` lists only `arduino`.
- `AGENTS.md:169-187` forbids native IDF. Enforcers:
  `tools/check_core_timing_guard.py:28-32,195-235`,
  `tools/check_docs.py:98-132`, `tools/check_cli_contract.py:10-18,160-166,335-355`
  and `tools/check_package.py:43-67,248-249,311-312`. `README.md:17-22`,
  `docs/MIGRATION.md:100-105` and `CONTRIBUTING.md:37` state the same scope.
  `AGENTS.md:188-194` records the election and points here.

An earlier port (`a389c14a`, `d0618728`, merged by `37e43eba`) was removed
during the v2 rewrite (`db09197b`, `798ad53c`, `9b279741`). Branch
`feature/at21cs-idf-port` (`bb082fd4`) is already an ancestor of `main`. Do not
restore, merge, cherry-pick or resume any of it; build fresh on the current core.

## Owner decisions applied here (final unless a primary source contradicts one)

1. Executing prompt 01 is the scope election; it authorizes the AGENTS, README,
   docs, checker, CI and package edits the prompts list by path and line.
2. ESP-IDF 6.0.1 for local builds and CI, targets `esp32s3` and `esp32s2`;
   `idf_component.yml` declares `idf: ">=6.0.1"`. No older-version claim.
3. One ESP32 Backend: widen the existing `Esp32Transport` guard to
   `ARDUINO_ARCH_ESP32 || ESP_PLATFORM`, register it in the root component with
   verified `PRIV_REQUIRES`, keep its timing code in IRAM and prove that on the
   IDF-built object. Keep the existing `CONFIG_PM_ENABLE` handling.
4. Update the four prohibiting checkers so they enforce the new design; never
   weaken a check of shipped behavior.
5. Example `examples/esp_idf/basic/` is the native port of example 01, with a
   fixed-name local wrapper component, sdkconfig console plus non-blocking
   `getchar`, an example-only CMake identity header and
   `tools/check_idf_example_contract.py`. No `override_path`. The IDF guide is
   the new `docs/ESP_IDF.md`. "Exactly two examples" stays true by edit.
6. One shared framework-neutral CLI engine, extracted from example 01 first with
   Arduino behavior preserved and proven, serves both shells with full command
   parity; `tools/check_cli_contract.py` pins parity for both.
7. Remote CI dispatch and waiting happen once, in prompt 03, on the exact final
   commit; prompts 01-02 use local checks only.
8. CI builds the native example from the validated, extracted package archive
   in a folder not named after the repository; the same build also runs
   locally before the checkpoint.
9. Prompt 03 adds `espidf` to `library.json` `frameworks` with the statement
   that PlatformIO's ESP-IDF framework is not tested; the package ships the
   component and the native example.
10. The `CONFIG_PM_ENABLE=y` build runs on both targets. No version bump in this
    set; a release is a separate owner step.

## Ordered prompts

| # | Prompt | Requires | Type | Status |
|---:|---|---|---|---|
| 01 | [ESP-IDF component and shared ESP32 Backend](01_IDF_COMPONENT_AND_BACKEND.md) | none | software, SDK build | NOT STARTED |
| 02 | [Shared CLI engine and native port of example 01](02_SHARED_CLI_AND_NATIVE_EXAMPLE.md) | 01 synced | software, SDK build | NOT STARTED |
| 03 | [CI, package, final docs and the remote-CI checkpoint](03_CI_PACKAGE_AND_CHECKPOINT.md) | 01, 02 synced | software, CI | NOT STARTED |

After execution this folder stays in `docs/prompts/` as executed historical
requirements (TUSB32x precedent); never delete or archive it. The last prompt
sets this header to
`**Status: EXECUTED (historical requirements, not open work); hardware NOT RUN.**`
and every row to DONE; packages keep excluding `docs/prompts/`.
The folder is kept permanently after execution and stays outside the package
allow-list (`library.json:47-62`). No checker exception is needed for it:
`tools/check_docs.py:116-132` does not list `docs/prompts/`, and
`tools/check_package.py:231-239` fails on any archive member outside the
allow-list. Execution evidence goes to `CHANGELOG.md` `[Unreleased]`,
`docs/ESP_IDF.md` and the final handoff, never into this index.

## Invocation

One prompt:

```text
In C:\Users\Honza\Documents\Projects\AT21CS11, execute
docs/prompts/01_IDF_COMPONENT_AND_BACKEND.md completely. First read AGENTS.md,
..\AGENTS.md, docs/prompts/README.md and the prompt. Fetch, inspect status,
branch and upstream, and work on main; preserve unrelated changes. Implement,
test and document the deliverables, run the prompt's local checks
(C:\esp\idf.cmd 6.0.1 with short build folders; .\scripts\pio.cmd for
PlatformIO), commit only owned files, push and verify upstream equality.
Report every evidence layer as PASS/FAIL/NOT RUN/BLOCKED. Do not start the
next prompt.
```

Whole set: the same text naming 01, 02 and 03 "in order", adding "commit and
sync after each prompt; start the next only when the previous acceptance
criteria hold; stop at the first BLOCKED gate and report it."

## Shared rules

- **Instructions:** follow `AGENTS.md` and `..\AGENTS.md` (CI policy `:3-19`,
  ESP-IDF toolchains `:24-38`), cite them rather than restating them, and keep
  the one-owner rule (`AGENTS.md:89-92`), the library's no-task/no-log rules
  (`:79-99`) and the frozen chip report (`:145-148`).
- **Toolchain:** only `C:\esp\idf.cmd 6.0.1 <idf.py args>` with
  `-B C:\esp\build\at21cs11-<name>-<target>-6.0.1` and
  `-D SDKCONFIG=C:\esp\build\<same>.sdkconfig` (see `C:\esp\README.md`). Never
  install ESP-IDF globally, run Espressif's installer or IDE setup, or set
  `IDF_PATH`/`IDF_TOOLS_PATH`. Do not download a PlatformIO `framework-espidf`
  package. PlatformIO only through `.\scripts\pio.cmd` (`AGENTS.md:36-40`). The
  root `.gitignore:41-48` already ignores ESP-IDF build output.
- **Timing:** do not change Backend timing constants, critical-section or
  scheduler-suspension scope, IRAM attributes or deadlines. AT21CS01 Standard
  Speed masks interrupts for up to about 5.8 ms per segment
  (`README.md:48-54`); every doc that introduces native use repeats that.
- **Irreversible operations:** no prompt, test, example or tool may call or
  expose `permanentlyLockSecurity()`, `permanentlyEnableRomZone()` or
  `permanentlyFreezeRomZones()` (`AGENTS.md:110-120`,
  `docs/IRREVERSIBLE_OPERATIONS.md`). `write-page` keeps its
  `CONFIRM_EEPROM_OVERWRITE` token.
- **No physical board:** nothing is flashed or run on hardware. Hardware and
  HIL evidence stay NOT RUN; a linked image proves linking only.
- **Evidence:** report each layer separately as PASS, FAIL, NOT RUN or BLOCKED
  with the exact command, tool/SDK version, commit and working-tree state:
  host tests; static/contract checks (sub-items: PlatformIO Arduino builds
  `ex_*`/`phy_smoke_*` and their IRAM checks); native ESP-IDF build (S2/S3)
  (sub-item: IRAM and undefined-reference checks on the IDF objects);
  package/clean consumer (including the native payload build); remote CI
  (last prompt only); physical hardware NOT RUN.
- **Verification:** mark anything the prompt flags as "verify" with what was
  observed. Show each changed checker rule rejecting a deliberate violation
  (temporary, reverted) and passing on the real tree.
- **Git (`AGENTS.md:21-34`):** stay on `main`; no branch, worktree, stash,
  discard, force-push or history rewrite. Stage only owned files, commit and
  push each completed chunk and prompt. Sync means fetch, integrate safely,
  push, fetch again, and confirm equal local/upstream IDs with zero
  ahead/behind. Record checkpoint SHAs in the next update or handoff, not in
  a self-referencing commit. Pushes in 01-02 start the existing workflow;
  do not wait on or cite those runs.
- **Documentation:** each commit states only what it proves. Update README,
  `docs/ESP_IDF.md`, `CHANGELOG.md` `[Unreleased]` and
  `test/COVERAGE_MATRIX.md` with the change; `library.json` stays the version
  source and `Version.h` is never edited by hand.

## Out of scope

Hardware or HIL runs; a native port of example 02; PlatformIO's ESP-IDF
framework builds; ESP-IDF versions other than 6.0.1 and targets other than
S2/S3; component-registry publication, release, tag or version bump;
multi-task owner qualification; Kconfig board menus; changes to the frozen chip
report or to Backend timing; anything in sibling repositories.
