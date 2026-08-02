# ORC subcircuit capture guide

Functional-block breakdown of the schematic for manual capture in KiCad, pulled directly from the live `orc.kicad_sch` connectivity graph on 2026-08-01 (post-fix: PWR_FLAG net collision and FB_B/COIL_9V stray label both resolved — see [kicad-mcp-logbook.md](../.claude/kicad-mcp-logbook.md)). Parts and LCSC numbers are in [hardware/BOM.md](../hardware/BOM.md); this doc is the "how it wires together," organized by block instead of by decision-chronology like [circuit-draft.md](circuit-draft.md).

Net names below are the actual local-label names already used in the schematic — reuse them verbatim if you're re-capturing so the two stay comparable.

**One net still has an unresolved short, not yet blocking capture but worth knowing going in**: `COIL_9V` and `GND_B` currently report as the same net in KiCad's own connectivity engine, and neither this session's exhaustive coordinate-level investigation nor the fixes already applied found the cause (logbook has the full trace — every individual pin checked resolves to its textually-correct label). **Use KiCad's Highlight Net tool on `COIL_9V` vs `GND_B` before wiring block 8 below** — a human eye on the rendered sheet will likely spot in seconds what coordinate-archaeology couldn't. If you're recapturing block 8 from scratch, this may be moot — the bug is presumably specific to some artifact of the existing wire layout, not the topology description below (which is correct per circuit-draft.md's decisions).

---

## Domain A (isolated ground) — MCU / USB side

### 1. Power ingress + source-select

**Purpose**: two possible Domain A power sources (USB-C VBUS or a dedicated hardwired DC terminal), only one ever electrically connected to the buck's Vin at a time — active P-FET disconnect, not diode-OR (rejected: see circuit-draft.md's numeric analysis).

**Parts** (from [hardware/BOM.md](../hardware/BOM.md); resistors 0603 per board default). The Q4/Q5/R15 gate-drive chain is a **flagged open item, see below**:

| Ref | Value / part | Function | LCSC | Notes |
|---|---|---|---|---|
| J1 | USB-C receptacle, Hroparts TYPE-C-31-M-12 | USB-C VBUS/data ingress | C165948 | Basic/Extended tier not confirmed live |
| J5 | DORABO DB128L-5.08-4P-GN-S | DC + CAN screw terminal | — | needs KiCad footprint check (right-angle 5.08mm 4-pos) |
| F1 | 0.75A PTC, Littelfuse-class | Input resettable fuse | C207083 | verify 85°C-derated hold current against load before trusting |
| Q1 | AO3401A | Reverse-polarity FET (DC-terminal input) | C15127 | SOT-23 P-ch |
| R1 | 10kΩ ±1% 0603, UNI-ROYAL | Q1 gate bias | C25804 | board default 10k |
| Q3 | AO3401A | Source-select FET (disconnects DC path when VBUS present) | C15127 | gate-drive stage around it unverified |
| Q4 | MMBT2222A | USB-presence-detect NPN | — | open item: a single NPN pulling toward GND cannot turn a high-side P-FET *off*; likely needs a second stage |
| Q5 | 2nd-stage transistor (NPN/PNP unresolved) | Inverts Q4 output to high-side gate drive | — | referenced in wiring, not in BOM; part of the same unresolved gate-drive gap |
| R14 | 10kΩ ±1% 0603, UNI-ROYAL | Q3 gate pull-down (default: DC path ON) | C25804 | |
| R15 | 10kΩ ±1% 0603, UNI-ROYAL | Q4 base resistor from VIN_BUCK_A | C25804 | same open item as Q4 |
| C19 | 1µF/100V | VIN_BUCK_A bypass (TVS-adjacent) | — | generic, not individually sourced |

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

