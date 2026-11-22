# Contributing

Keep changes small, deterministic and consistent with the synchronous ownership
model described in [README.md](README.md).

Before opening a pull request, run the affected checks. The complete software
gate is:

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
```

On Windows always use `scripts\pio.cmd`; it selects the existing user-managed
PlatformIO installation. Do not install another PlatformIO Core for this
repository.

For native ESP-IDF 6.0.1 smoke builds on Windows, use the isolated launcher
and separate build and configuration paths for each target. For example, from
the repository root in PowerShell:

```powershell
$project = 'test\consumer\phy_smoke\idf'
$build = 'C:\esp\build\at21cs11-smoke-esp32s3-6.0.1'
C:\esp\idf.cmd 6.0.1 -C $project -B $build -D "SDKCONFIG=$build.sdkconfig" set-target esp32s3 build

$build = 'C:\esp\build\at21cs11-smokepm-esp32s3-6.0.1'
C:\esp\idf.cmd 6.0.1 -C $project -B $build -D "SDKCONFIG=$build.sdkconfig" -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.pm" set-target esp32s3 build
```

Repeat with `esp32s2` in the target and build paths. The launcher passes the
quoted defaults list to CMake. The resulting PM sdkconfig retains the 240 MHz
CPU, 1000 Hz tick and size optimization and sets `CONFIG_PM_ENABLE=y`.
An AT21CS01 Standard-Speed segment can mask interrupts for up to about
5.8 ms. The native example and CI are pending. See the [ESP-IDF integration guide](docs/ESP_IDF.md).

Contributions must preserve these boundaries:

- one external Backend and one Bus per physical SI/O wire;
- externally serialized synchronous calls;
- no core task, queue, scheduler, application-facing mutex, logging or retry
  policy;
- complete validation before device I/O;
- fixed-size steady-state library operation;
- exact NACK, transport and ambiguous-write evidence;
- one shared ESP32 Backend for Arduino-ESP32 and native ESP-IDF, and no v1 compatibility layer;
- no physical or irreversible-operation success claim without recorded HIL
  authorization and evidence.

Do not edit `include/AT21CS/Version.h` manually. Change `library.json`, run the
generator and verify `--check`.
