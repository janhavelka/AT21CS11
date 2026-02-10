# AT21CS01 / AT21CS11 — complete driver implementation report (datasheet DS20005857D)

This is a **driver-focused, datasheet-faithful** report for implementing **all features** of Microchip **AT21CS01** and **AT21CS11** single‑wire, I/O‑powered EEPROMs.

- Datasheet: **Microchip DS20005857D** (©2020)
- Devices: **AT21CS01** (Standard + High-Speed) and **AT21CS11** (High-Speed only)
- Interface: **single-wire serial** with **I²C-like protocol structure** (Start/Stop + 8-bit bytes + ACK/NACK), but **timed bit frames on one wire**.

---

## 0) What this chip “is” in practice

- A 2‑pin EEPROM (**SI/O + GND**) where **SI/O is both data and power** (powered through the pull-up).
- Master generates **bit frames** by pulling SI/O low for a controlled time.
- Bytes are **MSb first**, with a **9th ACK/NACK slot** after each 8 data bits.
- There are **no unused clock cycles** during byte transfer; **don’t insert breaks** inside a byte + ACK/NACK cycle (see §4.6).

---

## 1) Part differences (AT21CS01 vs AT21CS11)

### 1.1 Pull-up voltage range (V_PUP)
| Part | V_PUP range |
|---|---|
| AT21CS01 | **1.7–3.6 V** (High-Speed), **2.7–3.6 V** (Standard Speed) |
| AT21CS11 | **2.7–4.5 V** (High-Speed only) |

### 1.2 Speed modes
| Mode | AT21CS01 | AT21CS11 |
|---|---|---|
| Standard Speed | Supported (max **15.4 kbps**) | **Not supported** (opcode **Dh** NACKs) |
| High-Speed | Supported (max **125 kbps**) | Supported (max **125 kbps**) |

After **Reset + Discovery**, both devices default to **High-Speed**.

---

## 2) Memory map (“registers” and nonvolatile state)

### 2.1 Main EEPROM array
- Organization: **128 × 8 bytes** (1 Kbit)
- Address range: **0x00–0x7F**
- Page size: **8 bytes**
- Page count: **16 pages**

**Page boundaries** (8 bytes):
- 0x00–0x07, 0x08–0x0F, …, 0x78–0x7F

### 2.2 Security Register (32 bytes, “256-bit security register”)
- Size: **32 bytes** (4 pages × 8 bytes)
- Address range: **0x00–0x1F**
- Lower 16 bytes (0x00–0x0F): **read-only**
- Upper 16 bytes (0x10–0x1F): **user-programmable**, can be permanently locked

**Factory 64-bit serial number** is stored at Security Register addresses **0x00–0x07** (details §7.4).

### 2.3 Shared Address Pointer (important!)
The device has **one shared Address Pointer** used for:
- EEPROM accesses, and
- Security Register accesses

If you switch regions (EEPROM ↔ Security), the first read in the new region should be a **Random Read** (“dummy write”) to set the Address Pointer to a known value.

### 2.4 ROM Zones (permanent read-only sections of EEPROM)
EEPROM is split into **four 256‑bit zones** (32 bytes each):

| Zone | EEPROM range | ROM Zone Register address |
|---:|---|---|
| 0 | 0x00–0x1F | 0x01 |
| 1 | 0x20–0x3F | 0x02 |
| 2 | 0x40–0x5F | 0x04 |
| 3 | 0x60–0x7F | 0x08 |

Each zone can be permanently set to ROM (read-only) via **opcode 7h**.
ROM Zone Registers can be **frozen permanently** via **opcode 1h**.

### 2.5 Nonvolatile “state bits” you must support
Think of these as persistent “register-like” states:
- **Security Register locked** (irreversible) — opcode **2h**
- **ROM Zone bits** (4× one-way bits) — opcode **7h**
- **ROM Zone freeze** (irreversible) — opcode **1h**
- **Speed mode** (volatile until reset): Standard/High-Speed — opcodes **Dh / Eh** (AT21CS01), **Eh** only (AT21CS11)

---

## 3) Electrical requirements (do not skip these)

### 3.1 SI/O is open-drain and requires pull-up
- SI/O must be pulled up to V_PUP through **R_PUP**
- The device harvests power from the pull-up node

