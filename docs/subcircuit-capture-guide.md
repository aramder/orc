# ORC subcircuit capture guide

Single source of truth for parts, LCSC numbers, and wiring — organized by the **actual KiCad sheet hierarchy** in `hardware/*.kicad_sch`. `hardware/BOM.md` was retired 2026-08-01 (redundant, drifted out of sync with the new sheets' ref numbering) — everything from it that was still current lives here now.

Five sheets exist today: Power Side A, Power Side B, MCU, Communications, I2C Isolator. Same one-sheet-per-function pattern continues for whatever gets captured next. **Root `orc.kicad_sch` is legacy** — its content is being superseded sheet-by-sheet, not maintained in parallel. Where a sheet below is missing parts a legacy-root block already had, that's flagged explicitly as **not yet migrated**, not a design change, unless stated otherwise.

**Ref designators were reassigned during the hierarchical split** — don't cross-reference old block-numbered refs (U1–U8, Q1–Q5, R1–R18 from the pre-split BOM) against these; use the ref shown in each sheet's table, which matches what's actually in the file today.

**Status legend** (carried from the old BOM.md): ✅ LCSC confirmed live · ⚠️ real part/value decided, tier or wiring detail still open · ❌ real unresolved gap · 🔧 KiCad-side symbol/footprint work needed, not a sourcing gap.

## OPEN ARCHITECTURE DECISION — MCU module, not locked, don't source against U7/J4/U5/R11/R12 yet

**Mechanical finding, 2026-08-01**: only ~8.5mm clearance between the two stacked PCBs in the enclosure — not enough room for both a USB-C connector and the required DC+CAN screw terminal on a single-sided board. There's a cutout in the upper board exposing ~20mm of the lower board's edge near the connector location, which could fit the screw terminal, but the USB-C receptacle's current footprint doesn't obviously coexist with it.

**Direction, pending research**: rather than solve the connector-placement problem, replace the bare ESP32-S3-WROOM-1U + custom USB-C/ESD/CC-pulldown circuit (U7, J4, U5, R11, R12 below) with a **complete pre-made ESP32-S3 module** that already has USB-C, ESD, and USB circuitry built in — mounted onto ORC's board rather than designed on it. **Hard requirement carried forward unchanged: external antenna (U.FL) support** — this board is in a sealed metal enclosure, PCB-trace-antenna-only modules are disqualified regardless of price. A sourcing/research pass for real candidates is in progress (2026-08-01) — **do not source or lock U7/J4/U5/R11/R12 further until that lands**; they may all get replaced by a single module part number. SW1/SW2 (EN/BOOT buttons) may also become redundant if the chosen module already has its own — don't build those into the schematic as final until the module is picked.

This also has a welcome side effect if it proceeds: dropping the custom USB-C ingress removes the VBUS-vs-DC-terminal source-select stage (Q1/Q3/Q4/Q5, still legacy-root-only) entirely — including Q4/Q5's gate-drive chain, which has been an unresolved electrical gap since early in the project. Domain A power ingress would simplify to just the DC terminal path, same shape as Domain B.

## Housekeeping — found in the 2026-08-01 documentation audit, not yet resolved

- **Catch diode value — resolved 2026-08-01: SS34 (C8678) is correct.** Root schematic's D2/D3 still show "SS56" (stale, predates the correction) — harmless since root is legacy and not sourced from, but don't let it confuse a future read of root.
- **D1 (SMBJ26CA, load-dump TVS) is still placed in root** even though the design decision to drop it was made and documented — the symbol was never deleted. Low priority since root is legacy, but noted so it isn't mistaken for a live requirement if root gets referenced.
- **Root's `J2` symbol carries "(C2827883)" embedded in its description field.** As of the latest Gate 1 pass this number is confirmed correct: DORABO DB128L-5.08-4P-GN-S, 5.08mm 4-pos screw terminal, C2827883 (Extended, 16A/300V, 28.9k stock). It's now carried in J6's LCSC column in Block 4.
- **Root has a stray `R4 22kΩ`** with no documented function anywhere in current or historical BOM content. Possibly a leftover from the dropped TVS/load-dump analysis network. Needs a function check before it's either sourced or written off as dead.
- **PCB is significantly out of sync with the current schematic** (checked 2026-08-01): the entire Power Side B subcircuit (U10/R26/L4/R27/D6) is placed in the schematic but absent from the PCB; U7 (ESP32) and U9 (ADuM1250) are also missing from the PCB despite their support passives being placed. What *is* on the PCB uses stale ref numbers from before the last schematic re-annotation (`U3`→`U8`, `R18`/`R19`→`R24`/`R25` are the same physical parts). Needs a `pcb_sync_from_schematic` pass once the schematic itself is further along — resyncing now would just need to happen again.
- **Still unresolved from the prior pass**: J5 (Communications, 2-pin header — unclear role), R13 (Communications, 10kΩ 0.1% — unclear role), and a bare `GND` power symbol on the MCU sheet where everywhere else uses `GND_A` specifically. (SW1's function is resolved — see MCU sheet — and it's the same part as the still-to-be-placed SW2/BOOT button.)

---

## Power Side A — `power_side_a.kicad_sch`

**Purpose**: Domain A buck regulator, VIN → 3V3_A.

**Parts placed today**:

| Ref | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| U8 | LM2596S-ADJ | Buck regulator | C963385 | ✅ TO-263, adjustable, 3A, Vin 4.5–40V |
| L3 | **68µH, SXN SMDRI127-680MT** | Buck inductor | C9907 | ✅ sourced live 2026-08-01, replacing an earlier KOHERelec pick that had only 194 units in stock. 49,310 in stock, Basic tier, 4A Isat/2.1A rated (3×+ margin over the 0.67A load), DCR 140mΩ, 12.3×12.3×8mm. **Open**: automotive/extended-temp rating not stated on the listing. **Pending edit**: sheet currently shows this part placed at 15µH (an unintended template placeholder, confirmed) — value and footprint both need updating in KiCad. |
| D5 | SS34, MDD (Microdiode Semiconductor) | Catch diode | C8678 | ✅ 40V/3A Schottky, SMA(DO-214AC), Basic, 2.37M in stock. Meets the ≥36V requirement (1.25×28.8V) with the exact TI Fig 9-13 bracket part. |
| R24 | 1.2kΩ ±1% 0603, UNI-ROYAL | FB divider, bottom | C22765 | ✅ Basic |
| R25 | 2.0kΩ ±1% 0603, UNI-ROYAL | FB divider, top | C22975 | ✅ Basic |

**Not yet migrated from legacy root**: CIN bulk caps (47µF/63V ×2, C3274436), CIN ceramic bypass (1µF/100V X7R, C13832), COUT bulk cap (220µF/35V, C5243827) and its ceramic bypass (1µF/50V X7R, C28323). Without these the sheet has the feedback/switching side wired but no bulk input/output capacitance — expected mid-capture, flagging so it isn't mistaken for finished.

**Wiring**: `VIN_BUCK_A` → U8 Vin, switch node = `SW_A` → L3 → `3V3_A` output; D5 catch diode on `SW_A`↔`GND_A`; FB divider `3V3_A` → R25 → `FB_A` → U8 FB, R24 → `GND_A`. Vout = 1.23×(1+2000/1200) = **3.28V**.

---

## Power Side B — `power_side_b.kicad_sch`

**Purpose**: Domain B (coil-drive) buck regulator, V_COIL_IN → COIL_9V.

**Parts placed today**:

| Ref | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| U10 | LM2596S-ADJ | Buck regulator | C963385 | ✅ same line as U8 |
| L4 | **68µH, SXN SMDRI127-680MT** | Buck inductor | C9907 | ✅ same part as L3 — see L3's line for full sourcing detail. Same pending edit: currently placed at 15µH, needs correcting. |
| D6 | SS34, MDD | Catch diode | C8678 | ✅ same part as D5; also the flyback-diode candidate if ORC ever needs to supply one for the relay board (see harness section below) |
| R26 | 7.5kΩ ±1% 0603, UNI-ROYAL | FB divider, top | C23234 | ✅ Basic |
| R27 | 1.2kΩ ±1% 0603, UNI-ROYAL | FB divider, bottom | C22765 | ✅ same part as R24 |

**Not yet migrated**: same gap as Power Side A — CIN/COUT bulk caps + bypass, plus the feedforward cap (1000pF/1nF C0G 0805, Fenghua 0805CG102J500NT, C29925 — TI Table 9-6 lists this at the 9V row despite the >10V prose rule).

**Wiring**: `V_COIL_IN` → U10 Vin, switch node `SW_B` → L4 → `COIL_9V`; D6 on `SW_B`↔`GND_B`; FB divider `COIL_9V` → R26 → `FB_B` → U10 FB, R27 → `GND_B`. Vout = 1.23×(1+7500/1200) = **8.92V**.

---

## MCU — `mcu.kicad_sch`

**Purpose**: ESP32-S3 core, plus reset/boot buttons.

**Parts placed today**:

| Ref | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| U7 | ESP32-S3-WROOM-1**U**-N8 | MCU, external antenna | — | 🔧 schematic symbol's assigned footprint is the generic non-U (onboard PCB antenna) variant — needs the real -1U footprint (external antenna; the U.FL launch area needs its own keepout, checked against the module datasheet's Fig. 10) before layout. Also re-confirm -N8 = 8MB flash/no PSRAM against Espressif's literal ordering table (not yet re-confirmed). |
| C14 | 10µF/25V, Samsung | 3.3V entrance decoupling | C96446 | ✅ JLC Basic |
| C15 | 10µF/25V, Samsung | EN RC cap (or entrance dup — verify which) | C96446 | ✅ same part as C14 |
| C16 | 0.1µF/50V, Samsung | 3V3-pin decoupling | C14663 | ✅ JLC Basic |
| R15 | 10kΩ ±1% 0603, UNI-ROYAL | strap/EN pull (verify which — 3 identical parts placed, function not distinguished in the pulled data) | C25804 | ⚠️ part confirmed, exact net assignment not individually re-verified |
| R16 | 10kΩ ±1% 0603, UNI-ROYAL | strap/EN pull | C25804 | ⚠️ same caveat |
| R17 | 10kΩ ±1% 0603, UNI-ROYAL | strap/EN pull | C25804 | ⚠️ same caveat |
| SW1 | XKB Connection TS-1187A-B-A-B (tactile switch) | **Confirmed 2026-08-01**: EN/reset button — momentary, pulls EN low through this switch to GND | C318884 | ✅ sourced live 2026-08-01: SMD-4P 5.1×5.1mm, 12V/50mA rating (plenty for logic-level use), **-30 to +85°C** (comfortably covers this enclosure's 65-85°C ambient — no thin-margin concern like the PCA9555's), 100k-cycle life, 1,068,940 in stock. Tier badge didn't render to fetch; stock depth strongly suggests Basic, not independently confirmed. |
| SW2 | Same part, C318884 | **New, not yet placed** — GPIO0/BOOT button, same electrical role as SW1 (momentary to GND). EN alone can't select boot mode; without this there's no way to force download/bootloader mode without desoldering the sealed enclosure if the native-USB auto-reset path ever fails. GPIO0_A's pull-up already exists (one of R15/R16/R17) — SW2 just needs to land on that same net → GND. | C318884 | 🔧 needs to be added to the schematic — part already sourced (same as SW1), just not drawn yet |

