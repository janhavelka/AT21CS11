# 02: Shared CLI engine and native ESP-IDF port of example 01

**Status: NOT STARTED.** Prerequisite: [01](01_IDF_COMPONENT_AND_BACKEND.md)
committed and synced, with its recorded `PRIV_REQUIRES`, IRAM evidence and
smoke builds; inspect its final diff first. Successor:
[03](03_CI_PACKAGE_AND_CHECKPOINT.md). Read [the index](README.md); its shared
rules are part of this prompt.

## Outcome

Two checkpoints, in order:

- **A.** Example 01's command handling moves into one framework-neutral engine
  in `examples/common/`. The Arduino example becomes a thin shell with
  byte-identical console behavior, proven by host tests and the existing
  contract checks.
- **B.** `examples/esp_idf/basic/` is the native ESP-IDF port of example 01:
  the same engine and the same 14 commands, S2/S3 images linked with ESP-IDF
  6.0.1, IRAM-checked, documented, with its own contract checker.

## Owned paths

- `examples/01_basic_bringup_cli/main.cpp`, `examples/common/StatusText.h`,
  new `examples/common/CliOutput.h` (bounded output sink),
  `examples/common/BringupCli.h` (engine) and one Arduino output adapter in
  `examples/common/` (other names are fine if recorded).
- `examples/02_multi_device_cli/main.cpp`, only to pass the output sink; its
  output stays byte-identical.
- New `examples/esp_idf/basic/**`, `tools/check_idf_example_contract.py` and
  `tools/test_check_idf_example_contract.py`.
- `tools/check_cli_contract.py`, `tools/check_docs.py`,
  `test/test_example_cli.cpp` (or a new `test/test_bringup_cli.cpp`) and its
  registration in `test/test_main.cpp`.
- `AGENTS.md`, `README.md`, `docs/ESP_IDF.md`, `CHANGELOG.md`,
  `test/COVERAGE_MATRIX.md`.

Not here: library or Backend code; the command catalog
(`examples/common/CommandContract.h:51-82` stays frozen; no new command);
workflows, `library.json` and package rules (03).

## Authorized edits

| Path:line | Change |
|---|---|
| `AGENTS.md:73-75,186-187` | exactly two example programs: 01 (an Arduino shell and a native ESP-IDF shell over one engine) and 02 (Arduino) |
| `README.md:307-329` | add the native shell and its build |
| `tools/check_cli_contract.py:10-18` | exact layout including the engine and native files |
| `tools/check_cli_contract.py:61-87` | the single-device marker stops requiring `millis()` (`:73`); `MULTI_ACTION_MARKERS` (`:78-87`) stays unchanged |
| `tools/check_cli_contract.py:160-166` | drop the `tools/check_idf_example_contract.py` ban |
| `tools/check_cli_contract.py:300-329` | parse single-device registrations and handlers from the engine; both shells delegate to it and define none |
| `tools/check_cli_contract.py:330-358` | keep `app_main`, `freertos/task.h` and `gpio_set_level` banned in Arduino examples and `examples/common/`; ban `Arduino.h`, `Serial.`, `millis(`, `delay(` and `String ` (with trailing space) in `*.h`/`*.cpp` under `examples/esp_idf/` (the scan at `:330-334` reads every file, and console text needs "USB Serial/JTAG"); keep irreversible and obsolete tokens banned everywhere |
| `tools/check_cli_contract.py:377-406` | also reject any PlatformIO environment selecting `examples/esp_idf/**` |
| `tools/check_docs.py:65-69,127,261-299` | document and check the native example; remove only `tools/check_idf_example_contract.py` from the obsolete paths |

## Audit before editing

