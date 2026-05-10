# Protocol Commands And Transactions

The bus is single-wire but the byte structure emulates an I2C serial EEPROM. A transaction starts with a single-wire Start condition and an 8-bit device address byte. The upper four bits are an opcode, the next three bits are preprogrammed slave address bits A2..A0, and bit 0 is the read/write bit. Source: datasheet, p. 17.

| Opcode | Bits | Operation | Transaction facts | Source |
|---|---:|---|---|---|
| `0xA` | `1010` | EEPROM access | Read/write main 128-byte EEPROM. | datasheet, p. 19 |
| `0xB` | `1011` | Security register access | Read serial number/reserved bytes/user security area; write user security bytes before lock. | datasheet, pp. 19, 26 |
| `0x2` | `0010` | Lock security register | Permanently lock user-programmable security bytes. | datasheet, pp. 19, 23 |
| `0x7` | `0111` | ROM zone register access | Read or set ROM-zone state bits. | datasheet, pp. 19, 29-31 |
| `0x1` | `0001` | Freeze ROM zone state | Permanently prevent further ROM-zone changes. | datasheet, pp. 19, 31 |
| `0xC` | `1100` | Manufacturer ID read | Read manufacturer, density, and revision data. | datasheet, pp. 20, 27 |
| `0xD` | `1101` | Standard speed mode | AT21CS01 only; AT21CS11 NACKs. | datasheet, p. 20 |
| `0xE` | `1110` | High-speed mode | AT21CS11 ACKs; AT21CS01 supports high-speed selection/check. | datasheet, p. 20 |

Memory-address byte: bit 7 is don't-care and bits 6..0 select the 0x00-0x7F EEPROM byte. Source: datasheet, p. 17.

ACK is a logic 0 response in the ninth bit window; NACK is a logic 1 response.
If the opcode is invalid or A2..A0 do not match the factory-programmed device
address, the device does not respond and returns to standby. Source: datasheet,
pp. 12, 17.

Transaction patterns:

- Byte/page write: Start, address byte with write bit, memory/security address, data bytes, Stop; then wait for the internal write cycle. Source: datasheet, pp. 21-23.
- Random read: perform a dummy write to load the address, then Start again with read bit and clock out data. Source: datasheet, pp. 25-26.
- Sequential read: continue clocking bytes; the internal address advances. Source: datasheet, p. 26.
- Manufacturer ID read uses opcode `0xC` and returns three bytes; the read rolls over to the first ID byte if continued. Source: datasheet, p. 27.
- Lock security register: opcode `0x2` with a memory address whose upper nibble is `0x6`; lower address bits and data byte are don't-care but must be transmitted. ACK to the address and data byte means not locked; NACK means already locked. Source: datasheet, p. 23.
- ROM-zone write: opcode `0x7`, ROM-zone register address `0x01`, `0x02`, `0x04`, or `0x08`, data byte `0xFF`, then Stop to start `tWR`. Source: datasheet, pp. 29-30.
- Freeze ROM zones: opcode `0x1`, address byte `0x55`, data byte `0xAA`, then Stop to start `tWR`; any other address or data byte is NACKed and does not freeze the ROM-zone state. Source: datasheet, pp. 30-31.