**Nets present**: `EN_A`, `GPIO0_A`, `GPIO46_A`, `SCL_A`, `SDA_A`, `UART_RX_A`, `UART_TX_A`, `USB_N`, `USB_P` (renamed from `USB_DM`/`USB_DP` — same signals). Plus a bare `GND` power symbol near SW1 — inconsistent with every other Domain A ground reference (`GND_A`), see Housekeeping above.

**Open items**:
- R15/R16/R17's exact roles (EN pull-up / GPIO0 pull-up / GPIO46 pull-down) not individually confirmed from position alone.
- 28-GPIO no-connect marking carried from legacy root (pins 4,5,6,7,8,9,10,11,15,18,19,20,21,22,23,24,25,26,28,29,30,31,32,33,34,35,38,39, all reversible) hasn't been re-verified as present on this sheet's U7 instance.

---

## Communications — `communications.kicad_sch`

**Purpose**: USB-C connector, CAN transceiver, CAN termination, and USB ESD protection — combines the old ingress and CAN/ESD blocks into one sheet.

**Parts placed today**:

| Ref | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| J4 | USB-C receptacle, Hroparts TYPE-C-31-M-12 | USB-C VBUS/data ingress | C165948 | ⚠️ Basic/Extended tier not confirmed live |
| R11 | 5.1kΩ ±0.1%, 0402, YAGEO RT0402BRD075K1L | **USB-C CC1 Rd pull-down — closes a previously-flagged open item** (USB-C spec requires this for the port to be recognized by a host at all) | C852856 | ✅ Extended, ±25ppm/℃ thin film, 72k stock |
| R12 | 5.1kΩ ±0.1%, 0402, YAGEO RT0402BRD075K1L | USB-C CC2 Rd pull-down (pair with R11) | C852856 | ✅ same part as R11 |
| J5 | 01x02 header | Unclear function — doesn't match any legacy-root part at this position | — | ❌ needs a label/purpose check before sourcing |
| J6 | 5.08mm screw terminal, 4-pos, DORABO DB128L-5.08-4P-GN-S | DC + CAN field terminal | C2827883 | ✅ Extended, 16A/300V, 12-22 AWG, 28.9k stock. The root's "C2827883" number is confirmed correct after a Gate 1 pass |
| U6 | SN65HVD230 | CAN transceiver, 3.3V native | C12084 | ⚠️ Basic/Extended tier not independently confirmed live. ±16kV HBM ESD on bus pins built in — no external CAN ESD part needed |
| U5 | USBLC6-2SC6 | USB D+/D- ESD array | C2827654 | ⚠️ SOT-23-6, Extended tier verified live. Check series-vs-parallel insertion (legacy root wired it as a parallel stub, not true series — worth doing properly on this fresh capture) |
| C13 | 100nF | U6 VCC bypass, at the pin | — | ⚠️ generic |
| R13 | 10kΩ ±0.1% | Unclear function — doesn't match any known legacy-block role; nothing else on this design uses 0.1% except R11/R12 (which are 5.1k, not 10k) | — | ❌ flag for a wiring check, don't assume it's a stray |
| R14 | 120Ω ±1% 1206, UNI-ROYAL | CAN bus termination | C17909 | ✅ Basic |