- `examples/01_basic_bringup_cli/main.cpp`: handlers `:42-229` (`millis()` at
  `:62,196`), `REGISTRATIONS` `:231-246`, help `:251-259`, dispatch errors
  `:261-281`, input polling `:283-305`, setup `:309-338` (the Serial wait
  `:310-314` stays in the shell), loop `:340-367`. `StatusText.h:3` includes
  `<Arduino.h>` and prints through `Serial`. Already framework-free:
  `BoundedCli.h`, `CommandContract.h`, `WireInstance.h`, `BoardConfig.h`.
- Bytes on the wire: `Serial.println` ends with CRLF, `Serial.printf("\n")`
  with LF. ESP-IDF 6.0.1 converts console LF to CRLF by default
  (`components/esp_libc/Kconfig:56-80`) and selects picolibc by default
  (`:3-13`); Arduino-ESP32 uses newlib. `StatusText.h` uses `%llu`, `%06lX`,
  `%ld` and `%02X`. `BoundedCli.h:25-49` accepts CR, LF and CRLF.
- Tests: `test/test_example_cli.cpp`, `test/test_example_policy.cpp`,
  `test/test_main.cpp`, `test/support/TestAccess.h:179` (`lineEventCount`).
- `check_cli_contract.py:356-358` bans the words `scan`, `stress`, `rawtx` and
  `full erase` and `:335-355` bans irreversible method names in every file
  under `examples/`, README and CMake files included; word the native README
  accordingly.
- Siblings, read-only (record path, revision, adopted or rejected): console
  `OPT4001/examples/esp_idf/basic/main/main.cpp:1420-1449`; per-target console
  `TMP1x2/examples/esp_idf/basic/sdkconfig.defaults.esp32s2` and `.esp32s3`,
  and `main/CMakeLists.txt:5-9` (ROM CDC component on IDF 6); identity
  `INA228/examples/esp_idf/basic/main/CMakeLists.txt:9-31` and
  `GenerateGitProvenance.cmake`; checker shape
  `INA228/tools/check_idf_example_contract.py` and the smaller
  `OPT4001/tools/check_idf_example_contract.py`; engine and shell split
  `MAX31865/examples/06_diagnostic_cli/Max31865Cli.h:11-68`.

## Implement A: extract the engine (own checkpoint commit)

1. `CliOutput.h`: a sink with `write(const char*, size_t)` and bounded
   `printf`-style formatting into a fixed buffer sized for the longest line,
   with explicit truncation reporting and no heap. The Arduino adapter writes
   to `Serial`; it is the only Arduino-specific file in `examples/common/`.
2. `StatusText.h`: the same text through the sink, no Arduino include. Verify
   that ESP-IDF 6.0.1's default libc formats `%llu` and `%06lX`; if not, produce
   the same text without those specifiers.
3. `BringupCli.h`: move the handlers, `REGISTRATIONS`, help, dispatch-error
   text, input polling (at most `LINE_BYTES` characters per poll, same
   rejection lines), startup reporting after `WireInstance::start()` and the
   arbiter and hot-plug owner step, unchanged in order and text. Platform
   hooks: `nowMs()`, non-blocking `readChar()` returning -1 when empty, and the
   sink. No new command, framework header, task or heap.
4. `01_basic_bringup_cli/main.cpp` becomes the Arduino shell: Serial begin and
   3 s wait, the hooks, `setup()`/`loop()` calling the engine.
   `02_multi_device_cli/main.cpp` only adopts the sink.
5. Host tests with a recording sink and literal strings copied from the
   pre-extraction source, as exact bytes including CR and LF: help order;
   unknown command, wrong arity, missing confirmation, line too long, too many
   arguments; every invalid-argument message of `read-eeprom`,
   `read-security`, `rom-zone`, `speed` and `write-page` with no Backend line
   event (`TestAccess::lineEventCount`); `presence` inactive and disabled;
   startup with an absent device; `shutdown`; truncation reporting. Register
   them in `test/test_main.cpp`.
