# AT21CS01 / AT21CS11 — Datasheet Reference for Driver Implementation

> Source: Microchip DS20005857D (©2020). This document extracts **every** implementation-relevant fact from the datasheet. No interpretation or implementation advice — pure chip data.

---

## 1. Device Identification

| Property | AT21CS01 | AT21CS11 |
|---|---|---|
| Manufacturer ID (24-bit) | `0x00D200` | `0x00D380` |
| Product ID byte (Security Reg byte 0) | `0xA0` | `0xA0` |
| Datasheet | DS20005857D | DS20005857D |
| Memory size | 1 Kbit (128 × 8) | 1 Kbit (128 × 8) |
| Interface | Single-wire (SI/O + GND) | Single-wire (SI/O + GND) |
| Power source | Parasitic from SI/O pull-up | Parasitic from SI/O pull-up |
| Pins | 2 (SI/O, GND) | 2 (SI/O, GND) |

---

## 2. Variant Differences

### 2.1 Speed Mode Support

| Speed Mode | AT21CS01 | AT21CS11 |
|---|---|---|
| Standard Speed (opcode `0xD_`) | **Supported** (max 15.4 kbps) | **Not supported** — device NACKs opcode `0xD_` |
| High-Speed (opcode `0xE_`) | **Supported** (max 125 kbps) | **Supported** (max 125 kbps) |

Both devices default to **High-Speed after every Reset + Discovery**.

### 2.2 Pull-Up Voltage Range (V_PUP)

| Part | V_PUP Min | V_PUP Max | Notes |
|---|---|---|---|
| AT21CS01 (High-Speed) | 1.7 V | 3.6 V | |
| AT21CS01 (Standard Speed) | 2.7 V | 3.6 V | Standard Speed requires higher V_PUP |
| AT21CS11 (High-Speed only) | 2.7 V | 4.5 V | Higher max than AT21CS01 |

---

## 3. Memory Architecture

### 3.1 Main EEPROM Array

| Property | Value |
|---|---|
| Total size | 128 bytes (1 Kbit) |
| Address range | `0x00`–`0x7F` |
| Page size | 8 bytes |
| Page count | 16 |
| Write endurance | 1,000,000 cycles |
| Data retention | 100 years |
| Write cycle time (t_WR) | max 5 ms |

Page boundaries (8-byte aligned):

| Page | Address Range |
|---|---|
| 0 | `0x00`–`0x07` |
| 1 | `0x08`–`0x0F` |
| 2 | `0x10`–`0x17` |
| 3 | `0x18`–`0x1F` |
| 4 | `0x20`–`0x27` |
| 5 | `0x28`–`0x2F` |
| 6 | `0x30`–`0x37` |
| 7 | `0x38`–`0x3F` |
| 8 | `0x40`–`0x47` |
| 9 | `0x48`–`0x4F` |
| 10 | `0x50`–`0x57` |
| 11 | `0x58`–`0x5F` |
| 12 | `0x60`–`0x67` |
| 13 | `0x68`–`0x6F` |
| 14 | `0x70`–`0x77` |
| 15 | `0x78`–`0x7F` |

**Page write roll-over behavior:** During page write, only the low 3 address bits increment. When the internal counter reaches the page boundary, it wraps to the start of the **same page** — it does NOT advance to the next page. Data written past the boundary overwrites the beginning of the current page.

### 3.2 Security Register

| Property | Value |
|---|---|
| Total size | 32 bytes (256 bits) |
| Address range | `0x00`–`0x1F` |
| Pages | 4 × 8 bytes |
| Lower half (`0x00`–`0x0F`) | **Read-only** (factory-programmed) |
| Upper half (`0x10`–`0x1F`) | **User-programmable** (can be permanently locked) |

**Current-address read is NOT supported for the Security Register.** Always use Random Read (dummy write) to set the address pointer first.

#### 3.2.1 Factory Serial Number (Security Register `0x00`–`0x07`)

| Byte Offset | Content |
|---|---|
| 0 | Product ID: `0xA0` |
| 1–6 | 48-bit unique serial number |
| 7 | CRC-8 of bytes 0–6 |

CRC polynomial: **X⁸ + X⁵ + X⁴ + 1** (CRC-8, polynomial value `0x31`, init `0x00`, no reflect, no final XOR).