**Nets present**: `CAN_H`, `CAN_L`, `UART_RX_A`, `UART_TX_A`, `USB_N`, `USB_P`.

**Not yet migrated from legacy root**: the reverse-polarity/source-select stage (Domain A power ingress — reverse-polarity FET, USB-presence-detect gate drive, PTC fuse) doesn't appear on Communications, Power Side A, or any other new sheet yet — still only in root. Given J4 already lives here, this stage likely belongs on Communications too, but that's an open placement call. **This stage is also known-electrically-incomplete even in legacy root** — a single NPN pulling toward GND can't turn a high-side P-FET off; the gate-drive chain (root's Q4/Q5-equivalent) likely needs a second stage properly derived before it's worth recapturing as-is.

Also not yet migrated: the CAN termination jumper (2.54mm header+shunt, C36717/C5305, field-removable) — confirm whether R14's termination is now hardwired or the jumper just isn't placed yet.

---

## I2C Isolator — `i2c_isolator.kicad_sch`

**Purpose**: the entire Domain A↔B galvanic isolation barrier.

**Parts placed today**:

| Ref | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| U9 | ADuM1250ARZ-RL7 | 2-channel isolated I2C buffer — the entire A↔B barrier | C13839 | ✅ part confirmed; isolation-voltage/data-rate figures not re-verified against ADI's Rev. L table |
| C17 | 100nF | Primary-side decoupling | — | ⚠️ generic 0603 |
| C18 | 100nF | Secondary-side decoupling | — | ⚠️ generic 0603 |
| R20 | 10kΩ ±1% 0603, UNI-ROYAL | SDA_A pull-up | C25804 | ✅ |
| R21 | 10kΩ ±1% 0603, UNI-ROYAL | SCL_A pull-up | C25804 | ✅ |
| R22 | 10kΩ ±1% 0603, UNI-ROYAL | SDA_B pull-up | C25804 | ✅ |
| R23 | 10kΩ ±1% 0603, UNI-ROYAL | SCL_B pull-up | C25804 | ✅ |

