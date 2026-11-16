# 01: ESP-IDF component and shared ESP32 Backend

**Status: NOT STARTED.** Prerequisite: none. Successor:
[02](02_SHARED_CLI_AND_NATIVE_EXAMPLE.md). Preparation anchor `9dc5b144`;
record the fetched `main` HEAD. Read [the index](README.md) first; its shared
rules are part of this prompt.

Executing this prompt is the owner's scope election of 2026-11-16. It
authorizes the edits listed below; do not ask for another scope decision. Build
fresh on the current core; restore nothing from the removed port.

## Outcome

The repository root becomes an ESP-IDF 6.0.1 component for `esp32s2` and
`esp32s3` that compiles the core and the existing `Esp32Transport` Backend.
Evidence: linked S2/S3 images of a new IDF smoke consumer (default and
`CONFIG_PM_ENABLE=y`), IRAM checks on the IDF-built Backend objects, and an
unchanged Arduino Backend object. The native example is 02; CI is 03.

## Owned paths

- New: `CMakeLists.txt`, `idf_component.yml`, `docs/ESP_IDF.md`,
  `test/consumer/phy_smoke/idf/**`.
- Guard lines only: `include/AT21CS/platform/esp32/Esp32Transport.h:11-22,167-173`
  and `src/platform/esp32/Esp32Transport.cpp:9-28`.
- `tools/check_core_timing_guard.py`, `tools/check_docs.py`,
  `tools/check_package.py`, `tools/check_esp32_iram_sections.py`.
- `AGENTS.md`, `README.md`, `docs/MIGRATION.md`, `CONTRIBUTING.md`,
  `CHANGELOG.md`, `test/COVERAGE_MATRIX.md`, `Doxyfile` `INPUT`, and
  `library.json` `export.include` (only to add `docs/ESP_IDF.md`).

Not here: any other library or Backend code, `examples/**` (02),
`.github/workflows/**`, `library.json` `frameworks` and packaging of the
component itself (03), the frozen chip report, sibling repositories.

## Authorized policy and guard edits

| Path:line | Today | After this prompt |
|---|---|---|
| `AGENTS.md:45,61` | Arduino-only target; "Arduino-ESP32 Backend API" | Arduino-ESP32 and native ESP-IDF 6.0.1 on S2/S3; one ESP32 Backend |
| `AGENTS.md:169-185` | native ESP-IDF forbidden | core stays neutral; the Backend compiles for both; root component rules; `C:\esp\idf.cmd` tooling; still no PlatformIO `framework-espidf` |
| `AGENTS.md:188-194` | planning note | keep a one-line pointer to `docs/prompts/` until 03 |
| `README.md:17-22`, `docs/MIGRATION.md:100-105`, `CONTRIBUTING.md:37` | no second framework | Backend also builds as a native component (build evidence only; example and CI pending); README links `docs/ESP_IDF.md` |
| `tools/check_core_timing_guard.py:23-32,220-222` | root CMake, manifest and `test/consumer/phy_smoke/idf` forbidden | required, with content rules (step 5) |
| `tools/check_core_timing_guard.py:195-218` | `ESP_PLATFORM` banned; Arduino-only `#error` pinned | widened predicate required at every guarded site |
| `tools/check_docs.py:32-63`, `tools/check_package.py:27-35` | consumer/packaged docs | add `docs/ESP_IDF.md` |

Kept unchanged here: `check_core_timing_guard.py:224-235` (`frameworks`, 03);
`check_docs.py:109,116-132` (the `framework = espidf` text ban and the old
port's paths stay; this set uses new names; 02 removes only
`tools/check_idf_example_contract.py`); `check_cli_contract.py` (its IDF rules
govern examples, 02); `check_package.py:43-67,248-249,311-312` (component
packaging, 03). Do not loosen a rule early for files that do not exist yet.

## Audit before editing

Record path, revision and adopted or rejected for each reference.

