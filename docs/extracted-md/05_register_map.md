# Register Map

This device does not have conventional I2C registers. It has addressable memory regions selected by opcode and address byte.

| Region | Opcode | Address range | Access | Notes | Source |
|---|---:|---:|---|---|---|
| Main EEPROM | `0xA` | `0x00-0x7F` | Read/write before protection | 128 bytes, 16 pages of 8 bytes. | datasheet, pp. 17-18 |
| Security serial number | `0xB` | `0x00-0x07` | Read-only | Factory-programmed unique 64-bit serial number. | datasheet, pp. 1, 17-18 |
| Security reserved | `0xB` | `0x08-0x0F` | Read-only | Reserved bytes read as `0xFF`. | datasheet, pp. 1, 18 |
| Security user area | `0xB` | `0x10-0x1F` | Read/write until locked | User-programmable, then permanently lockable. | datasheet, pp. 17-18, 23 |
| ROM zone registers | `0x7` | `0x01`, `0x02`, `0x04`, `0x08` | Read/set | One nonvolatile bit per 256-bit EEPROM zone. Read returns `0x00` or `0xFF`; write `0xFF` to set. | datasheet, pp. 29-31 |
| Manufacturer ID | `0xC` | implicit | Read-only | AT21CS01 returns `0x00D200`; AT21CS11 returns `0x00D380`. | datasheet, p. 27 |

ROM zone address ranges:

| Zone | EEPROM range | ROM-zone register address | Size | Source |
|---:|---:|---:|---:|---|
| 0 | `0x00-0x1F` | `0x01` | 32 bytes / 256 bits | datasheet, pp. 18, 29 |
| 1 | `0x20-0x3F` | `0x02` | 32 bytes / 256 bits | datasheet, pp. 18, 29 |
| 2 | `0x40-0x5F` | `0x04` | 32 bytes / 256 bits | datasheet, pp. 18, 29 |
| 3 | `0x60-0x7F` | `0x08` | 32 bytes / 256 bits | datasheet, pp. 18, 29 |

Manufacturer ID byte order is D23:D16, D15:D8, then D7:D0. The upper 12 bits
are Microchip manufacturer code `0x00D`; AT21CS11 device/revision bits produce
the complete value `0x00D380`. Source: datasheet, p. 27.

Protection operations are one-way. A locked security register, enabled ROM zone, or frozen ROM-zone state cannot be restored by software. Source: datasheet, pp. 23, 29-31.