**Nets present**: `SCL_A`, `SCL_B`, `SDA_A`, `SDA_B`, plus power symbols `+3V3_A`/`GND_A` (primary side) and `+3V3_B`/`GND_B` (secondary side).

**Not yet migrated**: nothing obvious — this reads as a complete, self-contained migration of the old barrier block. Worth a `run_erc` pass specifically on this sheet to confirm.

**Nothing else crosses the barrier, deliberately** — if there's ever a temptation to add a second isolated signal (e.g. a fault/interrupt line), that's a topology decision to flag, not just wire. The design polls status over the existing I2C link instead.

---

## Not yet captured into any sheet — still legacy-root-only

Whoever captures these next should give them their own sheet(s), following the same one-function-per-sheet pattern above. No names are reserved — that's an open call for whoever does the capture.

### Domain B power ingress

| Ref (legacy) | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| F2 | 0.75A PTC, Littelfuse-class | Domain B input resettable fuse | C207083 | ⚠️ **check 85°C-derated hold current against the actual load before trusting this as protection** — a prior PTC selection pass on this exact project already caught one part (2920L075/60) that derates to 0.34A at 85°C, below its intended load. Confirm this part's derating table directly. |
| F1 | 0.75A PTC, Littelfuse-class | Domain A input resettable fuse | C207083 | ⚠️ same part/caveat as F2 |
| Q2 | **DMP4015SK3Q-13** (Diodes Inc.) | Domain B reverse-polarity FET — replaces an undersized SOT-23 AO3401A placeholder | C461089 | ⚠️ sourced live 2026-08-01: TO-252 (DPAK), P-ch, Vds -40V (25% margin over 32V worst-case input), Id -35A (50× the ~0.7A load), Rds(on) 7mΩ@10V/9mΩ@4.5V, **AEC-Q101 qualified**, 1,544 in stock. Clears every requirement with large margin — spec confidence High. **Tier not confirmed**: stock depth/price suggest Extended, not independently confirmed. **This is a package change from the placeholder (TO-252 vs SOT-23), not just a value swap — needs a new footprint, not a property edit.** |

