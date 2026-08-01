# ORC subcircuit capture guide

Functional-block breakdown of the schematic for manual capture in KiCad, pulled directly from the live `orc.kicad_sch` connectivity graph on 2026-08-01 (post-fix: PWR_FLAG net collision and FB_B/COIL_9V stray label both resolved — see [kicad-mcp-logbook.md](../.claude/kicad-mcp-logbook.md)). Parts and LCSC numbers are in [hardware/BOM.md](../hardware/BOM.md); this doc is the "how it wires together," organized by block instead of by decision-chronology like [circuit-draft.md](circuit-draft.md).

Net names below are the actual local-label names already used in the schematic — reuse them verbatim if you're re-capturing so the two stay comparable.

**One net still has an unresolved short, not yet blocking capture but worth knowing going in**: `COIL_9V` and `GND_B` currently report as the same net in KiCad's own connectivity engine, and neither this session's exhaustive coordinate-level investigation nor the fixes already applied found the cause (logbook has the full trace — every individual pin checked resolves to its textually-correct label). **Use KiCad's Highlight Net tool on `COIL_9V` vs `GND_B` before wiring block 8 below** — a human eye on the rendered sheet will likely spot in seconds what coordinate-archaeology couldn't. If you're recapturing block 8 from scratch, this may be moot — the bug is presumably specific to some artifact of the existing wire layout, not the topology description below (which is correct per circuit-draft.md's decisions).

---

## Domain A (isolated ground) — MCU / USB side

### 1. Power ingress + source-select

**Purpose**: two possible Domain A power sources (USB-C VBUS or a dedicated hardwired DC terminal), only one ever electrically connected to the buck's Vin at a time — active P-FET disconnect, not diode-OR (rejected: see circuit-draft.md's numeric analysis).

**Parts**: J1 (USB-C receptacle), J5 (DC+CAN terminal), F1 (PTC fuse), Q1 (reverse-polarity FET), Q3 (source-select FET), Q4/R14/R15 (USB-presence detect — **flagged open item, see below**).

**Wiring**:
- `J5:1` (DC terminal, "AUX" pin) → `F1:1` → `F1:2` = net **`VEH_RAIL_A`** → `Q1:2` (drain)
- `Q1:1` (gate) — biased by `R1` from `Q1_GATE` net; `Q1:3` (source) → net **`V_PROT_A`**, shared with `Q3:2` and the TVS/analysis nodes
- `Q3:1` (gate) ← `Q3_GATE` net ← `R14` (pull-down, default ON) and `Q5:3` (NPN collector, when driven)
- `Q3:3` (source) → net **`VIN_BUCK_A`** — this is the buck's actual Vin node
- USB-C: `J1:A4/A9/B4/B9` (VBUS pins, all 4 shorted per USB-C spec) also land directly on **`VIN_BUCK_A`** — i.e. VBUS and the DC-terminal path (through Q3) both feed the same Vin node, which is the whole point of the source-select
- `Q4` (NPN) senses VBUS presence via its base (`Q4_BASE` net ← `R15` from `VIN_BUCK_A`) and its collector (`Q4_COL` net) drives `Q5` (`Q5_BASE` net), which drives `Q3`'s gate

