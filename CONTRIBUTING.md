# Contributing

Run the checks that cover your change from the repository root. To find tests
for a driver operation, see the [coverage matrix](https://github.com/janhavelka/AT21CS11/blob/main/test/COVERAGE_MATRIX.md).
For the original native ESP-IDF requirements, see the [completed prompt index](https://github.com/janhavelka/AT21CS11/blob/main/docs/prompts/README.md).

## Host tests and documentation

On Windows, use `.\scripts\pio.cmd` for every PlatformIO command. It selects
the current user's existing installation. The documentation checker also
requires Doxygen on `PATH`.

```powershell
.\scripts\pio.cmd test -e native
.\scripts\pio.cmd test -e native_sanitize
python tools/check_core_timing_guard.py
python tools/check_no_production_placeholders.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python tools/test_check_idf_example_contract.py
python tools/check_docs.py
python scripts/generate_version.py --check
```

## Firmware builds

Build the two Arduino CLI programs and the Backend smoke consumer on each target.

```powershell
.\scripts\pio.cmd run -e ex_cli_s2 -e ex_cli_s3 -e ex_multi_s2 -e ex_multi_s3
.\scripts\pio.cmd run -d test/consumer/phy_smoke/arduino -e phy_smoke_s2 -e phy_smoke_s3
```

For native builds on Windows, use the isolated ESP-IDF 6.0.1 launcher. Run
this block with `$target` set to `esp32s2`, then `esp32s3`. Each target and
power-management profile gets a separate build directory and sdkconfig.

```powershell
$target = 'esp32s2'
$build = "C:\esp\build\at21cs11-example-$target-6.0.1"
C:\esp\idf.cmd 6.0.1 -C examples\esp_idf\basic -B $build -D "SDKCONFIG=$build.sdkconfig" set-target $target build

$project = 'test\consumer\phy_smoke\idf'
$build = "C:\esp\build\at21cs11-smoke-$target-6.0.1"
C:\esp\idf.cmd 6.0.1 -C $project -B $build -D "SDKCONFIG=$build.sdkconfig" set-target $target build

$build = "C:\esp\build\at21cs11-smokepm-$target-6.0.1"
C:\esp\idf.cmd 6.0.1 -C $project -B $build -D "SDKCONFIG=$build.sdkconfig" -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.pm" set-target $target build
```

The smoke builds use a 240 MHz CPU, a 1000 Hz tick and size optimization.
An AT21CS01 Standard-Speed segment can mask interrupts for up to about 5.8 ms.
Use `tools/check_esp32_iram_sections.py` with the target's objdump on each
Backend object. For native PM builds, pass the linked smoke image with
`--elf` to check both PM wrappers. [ESP-IDF integration guide](docs/ESP_IDF.md)

## Package and clean consumers

The package checker verifies the exported file list and builds consumers from
the extracted archive. For native builds, use a short output path outside the
checkout and the system temp directory. The packaged CLI must report
`unknown` for both its Git commit and status.

```powershell
python tools/check_package.py --inspect
python tools/check_package.py --build-platform-neutral
python tools/check_package.py --build-arduino
python tools/check_package.py --export-archive C:\esp\build\at21cs11-payload
python tools/check_package.py --build-idf --out C:\esp\build\at21cs11-pkg --target esp32s2 --idf-command "C:\esp\idf.cmd 6.0.1"
python tools/check_package.py --build-idf --out C:\esp\build\at21cs11-pkg --target esp32s3 --idf-command "C:\esp\idf.cmd 6.0.1"
```

## Driver and version changes

Keep one externally owned Backend and Bus per physical wire. Callers serialize
synchronous operations. Validate arguments before device I/O, use fixed storage
in steady state, and preserve exact NACK, transport and ambiguous-write evidence.
The core leaves scheduling, logging and retry policy to firmware. Both
frameworks use the same ESP32 Backend. See the [driver contract](README.md)
and the [provisioning guide](docs/IRREVERSIBLE_OPERATIONS.md).

For a version change, set the version in `library.json`, match it in
`idf_component.yml`, then run `python scripts/generate_version.py` to generate
`include/AT21CS/Version.h`. Verify the result with `--check`.