### Domain B logic supply

| Ref (legacy) | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| U6 | AMS1117-3.3 | Domain B logic LDO, COIL_9V → 3.3V | — | ❌ never individually sourced — needs its own Gate 1 pass (package: SOT-223) |
| C14/C15 (legacy refs) | 10µF ×2 | LDO in/out | — | ⚠️ generic |

### PCA9555 I2C GPIO expander

| Ref (legacy) | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| U5 (legacy ref) | PCA9555PW,118 (NXP) | I2C GPIO expander, addr 0x20, 10-of-16 I/O used | C128392 | ⚠️ re-pulled live 2026-08-01, 8,773 in stock, TSSOP-24. Basic/Extended tier not confirmed (badge didn't render to automated fetch). **Temp margin thin**: datasheet rated −40 to +85°C against this enclosure's 65–85°C ambient — near-zero headroom for self-heating above bulk ambient, same class of concern as the PTC 85°C-derating finding. Address 0x20 is set by A0–A2 board strapping, not part-number-specific. |
| C16 (legacy ref) | 0.1µF | Decoupling | — | ⚠️ generic |

### Per-channel coil driver ×10 (channels 1–10)

Identical stage repeated for each relay channel: `RBn` (PCA9555 output → NPN base) → `QNn` (level-shift NPN) → `RPn` (gate pull-up) → `QPn` (high-side coil switch) → harness pin `n+2`.

| Ref | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| RB1–RB10 | 10kΩ ×10 | PCA9555 output → NPN base | — | ⚠️ generic 0603 |
| QN1–QN10 | **MMBT3904** (JSCJ) | Level-shift NPN | C20526 | ✅ **decided 2026-08-01** — Basic-tier preference over the literal MMBT2222A (which didn't appear on JLCPCB's Basic-parts lists). 253,450 in stock. Functionally interchangeable for this role (3.3V I2C-expander output → 10k base, no high-current/high-freq need). |
| RP1–RP10 | 10kΩ ×10 | Gate pull-up to V_COIL_IN | — | ⚠️ generic 0603 |
| QP1–QP10 | AO3401A ×10 | High-side coil switch | C15127 | ✅ re-verified live 2026-08-01: 186,375 in stock, $0.10/pc single-unit. **Tier caveat applies to every C15127 line in this doc** (this bank plus the Domain A reverse-polarity/source-select FETs once migrated): LCSC's page shows no Basic/Extended badge, JLCPCB's own page didn't render one either — circumstantially Basic per third-party list cross-check, not a direct-page confirmation. |

**Open, design-level item**: flyback diode presence on the *relay board itself* is still unconfirmed — needs bench inspection or a continuity/diode check across a coil's harness pins. If ORC needs to supply flyback diodes per channel, reuse SS34 (C8678, already qualified, same part as D5/D6) — 40V/3A is far more than the ~9V/45mA-per-coil duty needs.

### Harness connector

| Ref | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| J2/J3 (legacy refs) | ZHOURI 2.54mm 1×40 breakable strip (snap to 14 pos) | Harness to relay board | C2977586 | ✅ pitch/pin-count/pinout confirmed and locked, per design-inputs.md |

**Pinout** (14 positions, 0.1" pitch): pin 1 = `GND_B` (coil common return), pin 2 = `VEH_RAIL_B_IN` (A+, harness-carried, → F2 → `VEH_RAIL_B`), pins 3–12 = `COIL1`–`COIL10`, pins 13/14 = `GND_B` (chassis side, tied to the same net as pin 1 per design-inputs.md's explicit call).

### Power flags (schematic-only, not orderable)

PF1, PF3, PF5, PF6, PF7, PF8 — `power:PWR_FLAG`, 6×, ERC bookkeeping only. Each instance's Value field was renamed (PWR_FLAG1..PWR_FLAG8) to prevent them silently sharing one net by symbol identity — see kicad-mcp-logbook.md's 2026-08-01 entries if the reason isn't obvious from context.

---

## Section 🔧 — needs real KiCad symbol/footprint work, not a sourcing gap

- **U1/U7 — ESP32-S3-WROOM-1U footprint.** See MCU sheet above.
- **SW2 — GPIO0/BOOT button.** Not a footprint issue, a placement issue: part is sourced (XKB TS-1187A-B-A-B, C318884, same as SW1), just needs to actually be drawn on the MCU sheet and wired to `GPIO0_A` ↔ GND.
- **J5/J6 — DORABO DB128L-5.08-4P-GN-S, LCSC C2827883.** 5.08mm screw terminal, 4-pos, mechanically specific (chosen after two push-in-spring candidates failed the wire-entry/actuator requirement). Verified live (Extended, 16A/300V, 12-22 AWG). Verify KiCad's connector footprint libraries have a matching 5.08mm 4-pos footprint, or build one from DORABO's datasheet drawing.
- **F1, F2 — PTC fuse footprint.** Confirm the placed footprint actually matches C207083's real SMD 2-pad package, not a generic fuse symbol's default footprint.
- **Q2 — Domain B reverse-polarity FET footprint.** TO-252/DPAK, not the SOT-23 the placeholder used — real footprint swap, not just a value edit.
- **All ~35 generic-value passives** (10k resistors, 0.1µF/1µF ceramic caps for decoupling/bias/pull-ups) use KiCad's standard `Device:R`/`Device:C` symbols with standard footprints — no custom KiCad work, just a Gate 1 distributor pull once ready to lock values.

## Quick net-name index (legacy root — not yet re-verified against the new sheets)

Carried forward for whoever migrates the remaining blocks; net names inside the 5 sheets above have already been re-pulled live and may not match these 1:1 (e.g. `USB_DM`/`USB_DP` became `USB_N`/`USB_P` during migration):

`VEH_RAIL_A`, `VEH_RAIL_A_IN`, `V_PROT_A`, `Q1_GATE`, `Q3_GATE`, `Q4_BASE`, `Q4_COL`, `Q5_BASE`, `VIN_BUCK_A`, `VEH_RAIL_B`, `VEH_RAIL_B_IN`, `Q2_GATE`, `V_COIL_IN`, `GPIO_CH1`–`GPIO_CH10`, `BASE1`–`BASE10`, `GATE1`–`GATE10`, `COIL1`–`COIL10`.
