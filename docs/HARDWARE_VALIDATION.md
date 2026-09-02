# Hardware validation record

This document records only hardware behavior that was actually observed. Host
tests and successful firmware builds are separate software evidence.

## 2026-08-05 destructive ESP32-S2 / AT21CS11 run

The maintainer authorized destructive testing on the one device identified by
serial number `A0E86E3703000069`. The serial-bound test ran on `COM10` and
finished with `85` checks passed and `0` failures.

Recorded configuration:

- MCU: ESP32-S2FH4 revision 1.0, 4 MB embedded flash, no embedded PSRAM;
- framework: Arduino-ESP32 3.3.11 through pinned PioArduino 55.03.311;
- PlatformIO Core 6.1.19 and Xtensa toolchain 14.2.0+20260121;
- SI/O GPIO 6, `presencePin == -1`, address bits 0;
- expected and detected part: AT21CS11 revision 0;
- High-Speed mode and `offlineThreshold == 5`.

Observed results:

| Scope | Result |
|---|---|
| Startup, Reset/Discovery, identity, CRC and manufacturer ID | PASS |
| Eight-byte EEPROM page write, readback, and restoration | PASS |
| Complete 128-byte EEPROM write and readback | PASS |
| Security-user write and readback | PASS |
| Permanent Security Lock and post-lock write rejection | PASS |
| Permanent ROM enable for zones 0, 1 and 2 and protected-write rejection | PASS |
| Permanent ROM-zone configuration Freeze | PASS |
| Rejected attempt to enable zone 3 after Freeze | PASS |
| Successful EEPROM write in still-writable zone 3 after Freeze | PASS |
| Explicit `recover()` and final EEPROM/Security/state readback | PASS |

The five Driver failures reported at the end were the five intentional
protected-write rejection checks. There was no unexpected failure, ambiguous
write evidence, or mutation replay.

## Permanent state of that exact chip

Do not use serial `A0E86E3703000069` for clean-state or factory-default tests:

- Security-user bytes `0x10..0x1F` are permanently locked as
  `C3 D0 E5 FA 8F 9C B1 46 5B 68 7D 12 27 34 C9 DE`;
- EEPROM zones 0, 1 and 2 (`0x00..0x5F`) are permanently read-only and contain
  test data;
- EEPROM zone 3 (`0x60..0x7F`) remains writable;
- the ROM-zone configuration is permanently frozen, so zone 3 can never be
  converted to ROM;
- final 128-byte EEPROM SHA-256:
  `149C6E8B45C2FCAA1EC950066F32E89AEE865330446ACEE17EB6F8BE6D9FED0B`;
- final 16-byte Security-user SHA-256:
  `20DFB26C0DFD63B0DDA4C940559B76086FEBB203726FF0A74E548E849E5EDEB9`.

The final EEPROM image is recorded because bytes `0x00..0x5F` can no longer be
changed on this chip:

```text
5A7F1035CEE38459721728CDE6BB5C710A2FC0E5BE53740922C798BD566B0C21
FA9FB0556E0324F992B7486D06DBFC91AA4F6005DEF394A9426738DDF68BAC41
1A3FD0F58EA3441932D7E88DA67B1C31CAEF80A57E1334C9E287587D162BCCE1
896B452319FBDD835277082DC69BBC516A0F20C59EB354690227F89DB64B6C01
```

The board was left running the one-shot HIL firmware. Reflash the intended
application before normal use. The exact-device destructive fixture was
removed from the repository after the run because its clean-state assumptions
are no longer true and accidental reuse would be unsafe.

## Scope not tested or claimed

No oscilloscope or logic analyzer was connected. Pull-up value, supply voltage,
temperature, physical package marking, rise time, open-drain waveform and
protocol timing margins were not recorded. These electrical and waveform
properties are not qualified by this run.

For new board validation, apply the ESP32 Backend's documented
[pull-up and rise-time envelope](../README.md#esp32-pull-up-and-rise-time-envelope)
in addition to the datasheet limits, and measure the actual released-high
waveform with the complete bus loading fitted.

No ESP32-S3/AT21CS01, shared-wire multi-address, two-independent-wire physical
hot-plug, or optional presence-pin fixture was run. Those setups are not needed
for the recorded single-device board with `presencePin == -1`, and no claim is
made for them. Their software paths remain covered by host tests and S2/S3
build checks.

The application does not need a detect pin when the device is fixed. If future
hardware must support physical hot-plug without one, firmware may make one
bounded `probe()` or `recover()` call at its chosen polling event as described
in the README; the library creates no poller.
