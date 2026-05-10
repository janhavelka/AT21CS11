# Chip Overview

The AT21CS01/AT21CS11 family is a 1-Kbit serial EEPROM organized as 128 x 8 bytes. It is accessed through a single SI/O pin and is powered from the pull-up on that same line. The protocol is single-wire and bit-timed, while each transaction starts with an 8-bit address byte whose upper nibble is an opcode, bits 3:1 are factory-programmed A2..A0 address bits, and bit 0 is the read/write bit. Source: datasheet, pp. 1, 6, 17.

Key implementation facts:

| Item | Fact | Source |
|---|---|---|
| Main EEPROM | 128 bytes, arranged as 16 pages of 8 bytes | datasheet, pp. 1, 17 |
| Security register | 32 bytes; lower 8 bytes are factory-programmed serial number, next 8 bytes read FFh, upper 16 bytes are user-programmable and lockable | datasheet, pp. 1, 17-18 |
| Page write | Single-byte writes and 8-byte page writes are supported | datasheet, pp. 1, 21 |
| Write cycle | Self-timed nonvolatile write, 5 ms max | datasheet, pp. 1, 10 |
| Endurance / retention | 1,000,000 write cycles; 100-year data retention | datasheet, pp. 1, 10 |
| ROM zones | Four 256-bit zones can be permanently made read-only | datasheet, pp. 1, 18, 29 |
| Manufacturer ID | Opcode `0xC` returns 24 bits; AT21CS11 returns `0x00D380` | datasheet, pp. 19-20, 27 |

AT21CS11 operates only in high-speed mode at up to 125 kbps; opcode `0xD`
for standard-speed mode is NACKed by AT21CS11. Opcode `0xE` selects or checks
high-speed mode and is ACKed by AT21CS11. Source: datasheet, pp. 1, 10, 20.
