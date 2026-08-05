# Irreversible Security and ROM protection

The AT21CS01 and AT21CS11 contain three one-way protection mechanisms. A
Reset, power cycle, firmware update, or later write cannot undo them.

> **Warning:** Keep these APIs out of normal application command paths. Use
> them only in a deliberate provisioning procedure for an identified device.
> The two shipped examples intentionally do not expose them.

## What each operation changes

| API | Permanent effect | What it does not do |
|---|---|---|
| `Driver::permanentlyLockSecurity()` | Prevents all future writes to Security-user bytes `0x10..0x1F`; the entire 32-byte Security register is then read-only. | It does not protect the main EEPROM. |
| `Driver::permanentlyEnableRomZone(zone)` | Makes one 32-byte main-EEPROM zone read-only forever. | It does not affect other zones or the Security register. |
| `Driver::permanentlyFreezeRomZones()` | Locks the current four ROM-zone configuration bits forever. | It does not freeze EEPROM data and does not make a writable zone read-only. |

The ROM-zone index passed to the API maps directly to the main EEPROM:

| Zone | EEPROM bytes | ROM-zone register |
|---:|---|---:|
| 0 | `0x00..0x1F` | `0x01` |
| 1 | `0x20..0x3F` | `0x02` |
| 2 | `0x40..0x5F` | `0x04` |
| 3 | `0x60..0x7F` | `0x08` |

Freeze is the easiest operation to misunderstand. After Freeze:

- zones already enabled as ROM remain permanently read-only;
- zones not enabled remain writable;
- those writable zones can never later be converted to ROM.

Therefore, configure and verify every desired ROM zone before Freeze. Do not
call `permanentlyFreezeRomZones()` merely to inspect an unknown device: when
the precheck finds an unfrozen device, the same call performs the permanent
Freeze operation. The datasheet provides no separate read-only Freeze query,
and the library does not invent one.

## Safe provisioning order

Provisioning firmware should use an explicit, operator-authorized sequence:

1. Initialize the Driver, read the manufacturer ID and serial number, and
   compare them with the device authorized for provisioning.
2. Write all EEPROM and Security-user data, then read back and compare every
   byte.
3. If required, call `permanentlyLockSecurity()` and verify its result.
4. For every EEPROM zone that must become read-only, call
   `permanentlyEnableRomZone(zone)` and verify it with
   `readRomZoneState(zone, enabled)`.
5. Read all four zone states and compare the complete map with the approved
   map.
6. Only if the map is final and explicitly approved, call
   `permanentlyFreezeRomZones()`.
7. Perform final readback and record the serial number, Security Lock state,
   four-zone map, returned `Status`, and `MutationResult`.

Never combine these steps with automatic discovery, retries, or a general
remote command endpoint. Never automatically replay an irreversible command.
If power or communication is lost, stop and inspect the device before deciding
whether another permanent command is authorized.

## Interpreting `MutationResult`

The returned `Status` and `MutationResult` must be considered together:

| Evidence | Meaning | Required caller action |
|---|---|---|
| `VERIFIED`, successful `Status` | The requested permanent state was observed after the call. | Record the result and continue. |
| `VERIFIED` with `alreadyApplied == true` | The precheck found the state already permanent; this call did not program it again. | Confirm that the observed state is expected for this device. |
| `ACCEPTED`, failed `Status` | The device accepted the command, but post-write verification failed. | Assume it may be permanent; do not replay automatically. |
| `MAY_HAVE_COMMITTED` | Transport evidence cannot prove whether the permanent command committed. | Quarantine the device for deliberate inspection; do not replay automatically. |
| `NOT_ATTEMPTED` | No accepted mutation payload was established. | Still obey the returned `Status`; `INDETERMINATE` is not permission to retry. |

A provisioning step is confirmed only when `status.ok()` is true and
`result.effect == MutationEffect::VERIFIED`. Any other combination requires a
human or product-specific recovery decision outside this library.

## What the library sends

The public methods use only the sequences documented by Microchip
DS20005857I:

- Security Lock: precheck with opcode `2h/W` and address `0x60`, then the Lock
  write and a postcheck;
- ROM-zone enable: read the selected opcode-`7h` register, write `0xFF` to that
  register, then read it back;
- ROM-zone Freeze: observe opcode `1h/W` address acknowledgement, then, only
  when unfrozen, write fixed address `0x55` and data `0xAA`, followed by a
  postcheck.

After an accepted write, the Bus keeps SI/O released high for the fixed 10 ms
library policy interval. It does not ACK-poll or replay an uncertain mutation.
The application should call the public Driver methods rather than constructing
raw command frames.