### 3.2 R_PUP limits (from DC characteristics)
**AT21CS01 (High-Speed / Standard-Speed limits differ by V_PUP):**
- V_PUP = 1.7 V → R_PUP **130–200 Ω**
- V_PUP = 2.7 V → R_PUP **0.2–1.8 kΩ**
- V_PUP = 3.6 V → R_PUP **0.33–4 kΩ**

**AT21CS11:**
- V_PUP = 2.7 V → R_PUP **0.2–1.8 kΩ**
- V_PUP = 4.5 V → R_PUP **0.4–5.4 kΩ**

### 3.3 Bus capacitance
- C_BUS max: **1000 pF**

### 3.4 Thresholds and hysteresis (high-level)
- V_IL max: **0.5 V**
- V_IH min: **0.7 × V_PUP**
- SI/O hysteresis V_HYS: device-specific (datasheet notes it depends on the internal supply derived from V_PUP, R_PUP, C_BUS, and timing).

---

## 4) Timing tables (AC characteristics)

> The critical design constraint is: pick **R_PUP**, **C_BUS**, and bit timing such that **t_RCV** is met.

### 4.1 Reset and Discovery timing (High-Speed timing applies after reset)
| Parameter | Symbol | Standard Speed (AT21CS01) | High-Speed (AT21CS01/11) | Units |
|---|---|---:|---:|---|
| Reset low time (inactive) | t_RESET | min **480** | min **96** | µs |
| Discharge low time (interrupt active op) | t_DSCHG | min **150** | min **150** | µs |
| Reset recovery time | t_RRT | N/A | min **8** | µs |
| Discovery Response request | t_DRR | N/A | **1 to (2 − t_PUP)** | µs |
| Discovery Response acknowledge | t_DACK | N/A | **8–24** | µs |
| Master strobe (during Discovery) | t_MSDR | N/A | **2–6** | µs |
| SI/O high time for Start/Stop | t_HTSS | N/A | min **150** | µs |

**Notes from datasheet:**
- Device defaults to **High-Speed after Reset**, so **High-Speed timing applies** after t_RESET.
- t_PUP is the line rise time from V_IL to V_IH, set by your R_PUP and C_BUS.

### 4.2 Data communication timing
| Parameter | Symbol | Standard Speed (AT21CS01) | High-Speed (AT21CS01/11) | Units |
|---|---|---:|---:|---|
| Bit frame duration | t_BIT | **40–100** | max **25**; min is constrained by t_LOW0 + t_PUP + t_RCV | µs |
| Start/Stop high time | t_HTSS | min **600** | min **150** | µs |
| SI/O low time for logic 0 | t_LOW0 | **24–64** | **6–16** | µs |
| SI/O low time for logic 1 | t_LOW1 | **4–8** | **1–2** | µs |
| Master low time during read (strobe) | t_RD | **4–8** | **1–2** | µs |
| Master read strobe time (sample window relation) | t_MRS | t_RD + 8 (+t_PUP) | t_RD + 2 (+t_PUP) | µs |
| Data output hold time for logic 0 | t_HLD0 | **8–24** | **2–6** | µs |
| Slave recovery time | t_RCV | min **8** | min **2** | µs |
| Noise filtering capability | t_NOISE | min **0.5** | — | µs |

**Constraint formula given by datasheet:**
- **t_BIT = t_LOW0 + t_PUP + t_RCV**

### 4.3 EEPROM cell characteristics
| Operation | Max | Units |
|---|---:|---|
| Write cycle time (t_WR) | **5** | ms |
| Write endurance | **1,000,000** | cycles |
| Data retention | **100** | years |

---

## 5) Physical layer (bit frames)

### 5.1 Input bit frame (master → device)
- Master starts every bit by pulling SI/O below V_IL.
- Logic value determined by how long master holds low:
  - **Logic 0:** hold low for t_LOW0
  - **Logic 1:** hold low for t_LOW1
- Device samples after max t_LOW1 and before min t_LOW0.

### 5.2 Output bit frame (device → master)
- Master begins output frame by pulling low for t_RD, then releases.
- Device responds:
  - **Logic 0:** holds SI/O low for t_HLD0 (so master samples low)
  - **Logic 1:** does not pull low (line rises via pull-up)