- Backend: `Esp32Transport.cpp:9-28` (includes, predicate, target `#error`),
  PM lock `:139-198,212-230,827-858`, scheduler suspension
  `:328,521,559,603`, critical sections `:860-882`, and the final-wait guard
  `:636-672` that compares CPU cycle counts with interrupts enabled. Confirm
  `git grep -n -E "Arduino\.h|millis|micros|delayMicroseconds|pinMode|digitalWrite|Serial" -- include/AT21CS/platform src/platform`
  finds nothing.
- ESP-IDF 6.0.1 (`C:\esp\v6.0.1\esp-idf`): common requirements
  `tools/cmake/build.cmake:329-331`; `ESP_PLATFORM` `:781-782`; default
  `-Wall -Werror -Wextra` `:178-198`; C++ standard choice `:246-262`. Header
  owners: `driver/gpio.h` in `esp_driver_gpio` (publicly requires
  `esp_hal_gpio`, `components/esp_driver_gpio/CMakeLists.txt:32-35`),
  `hal/gpio_ll.h` and `GPIO_IS_VALID_*` in `esp_hal_gpio`, `esp_pm.h` in
  `esp_pm`, `esp_timer.h` in `esp_timer`. Defaults: `CONFIG_PM_ENABLE` off
  (`components/esp_pm/Kconfig:14-18`), CPU 160 MHz
  (`components/esp_system/port/soc/esp32s3/Kconfig.cpu:4`, same for S2),
  `CONFIG_FREERTOS_HZ` 100 (`components/freertos/Kconfig:33-37`), main task on
  CPU0 (`components/esp_system/Kconfig:240-260`).
- Arduino build the Backend was qualified with:
  `C:\pio\packages\framework-arduinoespressif32-libs` (`versions.txt`: ESP-IDF
  v5.5.5 b774170ff46) defines `-DESP_PLATFORM` (`esp32s3/flags/defines`); its
  `sdkconfig.h` has CPU 240 MHz, `CONFIG_FREERTOS_HZ 1000`,
  `CONFIG_COMPILER_OPTIMIZATION_SIZE`, no `CONFIG_PM_ENABLE` and, on S3,
  `CONFIG_ARDUINO_RUNNING_CORE 1`. Verify it is the package the 55.03.311 pin
  resolves (`.\scripts\pio.cmd run -e ex_cli_s3 -v`).
- Siblings, read-only: `SHZK-PT/CMakeLists.txt:1-8` (absolute paths) and
  `SHZK-PT/examples/common/components/shzk_pt/CMakeLists.txt:1-4` (wrapper
  `include()`), `SCD41/examples/idf/basic/components/SCD41/CMakeLists.txt:1-4`
  (why the name is fixed), `MCP45HVX1/idf_component.yml:1-34`.
- `RV3032-C7/CHANGELOG.md:194-196` and `docs/VERIFICATION.md:43-46`: PioArduino
  can write a transient root `idf_component.yml` while repairing its framework.
  This repository's root is also the PlatformIO project.

## Implement

1. **Guard.** Use one predicate,
   `defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)`, at
   `Esp32Transport.h:11,15,167,173` and `Esp32Transport.cpp:9`, and make the
   `#error` name both supported builds. Leave every `AT21CS_TESTING` branch,
   timing constant and attribute as it is. Arduino-ESP32 already defines
   `ESP_PLATFORM`, so the Arduino object must not change (step 6).
2. **Root `CMakeLists.txt`.** `idf_component_register` with
   `${CMAKE_CURRENT_LIST_DIR}`-absolute `SRCS` `src/AT21CS.cpp`, `src/Bus.cpp`,
   `src/platform/esp32/Esp32Transport.cpp` and `INCLUDE_DIRS include`;
   `PRIV_REQUIRES` for the non-common components whose headers the Backend
   includes (expected `esp_driver_gpio esp_hal_gpio esp_pm esp_timer`; verify by
   build and record); no public `REQUIRES` unless a public header needs a
   non-common component (`Esp32Transport.h:16-18` uses common `esp_attr.h` and
   FreeRTOS only); `target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)`;
   a short comment on why paths are absolute. Fix warnings in code, never with
   suppression flags; if a fix would touch timing code, stop and report.
