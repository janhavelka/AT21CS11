# Variants And Open Questions

| Feature | AT21CS01 | AT21CS11 | Source |
|---|---|---|---|
| Pull-up voltage | 1.7 V to 3.6 V high speed; 2.7 V to 3.6 V standard speed | 2.7 V to 4.5 V high speed | datasheet, pp. 8-9 |
| Standard-speed mode | Supported | Not supported; standard-speed opcode NACKs | datasheet, pp. 1, 10, 20 |
| High-speed mode | Supported and reset default | Only supported mode | datasheet, pp. 1, 20 |
| Memory organization | 128-byte EEPROM plus 32-byte security register | Same | datasheet, pp. 17-18 |
| Protection model | Security lock, ROM zones, ROM freeze | Same | datasheet, pp. 23, 29-31 |

Not documented in PDFs:

- The provided PDFs do not state the factory-programmed A2..A0 value for any
  specific board in this workspace.
- The datasheet defines AT21CS01 standard-speed behavior, but the repository
  name targets AT21CS11; no PDF in `docs/` states whether this library must
  expose AT21CS01-only standard-speed APIs.
- The PDFs define irreversible operations but do not specify host API
  confirmation policy for security lock, ROM-zone set, or ROM-zone freeze.
- The PDFs provide SI/O timing limits, but no PDF states timing accuracy of a
  specific MCU/adapter implementation in this repository.

Raw extraction remains available at `docs/pdf-extracted-md/AT21CS11_datasheet.md` for page-level verification.