#### 3.2.2 Security Register Locking

- Locking is **irreversible** — the entire 32-byte register (both halves) becomes permanently read-only.
- After lock: write attempts to Security Register → device ACKs device address and word address, **NACKs the data byte**, and returns to ready immediately (no t_WR triggered).

### 3.3 Shared Address Pointer

The device maintains **one single Address Pointer** shared between EEPROM and Security Register regions. Consequence:

- After accessing EEPROM, the pointer points into EEPROM space.
- After accessing Security Register, the pointer points into Security Register space.
- **When switching regions**, the first access in the new region must be a **Random Read** (dummy write + re-start) to set the pointer to a known address.
- Current Address Read uses whatever address the pointer currently holds. If the pointer was last set by a Security Register access, a Current Address Read with EEPROM opcode will read from whatever address the pointer holds (not necessarily valid EEPROM context).

### 3.4 ROM Zones (EEPROM Permanent Write Protection)

The EEPROM is divided into **four 256-bit (32-byte) zones**. Each zone can be independently and **permanently** set to read-only (ROM mode).

| Zone | EEPROM Address Range | ROM Zone Register Address |
|---|---|---|
| 0 | `0x00`–`0x1F` | `0x01` |
| 1 | `0x20`–`0x3F` | `0x02` |
| 2 | `0x40`–`0x5F` | `0x04` |
| 3 | `0x60`–`0x7F` | `0x08` |

**ROM Zone Register values:**
- `0x00` = zone is **writable** (EEPROM mode)
- `0xFF` = zone is **read-only** (ROM mode, permanent)

**Writing to a ROM zone:** Device ACKs device address and word address, **NACKs the data byte**, and becomes immediately ready (no t_WR triggered).

**To set a zone to ROM:** Write `0xFF` to its ROM Zone Register address via opcode `0x7_`.

#### 3.4.1 ROM Zone Freeze

ROM Zone Registers can be **permanently frozen** — after freeze, no further ROM zone changes are possible (zones that are EEPROM stay EEPROM; zones that are ROM stay ROM).

- Freeze is **irreversible**.
- Freeze command requires specific address (`0x55`) and data (`0xAA`) bytes as a safety guard. If either byte is wrong, device NACKs and does not freeze.

---

## 4. Electrical Specifications

### 4.1 SI/O Pin Characteristics

- **Open-drain output** — requires external pull-up resistor to V_PUP.
- Device harvests operating power from the pull-up voltage on SI/O.
- Logic thresholds:
  - V_IL (input low) max: **0.5 V**
  - V_IH (input high) min: **0.7 × V_PUP**
  - V_HYS (hysteresis): device-specific, depends on internal supply derived from V_PUP, R_PUP, C_BUS, and timing.
- Maximum bus capacitance (C_BUS): **1000 pF**

### 4.2 Pull-Up Resistor Ranges (R_PUP)

#### AT21CS01

| V_PUP | R_PUP Min | R_PUP Max |
|---|---|---|
| 1.7 V | 130 Ω | 200 Ω |
| 2.7 V | 200 Ω | 1.8 kΩ |
| 3.6 V | 330 Ω | 4.0 kΩ |

#### AT21CS11

| V_PUP | R_PUP Min | R_PUP Max |
|---|---|---|
| 2.7 V | 200 Ω | 1.8 kΩ |
| 4.5 V | 400 Ω | 5.4 kΩ |

### 4.3 Key Constraint

R_PUP and C_BUS together determine the rise time t_PUP (from V_IL to V_IH). The bit timing formula is:

**t_BIT = t_LOW0 + t_PUP + t_RCV**

The values of R_PUP and C_BUS must be chosen so that t_PUP is small enough that the bit frame timing constraints are met.

---

## 5. Timing Specifications (AC Characteristics)

### 5.1 Reset and Discovery Timing