- Master samples within the t_MRS window.

---

## 6) Transaction framing: Start/Stop, bytes, ACK/NACK, interruptions

### 6.1 Start/Stop
- Start and Stop are created by holding SI/O **high at V_PUP** for at least **t_HTSS**.
- Datasheet calls Stop a “null bit frame with SI/O pulled high”, so the master may not need to actively drive high (release line and wait).

### 6.2 Byte + ACK/NACK
- 8 data bits MSb-first
- 9th bit is ACK/NACK:
  - ACK = receiver pulls low (logic 0)
  - NACK = line stays high (logic 1)

### 6.3 Important: do not break inside a byte
Datasheet explicitly states:
- **No unused clock cycles** during read/write.
- **No interruptions or breaks** during each byte transfer and its ACK/NACK.

### 6.4 Communication interruptions (if you absolutely must)
Datasheet allows resuming if SI/O idle time is less than the maximum allowed t_BIT for the current speed mode.
Critical warning:
- **Do not interrupt immediately after an ACK (logic 0) during a write data stream** — the device may interpret it as Stop and start an internal write cycle (busy for t_WR).

### 6.5 Internal write cycle behavior (busy)
- While internally writing, device **does not recognize commands**.
- If interrupted briefly without depleting internal power store: device tends to **NACK** indicating busy.
- If SI/O is held low longer than **t_DSCHG** during an active write, the internal supply discharges; write may terminate with **data corruption**.

### 6.6 Interrupting an active operation (forced reset)
To interrupt a busy device intentionally:
- Pull SI/O low for **t_DSCHG** (min 150 µs) to discharge internal power store,
- Release and perform normal Discovery sequence.

---

## 7) Addressing model (I²C-like, but on one wire)

### 7.1 Device Address byte (8 bits)
Format:
- Bits [7:4] = **4-bit opcode**
- Bits [3:1] = **A2:A0** (preprogrammed slave address)
- Bit [0] = **R/W** (0 write, 1 read)

If opcode invalid or A2:A0 mismatch → device returns to Standby and does not respond (NACK/no drive).

### 7.2 Memory Address byte (EEPROM / Security register)
- Bit 7: don’t care
- Bits [6:0]: address bits (EEPROM uses 0–127; Security uses 0–31; upper bits are don’t care for Security)

---

## 8) Opcode map (full command surface)

| Command | Opcode (bits 7:4) | Meaning |
|---|---|---|
| EEPROM access | **Ah** (1010) | Read/write main EEPROM array |
| Security register access | **Bh** (1011) | Read/write Security register (writes only user area) |
| Lock Security register | **2h** (0010) | Permanently lock Security register |
| ROM zone register access | **7h** (0111) | Read/write ROM Zone registers |
| Freeze ROM zone state | **1h** (0001) | Permanently freeze ROM Zone register state |
| Manufacturer ID read | **Ch** (1100) | Read 24-bit manufacturer/device ID |
| Standard Speed mode | **Dh** (1101) | AT21CS01 only (AT21CS11 NACK) |
| High-Speed mode | **Eh** (1110) | Enter/check High-Speed mode |

---

## 9) Reset + Discovery (required after power-up / reset)

### 9.1 Reset
- If device not busy: pull low for **t_RESET** (min 96 µs in High-Speed; 480 µs in Standard Speed spec)
- If device might be busy or you can’t be sure: pull low for **t_DSCHG** (150 µs) to force discharge reset.

### 9.2 Discovery Response (presence handshake)
After releasing SI/O:
1. Wait **t_RRT** (min 8 µs)
2. Drive SI/O low for **t_DRR**
3. Device concurrently drives low and holds for **t_DACK**
4. Master issues a short strobe **t_MSDR** within t_DACK and samples:
   - low → device present
   - high → no device
5. Wait **t_HTSS** to create Start, then proceed with first command

---

## 10) Read operations

### 10.1 EEPROM Current Address Read (opcode Ah, R/W=1)
Requirements:
- Address Pointer must currently be in EEPROM region.

Sequence:
1. Start
2. Device address: **Ah + A2:A0 + R/W=1**
3. Device outputs 1 byte
4. Master sends **NACK** to end
5. Stop

