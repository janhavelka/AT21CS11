# Initialization, Reset, And Operational Notes

AT21CS11 high-speed bring-up sequence:

1. Ensure SI/O is released high through the correct pull-up for the selected device variant.
2. Drive SI/O low for the reset low time, release it, wait reset recovery, then request and sample the discovery response.
3. Treat the device as high-speed after reset. Do not try standard-speed mode on AT21CS11.
4. Use the configured preprogrammed A2..A0 bits when constructing address bytes.
5. For writes, protection changes, security lock, and ROM freeze, wait the 5 ms max write-cycle time before any further bus access.

Sources: datasheet, pp. 10, 12-14, 17, 20-24.

Operational notes:

- Current-address reads depend on the internal address pointer left by previous successful operations; random reads are safer for public APIs that take an address. Source: datasheet, pp. 25-26.
- Page writes are limited to 8-byte page boundaries; split larger writes at page boundaries to avoid undefined wrap expectations. Source: datasheet, pp. 17, 21.
- Security-register writes must stay within the user-programmable `0x10-0x1F` range unless a specific operation is intentionally reading the serial/reserved ranges. Source: datasheet, pp. 17-18, 22.
- Bus sharing is possible only when devices have distinct factory-programmed address bits. Source: datasheet, p. 17.
- For AT21CS11, opcode `0xD` is a NACKed standard-speed command; remain in high-speed timing after reset. Source: datasheet, p. 20.