| Parameter | Symbol | Standard Speed (AT21CS01 only) | High-Speed (both parts) | Unit |
|---|---|---|---|---|
| Reset low time | t_RESET | min 480 | min 96 | µs |
| Discharge low time (force-abort) | t_DSCHG | min 150 | min 150 | µs |
| Reset recovery time | t_RRT | N/A | min 8 | µs |
| Discovery Response request low | t_DRR | N/A | 1 to (2 − t_PUP) | µs |
| Discovery Response acknowledge | t_DACK | N/A | 8–24 | µs |
| Master strobe during Discovery | t_MSDR | N/A | 2–6 | µs |
| SI/O high time for Start/Stop | t_HTSS | min 600 | min 150 | µs |

**After Reset, device always defaults to High-Speed mode.** High-Speed timing applies for the Discovery sequence regardless of which speed was active before reset.

### 5.2 Data Communication Timing — High-Speed Mode

| Parameter | Symbol | Min | Max | Unit | Notes |
|---|---|---|---|---|---|
| Bit frame duration | t_BIT | t_LOW0 + t_PUP + t_RCV | 25 | µs | See constraint formula |
| Start/Stop high time | t_HTSS | 150 | — | µs | |
| Master low for logic 0 | t_LOW0 | 6 | 16 | µs | |
| Master low for logic 1 | t_LOW1 | 1 | 2 | µs | |
| Master read strobe low | t_RD | 1 | 2 | µs | Same as t_LOW1 |
| Master read sample window | t_MRS | — | t_RD + 2 (+ t_PUP) | µs | Sample SI/O within this window after releasing |
| Device hold time for output 0 | t_HLD0 | 2 | 6 | µs | Device holds low after master releases |
| Slave recovery time | t_RCV | 2 | — | µs | Time SI/O must be high before next bit |

### 5.3 Data Communication Timing — Standard Speed Mode (AT21CS01 only)

| Parameter | Symbol | Min | Max | Unit | Notes |
|---|---|---|---|---|---|
| Bit frame duration | t_BIT | 40 | 100 | µs | |
| Start/Stop high time | t_HTSS | 600 | — | µs | |
| Master low for logic 0 | t_LOW0 | 24 | 64 | µs | |
| Master low for logic 1 | t_LOW1 | 4 | 8 | µs | |
| Master read strobe low | t_RD | 4 | 8 | µs | Same as t_LOW1 |
| Master read sample window | t_MRS | — | t_RD + 8 (+ t_PUP) | µs | |
| Device hold time for output 0 | t_HLD0 | 8 | 24 | µs | |
| Slave recovery time | t_RCV | 8 | — | µs | |
| Noise filtering capability | t_NOISE | 0.5 | — | µs | Standard Speed only |

### 5.4 EEPROM Cell Timing

| Parameter | Symbol | Value | Unit |
|---|---|---|---|
| Write cycle time | t_WR | max 5 | ms |

---

## 6. Physical Layer — Bit Frames

### 6.1 Input Bit Frame (Master → Device)

Every bit begins with master pulling SI/O below V_IL.

**Transmit logic 0:**
1. Master pulls SI/O low.
2. Master holds low for t_LOW0 duration.
3. Master releases SI/O (goes Hi-Z).
4. Line rises via pull-up.
5. SI/O must remain high for at least t_RCV before next bit.

**Transmit logic 1:**
1. Master pulls SI/O low.
2. Master holds low for t_LOW1 duration.
3. Master releases SI/O.
4. Line rises via pull-up.
5. SI/O must remain high for at least t_RCV before next bit.

**Device samples** the bit after t_LOW1 max and before t_LOW0 min. If SI/O is still low → logic 0. If SI/O has risen → logic 1.

### 6.2 Output Bit Frame (Device → Master)

**Master read strobe:**
1. Master pulls SI/O low for t_RD (same range as t_LOW1).
2. Master releases SI/O and switches to input mode.
3. If device is sending **logic 0**: device holds SI/O low for t_HLD0 after master releases.
4. If device is sending **logic 1**: device does not pull low; line rises via pull-up.
5. Master samples SI/O within the t_MRS window.
6. SI/O must remain high for at least t_RCV before next bit.

### 6.3 Byte Transfer

- 8 data bits transmitted **MSb first** (bit 7 first, bit 0 last).
- 9th slot is **ACK/NACK**:
  - **ACK**: receiver (device or master) pulls SI/O low → reads as logic 0.
  - **NACK**: receiver does not drive → line stays high → reads as logic 1.