6. Update `check_cli_contract.py` per the table; keep `SINGLE_REGISTRATIONS`,
   `MULTI_REGISTRATIONS` and `EXPECTED_CATALOG` (`:33-59,89-133`) unchanged.
   Build all four `ex_*` environments, run the host suites and checkers, then
   commit and sync this checkpoint.

## Implement B: native shell

1. Layout. No `EXTRA_COMPONENT_DIRS` pointing at the checkout, no
   `override_path`, no example `idf_component.yml`:

   ```text
   examples/esp_idf/basic/
     CMakeLists.txt                project(at21cs_basic)
     README.md
     sdkconfig.defaults  sdkconfig.defaults.esp32s2  sdkconfig.defaults.esp32s3
     components/AT21CS01_AT21CS11/CMakeLists.txt   include() of the root file only
     main/CMakeLists.txt           SRCS main.cpp; INCLUDE_DIRS . ../../../common;
                                   REQUIRES AT21CS01_AT21CS11 esp_timer
                                   freertos vfs plus the verified console
                                   components
     main/GenerateBuildIdentity.cmake   example-only identity header
     main/main.cpp
   ```

   `fcntl()` needs `vfs` (`OPT4001/examples/esp_idf/basic/main/CMakeLists.txt:6`).

2. `main.cpp`: `extern "C" void app_main()` runs on the IDF main task, the
   only task that touches AT21CS objects; keep its default CPU0 affinity
   (`docs/ESP_IDF.md` explains why). No other task, ISR or callback calls the
   library. Configure the console as OPT4001 does (unbuffered, `O_NONBLOCK`;
   refuse to start the CLI if that fails), print the identity banner, start
   the engine with the `BoardConfig.h` defaults, then loop: poll input, run one
   engine step with `nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000)`,
   and yield at least one tick with `vTaskDelay(1)` on every iteration, busy or
   idle, so IDLE and the task watchdog run. No GPIO call, no logging inside a
   command, no heap after start.
