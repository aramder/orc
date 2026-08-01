# ORC hardware BOM — generated from schematic, NOT gate-cleared

**Generated from `orc.kicad_sch` via `kicad-cli sch export bom`.** This reflects what's on the schematic today, not a locked procurement list. Per [docs/hardware-workflow.md](../docs/hardware-workflow.md) Gate 1, nothing here should be ordered until every line has a distributor part number pulled from a **live** catalog page — most lines below have not had that treatment. Status column is honest about which.

## Status legend

| Symbol | Meaning |
|---|---|
| ✅ | LCSC/distributor part number confirmed in [circuit-draft.md](../docs/circuit-draft.md)'s sourcing pass |
| ⚠️ | Real part, but not re-verified against a live catalog page this pass, or tier/spec still flagged open |
| ❌ | **Placeholder** — generic KiCad symbol standing in for an unselected/unsourced part; do not order against this line |

## Domain A — MCU / USB (isolated ground)

| Ref | Qty | Part | Function | LCSC | Status |
|---|---|---|---|---|---|
| U1 | 1 | ESP32-S3-WROOM-1 (symbol; **actual part is -1U, ext. antenna**) | MCU | — | ⚠️ symbol is pin-compatible generic -1, swap footprint to -1U before layout |
| J1 | 1 | USB_C_Receptacle_USB2.0_16P (symbol) — real part **Hroparts TYPE-C-31-M-12** | USB-C connector | — | ⚠️ Basic/Extended tier not confirmed live |
| J5 | 1 | DORABO DB128L-5.08-4P-GN-S | DC+CAN terminal, 4-pos right-angle screw | C2827883 | ✅ confirmed Extended, right-angle wire entry + top-accessible screws verified against datasheet drawing |
| Q1 | 1 | AO3401A | Reverse-polarity FET, DC terminal input | C15127 | ✅ (same LCSC line as #5a) |
| R1 | 1 | 10k | Q1 gate bias | — | ⚠️ generic 0603, no LCSC pulled |
| U3 | 1 | LM2596S-ADJ | Domain A buck, →3.3V | C963385 | ✅ |
| L1 | 1 | 33uH | U3 buck inductor | — | ❌ value from TI datasheet selection table, not sourced |
| D3 | 1 | SS34 (catch diode) | U3 catch diode | — | ❌ generic `Device:D`, not a sourced Schottky part |
| C1 | 1 | 100uF | U3 Vin bulk | — | ⚠️ generic electrolytic |
| C2 | 1 | 220uF | U3 Vout bulk | — | ⚠️ generic electrolytic |
| C3 | 1 | 0.1uF | U3 Vout ceramic | — | ⚠️ generic 0603 |
| R2 | 1 | 1.69k (E96) | U3 FB divider, top | — | ❌ computed from TI Vref=1.23V formula, not sourced |
| R3 | 1 | 1.00k | U3 FB divider, bottom | — | ❌ same as R2 |
| Q3 | 1 | AO3401A | **Source-select FET** — disconnects DC-terminal path when USB VBUS present (active, not diode-OR) | C15127 | ⚠️ part reused from #5a; the *circuit* around it (see Q4/R15) is not verified |
| R14 | 1 | 10k | Q3 gate pull-down (default state: DC path ON) | — | ⚠️ generic |
| Q4 | 1 | MMBT2222A | USB-presence-detect NPN, drives Q3's gate | — | ❌ **circuit-draft.md open item — "values not yet worked out."** Also: as placed, a single NPN pulling toward GND cannot pull a high-side P-FET gate up to source potential to turn it off — this topology likely needs a second stage. Flagged, not solved. |
| R15 | 1 | 10k | Q4 base resistor from VIN_BUCK_A | — | ❌ same open item as Q4 |
| U7 | 1 | SN65HVD230 | CAN transceiver, 3.3V native | C12084 | ⚠️ Basic/Extended tier not independently confirmed live |
| C17 | 1 | 100nF | U7 VCC bypass | — | ⚠️ generic |
| R16 | 1 | 120Ω, UNI-ROYAL 0805W8F1200T5E | CAN termination resistor | C17437 | ✅ **Basic, high confidence** (multiple independently-compiled JLCPCB Basic lists) |
| J6 | 1 | 2.54mm 1×2 header + shunt | CAN termination jumper (field-removable) | C36717 (header) / C5305 (shunt) | ⚠️ tier not cleanly re-verified; jumper-vs-solder-jumper mechanism not locked |
| U8 | 1 | USBLC6-2SC6 | USB D+/D- ESD protection | C7519 | ⚠️ part-selection high confidence, tier unconfirmed |
| D6 | 1 | Device:D_TVS_Dual_AAC (symbol) — real part **Nexperia PESD1CAN,215** | CAN_H/CAN_L ESD/TVS | C15771 | ❌ **no PESD1CAN symbol in this KiCad lib** — generic dual-TVS standing in; part-selection confidence high, tier unconfirmed |
| C4–C7 | 4 | 10uF/1uF/0.1uF/0.1uF | ESP32 decoupling (datasheet-specified values) | — | ⚠️ generic, values per Espressif datasheet |
| R4, C8 | 2 | 10k / 1uF | EN RC | — | ⚠️ generic |
| R5, R6 | 2 | 10k / 10k | GPIO0 pull-up / GPIO46 pull-down (boot straps) | — | ⚠️ generic |

**Removed this pass:** the old diode-OR (D1, D2) and the plain 2-pin vehicle-tap connector (J3) are gone — superseded by the active source-select FET (Q3) and the combined DC+CAN terminal (J5), per circuit-draft.md's "source arbitration" and "Domain A power entry" decisions.

## Barrier — ADuM1250 isolated I2C

| Ref | Qty | Part | Function | LCSC | Status |
|---|---|---|---|---|---|
| U2 | 1 | ADuM1250ARZ-RL7 | Isolated I2C buffer | C13839 | ✅ part number confirmed; isolation-voltage/data-rate figures **not** re-verified against ADI Rev. L table |
| C9, C10 | 2 | 0.1uF | Decoupling, each side | — | ⚠️ generic 0603 |
| R7–R10 | 4 | 10k | I2C pull-ups, each side/line | — | ⚠️ generic 0603 |

## Domain B — coil-drive (chassis ground)

| Ref | Qty | Part | Function | LCSC | Status |
|---|---|---|---|---|---|
| D4 | 1 | SMBJ26CA | Load-dump TVS, 12V input | C135063 | ⚠️ **interim only** — 600W/10×1000µs generic surge rating, not a confirmed ISO 7637-2 pulse-5 load-dump rating |
| Q2 | 1 | AO3401A | Reverse-polarity FET, Domain B | C15127 | ❌ **circuit-draft.md open item 5b** — undersized for sustained full-coil current; needs a DPAK/SO-8-class automotive P-ch part |
| R11 | 1 | 10k | Q2 gate bias | — | ⚠️ generic 0603 |
| U4 | 1 | LM2596S-ADJ | Coil buck, →9V (≤32V in) | C963385 | ✅ same part as U3; locked in per circuit-draft.md, one fewer distinct BOM line |
| L2 | 1 | 47uH | U4 buck inductor | — | ❌ not sourced |
| D5 | 1 | SS34 (catch diode) | U4 catch diode | — | ❌ generic, not sourced |
| C11 | 1 | 100uF | U4 Vin bulk | — | ⚠️ generic electrolytic |
| C12 | 1 | 220uF | U4 Vout bulk | — | ⚠️ generic electrolytic |
| C13 | 1 | 0.1uF | U4 Vout ceramic | — | ⚠️ generic 0603 |
| R12 | 1 | 6.34k (E96) | U4 FB divider, top | — | ❌ computed, not sourced |
| R13 | 1 | 1.00k | U4 FB divider, bottom | — | ❌ computed, not sourced |
| U6 | 1 | AMS1117-3.3 | Domain B logic LDO, fed from COIL_9V | — | ❌ not in circuit-draft.md at all — needs its own sourcing line |
| C14, C15 | 2 | 10uF | U6 LDO in/out | — | ⚠️ generic |
| U5 | 1 | PCA9555PW | I2C GPIO expander, addr 0x20 | — | ❌ circuit-draft.md flags prior LCSC pull (`C9900150829`) as unverified/malformed — **re-pull before ordering** |
| C16 | 1 | 0.1uF | U5 decoupling | — | ⚠️ generic |

## Per-channel coil driver (×10, channels 1–10)

| Ref | Qty | Part | Function | LCSC | Status |
|---|---|---|---|---|---|
| RB1–RB10 | 10 | 10k | PCA9555 output → NPN base | — | ⚠️ generic 0603 |
| QN1–QN10 | 10 | MMBT2222A | Level-shift NPN | — | ❌ "not yet verified on live catalog" |
| RP1–RP10 | 10 | 10k | Gate pull-up to V_COIL_IN | — | ⚠️ generic 0603 |
| QP1–QP10 | 10 | AO3401A | High-side coil switch | C15127 | ✅ same LCSC line as Q1/#5a — separate BOM qty, don't merge |
| J2 | 1 | 0.1" (2.54mm) unshrouded vertical THT male header, ZHOURI 2.54-1×40 (breakable strip, snap to 14 pos) | Harness to relay board — **pinout now measured and locked**: pin1 coil common (→GND_B), pin2 A+ (→VEH_RAIL_B), pins3–12 Coil1+…10+, pins13–14 chassis (→GND_B) | C2977586 | ✅ pitch/pin-count/pinout all confirmed this pass — **the project's former gating item is resolved** |

## Power flags (schematic-only, not orderable)

PF1, PF3, PF5, PF6, PF7, PF8 — `power:PWR_FLAG`, 6× — ERC bookkeeping only, no physical part.

## Summary

- **~65 unique reference designators, ~115 placed components.**
- **Confirmed-sourced (✅): 8 lines** — Q1/Q3/QP1–10 (AO3401A, C15127), U2 (C13839), U3/U4 (C963385), J2 (C2977586, harness — newly resolved), J5 (C2827883, DC+CAN terminal), R16 (C17437, CAN termination resistor).
- **Needs live re-verification (⚠️): ~20 lines** — generic passives plus J1, D4, U7, J6, U8 which have sourcing-pass context but unconfirmed Basic/Extended tier.
- **Placeholder, don't order (❌): ~15 lines** — both FB dividers, all catch diodes, U6 (never scoped before this pass), U5's stale LCSC number, Q2 (known-undersized), D6 (no real PESD1CAN symbol in this library), and the Q4/R15 source-select gate-drive stage (circuit-draft.md's own open item — flagged here as likely electrically incomplete, not just unsourced).

**What changed this pass:** harness header resolved (J2 now real part + real pinout, no longer the gating item); D1/D2 diode-OR removed and replaced by Q3 active source-select per the rejected-diode-OR analysis; J3 removed, replaced by J5 (combined DC+CAN terminal, since A+ can no longer share the harness tap without defeating the ADuM1250 isolation barrier); CAN subsystem added (U7 transceiver, R16/J6 termination, U8/D6 ESD) — all provisional per circuit-draft.md's own "descope if it doesn't fit" caveat.

**Bottom line for Gate 1**: still don't lock this BOM. The ❌ lines are this session's live copy of circuit-draft.md's "Open items before schematic capture" checklist, mapped onto actual schematic reference designators.