- During write: device ACKs/NACKs in the 9th slot.
- During read: master ACKs/NACKs in the 9th slot.

### 6.4 Start Condition

SI/O must be held high (at V_PUP) for at least **t_HTSS** before the first bit of a transaction. This is called Start.

### 6.5 Stop Condition

SI/O must be held high (at V_PUP) for at least **t_HTSS** after the last bit/ACK of a transaction. This signals end of transaction and (for writes) triggers the internal write cycle.

The datasheet describes Stop as a "null bit frame with SI/O pulled high" — master simply releases the line and waits t_HTSS.

### 6.6 Critical: No Breaks Inside Byte + ACK

The datasheet **explicitly states**: no unused clock cycles are permitted **during** a byte transfer and its ACK/NACK slot. The 9 bit-frames (8 data + 1 ACK/NACK) must be transmitted without interruption.

### 6.7 Communication Interruption Rules

If SI/O idle time between bit frames is **less than the maximum t_BIT** for the current speed mode, the transaction may continue. If the idle time exceeds max t_BIT, the device may interpret it as a communication error.

**Critical hazard:** Do **not** interrupt immediately after an ACK (logic 0) during a **write data stream**. The device may interpret the subsequent high period as a Stop condition and begin an internal write cycle (busy for t_WR), potentially with partial/corrupt data.

---

## 7. Addressing

### 7.1 Device Address Byte (8 bits)

```
Bit:  [7] [6] [5] [4] [3] [2] [1] [0]
       |--- Opcode ---|  |-- A2:A0 -| R/W
```

- Bits [7:4] = **4-bit opcode** (command selector)
- Bits [3:1] = **A2:A0** (3-bit slave address, hardcoded in device)
- Bit [0] = **R/W** direction: `0` = write, `1` = read

If the opcode is invalid or A2:A0 does not match the device's preprogrammed address, the device returns to Standby mode and does **not respond** (effectively a NACK — line stays high).

### 7.2 Memory Address Byte

