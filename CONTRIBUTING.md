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

Contributions must preserve these boundaries:

- one external Backend and one Bus per physical SI/O wire;
- externally serialized synchronous calls;
- no core task, queue, scheduler, application-facing mutex, logging or retry
  policy;
- complete validation before device I/O;
- fixed-size steady-state library operation;
- exact NACK, transport and ambiguous-write evidence;
- no native alternative framework path or v1 compatibility layer;
- no physical or irreversible-operation success claim without recorded HIL
  authorization and evidence.

Do not edit `include/AT21CS/Version.h` manually. Change `library.json`, run the
generator and verify `--check`.