### 10.2 EEPROM Random Read (sets Address Pointer, then reads)
Sequence:
1. Start
2. Device address: **Ah + A2:A0 + R/W=0** (dummy write)
3. Memory address byte (A6:A0; A7 don’t care)
4. Repeated Start
5. Device address: **Ah + A2:A0 + R/W=1**
6. Read byte(s): ACK each to continue
7. NACK final byte
8. Stop

### 10.3 EEPROM Sequential Read
- Start with Current Address Read or Random Read
- After each received byte, master sends:
  - ACK → continue (Address Pointer increments; wraps to 0x00 after 0x7F)
  - NACK → stop

### 10.4 Security Register Read (opcode Bh)
Rules:
- Current-address read is **not supported** for Security register.
- Use Random/Sequential read (dummy write to set Address Pointer).

Sequence (Random/Sequential):
1. Start
2. Device address: **Bh + A2:A0 + R/W=0** (dummy write)
3. Security address byte (0x00–0x1F; A7–A5 don’t care)
4. Repeated Start
5. Device address: **Bh + A2:A0 + R/W=1**
6. Read bytes (ACK to continue, NACK to end)
7. Stop

### 10.5 Serial Number Read (from Security register)
The first 8 bytes of Security register:
- Byte 0: product identifier **0xA0**
- Bytes 1–6: 48-bit unique number
- Byte 7: CRC of bytes 0–6 using polynomial **X^8 + X^5 + X^4 + 1** (CRC-8 poly 0x31)

Recommended driver behavior:
- Read exactly 8 bytes starting at security address **0x00**.
- Validate:
  - product identifier == 0xA0
  - CRC check passes

### 10.6 Manufacturer ID Read (opcode Ch)
Read-only 24-bit ID (3 bytes). Write-form is not allowed (R/W=0 NACKs).

Sequence:
1. Start
2. Device address: **Ch + A2:A0 + R/W=1**
3. Read 3 bytes:
   - ACK after byte 1 and byte 2
   - NACK after byte 3
4. Stop

Expected 24-bit hex values:
- AT21CS01 → **00D200h**
- AT21CS11 → **00D380h**

---

## 11) Write operations

### 11.1 General write framing (EEPROM and Security Register writes)
Sequence structure:
1. Start
2. Device address: opcode Ah (EEPROM) or Bh (Security) with **R/W=0**
3. Memory address byte
4. Data byte(s) (8-bit)
5. Stop → triggers internal write cycle (t_WR, max 5 ms)

**Abort rule:**
- If Stop is sent somewhere other than a byte boundary, write is aborted.

### 11.2 Internal write cycle constraints
- During t_WR, device blocks commands.
- SI/O must stay **pulled high** for entire t_WR (device power).
- In multi-slave on one line: do **not** communicate with other devices while any device is in t_WR.

### 11.3 EEPROM Byte Write
- Use opcode **Ah**, R/W=0, send EEPROM address, then 1 data byte, Stop.

### 11.4 EEPROM Page Write (up to 8 bytes)
- Same as byte write, but send up to 8 data bytes (within same 8-byte page).
- Low 3 bits of address increment; higher bits do not.
- When address reaches page boundary, it **rolls over** within the page (avoid rollover to prevent accidental overwrite).

### 11.5 Security Register Write (user area only)
Rules:
- Only upper 16 bytes (0x10–0x1F) are writable.
- Same page rules as EEPROM (8-byte pages).

### 11.6 Locking the Security Register (opcode 2h)
Irreversible — after lock, entire 32-byte Security register is read-only.

Lock sequence emulates a Security byte write but:
- Opcode nibble: **2h**
- Memory address byte must have **A7..A4 = 0b0110 (6h)**; remaining bits are don’t care.
- Data byte is don’t care but must be transmitted.

Device responses:
- If not locked:
  - ACK to address and data, then Stop triggers t_WR → becomes locked
- If already locked:
  - NACK indicates locked

**Check Lock (non-destructive):**
- Use opcode **2h** with **R/W=1**
- Device returns:
  - ACK if **unlocked**
  - NACK if **locked**

**Behavior after lock when writing Security register:**
- Device ACKs device address and word address,
- NACKs the data byte and becomes immediately ready for next command.