- Bit [7]: **don't care** (ignored by device)
- Bits [6:0]: 7-bit address
  - EEPROM: `0x00`–`0x7F` (full range used)
  - Security Register: `0x00`–`0x1F` (bits [7:5] are don't care)
  - ROM Zone Register: one of `0x01`, `0x02`, `0x04`, `0x08`

---

## 8. Command Reference (Complete Opcode Map)

| Opcode (hex) | Binary [7:4] | Command | R/W=0 (Write) | R/W=1 (Read) |
|---|---|---|---|---|
| `0xA_` | `1010` | EEPROM Access | Write byte/page | Current/Random/Sequential Read |
| `0xB_` | `1011` | Security Register Access | Write user area (`0x10`–`0x1F`) | Random/Sequential Read |
| `0x2_` | `0010` | Lock Security Register | Lock (irreversible) | Check lock status |
| `0x7_` | `0111` | ROM Zone Register Access | Set zone to ROM | Read zone status |
| `0x1_` | `0001` | Freeze ROM Zones | Freeze (irreversible) | Check freeze status |
| `0xC_` | `1100` | Manufacturer ID | **Not allowed** (NACKs) | Read 3-byte ID |
| `0xD_` | `1101` | Standard Speed Mode | Set Standard Speed | Check if Standard Speed |
| `0xE_` | `1110` | High-Speed Mode | Set High-Speed | Check if High-Speed |

---

## 9. Command Sequences (Complete Protocol Detail)

### 9.1 Reset + Discovery (Mandatory After Power-Up)

**Reset phase:**
1. Pull SI/O low for **t_RESET** (min 96 µs High-Speed / 480 µs Standard Speed).
2. If device might be busy (e.g., in t_WR): pull low for **t_DSCHG** (min 150 µs) to force-discharge internal power and abort any active write.
3. Release SI/O (Hi-Z).

**Discovery phase (immediately after reset release):**
1. Wait **t_RRT** (min 8 µs) for reset recovery.
2. Master drives SI/O low for **t_DRR** (1 to 2−t_PUP µs).
3. Master releases SI/O. Device, if present, concurrently drives SI/O low and holds for **t_DACK** (8–24 µs).
4. Within the t_DACK window, master issues a short read strobe: pull low for **t_MSDR** (2–6 µs), release, and sample SI/O:
   - **Low** → device present (ACK).
   - **High** → no device detected (NACK).
5. Wait **t_HTSS** (min 150 µs) with SI/O high for the Start condition before issuing first command.

**After successful Discovery, device is in High-Speed mode and ready for commands.**

### 9.2 EEPROM Current Address Read

Reads 1 byte from the current Address Pointer position (must already be in EEPROM region).

```
[Start] → [0xA_ | A2:A0 | R/W=1] → device ACK
        → device outputs 1 byte → master NACK → [Stop]
```

Address Pointer increments by 1 after read. Wraps from `0x7F` to `0x00`.

### 9.3 EEPROM Random Read (Single Byte)

Sets Address Pointer, then reads.

```
[Start] → [0xA_ | A2:A0 | R/W=0] → device ACK
        → [memory address byte] → device ACK
[Start] → [0xA_ | A2:A0 | R/W=1] → device ACK
        → device outputs 1 byte → master NACK → [Stop]
```

The first Start + write portion is a "dummy write" to set the Address Pointer. The second Start (Repeated Start) begins the read.

### 9.4 EEPROM Sequential Read

Start with either Current Address Read or Random Read. After each received byte:
- Master sends **ACK** → device outputs next byte (Address Pointer increments, wraps `0x7F` → `0x00`).
- Master sends **NACK** → transfer ends, followed by Stop.

```
[Start] → [0xA_ | A2:A0 | R/W=0] → device ACK
        → [memory address byte] → device ACK
[Start] → [0xA_ | A2:A0 | R/W=1] → device ACK
        → byte₀ → master ACK → byte₁ → master ACK → ... → byteₙ → master NACK → [Stop]
```

### 9.5 EEPROM Byte Write

```
[Start] → [0xA_ | A2:A0 | R/W=0] → device ACK
        → [memory address byte] → device ACK
        → [data byte] → device ACK → [Stop]
```

Stop triggers internal write cycle (t_WR, max 5 ms). SI/O **must** remain pulled high during t_WR (device power).

### 9.6 EEPROM Page Write (Up to 8 Bytes)

```
[Start] → [0xA_ | A2:A0 | R/W=0] → device ACK
        → [memory address byte] → device ACK
        → [data₀] → device ACK → [data₁] → device ACK → ... → [data₇] → device ACK → [Stop]
```

- Maximum 8 data bytes per page write.
- Only the low 3 bits of the address counter increment during page write. Higher bits stay fixed.
- When the counter reaches the page boundary, it **wraps to the start of the same page** — data written past the boundary overwrites the beginning of the current page.
- Stop triggers t_WR for the entire page.

### 9.7 Security Register Random/Sequential Read

**Current Address Read is NOT supported** for Security Register. Always use Random Read.

```
[Start] → [0xB_ | A2:A0 | R/W=0] → device ACK
        → [security address byte (0x00–0x1F)] → device ACK
[Start] → [0xB_ | A2:A0 | R/W=1] → device ACK
        → byte₀ → master ACK → ... → byteₙ → master NACK → [Stop]
```

Address bits [7:5] in the security address byte are don't care.

### 9.8 Security Register Write (User Area Only)

Only addresses `0x10`–`0x1F` are writable. Same page-write rules as EEPROM (8-byte pages within the Security Register).

```
[Start] → [0xB_ | A2:A0 | R/W=0] → device ACK
        → [security address byte (0x10–0x1F)] → device ACK
        → [data byte(s)] → device ACK each → [Stop]
```

Stop triggers t_WR (max 5 ms).

### 9.9 Lock Security Register (Opcode `0x2_`)

**Irreversible.** After locking, the entire 32-byte Security Register becomes permanently read-only.

**Lock command:**
```
[Start] → [0x2_ | A2:A0 | R/W=0] → device ACK (if not yet locked) / NACK (if already locked)
        → [address: A7..A4 = 0b0110 (0x6_), lower bits don't care] → device ACK
        → [data: don't care, but must be sent] → device ACK → [Stop]
```

Stop triggers t_WR. After completion, Security Register is permanently locked.

If device is **already locked**, it NACKs the device address byte.

**Check lock status (non-destructive):**
```
[Start] → [0x2_ | A2:A0 | R/W=1]
        → device ACK = **unlocked**
        → device NACK = **locked**
```

### 9.10 Manufacturer ID Read (Opcode `0xC_`)

Read-only. The write form (R/W=0) is **not allowed** and the device will NACK.

```
[Start] → [0xC_ | A2:A0 | R/W=1] → device ACK
        → byte₀ → master ACK → byte₁ → master ACK → byte₂ → master NACK → [Stop]
```

3 bytes returned. Expected values:
- AT21CS01: `0x00`, `0xD2`, `0x00` → combined `0x00D200`
- AT21CS11: `0x00`, `0xD3`, `0x80` → combined `0x00D380`

### 9.11 ROM Zone Register Read (Opcode `0x7_`)

Random-read-like sequence. Address must be one of: `0x01`, `0x02`, `0x04`, `0x08`.

```
[Start] → [0x7_ | A2:A0 | R/W=0] → device ACK
        → [ROM zone register address] → device ACK
[Start] → [0x7_ | A2:A0 | R/W=1] → device ACK
        → 1 byte → master NACK → [Stop]
```

Returned value:
- `0x00` = zone is EEPROM (writable)
- `0xFF` = zone is ROM (permanently read-only)

### 9.12 ROM Zone Register Write (Opcode `0x7_`)

Sets a zone permanently to ROM. Data byte **must be `0xFF`**.

```
[Start] → [0x7_ | A2:A0 | R/W=0] → device ACK
        → [ROM zone register address (0x01/0x02/0x04/0x08)] → device ACK
        → [0xFF] → device ACK → [Stop]
```

Stop triggers t_WR.

### 9.13 Freeze ROM Zones (Opcode `0x1_`)

**Irreversible.** Permanently freezes all ROM zone register states.

**Freeze command:**
```
[Start] → [0x1_ | A2:A0 | R/W=0] → device ACK (not frozen) / NACK (already frozen)
        → [address: 0x55] → device ACK
        → [data: 0xAA] → device ACK → [Stop]
```

- If address ≠ `0x55` or data ≠ `0xAA`: device NACKs the incorrect byte and does **not** freeze.
- Stop triggers t_WR.

**Check freeze status (non-destructive):**
```
[Start] → [0x1_ | A2:A0 | R/W=1]
        → device ACK = **not frozen**
        → device NACK = **frozen**
```

### 9.14 Set Standard Speed Mode (Opcode `0xD_`) — AT21CS01 Only

**AT21CS11 always NACKs this opcode.**

**Set Standard Speed:**
```
[Start] → [0xD_ | A2:A0 | R/W=0] → device ACK → [Stop]
```

After ACK, device is in Standard Speed mode. All subsequent timing must use Standard Speed values.

**Check Standard Speed:**
```
[Start] → [0xD_ | A2:A0 | R/W=1]
        → device ACK = currently in Standard Speed
        → device NACK = not in Standard Speed (i.e., in High-Speed)
```

### 9.15 Set High-Speed Mode (Opcode `0xE_`)

**Set High-Speed:**
```
[Start] → [0xE_ | A2:A0 | R/W=0] → device ACK → [Stop]
```

**Check High-Speed:**
```
[Start] → [0xE_ | A2:A0 | R/W=1]
        → device ACK = currently in High-Speed
        → device NACK = not in High-Speed (i.e., in Standard Speed)
```

**Speed mode is volatile** — it resets to High-Speed on every power-up/reset.

---

## 10. Write Cycle Behavior and Busy Handling

### 10.1 Internal Write Cycle (t_WR)

- Triggered by **Stop** after a valid write command.
- Duration: up to **5 ms**.
- During t_WR, the device **does not recognize any commands**. All opcodes will NACK.
- **SI/O must remain pulled high** for the entire t_WR duration — the device relies on the pull-up voltage for power during the write cycle.

### 10.2 Busy Polling (ACK Polling)

To determine when the write cycle completes:
1. After the write's Stop, wait briefly, then attempt a new transaction (e.g., send Start + device address with the same opcode).
2. If device **NACKs** → still busy writing.
3. If device **ACKs** → write complete, device ready.
4. Repeat with bounded iterations / timeout.

### 10.3 Multi-Device Warning

If multiple devices share the **same SI/O line** (differentiated by A2:A0), do **not** communicate with any other device while one device is in t_WR. The write cycle requires uninterrupted power via the pull-up, and bus activity (pulling SI/O low) could corrupt the writing device's internal supply.

### 10.4 Forced Abort of Active Write

To interrupt a device that may be in t_WR:
1. Pull SI/O low for **t_DSCHG** (min 150 µs) — this discharges the device's internal power store.
2. Release SI/O.
3. Perform normal Reset + Discovery sequence.

**Warning:** Data being written during the aborted cycle may be **corrupted**.

---

## 11. Peculiarities and Edge Cases

### 11.1 Opcode/Address Mismatch Behavior

If the device address byte contains an invalid opcode or a non-matching A2:A0 value, the device **silently returns to Standby**. It does not drive the line (effectively a NACK), but there is no explicit error signal — the master simply sees the line stay high during the ACK slot.

### 11.2 Write Abort on Non-Byte-Boundary Stop

If a Stop condition occurs at a point other than a byte boundary during a write, the **write is aborted** — no data is committed and no t_WR cycle occurs.

### 11.3 Post-ACK Interruption Hazard

**Do not** pause or insert delays immediately after the device sends an ACK (logic 0) during a **write data stream**. The sustained high idle after the ACK may be interpreted by the device as a Stop condition, triggering a premature internal write cycle with potentially incomplete data.

### 11.4 Security Register Write After Lock

After the Security Register is locked:
- Write attempts → device ACKs device address byte and word address byte, but **NACKs the data byte**.
- No t_WR is triggered.
- Device is immediately ready for the next command.

### 11.5 ROM Zone Write to Protected Zone

When writing to an EEPROM address that falls within a ROM-protected zone:
- Device ACKs device address byte and word address byte, but **NACKs the data byte**.
- No t_WR is triggered.
- Device is immediately ready for the next command.

### 11.6 Speed Mode and Reset Interaction

- Speed mode is **volatile** — always resets to High-Speed on power-up or reset.
- After Reset + Discovery, the device is **always in High-Speed mode** regardless of what speed was active before the reset.
- The Discovery sequence itself uses High-Speed timing.

### 11.7 Sequential Read Wrap-Around

- EEPROM sequential read: Address Pointer wraps from `0x7F` → `0x00` and continues indefinitely.
- Security Register sequential read: Address Pointer wraps from `0x1F` → `0x00` within the Security Register space.

### 11.8 Security Register Lock Command Address Format

The lock command (opcode `0x2_`) requires the memory address byte to have bits [7:4] = `0b0110` (`0x6_`). The lower 4 bits are don't care. Example valid addresses: `0x60`, `0x6F`, `0x65`, etc.

### 11.9 Manufacturer ID Write Attempt

Opcode `0xC_` with R/W=0 (write direction) is **not a valid command**. The device NACKs the device address byte.

### 11.10 Freeze Command Safety Guard

The Freeze ROM Zones command (opcode `0x1_`) requires **exact** address byte `0x55` and data byte `0xAA`. If either is incorrect:
- Device NACKs the incorrect byte.
- ROM zones are not frozen.
- No t_WR is triggered.

### 11.11 Discovery After Power Loss

After any power interruption (SI/O low long enough to discharge internal supply), a fresh Reset + Discovery is required before the device will respond to commands.

### 11.12 t_DSCHG vs t_RESET

- **t_RESET** (≥96 µs HS / ≥480 µs SS) performs a clean reset when the device is idle or in a known state.
- **t_DSCHG** (≥150 µs) is specifically for discharging the internal power store to force-abort an active operation. Using t_DSCHG during an active write **will corrupt** the write data.
- For a general-purpose "always works" reset, using t_DSCHG (≥150 µs) followed by the standard Discovery sequence is the safest approach since t_DSCHG ≥ t_RESET for High-Speed mode.

---

## 12. CRC-8 Specification

Used for validating the factory serial number (Security Register bytes 0–7).

| Property | Value |
|---|---|
| Polynomial | X⁸ + X⁵ + X⁴ + 1 |
| Polynomial hex | `0x31` |
| Input | Security Register bytes 0–6 (7 bytes) |
| Expected result | Security Register byte 7 |
| Algorithm | Standard CRC-8: init 0, no input reflection, no output reflection, no final XOR |

Verification: compute CRC-8 over bytes 0–6 and compare with byte 7. Alternatively, compute CRC-8 over all 8 bytes — result should be `0x00`.

---

## 13. Summary of Irreversible Operations

| Operation | Opcode | What It Locks | Can Be Undone? |
|---|---|---|---|
| Lock Security Register | `0x2_` (W) | Entire 32-byte Security Register becomes read-only | **No** |
| Set ROM Zone | `0x7_` (W, data=`0xFF`) | Single 32-byte EEPROM zone becomes read-only | **No** |
| Freeze ROM Zones | `0x1_` (W, addr=`0x55`, data=`0xAA`) | All ROM zone register states become permanent | **No** |

---

## 14. Summary of NACK Conditions (Error Differentiation)

Understanding **where** in a transaction a NACK occurs is critical for error diagnosis:

| NACK Position | Meaning |
|---|---|
| Device address byte NACK | Opcode invalid, A2:A0 mismatch, device not present, device busy (in t_WR), or unsupported command (e.g., `0xD_` on AT21CS11) |
| Memory address byte NACK | Invalid/unexpected address for the command |
| Data byte NACK | Write to locked Security Register, write to ROM-protected zone, freeze with wrong guard bytes |
| Discovery NACK (line high) | No device on bus |

### 14.1 Device Address NACK Causes (Detail)

- Device not present on bus
- Device busy (in t_WR write cycle)
- A2:A0 address mismatch
- Invalid opcode
- AT21CS11 receiving Standard Speed opcode `0xD_`
- Lock Security check (R/W=1) when already locked → NACK
- Freeze check (R/W=1) when already frozen → NACK
- High-Speed check (R/W=1) when in Standard Speed → NACK
- Standard Speed check (R/W=1) when in High-Speed → NACK

---

## 15. Complete Timing Quick-Reference

### High-Speed Mode (default, both parts)

| Symbol | Min | Max | Unit | Purpose |
|---|---|---|---|---|
| t_RESET | 96 | — | µs | Reset low pulse |
| t_DSCHG | 150 | — | µs | Force-discharge low pulse |
| t_RRT | 8 | — | µs | Reset recovery |
| t_DRR | 1 | 2 − t_PUP | µs | Discovery request low |
| t_DACK | 8 | 24 | µs | Discovery response window |
| t_MSDR | 2 | 6 | µs | Discovery master strobe |
| t_HTSS | 150 | — | µs | Start/Stop high time |
| t_BIT | (*) | 25 | µs | Bit frame duration |
| t_LOW0 | 6 | 16 | µs | Write-0 low pulse |
| t_LOW1 | 1 | 2 | µs | Write-1 low pulse |
| t_RD | 1 | 2 | µs | Read strobe low pulse |
| t_MRS | — | t_RD+2+t_PUP | µs | Read sample window |
| t_HLD0 | 2 | 6 | µs | Device output-0 hold |
| t_RCV | 2 | — | µs | Recovery (line high) |
| t_WR | — | 5000 | µs | Write cycle time |

(*) t_BIT min = t_LOW0 + t_PUP + t_RCV

### Standard Speed Mode (AT21CS01 only)

| Symbol | Min | Max | Unit | Purpose |
|---|---|---|---|---|
| t_RESET | 480 | — | µs | Reset low pulse |
| t_DSCHG | 150 | — | µs | Force-discharge low pulse |
| t_HTSS | 600 | — | µs | Start/Stop high time |
| t_BIT | 40 | 100 | µs | Bit frame duration |
| t_LOW0 | 24 | 64 | µs | Write-0 low pulse |
| t_LOW1 | 4 | 8 | µs | Write-1 low pulse |
| t_RD | 4 | 8 | µs | Read strobe low pulse |
| t_MRS | — | t_RD+8+t_PUP | µs | Read sample window |
| t_HLD0 | 8 | 24 | µs | Device output-0 hold |
| t_RCV | 8 | — | µs | Recovery (line high) |
| t_NOISE | 0.5 | — | µs | Noise filter threshold |
| t_WR | — | 5000 | µs | Write cycle time |