3. `sdkconfig.defaults`, one comment per setting:
   `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y`, `CONFIG_FREERTOS_HZ=1000`,
   `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`, `# CONFIG_PM_ENABLE is not set` (01's
   smoke build proves the PM branch), `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`
   (the Arduino loop-task size; IDF default 3584,
   `components/esp_system/Kconfig:232-234`; the stack high-water mark is NOT
   RUN without hardware, so review the engine's buffers statically), 4 MB
   flash as in the Arduino environments, and console line endings chosen so
   the byte stream equals the Arduino one (verify). Per target: S3
   `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, S2 `CONFIG_ESP_CONSOLE_USB_CDC=y`,
   both `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y`.
4. Identity: `main/GenerateBuildIdentity.cmake` (INA228 pattern, refreshed on
   every build) writes a header with the commit from
   `git rev-parse --short=12 HEAD` and a status `clean`, `dirty` or `unknown`;
   both are `unknown` without Git or when
   `git -C <repository root> rev-parse --show-toplevel` is not that root (for
   example in an extracted package). This repository's Arduino build has no
   identity fields to copy (`scripts/generate_version.py` writes only the
   version into `Version.h`), so use exactly these values. The native banner
   prints `AT21CS::VERSION`, commit, status, `esp_get_idf_version()` and
   `CONFIG_IDF_TARGET`; the Arduino output stays unchanged. The core never
   needs Git or Python.
5. `README.md`: wiring table (SI/O GPIO 6 open drain, detect disabled, address
   0, AT21CS11; override the `AT21CS_EXAMPLE_*` macros with
   `target_compile_definitions`); an external pull-up is required because the
   Backend disables internal pulls (`Esp32Transport.cpp:154,167,183`); the
   rise-time envelope (0.60 microseconds High Speed, 1.00 microsecond Standard
   Speed and the `R_PUP` table with its 3.3 V assumption) from
   `README.md:56-81`; console per target; build, flash and monitor commands;
   first-session commands; `write-page` needs `CONFIRM_EEPROM_OVERWRITE`;
   irreversible provisioning is not exposed (link the guide, do not name the
   methods); interrupt masking and the one-owner rule; "a native build is not
   hardware validation".
6. `tools/check_idf_example_contract.py`: required files; wrapper only
   `include()`s the root file; main `REQUIRES`; no `override_path` or example
   manifest; `main.cpp` has `app_main`, the engine include, the non-blocking
   console and a yield, and no Arduino token, registration table, handler,
   GPIO call or task creation; required sdkconfig keys; required README and
   `docs/ESP_IDF.md` statements (envelope numbers, build commands, not
   hardware validation). Fail with a precise message. Add
   `tools/test_check_idf_example_contract.py` (standard `unittest`, temporary
   copies, never the real tree) with mutation cases that must fail: wrapper
   does more than `include()` the root file, an Arduino token or a
   registration table in `main.cpp`, a missing required sdkconfig key, and an
   `override_path`; plus one passing case on an unmodified copy (precedents
   `OPT4001/tools/test_idf_example_contract.py`,
   `LSM6DS3TR/tools/test_check_idf_example_contract.py`). 03 runs it in the
   `docs-contract` job.
7. Docs: `docs/ESP_IDF.md` example section, README examples section, the
   AGENTS rule, CHANGELOG, `test/COVERAGE_MATRIX.md`. The example is not
   packaged until 03, so packaged docs name it as code text, not a link.

## Tests and acceptance

- A: host suites pass with the new engine tests; four `ex_*` builds; contract
  checks pass with the catalog and registrations unchanged; the literal tests
  prove example 01 and 02 output unchanged.
- B: S2 and S3 example images link under IDF default warnings; the IRAM check
  passes on the example's Backend objects; `check_idf_example_contract.py`
  passes on the real tree and `test_check_idf_example_contract.py` proves each
  mutation fails; `check_cli_contract.py` proves both shells use the same
  engine and registrations.
- Nothing is flashed; hardware stays NOT RUN.

## Verify (each PASS/FAIL/NOT RUN/BLOCKED)

```text
.\scripts\pio.cmd test -e native
.\scripts\pio.cmd test -e native_sanitize
.\scripts\pio.cmd run -e ex_cli_s3      (and ex_cli_s2, ex_multi_s3, ex_multi_s2)
C:\esp\idf.cmd 6.0.1 -C examples\esp_idf\basic -B C:\esp\build\at21cs11-example-esp32s3-6.0.1 -D SDKCONFIG=C:\esp\build\at21cs11-example-esp32s3-6.0.1.sdkconfig set-target esp32s3 build
    (and esp32s2 with at21cs11-example-esp32s2-6.0.1)
python tools/check_esp32_iram_sections.py --objdump C:\esp\v6.0.1\tools\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin\xtensa-esp32s3-elf-objdump.exe --object (Get-ChildItem C:\esp\build\at21cs11-example-esp32s3-6.0.1 -Recurse -Filter Esp32Transport.cpp.obj).FullName
    (and the esp32s2 build with xtensa-esp32s2-elf-objdump.exe)
python tools/check_idf_example_contract.py
python tools/test_check_idf_example_contract.py
python tools/check_cli_contract.py
python tools/check_core_timing_guard.py
python tools/check_no_production_placeholders.py
python tools/check_docs.py
python scripts/generate_version.py --check
python tools/check_package.py --inspect      (and --build-platform-neutral, --build-arduino)
git diff --check
```

## Close out

Commit and sync checkpoint A, then B, each with its tests and docs. Report the
six evidence layers from the index: host tests; static/contract checks (with
the Arduino builds); native ESP-IDF build (S2/S3, with IRAM checks);
package/clean consumer; remote CI NOT RUN (03); physical hardware NOT RUN.
Set this prompt's index status.
Hand off the example object paths, console components and line-ending
settings, and anything the CI jobs need.