---

## 12) Speed mode control (AT21CS01 + AT21CS11)

### 12.1 Standard Speed mode (AT21CS01 only) — opcode Dh
- Set Standard Speed:
  - Start → device address **Dh + A2:A0 + R/W=0** → device ACK → ready in Standard mode
- Check Standard Speed:
  - Start → device address **Dh + A2:A0 + R/W=1**
  - ACK if in Standard Speed, NACK otherwise
- AT21CS11: **always NACKs** opcode Dh

### 12.2 High-Speed mode (both) — opcode Eh
- Set High-Speed:
  - Start → device address **Eh + A2:A0 + R/W=0** → ACK
- Check High-Speed:
  - Start → device address **Eh + A2:A0 + R/W=1**
  - ACK if in High-Speed, NACK otherwise

---

## 13) ROM Zones (opcode 7h) and Freeze (opcode 1h)

### 13.1 Reading a ROM Zone Register (opcode 7h)
Random-read-like sequence:
1. Start
2. Device address: **7h + A2:A0 + R/W=0**
3. ROM Zone register address (one of 0x01/0x02/0x04/0x08)
4. Repeated Start
5. Device address: **7h + A2:A0 + R/W=1**
6. Read 1 byte:
   - **00h** → zone is EEPROM (writable)
   - **FFh** → zone is ROM (permanent read-only)
7. NACK + Stop

### 13.2 Writing a ROM Zone Register (opcode 7h)
1. Start
2. Device address: **7h + A2:A0 + R/W=0**
3. ROM Zone register address
4. Data byte must be **FFh**
5. Stop → triggers t_WR

### 13.3 Freeze ROM Zone Registers (opcode 1h)
Irreversible.

Freeze sequence:
1. Start
2. Device address: **1h + A2:A0 + R/W=0**
   - ACK if not frozen; NACK if already frozen
3. If ACK:
   - Send fixed address byte **0x55** → device ACK
   - Send data byte **0xAA** → device ACK
4. Stop → triggers t_WR

If address != 0x55 or data != 0xAA:
- Device NACKs and does not freeze.

**Check frozen state (non-destructive):**
- Start → device address **1h + A2:A0 + R/W=1**
- ACK if not frozen; NACK if frozen

### 13.4 Device response when writing inside ROM zone
If EEPROM write targets an address in a ROM zone:
- Device ACKs device address and word address,
- **NACKs the data byte**, and becomes ready for new command.

---

## 14) Recommended driver architecture notes (ESP32 / Arduino)

1. **Timing is tight in High-Speed mode.** Do not bit-bang with slow GPIO APIs in a jittery loop.
2. Implement a **timing-accurate PHY**:
   - direct GPIO register access OR
   - ESP32 RMT or another timing peripheral (optional)
3. Avoid mid-byte interrupts:
   - Use critical sections (disable interrupts briefly) around byte transfers.
4. Treat writes as “rare but special”:
   - `write*()` does Stop and starts t_WR
   - `waitReady()` polls for ACK by probing device address until it responds or timeout
   - optionally provide a nonblocking state-machine wrapper around `waitReady()`
5. For presence:
   - Protocol-level presence is Reset+Discovery
   - If your product needs “instant presence with no bus transaction”, use a dedicated **presence pin** (recommended).

---

## 15) Implementation checklist (all features)

### Must-have low-level
- [ ] drive low for exact microseconds
- [ ] release line (Hi‑Z)
- [ ] read line
- [ ] input bit frame (0/1)
- [ ] output bit frame (read bit)
- [ ] byte TX/RX with ACK/NACK
- [ ] Start/Stop (t_HTSS high time)
- [ ] Reset + Discovery (mandatory after reset/power-up)

### Commands
- [ ] EEPROM read: current, random, sequential
- [ ] EEPROM write: byte, page
- [ ] Security register read (random/sequential)
- [ ] Security user area write (0x10–0x1F only)
- [ ] Security lock + check lock
- [ ] Serial number read + CRC check
- [ ] Manufacturer ID read
- [ ] ROM zone register read/write
- [ ] Freeze ROM zones + check frozen
- [ ] Speed: set/check High-Speed; set/check Standard (AT21CS01 only)

---

*End of report.*