**Known-incomplete, don't just copy this stage as-is**: circuit-draft.md flags this exact gate-drive chain ("values not yet worked out") and this session's review adds: a single NPN pulling toward GND cannot pull a high-side P-FET gate up to *turn it off* — as drawn this likely needs a second stage (e.g. the existing Q5 PNP is probably meant to be that second stage, inverting Q4's output back to a high-side-appropriate drive level, but the exact resistor values around it were never derived). Verify the logic level at Q3's gate in both VBUS-present and VBUS-absent states before trusting this block.

**J1 USB-C pins not otherwise used**: `SHIELD`, `CC1`, `CC2`, `SBU1`, `SBU2`. SHIELD/SBU1/SBU2 are legitimately no-connect (no altmode support needed). **CC1/CC2 are a real open item, not a no-connect** — USB-C spec requires 5.1kΩ Rd pull-downs to ground on both CC pins for the port to be recognized by a host and receive VBUS at all. Not yet sourced or placed.

### 2. Domain A buck (U3) — VIN_BUCK_A → 3V3_A

**Parts**: U3 (LM2596S-ADJ), L1, D3, C1/C1B (CIN), C2 (COUT), C3 (CIN bypass), R2/R3 (FB divider).

**Wiring**:
- `VIN_BUCK_A` → `U3:1` (Vin) and `U3:5` (ON/OFF, tied high = always on) — also → `C1:1`/`C1B:1` (CIN+) and `C3:1` (bypass)
- `U3:2` (switch node) = net **`SW_A`** → `L1:1`; `D3:2` (catch diode cathode) also on `SW_A`
- `L1:2` → net **`3V3_A`** (buck output) → `C2:1`/`C2:2`... actually `C2:1` on `3V3_A`, `C2:2` on `GND_A` (standard output cap orientation) → feeds the whole Domain-A 3.3V rail (ESP32, CAN transceiver, ADuM1250 primary side, etc. — see block 3/5)
- FB divider: `3V3_A` → `R2:1`, `R2:2`/`R3:1` = net **`FB_A`** → `U3:4` (FB pin), `R3:2` → `GND_A`
- `U3:3` (GND) and `D3:1` (catch diode anode) both on **`GND_A`**

### 3. ESP32-S3 core (U1)

**Parts**: U1, C4–C7 (decoupling), R4/C8 (EN RC), R5/R6 (boot straps).

**Wiring**:
- `U1:2` (3.3V entrance) ← `3V3_A`, decoupled by C4 (10µF at entrance) + C6/C7 (0.1µF at VDD3P3_CPU/VDD3P3_RTC pins per Espressif's placement guidance — keep physically close to those specific pins, not just anywhere on the rail)
- `U1:1/40/41` (GND pins) → `GND_A`
- `U1:3` (EN) → net **`EN_A`** ← `R4` (10k to 3V3_A) with `C8` (1µF) to GND — standard EN RC, ≥50µs assert/deassert per datasheet
- `U1:27` (GPIO0) → net **`GPIO0_A`** ← `R5` pull-up to 3V3_A (normal-boot strap)
- `U1:16` (GPIO46) → net **`GPIO46_A`** ← `R6` pull-down to GND_A (SPI-boot strap)
- `U1:13/14` (GPIO19/20, native USB D-/D+) → `USB_DM`/`USB_DP` — see block 4
- `U1:12/17` (I2C) → `SDA_A`/`SCL_A` — see block 5
- `U1:36/37` (UART) → `UART_RX_A`/`UART_TX_A` ← `U7` (CAN transceiver) — **note this is a UART connection to the CAN transceiver's RXD/TXD pins**, i.e. U7 is doing UART-framed CAN, standard for this transceiver family
- **28 GPIOs currently marked no-connect** (this session): pins 4,5,6,7,8,9,10,11,15,18,19,20,21,22,23,24,25,26,28,29,30,31,32,33,34,35,38,39. All reversible — delete the flag if a pin gets a role later. GPIO45 (pin 26) is among these; its strap-voltage-selection requirement (affects VDD_SPI boot behavior) was never confirmed this project — don't assign it a function without checking that first.

### 4. CAN transceiver + USB ESD

**Parts**: U7 (SN65HVD230), C17 (bypass), R16 (120Ω termination), J6 (termination jumper), D6 (CAN ESD, needs real symbol — see BOM), U8 (USB ESD).

**Wiring**:
- `U7:3` (VCC) ← `3V3_A`, decoupled by `C17` (100nF) directly at the pin; `U7:2/8` (GND) → `GND_A`
- `U7:1/4` (TXD/RXD) → `UART_TX_A`/`UART_RX_A` ← `U1:37/36`
- `U7:6/7` (CANL/CANH) → nets **`CAN_L`**/**`CAN_H`** → `J5:4/3` (field-wireable terminal) and `D6:2/1` (ESD)
- `R16` sits across `CAN_H`↔`CAN_L` in series with `J6` (2-pin jumper) — pull the jumper to remove termination in the field if ORC isn't the end-of-bus node. `R16:1` on `CAN_H`, `R16:2` on net **`TERM_MID`**, `J6:1` on `TERM_MID`, `J6:2` on `CAN_L` — i.e. the resistor+jumper are in series between the two CAN lines, not in parallel with each other.
- `U7:5` (Vref) — no-connect, standard practice when nothing else on the bus needs the reference
- `U8` (USBLC6-2SC6): `U8:1/3` already on `USB_DP`/`USB_DM` (paralleling J1↔U1's direct connection); `U8:2/5` (GND/VBUS-side) on `GND_A`/`VIN_BUCK_A`. **This session added `U8:6`/`U8:4` (the array's other-side I/O pins) onto the same `USB_DP`/`USB_DM` nets as a parallel stub** — electrically active, but a from-scratch capture would more typically insert U8 *in series* (J1 → U8 → U1), cutting the direct J1–U1 wire and routing through the ESD array. Worth doing properly if recapturing this block.

---

## Barrier — ADuM1250 isolated I2C (U2)

**Purpose**: the entire galvanic isolation between Domain A and Domain B collapses to this one 2-channel isolated I2C buffer. Nothing else crosses the barrier — deliberately (see circuit-draft.md's "fault/interrupt line: poll, don't add a channel" note; if you're tempted to add a second isolated signal, that's a topology decision, flag it, don't just wire it).

**Wiring**:
- Primary (Domain A) side: `U2:1` ← `3V3_A`, `U2:4` → `GND_A`, `U2:2`/`U2:3` = `SDA_A`/`SCL_A` (pulled up by R7/R8 to 3V3_A)
- Secondary (Domain B) side: `U2:8` ← `3V3_B`, `U2:5` → **`GND_B`** (this is the pin the GND2-not-grounded ERC warning was about — fixed this session by adding an actual `power:GND` symbol renamed to `GND_B`, not just relying on the label), `U2:6`/`U2:7` = `SCL_B`/`SDA_B` (pulled up by R9/R10 to 3V3_B)

---

## Domain B (chassis-referenced ground) — coil drive

### 5. Power ingress (Q2, D4, F2)

Mirrors block 1's reverse-polarity protection, but simpler — no source-select needed since Domain B only has one power path (the harness A+ tap).

**Wiring**:
- `J2:2` (harness A+) → `F2:1` → `F2:2` = net **`VEH_RAIL_B`** → `Q2:2` (drain), `D4:1` (TVS)
- `Q2:1` (gate) ← `R11` bias from `Q2_GATE` net; `Q2:3` (source) → net **`V_COIL_IN`** — this is Domain B's actual Vin node, feeding U4's buck directly
- **`Q2` is a known-undersized placeholder** (AO3401A, ~1-2A SOT-23) for sustained full-coil current — real part selection (DPAK/SO-8 automotive P-ch) is still open, per BOM.

### 6. Domain B buck (U4) — V_COIL_IN → COIL_9V

Structurally identical to block 2 (same IC, same topology), different output voltage and one extra part (CFF).

**Wiring**:
- `V_COIL_IN` → `U4:1`/`U4:5` (Vin/ON-OFF), `C11:1`/`C11B:1` (CIN), `C13:1` (CIN bypass)
- `U4:2` (switch node) = net **`SW_B`** → `L2:1`; `D5:2` on `SW_B`
- `L2:2` → net **`COIL_9V`** (the 9V rail feeding all ten coil drivers) → `C12` (COUT)
- FB divider: `COIL_9V` → `R12:1`, `R12:2`/`R13:1` = net **`FB_B`** → `U4:4`, `R13:2` → `GND_B`. `C18` (CFF) bridges `FB_B` → `COIL_9V` (feedforward, per TI's Table 9-6 recommendation at the 9V row)
- `U4:3` (GND), `D5:1` (catch diode anode) → `GND_B`

### 7. Domain B logic supply (U6) — COIL_9V → 3V3_B

**Wiring**: `U6:1` ← `COIL_9V`, `U6:3` → `GND_B`, `U6:2` → net **`3V3_B`**, decoupled by `C14`/`C15` (in/out). Feeds U5 and the ADuM1250's secondary side (block, above) — nothing else.

### 8. PCA9555 I2C GPIO expander (U5)

**Wiring**: `U5:24` ← `3V3_B` (decoupled by C16), `U5:12`/`U5:2`/`U5:21`/`U5:3` → `GND_B` (multiple ground pins, standard for this TSSOP-24 part), `U5:22`/`U5:23` = `SCL_B`/`SDA_B`. Ten I/O pins used (`U5:4` through `U5:14`, the `GPIO_CHn` nets below); `~INT` (`U5:1`) and 6 further I/O pins (`U5:15`–`U5:20`, i.e. `IO1_2`–`IO1_7`) are marked no-connect — the design polls over I2C rather than using the interrupt pin (see circuit-draft.md's "fault/interrupt line" note), and only 10 of 16 I/O are needed for 10 channels.

### 9. Per-channel coil driver (×10, identical stage repeated)

**One stage, channel `n` (1–10)**:
```
U5:pin(GPIO_CHn) ──RBn(10k)── QNn:1(base)
                              QNn:2(collector) ── GND_B  [common return]
                              QNn:3(emitter) ── GATEn net ── RPn(10k, pull-up)── V_COIL_IN
                                                          └── QPn:1(gate)
QPn:2(source) ── V_COIL_IN                    QPn:3(drain) ── COILn net ── J2:pin(n+2)
```
- PCA9555 output pin (open-drain-ish per the part, but driven by U5) pulls `RBn`→`QNn` base low to switch the channel on
- `QNn` (NPN, MMBT2222A) — when driven, its collector (on `GND_B`... wait, actually check per your capture: **collector connects to GND_B, emitter drives the gate net** — level-shifts the PCA9555's 3.3V logic down to pull `QPn`'s gate toward GND_B, turning the high-side P-FET on
- `RPn` holds `QPn`'s gate at `V_COIL_IN` (off) by default
- `QPn` (AO3401A, high-side) switches `V_COIL_IN` (~9V) onto the coil, net **`COILn`**, which goes straight to the harness pin

**Exact net names, all 10 channels** (for cross-checking a from-scratch capture): `GPIO_CH1`..`GPIO_CH10` (U5 pin → RBn), `BASE1`..`BASE10` (RBn → QNn base), `GATE1`..`GATE10` (QNn emitter / QPn gate / RPn), `COIL1`..`COIL10` (QPn drain → J2).

### 10. Harness connector (J2)

14 positions, 0.1" pitch, confirmed pinout (design-inputs.md):

| Pin | Net | Notes |
|---|---|---|
| 1 | `GND_B` | Coil common return |
| 2 | `VEH_RAIL_B_IN` (→ F2 → `VEH_RAIL_B`) | A+, harness-carried |
| 3–12 | `COIL1`–`COIL10` | One per channel |
| 13, 14 | `GND_B` | Chassis (relay-board side) — tied to the same net as pin 1 per design-inputs.md's explicit "no special handling needed" call |

---

## Quick net-name index

For grep-ability when cross-checking a recapture against this doc:

`VEH_RAIL_A`, `VEH_RAIL_A_IN`, `V_PROT_A`, `Q1_GATE`, `Q3_GATE`, `Q4_BASE`, `Q4_COL`, `Q5_BASE`, `VIN_BUCK_A`, `SW_A`, `3V3_A`, `FB_A`, `GND_A`, `EN_A`, `GPIO0_A`, `GPIO46_A`, `USB_DP`, `USB_DM`, `SDA_A`, `SCL_A`, `UART_RX_A`, `UART_TX_A`, `CAN_H`, `CAN_L`, `TERM_MID`, `3V3_B`, `SDA_B`, `SCL_B`, `VEH_RAIL_B`, `VEH_RAIL_B_IN`, `Q2_GATE`, `V_COIL_IN`, `SW_B`, `COIL_9V`, `FB_B`, `GND_B`, `GPIO_CH1`–`GPIO_CH10`, `BASE1`–`BASE10`, `GATE1`–`GATE10`, `COIL1`–`COIL10`.
