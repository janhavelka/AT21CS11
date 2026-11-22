# 03: CI, package and consumer rules, final docs and the remote-CI checkpoint

**Status: DONE.** The original implementation requirements follow.

Prerequisites: [01](01_IDF_COMPONENT_AND_BACKEND.md)
and [02](02_SHARED_CLI_AND_NATIVE_EXAMPLE.md) committed and synced; inspect
their final diffs and handoffs first. This is the set's only remote-CI
checkpoint. Read [the index](README.md); its shared rules are part of this
prompt.

## Outcome

The existing workflow gains native ESP-IDF jobs. The package contains the
component and the native example under exact rules, and CI builds that
example from the validated, extracted package archive in a folder not named
after the repository. `library.json` lists `espidf`. The docs describe the
final supported scope. The exact final commit passes remote CI.

## Owned paths

`.github/workflows/ci.yml`; `library.json` (`description`, `keywords`,
`frameworks`, `export.include`); `tools/check_package.py`;
`tools/check_core_timing_guard.py:224-235`; `tools/check_docs.py` (package
lists and the one approved sentence below); CI rules in
`tools/check_idf_example_contract.py`; `README.md`, `CONTRIBUTING.md`,
`docs/ESP_IDF.md`, `docs/MIGRATION.md`, `CHANGELOG.md`,
`test/COVERAGE_MATRIX.md`, `AGENTS.md`; the index header and Status column.
Library, Backend or example code changes only to fix a confirmed CI failure
within the contracts of 01 and 02; record each one.

## Authorized edits

| Path:line | Change |
|---|---|
| `library.json:34-36` | `frameworks: ["arduino", "espidf"]`, the sibling convention (INA228, MCP45HVX1, OPT4001, VTN4xx, SystemChrono) |
| `library.json:4,5-14` | description and keywords name native ESP-IDF |
| `library.json:47-62` | add root `CMakeLists.txt`, `idf_component.yml` and the exact native example files; no glob that can pick up local build output; `docs/prompts/` stays out |
| `tools/check_core_timing_guard.py:224-235` | require exactly `["arduino", "espidf"]` |
| `tools/check_package.py:27-42` | exact allow-list including the native files |
| `tools/check_package.py:58-67` | `CMakeLists.txt` and `idf_component.yml` allowed only at their exact packaged paths; denied everywhere else |
| `tools/check_package.py:43-57,248-249` | unchanged: tests, tools, scripts and `espidf_basic` stay denied |
| `tools/check_package.py:299-312`, `tools/check_docs.py:109,196-202` | allow exactly the approved sentence below; any other `framework = espidf` text still fails. Comment the exception in both checkers as permanent and prove it with a mutation |
| `tools/check_package.py:513-519` | new `--export-archive` and `--build-idf` modes |
| `tools/check_docs.py:41-63` | package lists match the new allow-list |
| `.github/workflows/ci.yml` | new jobs; existing jobs, triggers (`:3-5`), permissions and pins unchanged |
| `AGENTS.md:188-194` and 01's pointer | remove the planning note; state the native rules and CI jobs as current |

Approved sentence, verbatim, in `README.md` and `docs/ESP_IDF.md`:
"`library.json` lists `espidf` because the package ships the ESP-IDF component
and native example; PlatformIO `framework = espidf` builds are not tested (only
`idf.py` 6.0.1 for esp32s3/esp32s2)."

## Audit before editing

- The whole of `ci.yml`: `native`, `native-sanitize`, `static` (`:36-63`,
  Arduino IRAM steps `:46-63`), `docs-contract` (`:65-74`), `version`,
  `package-inspect`, `package-core`, `package-arduino`, `phy-smoke`
  (`:122-136`). Every action is pinned by full SHA with a version comment;
  keep that style.
- Action pins, verified 2026-11-16 with
  `gh api repos/<owner>/<repo>/git/ref/tags/<tag>` (all lightweight tags
  pointing at commits, so no peeling): `espressif/esp-idf-ci-action` tag
  `v1.2.0` = `e6f5c74232b1ccd4c97ed641f1e48553853f1fd5` (`v1` is a branch whose
  head `9d38657f3d789ca759b2b37aaf5ceffbc42c4f0d` differs only in `README.md`);
  `actions/upload-artifact` tag `v4` =
  `ea165f8d65b6e75b540449e92b4886f43607fa02`; `actions/download-artifact` tag
  `v4` = `d3f86a106a0bac45b974a628896c90dbdf5c8093`. Re-verify each at
  execution and record the result; never pin an unverified SHA.
- Sibling jobs, read-only: `OPT4001/.github/workflows/ci.yml:155-187`,
  `INA228/.github/workflows/ci.yml:95-124,153`; archive hand-off to an IDF job
  `TUSB32x/.github/workflows/ci.yml:189-201,243-258`.
- `check_package.py`: `expected_files` `:188-202`, archive validation
  `:217-253`, link and text checks `:275-312`, the Arduino clean-build trace
  check `:487-510`, the repository-unchanged check `:522-548`.
- What `pio pkg pack` puts in the archive with the root CMake files present;
  verify against the allow-list.

## Implement

1. `library.json` edits, the `frameworks` rule in `check_core_timing_guard.py`,
   the package lists and the approved-sentence exception in `check_docs.py`
   and `check_package.py`.