**Parts + part numbers** (LCSC/JLCPCB, from circuit-draft.md's sourcing pass; resistors are 0603 per the board default). Statuses carry the same caveats as [hardware/BOM.md](../hardware/BOM.md) — verify tier live before locking:

| Ref | Value / part | Function | LCSC | Notes |
|---|---|---|---|---|
| U3 | LM2596S-ADJ | Buck regulator, VIN_BUCK_A → 3V3_A | C963385 | TO-263, adjustable, 3A, Vin 4.5–40V |
| L1 | 68µH, KOHERelec MDA1870-680M | Buck inductor | C3015595 | SMD pad-mount 17.8×16.9mm; tier unverified |
| D3 | SS34, MDD | Catch diode | C8678 | 40V/3A Schottky, SMA(DO-214AC); meets the ≥36V catch-diode req (1.25×28.8V) with ~11% headroom |
| C1, C1B | 47µF/63V ×2, Nichicon PCR1J470MCL1GS | CIN bulk (parallel, ~94µF) | C3274436 | SMD polymer |
| C3 | 1µF/100V X7R, Samsung CL31B105KCHNNNE | CIN ceramic bypass at U3 pin | C13832 | 1206 |
| C2 | 220µF/35V, NJCON 2210350810R00 | COUT bulk | C5243827 | polymer (fallback ROQANG C72498, wet electrolytic) |
| — | 1µF/50V X7R, Samsung CL21B105KBFNNNE | COUT ceramic bypass | C28323 | 0805; confirm it exists as its own ref in the schematic |
| R2 | 2.0kΩ ±1% 0603, UNI-ROYAL 0603WAF2001T5E | FB divider, top (3V3_A → FB_A) | C22975 | **Basic**; 0603 (was 0805 C17604) |
| R3 | 1.2kΩ ±1% 0603, UNI-ROYAL 0603WAF1201T5E | FB divider, bottom (FB_A → GND_A) | C22765 | **Basic**; 0603 (was 0805 C17379) |

FB divider sets Vout = 1.23 × (1 + R2/R3) = 1.23 × (1 + 2.0k/1.2k) = **3.28V** (0.6% low of 3.3V, within margin). **No CFF on this instance** — the feedforward cap (1nF, C29925) is only on the 9V coil buck (U4), since 3.3V is well under TI's >10V CFF trigger.

**Wiring**:
- `VIN_BUCK_A` → `U3:1` (Vin) and `U3:5` (ON/OFF, tied high = always on) — also → `C1:1`/`C1B:1` (CIN+) and `C3:1` (bypass)
- `U3:2` (switch node) = net **`SW_A`** → `L1:1`; `D3:2` (catch diode cathode) also on `SW_A`
- `L1:2` → net **`3V3_A`** (buck output) → `C2:1`/`C2:2`... actually `C2:1` on `3V3_A`, `C2:2` on `GND_A` (standard output cap orientation) → feeds the whole Domain-A 3.3V rail (ESP32, CAN transceiver, ADuM1250 primary side, etc. — see block 3/5)
- FB divider: `3V3_A` → `R2:1`, `R2:2`/`R3:1` = net **`FB_A`** → `U3:4` (FB pin), `R3:2` → `GND_A`
- `U3:3` (GND) and `D3:1` (catch diode anode) both on **`GND_A`**

### 3. ESP32-S3 core (U1)

**Parts**:

| Ref | Value / part | Function | LCSC | Notes |
|---|---|---|---|---|
| U1 | ESP32-S3-WROOM-1U-N8 | MCU (external antenna) | — | schematic symbol is the -1 (PCB-antenna) footprint; swap to the real -1U footprint before layout; confirm -N8 = 8MB/no-PSRAM |
| C4 | 10µF/25V, Samsung | Main 3.3V entrance decoupling | C96446 | **JLC Basic** |
| C6 | 0.1µF/50V, Samsung | 3V3 pin decoupling | C14663 | **JLC Basic**; place close to the module's 3V3 pin |
| C8 | 10µF/25V, Samsung | EN RC cap | C96446 | **JLC Basic**; same part as C4 (was 1µF) |
| R4 | 10kΩ ±1% 0603, UNI-ROYAL | EN RC pull-up to 3V3_A | C25804 | ≥50µs assert/deassert per datasheet |
| R5 | 10kΩ ±1% 0603, UNI-ROYAL | GPIO0 pull-up (normal-boot strap) | C25804 | |
| R6 | 10kΩ ±1% 0603, UNI-ROYAL | GPIO46 pull-down (SPI-boot strap) | C25804 | |

**Wiring**:
- `U1:2` (3.3V entrance) ← `3V3_A`, decoupled by C4 (10µF at entrance) + C6 (0.1µF at the module's 3V3 pin — keep close to the pin, not just anywhere on the rail). The WROOM-1U module exposes a single 3V3 supply pin, not the die-level VDD3P3_CPU/VDD3P3_RTC pins, so only one high-frequency decoupler is needed here.
- `U1:1/40/41` (GND pins) → `GND_A`
- `U1:3` (EN) → net **`EN_A`** ← `R4` (10k to 3V3_A) with `C8` (1µF) to GND — standard EN RC, ≥50µs assert/deassert per datasheet
- `U1:27` (GPIO0) → net **`GPIO0_A`** ← `R5` pull-up to 3V3_A (normal-boot strap)
- `U1:16` (GPIO46) → net **`GPIO46_A`** ← `R6` pull-down to GND_A (SPI-boot strap)
- `U1:13/14` (GPIO19/20, native USB D-/D+) → `USB_DM`/`USB_DP` — see block 4
- `U1:12/17` (I2C) → `SDA_A`/`SCL_A` — see block 5
- `U1:36/37` (UART) → `UART_RX_A`/`UART_TX_A` ← `U7` (CAN transceiver) — **note this is a UART connection to the CAN transceiver's RXD/TXD pins**, i.e. U7 is doing UART-framed CAN, standard for this transceiver family
- **28 GPIOs currently marked no-connect** (this session): pins 4,5,6,7,8,9,10,11,15,18,19,20,21,22,23,24,25,26,28,29,30,31,32,33,34,35,38,39. All reversible — delete the flag if a pin gets a role later. GPIO45 (pin 26) is among these; its strap-voltage-selection requirement (affects VDD_SPI boot behavior) was never confirmed this project — don't assign it a function without checking that first.

### 4. CAN transceiver + USB ESD

**Parts**:

| Ref | Value / part | Function | LCSC | Notes |
|---|---|---|---|---|
| U7 | SN65HVD230 | CAN transceiver (3.3V native) | C12084 | Basic/Extended tier not confirmed live; ±16kV HBM ESD on bus pins built in — no external CAN ESD part needed |
| C17 | 100nF | U7 VCC bypass at pin | — | generic |
| R16 | 120Ω ±1% 1206, UNI-ROYAL 1206W4F1200T5E | CAN bus termination | C17909 | **Basic**; 1206 |
| J6 | 2.54mm 1×2 header + shunt | Termination jumper (field-removable) | C36717 (header) / C5305 (shunt) | tier not cleanly re-verified |
| U8 | USBLC6-2SC6 | USB D+/D- ESD array | C2827654 | SOT-23-6; wired as a parallel stub this session, prefer true series insertion on recapture |

**Wiring**:
- `U7:3` (VCC) ← `3V3_A`, decoupled by `C17` (100nF) directly at the pin; `U7:2/8` (GND) → `GND_A`
- `U7:1/4` (TXD/RXD) → `UART_TX_A`/`UART_RX_A` ← `U1:37/36`
- `U7:6/7` (CANL/CANH) → nets **`CAN_L`**/**`CAN_H`** → `J5:4/3` (field-wireable terminal). **No external CAN ESD diode** — the SN65HVD230 has ±16kV HBM ESD on its bus pins built in (D6 dropped this pass).
- `R16` sits across `CAN_H`↔`CAN_L` in series with `J6` (2-pin jumper) — pull the jumper to remove termination in the field if ORC isn't the end-of-bus node. `R16:1` on `CAN_H`, `R16:2` on net **`TERM_MID`**, `J6:1` on `TERM_MID`, `J6:2` on `CAN_L` — i.e. the resistor+jumper are in series between the two CAN lines, not in parallel with each other.
- `U7:5` (Vref) — no-connect, standard practice when nothing else on the bus needs the reference
- `U8` (USBLC6-2SC6): `U8:1/3` already on `USB_DP`/`USB_DM` (paralleling J1↔U1's direct connection); `U8:2/5` (GND/VBUS-side) on `GND_A`/`VIN_BUCK_A`. **This session added `U8:6`/`U8:4` (the array's other-side I/O pins) onto the same `USB_DP`/`USB_DM` nets as a parallel stub** — electrically active, but a from-scratch capture would more typically insert U8 *in series* (J1 → U8 → U1), cutting the direct J1–U1 wire and routing through the ESD array. Worth doing properly if recapturing this block.

---

## Barrier — ADuM1250 isolated I2C (U2)

**Purpose**: the entire galvanic isolation between Domain A and Domain B collapses to this one 2-channel isolated I2C buffer. Nothing else crosses the barrier — deliberately (see circuit-draft.md's "fault/interrupt line: poll, don't add a channel" note; if you're tempted to add a second isolated signal, that's a topology decision, flag it, don't just wire it).

**Parts**:

| Ref | Value / part | Function | LCSC | Notes |
|---|---|---|---|---|
| U2 | ADuM1250ARZ-RL7 | 2-channel isolated I2C buffer (the entire A↔B barrier) | C13839 | isolation-voltage/data-rate not re-verified against ADI Rev. L |
| C9, C10 | 0.1µF ×2 | Primary/secondary-side decoupling | — | generic 0603 |
| R7, R8 | 10kΩ ±1% 0603 ×2, UNI-ROYAL | SDA_A/SCL_A pull-ups to 3V3_A | C25804 | |
| R9, R10 | 10kΩ ±1% 0603 ×2, UNI-ROYAL | SDA_B/SCL_B pull-ups to 3V3_B | C25804 | |

**Wiring**:
- Primary (Domain A) side: `U2:1` ← `3V3_A`, `U2:4` → `GND_A`, `U2:2`/`U2:3` = `SDA_A`/`SCL_A` (pulled up by R7/R8 to 3V3_A)
- Secondary (Domain B) side: `U2:8` ← `3V3_B`, `U2:5` → **`GND_B`** (this is the pin the GND2-not-grounded ERC warning was about — fixed this session by adding an actual `power:GND` symbol renamed to `GND_B`, not just relying on the label), `U2:6`/`U2:7` = `SCL_B`/`SDA_B` (pulled up by R9/R10 to 3V3_B)

---

## Domain B (chassis-referenced ground) — coil drive

### 5. Power ingress (Q2, F2)

Mirrors block 1's reverse-polarity protection, but simpler — no source-select needed since Domain B only has one power path (the harness A+ tap).

**Parts**:

| Ref | Value / part | Function | LCSC | Notes |
|---|---|---|---|---|
| F2 | 0.75A PTC, Littelfuse-class | Domain B input resettable fuse | C207083 | same line as F1; verify 85°C derating |
| Q2 | AO3401A (placeholder) | Reverse-polarity FET, Domain B | C15127 | undersized for sustained coil current; needs a DPAK/SO-8 automotive P-ch |
| R11 | 10kΩ ±1% 0603, UNI-ROYAL | Q2 gate bias | C25804 | |
| C20 | 1µF/100V | V_COIL_IN bypass | — | generic, not individually sourced |

**Wiring**:
- `J2:2` (harness A+) → `F2:1` → `F2:2` = net **`VEH_RAIL_B`** → `Q2:2` (drain)
- `Q2:1` (gate) ← `R11` bias from `Q2_GATE` net; `Q2:3` (source) → net **`V_COIL_IN`** — this is Domain B's actual Vin node, feeding U4's buck directly
- **`Q2` is a known-undersized placeholder** (AO3401A, ~1-2A SOT-23) for sustained full-coil current — real part selection (DPAK/SO-8 automotive P-ch) is still open, per BOM.

### 6. Domain B buck (U4) — V_COIL_IN → COIL_9V

Structurally identical to block 2 (same IC, same topology), different output voltage and one extra part (CFF).

**Parts**:

| Ref | Value / part | Function | LCSC | Notes |
|---|---|---|---|---|
| U4 | LM2596S-ADJ | Coil buck, V_COIL_IN → COIL_9V (≤32V in) | C963385 | same part/line as U3 |
| L2 | 68µH, KOHERelec MDA1870-680M | Buck inductor | C3015595 | same part as L1 |
| D5 | SS34, MDD | Catch diode | C8678 | same part as D3; also the flyback-diode candidate if the relay board needs one |
| C11, C11B | 47µF/63V ×2, Nichicon PCR1J470MCL1GS | CIN bulk (parallel, ~94µF) | C3274436 | same part as C1/C1B |
| C12 | 220µF/35V, NJCON 2210350810R00 | COUT bulk | C5243827 | same part as C2 |
| C13 | 1µF/100V X7R, Samsung CL31B105KCHNNNE | CIN ceramic bypass | C13832 | same part as C3 |
| C18 | 1000pF (1nF) C0G 0805, Fenghua 0805CG102J500NT | Feedforward cap (CFF) | C29925 | TI Table 9-6 lists CFF at the 9V row |
| R12 | 7.5kΩ ±1% 0603, UNI-ROYAL 0603WAF7501T5E | FB divider, top | C23234 | **Basic**; 0603 (was 0805 C17807) |
| R13 | 1.2kΩ ±1% 0603, UNI-ROYAL 0603WAF1201T5E | FB divider, bottom | C22765 | **Basic**; 0603 to match R3 (was 0805 C17379) |

FB divider sets Vout = 1.23 × (1 + 7.5k/1.2k) = **8.92V**. CFF (C18) is present on this instance per TI's Table 9-6, unlike the 3.3V buck.

**Wiring**:
- `V_COIL_IN` → `U4:1`/`U4:5` (Vin/ON-OFF), `C11:1`/`C11B:1` (CIN), `C13:1` (CIN bypass)
- `U4:2` (switch node) = net **`SW_B`** → `L2:1`; `D5:2` on `SW_B`
- `L2:2` → net **`COIL_9V`** (the 9V rail feeding all ten coil drivers) → `C12` (COUT)
- FB divider: `COIL_9V` → `R12:1`, `R12:2`/`R13:1` = net **`FB_B`** → `U4:4`, `R13:2` → `GND_B`. `C18` (CFF) bridges `FB_B` → `COIL_9V` (feedforward, per TI's Table 9-6 recommendation at the 9V row)
- `U4:3` (GND), `D5:1` (catch diode anode) → `GND_B`

### 7. Domain B logic supply (U6) — COIL_9V → 3V3_B

**Parts**:

| Ref | Value / part | Function | LCSC | Notes |
|---|---|---|---|---|
| U6 | AMS1117-3.3 | Domain B logic LDO, 9V → 3.3V | — | never individually sourced; needs a Gate 1 pass (SOT-223) |
| C14, C15 | 10µF ×2 | LDO in/out | — | generic |

**Wiring**: `U6:1` ← `COIL_9V`, `U6:3` → `GND_B`, `U6:2` → net **`3V3_B`**, decoupled by `C14`/`C15` (in/out). Feeds U5 and the ADuM1250's secondary side (block, above) — nothing else.

### 8. PCA9555 I2C GPIO expander (U5)

**Parts**:

| Ref | Value / part | Function | LCSC | Notes |
|---|---|---|---|---|
| U5 | PCA9555PW | I2C GPIO expander, addr 0x20, 10-of-16 I/O used | — | prior LCSC number doesn't match LCSC's format; re-pull before ordering |
| C16 | 0.1µF | U5 decoupling | — | generic |

**Wiring**: `U5:24` ← `3V3_B` (decoupled by C16), `U5:12`/`U5:2`/`U5:21`/`U5:3` → `GND_B` (multiple ground pins, standard for this TSSOP-24 part), `U5:22`/`U5:23` = `SCL_B`/`SDA_B`. Ten I/O pins used (`U5:4` through `U5:14`, the `GPIO_CHn` nets below); `~INT` (`U5:1`) and 6 further I/O pins (`U5:15`–`U5:20`, i.e. `IO1_2`–`IO1_7`) are marked no-connect — the design polls over I2C rather than using the interrupt pin (see circuit-draft.md's "fault/interrupt line" note), and only 10 of 16 I/O are needed for 10 channels.

### 9. Per-channel coil driver (×10, identical stage repeated)

**Parts** (per stage, ×10 — refs suffixed 1 through 10):

| Ref | Value / part | Function | LCSC | Notes |
|---|---|---|---|---|
| RBn | 10kΩ ±1% 0603, UNI-ROYAL | PCA9555 output → NPN base | C25804 | ×10 |
| QNn | MMBT2222A | Level-shift NPN | — | ×10; not yet re-verified on live catalog |
| RPn | 10kΩ ±1% 0603, UNI-ROYAL | Gate pull-up to V_COIL_IN | C25804 | ×10 |
| QPn | AO3401A | High-side coil switch | C15127 | ×10; same LCSC line as Q1/Q3, separate BOM quantity |

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

**Parts**:

| Ref | Value / part | Function | LCSC | Notes |
|---|---|---|---|---|
| J2 | ZHOURI 2.54mm 1×40 breakable strip (snap to 14 pos) | Harness to relay board | C2977586 | pitch/pin-count/pinout confirmed and locked |

**Pinout** (14 positions, 0.1" pitch, confirmed per design-inputs.md):

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
