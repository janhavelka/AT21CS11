# Document Inventory

These files are compact engineering notes for the AT21CS01/AT21CS11 single-wire EEPROM family. They are curated from the raw extraction in `docs/pdf-extracted-md` and the PDFs in `docs/`; detailed page dumps remain in the raw extraction files.

| Source PDF | Raw extract | Pages used | Notes |
|---|---|---:|---|
| `docs/AT21CS11_datasheet.pdf` | `docs/pdf-extracted-md/AT21CS11_datasheet.md` | 1-47 | Primary source for electrical limits, single-wire transaction timing, opcodes, memory organization, ROM zones, security register, and package/pin data. |

Compact note set:

| File | Purpose |
|---|---|
| `01_chip_overview.md` | Device capabilities and driver-relevant scope. |
| `02_pinout_and_signals.md` | SI/O, GND, packages, pull-up powered bus notes. |
| `03_electrical_and_timing.md` | Voltage, speed, write-cycle, endurance, and reset/discovery timing. |
| `04_protocol_commands_and_transactions.md` | Single-wire transaction model, address byte, opcodes, reads, and writes. |
| `05_register_map.md` | Memory, security register, ROM-zone and manufacturer-ID data areas. |
| `06_modes_interrupts_status_and_faults.md` | Speed modes, busy/write behavior, lock/ROM status handling. |
| `07_initialization_reset_and_operational_notes.md` | Reset/discovery and practical bring-up flow. |
| `08_variant_differences_and_open_questions.md` | AT21CS01 versus AT21CS11 differences and unresolved implementation questions. |