2. `check_package.py`: exact native entries; the packaged `idf_component.yml`
   is byte-identical to the committed one (its content rules stay in
   `check_core_timing_guard.py`, from 01).
   - `--export-archive <dir>`: pack and validate as `--inspect` does, then
     write the archive and a SHA-256 file to `<dir>` for the CI artifact.
   - `--build-idf --out <dir> --target <target> --idf-command <command>`: pack,
     validate and extract into the caller-supplied short directory, which must
     lie outside the checkout, not be named `AT21CS11` and not be under the
     system temp directory (its depth breaks ESP-IDF 6 builds on Windows,
     `C:\esp\README.md:61-66`); locally `C:\esp\build\at21cs11-pkg` with
     `C:\esp\idf.cmd 6.0.1`. Build the extracted `examples/esp_idf/basic`
     with a short build folder, require the identity status `unknown`, and
     reject any reach into the checkout (build output and dependency traces,
     as `:487-510` does). Keep the repository-unchanged check.
3. CI, matrix `esp32s3` and `esp32s2`, `fail-fast: false`, logs uploaded with
   `if: always()`; the IDF action mounts only `$GITHUB_WORKSPACE`, so every
   path it builds lies under it:
   - `esp-idf-example`: build `examples/esp_idf/basic` with ESP-IDF v6.0.1 and
     run the IRAM check on its Backend object in the same action `command`
     (`xtensa-<target>-elf-objdump` on PATH; verify).
   - `esp-idf-smoke`: build `test/consumer/phy_smoke/idf` default and PM on
     both targets, with the IRAM and undefined-reference checks.
   - `package-payload`: the existing runner setup plus
     `python tools/check_package.py --export-archive payload`, uploaded with
     `if-no-files-found: error`.
   - `package-idf` (`needs: package-payload`): no checkout. Download into
     `$GITHUB_WORKSPACE/payload`, verify the SHA-256, extract into
     `$GITHUB_WORKSPACE/consumer`, fail (never skip) if any packaged example
     file is missing, and build `consumer/examples/esp_idf/basic` with the
     action. Without a checkout the build cannot reach one.
   - `docs-contract` also runs `python tools/check_idf_example_contract.py`
     and `python tools/test_check_idf_example_contract.py`.
   Then make `check_idf_example_contract.py` require those job names, the
   action pins, the version, targets and paths.
4. Docs: `README.md:17-22` (replace "planned" with the tested native scope and
   the approved sentence) and `:331-352` gates; `CONTRIBUTING.md:6-22` gates,
   including the `C:\esp\idf.cmd 6.0.1` and `--build-idf` commands;
   `docs/ESP_IDF.md` final matrix, the approved sentence, CI job names and the
   package consumer; `docs/MIGRATION.md` (native use needs ESP-IDF >= 6.0.1;
   Arduino use unchanged); `CHANGELOG.md` `[Unreleased]` (Added: component,
   native example, CI jobs; compatibility: native use needs ESP-IDF >= 6.0.1,
   only 6.0.1 is build-tested, no hardware run); `test/COVERAGE_MATRIX.md`;
   AGENTS. Now that the example is packaged, packaged docs may link it. No
   version bump; a release is a separate owner step.

## Tests and acceptance

- Every existing job still passes; the new jobs pass for both targets.
- The archive matches the allow-list exactly and excludes `docs/prompts/`;
  `--build-idf` links the extracted example for S2 and S3 without reaching
  into the checkout, and the repository is unchanged afterwards.
- `check_core_timing_guard.py`, `check_docs.py`, `check_cli_contract.py`,
  `check_idf_example_contract.py`, `test_check_idf_example_contract.py` and
  `check_package.py --inspect` pass, and each changed rule rejects a
  deliberate violation (including a second `framework = espidf` sentence).

## Verify locally (each PASS/FAIL/NOT RUN/BLOCKED)

Every command of 01 and 02, plus, before the checkpoint:

```text
python tools/check_package.py --build-idf --out C:\esp\build\at21cs11-pkg --target esp32s3 --idf-command "C:\esp\idf.cmd 6.0.1"
    (and esp32s2; record the exact arguments if the final option names differ)
python tools/check_package.py --export-archive C:\esp\build\at21cs11-payload
python -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml', encoding='utf-8'))"
    (NOT RUN if PyYAML is unavailable; use actionlint if installed)
git diff --check
```

## Remote-CI checkpoint

After the final commit is pushed and synced, find the push-triggered `CI` run
for that exact SHA (`gh run list --workflow CI --commit <sha>`), wait for the
terminal result (`gh run watch <id> --exit-status`), and record the run ID and
URL, every job with its conclusion, and the SHA. On a failure, fix it within
scope, re-run the affected local checks, commit, push, sync and verify the new
exact final commit; never cite an older run. If CI cannot run (access, quota),
report BLOCKED with the exact reason and the local SHA.

## Close out

Set the index header to
`**Status: EXECUTED (historical requirements, not open work); hardware NOT RUN.**`
and every row to DONE; keep `docs/prompts/` permanently (never delete or
archive it) and outside the package. Report the six evidence layers: host
tests; static/contract checks (with the Arduino builds); native ESP-IDF build
(S2/S3, with IRAM checks); package/clean consumer (including the native
payload build); remote CI; physical hardware NOT RUN. Remaining gates:
hardware and HIL NOT RUN; PlatformIO's ESP-IDF framework NOT RUN; no release,
tag or version change.