3. **`idf_component.yml`.** Version equal to `library.json`, description, MIT,
   maintainer `Jan Havelka (THYMOS Solution s.r.o.) <info@thymos.cz>`, url and
   repository from `library.json`, `targets: [esp32s2, esp32s3]`,
   `dependencies: idf: ">=6.0.1"`, and `files.exclude` for CI, PlatformIO,
   tests, tools, scripts, Doxygen, audits, prompts, the datasheet PDF and build
   output.
4. **Smoke consumer** `test/consumer/phy_smoke/idf/` (a test build, not an
   example; never packaged): project `CMakeLists.txt`;
   `components/AT21CS01_AT21CS11/CMakeLists.txt` that only `include()`s the root
   file, so the component name does not depend on the checkout directory;
   `main/` with an `app_main` equivalent of
   `test/consumer/phy_smoke/arduino/src/main.cpp` (pin `-1` by default, so no
   GPIO is touched); `sdkconfig.defaults` matching the Arduino build
   (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y`, `CONFIG_FREERTOS_HZ=1000`,
   `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`, `# CONFIG_PM_ENABLE is not set`), one
   comment per setting; `sdkconfig.pm` adding `CONFIG_PM_ENABLE=y`. That is the
   first build anywhere of the Backend's PM-lock branch.
5. **Checkers.** `check_core_timing_guard.py`: replace the forbidden-path list
   with content rules (root file registers exactly the three sources with
   absolute paths, the include directory and the recorded `PRIV_REQUIRES`;
   manifest has `idf: ">=6.0.1"`, exactly the two targets and the
   `library.json` version); require the predicate at every guarded site and
   reject a bare `defined(ARDUINO_ARCH_ESP32)` site; keep the
   `UNSUPPORTED_COMMAND` ban, every clean-core token ban (including
   `ESP_PLATFORM` in core headers) and the `frameworks` rule.
   `check_docs.py`, `check_package.py`, `library.json` `export.include` and
   `Doxyfile` `INPUT`: add `docs/ESP_IDF.md`, and check its required statements
   (step 7). `check_esp32_iram_sections.py`: keep every required method and
   the lambda and helper checks; adapt only for differences the IDF object
   shows; add an option requiring named undefined references, which proves the
   hardware branch compiled (`esp_timer_get_time`, `esp_rom_delay_us`,
   `vTaskSuspendAll`, `xTaskResumeAll`, `gpio_config`; `esp_pm_lock_acquire`
   and `esp_pm_lock_release` only in the PM build). The current CI invocations
   (`.github/workflows/ci.yml:46-63`) must stay valid.
6. **Timing evidence.** Before step 1, build `phy_smoke_s2` and `phy_smoke_s3`
   and save the Backend object's `objdump -d` outside the repository; after
   the change, rebuild and show the disassembly is unchanged. Run the IRAM
   check on both Arduino objects and the four IDF objects (S2/S3, default and
   PM). Compare the out-of-line callees of the IRAM functions (`objdump -dr`)
   between Arduino and IDF objects; an extra flash-resident callee in an IDF
   object is a defect to remove (prefer configuration such as `-Os`) and record.
7. **Docs.** New `docs/ESP_IDF.md`: status and tested matrix (ESP-IDF 6.0.1,
   S2/S3, build evidence only); root component and the fixed-name wrapper
   pattern; recorded `PRIV_REQUIRES`; the guard; Backend limits (at most 8
   payload bytes per frame, `Esp32Transport.cpp:69-72`; 9 ms High-Speed and
   24 ms Standard-Speed transfer deadlines and a 9 ms presence deadline,
   `include/AT21CS/Bus.h:70-72`; a released-high wait of at most 10 ms,
   `Esp32Transport.cpp:52`); timing (up to about 5.8 ms of interrupt masking
   per Standard-Speed segment, scheduler suspended per frame, IRAM placement
   and its check, recommended 240 MHz, `-Os`, 1000 Hz);
   `CONFIG_PM_ENABLE` (an `ESP_PM_CPU_FREQ_MAX` lock created in `begin()` and
   held per timing segment; light sleep NOT RUN); one task owns every object
   and stays pinned to one core because of `Esp32Transport.cpp:636-672` (the
   IDF main task is pinned to CPU0 by default); no ISR use; the pull-up
   envelope by link to `../README.md#esp32-pull-up-and-rise-time-envelope`;
   portable `idf.py` commands (the `C:\esp\idf.cmd` form belongs in
   `CONTRIBUTING.md`); what is not tested (hardware, PlatformIO's ESP-IDF
   framework, other targets and versions). Packaged docs link only to packaged
   files or `https://` URLs, so name the smoke consumer as code text. Do not
   write the literal `framework = espidf` (`check_docs.py:109`,
   `check_package.py:311-312`); only 03 adds the one approved sentence that
   contains it. Apply the table rows above; `CHANGELOG.md`
   (Added: native component; Changed: Backend guard);
   `test/COVERAGE_MATRIX.md` build gates.

## Tests and acceptance

- `native` and `native_sanitize` pass with no test expectation changed.
- S2 and S3 smoke images link, default and PM, under the IDF default warnings
  with no added suppression; the IDF objects pass the IRAM and
  undefined-reference checks; only the PM build references the PM lock calls.
- The Arduino Backend disassembly is unchanged; both Arduino IRAM checks pass;
  all four `ex_*` and both `phy_smoke_*` environments build.
- After every PlatformIO build, `git status` shows no new or modified tracked
  file, the root `idf_component.yml` is byte-identical, and no
  `idf_component.yml.orig` or `managed_components/` appears. Verify; if
  PioArduino rewrites the manifest, stop and record the evidence.
- Every changed checker rule rejects a deliberate violation and passes on the
  real tree.

## Verify (each PASS/FAIL/NOT RUN/BLOCKED)

```text
.\scripts\pio.cmd test -e native
.\scripts\pio.cmd test -e native_sanitize
.\scripts\pio.cmd run -e ex_cli_s3      (and ex_cli_s2, ex_multi_s3, ex_multi_s2)
.\scripts\pio.cmd run -d test/consumer/phy_smoke/arduino -e phy_smoke_s2   (and phy_smoke_s3)
C:\esp\idf.cmd 6.0.1 -C test\consumer\phy_smoke\idf -B C:\esp\build\at21cs11-smoke-esp32s3-6.0.1 -D SDKCONFIG=C:\esp\build\at21cs11-smoke-esp32s3-6.0.1.sdkconfig set-target esp32s3 build
    (also esp32s2; PM builds as at21cs11-smokepm-<target>-6.0.1 with
     -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.pm"; verify the list
     survives idf.cmd, otherwise record the alternative used)
python tools/check_esp32_iram_sections.py --objdump C:\esp\v6.0.1\tools\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin\xtensa-esp32s3-elf-objdump.exe --object (Get-ChildItem C:\esp\build\at21cs11-smoke-esp32s3-6.0.1 -Recurse -Filter Esp32Transport.cpp.obj).FullName
    (each IDF build with its target's objdump; Arduino objects with
     C:\pio\packages\toolchain-xtensa-esp-elf\bin\xtensa-esp32s3-elf-objdump.exe,
     toolchain 14.2.0+20260121 under PLATFORMIO_CORE_DIR=C:\pio, and the
     Esp32Transport.cpp.o under test\consumer\phy_smoke\arduino\.pio\build\phy_smoke_s3;
     re-check both toolchain paths at execution)
python tools/check_core_timing_guard.py
python tools/check_no_production_placeholders.py
python tools/check_cli_contract.py
python tools/check_docs.py
python scripts/generate_version.py --check
python tools/check_package.py --inspect      (and --build-platform-neutral, --build-arduino)
git diff --check
```

## Close out

Commit the guard, component, smoke consumer, checkers and docs as one coherent
checkpoint, push and sync. Report the six evidence layers from the index:
host tests; static/contract checks (with the Arduino builds and their IRAM
checks); native ESP-IDF build (S2/S3, with IRAM and undefined-reference
checks); package/clean consumer; remote CI NOT RUN (03); physical hardware
NOT RUN.
Set this prompt's index status. Hand off the recorded `PRIV_REQUIRES`,
objdump and object paths, the PM-list method and anything 02 or 03 needs.
