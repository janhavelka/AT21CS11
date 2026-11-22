# Native ESP-IDF implementation record

**Status: EXECUTED (historical requirements, not open work); hardware NOT RUN.**

The three prompts below record the November 2026 addition of native ESP-IDF
support for ESP32-S2 and ESP32-S3. The integration guide covers the shipped
component and example. The coverage matrix distinguishes build results from
physical tests. [ESP-IDF integration guide](../ESP_IDF.md) · [Coverage matrix](../../test/COVERAGE_MATRIX.md)

| Prompt | Status |
|---|---|
| 01. [ESP-IDF component and shared ESP32 Backend](01_IDF_COMPONENT_AND_BACKEND.md) | DONE |
| 02. [Shared CLI engine and native port of example 01](02_SHARED_CLI_AND_NATIVE_EXAMPLE.md) | DONE |
| 03. [CI and package checks, consumer builds and final documentation](03_CI_PACKAGE_AND_CHECKPOINT.md) | DONE |

Keep these records in `docs/prompts/` and outside distributed packages.
The prompts describe the repository as it stood during preparation at
`9dc5b144`. Their line numbers and scope decisions refer to that baseline.
[Original planning notes and invocation](https://github.com/janhavelka/AT21CS11/blob/7a499ce56654fc8db525b9617d3e38cabcc9a1d5/docs/prompts/README.md)

<details>
<summary>Original owner decisions and shared requirements</summary>

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

</details>
