# Modes, Interrupts, Status, And Faults

There is no interrupt pin and no status register. Status is inferred from ACK/NACK behavior, discovery response, read data, and protection-state reads.

Speed modes:

| Mode | AT21CS01 | AT21CS11 | Source |
|---|---|---|---|
| High speed | Supported; reset default | Only supported mode | datasheet, pp. 1, 20 |
| Standard speed | Supported at 15.4 kbps max | Not supported; standard-speed opcode is NACKed | datasheet, pp. 1, 10, 20 |

Fault/status behaviors useful to a driver:

- Invalid opcode or mismatched preprogrammed A2..A0 address causes no response and the device returns to standby. Source: datasheet, p. 17.
- During the internal write cycle, the device does not respond to commands until `tWR` has elapsed. Source: datasheet, pp. 21-24.
- ROM-zone register reads return `0x00` when a zone is not ROM and `0xFF` when permanently set as ROM. Source: datasheet, pp. 29-30.
- Writes into an enabled ROM zone ACK the device address and word address, then NACK the data byte and immediately return to command-ready state. Source: datasheet, p. 31.
- Locking the security register and freezing ROM-zone state are irreversible. Source: datasheet, pp. 23, 31.

Discovery response is the closest thing to presence/status detection. It is requested after reset/power-up timing and before normal commands. Source: datasheet, pp. 12-14.
