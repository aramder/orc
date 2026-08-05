# ORC subcircuit capture guide

Single source of truth for parts, LCSC numbers, and wiring — organized by the **actual KiCad sheet hierarchy** in `hardware/*.kicad_sch`. `hardware/BOM.md` was retired 2026-08-01 (redundant, drifted out of sync with the new sheets' ref numbering) — everything from it that was still current lives here now.

Five sheets exist today: Power Side A, Power Side B, MCU, Communications, I2C Isolator. Same one-sheet-per-function pattern continues for whatever gets captured next. **Root `orc.kicad_sch` is legacy** — its content is being superseded sheet-by-sheet, not maintained in parallel. Where a sheet below is missing parts a legacy-root block already had, that's flagged explicitly as **not yet migrated**, not a design change, unless stated otherwise.

**Ref designators were reassigned during the hierarchical split** — don't cross-reference old block-numbered refs (U1–U8, Q1–Q5, R1–R18 from the pre-split BOM) against these; use the ref shown in each sheet's table, which matches what's actually in the file today.

**Status legend** (carried from the old BOM.md): ✅ LCSC confirmed live · ⚠️ real part/value decided, tier or wiring detail still open · ❌ real unresolved gap · 🔧 KiCad-side symbol/footprint work needed, not a sourcing gap.

## OPEN ARCHITECTURE DECISION — MCU module, exact board TBD, don't source against U7/J4/U5/R11/R12 yet

**Mechanical finding, 2026-08-01**: only ~8.5mm clearance between the two stacked PCBs in the enclosure — not enough room for both a USB-C connector and the required DC+CAN screw terminal on a single-sided board.

**Resolved 2026-08-01 — chip family and wireless, both decided**: rather than solve the connector-placement problem, replace the bare ESP32-S3-WROOM-1U + custom USB-C/ESD/CC-pulldown circuit (U7, J4, U5, R11, R12 below) with a **complete pre-made ESP32-C3 module** ("ESP32-C3 Super Mini" class, USB-C already built in, ~$2-3/ea in bulk). Two decisions landed together:
- **Wireless (WiFi/BLE) dropped entirely** — CAN is now the sole control path, no config/monitoring path over WiFi. This retires design-inputs.md's external-antenna requirement outright: radio is never enabled in firmware, so a sealed metal enclosure blocking a PCB-trace antenna is no longer a real constraint. The plain (non-"Plus") C3 Super Mini's PCB-only antenna is fine now.
- **Chip family: ESP32-C3, not S3** — single-core RISC-V confirmed adequate. This board's whole job is CAN in (via the already-sourced SN65HVD230 transceiver, UART-framed) and driving the PCA9555 I2C GPIO expander out (through the ADuM1250 isolator) — light duty, doesn't need dual-core Xtensa or PSRAM.

**Board specifics, 2026-08-01** — sourcing pass complete, not yet applied to the schematic:

- **Chip**: bare **ESP32-C3FN4** die (RISC-V, 160MHz, 4MB flash, 400KB SRAM), not Espressif's official castellated ESP32-C3-MINI-1 module — a generic/widely-cloned board from many uncredited sellers, no single manufacturer. **QC flag**: some clone batches reportedly carry chips marked only "ESP32-C3" with no flash-size suffix that don't match the board's layout — worth a visual chip-marking check on receipt.
- **Pricing**: single-unit ~$3.68–3.82 (AliExpress), eBay 3-pack ~$2.33/unit. The "$2-3/ea bulk" figure is plausible and directionally consistent but **not confirmed at a specific quantity** — AliExpress blocks automated price-tier scraping (same JS-rendering limit as LCSC). Needs a manual browser check before locking into a BOM quantity/price.
- **Not on LCSC/JLCPCB** — confirmed this stays a separate hand-assembly sourcing line, not foldable into the main PCBA order. (LCSC does carry Espressif's *official* bare ESP32-C3-MINI-1-N4 module, C2838502, ~$2.12-2.47 — but that's a different part: no USB, no regulator, no buttons, meant for SMT reflow into a custom design, not a substitute for the dev-board module here.)
- **Form factor**: 22.5×18mm, 2×7-pin 0.1"/2.54mm through-hole headers (16 pins, 13 usable GPIO) — not castellated, needs pin headers/sockets or hand soldering, not reflow.
- **Buttons**: RST and BOOT both confirmed present onboard. **SW1/SW2 are now redundant** — don't build them into the schematic; just make sure the module's mounted so both buttons stay physically reachable.
- **Pinout**: I2C on GPIO8(SDA)/GPIO9(SCL), UART on GPIO21(TX)/GPIO20(RX), all broken to headers and otherwise unused. **GPIO9-as-BOOT-strap question — resolved 2026-08-01, safe, datasheet-cited, static analysis only (no hardware to confirm on yet)**: an idle-high I2C bus on GPIO9 does not conflict with the ROM boot-mode strap read. Per Espressif's *ESP32-C3 Series Datasheet* v2.4 §3 (Table 3-1/3-2/3-3, Figure 3-1), the strap read is a one-shot latch completing within a fixed **t_H ≥ 3 ms** window anchored to `CHIP_EN` release — before firmware runs and before the IO MUX/GPIO Matrix has routed the I2C peripheral onto GPIO8/9 at all (Table 2-1: GPIO9's at-reset/after-reset state is plain `IE, WPU`, not an I2C pin, until application code configures it, §4.1.3.1). GPIO9's documented default is a weak pull-up (bit=1=SPI boot, Table 3-1); an external I2C pull-up reinforces that same direction rather than fighting it — the only way to disturb the strap is to actively drive GPIO9 **low** during the ~3 ms window, which ordinary I2C (SCL pulled low only during firmware-initiated clock pulses, well after the strap read completes) cannot do. No published ESP32-C3 errata mention GPIO8/GPIO9/strapping/I2C. GPIO8 (SDA) is also a strapping pin (chip-boot-mode + UART-print-control) and is safe by the same timing argument, but defaults to **floating**, not pulled — the one real caveat carried forward: if some other I2C slave on this bus holds SDA low during its own power-on reset at the same instant as the ESP32-C3's `CHIP_EN` release, that could affect the GPIO8 strap read (only matters if GPIO9 also reads 0 at that instant, Table 3-3) — a general I2C power-sequencing hygiene point, not a reason to reassign pins. Full analysis and citations: `firmware/README.md`. Empirical confirmation on real hardware (via `firmware/src/i2c_scanner`) is still the right final check once a board exists — this closes the "needs confirming" static-analysis gap, not the "needs confirming on real hardware" one.
- GPIO8 driving the onboard LED on some clones (noted below) is a separate, cosmetic concern (an LED toggling as a side effect of I2C traffic) — not a strapping conflict, and doesn't block using GPIO8 as SDA.
- **USB**: USB-C confirmed. Native USB-Serial-JTAG (no CH340/manual BOOT-sequence needed) is likely but **one source claimed CH340** instead — varies by seller/revision, treat as unconfirmed until a specific listing's schematic is checked.

**Still open before this is schematic-ready**: pick and buy a specific listing (confirming real bulk pricing and the flash-marking QC risk directly), empirically confirm GPIO8/9 I2C-vs-strapping behavior on real hardware once it arrives (static/datasheet analysis is now done and says safe — see above; this remaining item is the bench-verification step, not an open question about the design), confirm native-USB vs. CH340 for that specific listing. **Don't source or lock U7/J4/U5/R11/R12 further, and don't remove SW1/SW2 from the schematic, until a specific listing is actually bought and confirmed** — this research covers the "Super Mini" class generically, not one verified purchasable SKU.

**New GPIO reservation, 2026-08-02 (design-only, see circuit-draft.md's "Node-ID address input" section)**: 4 of the 13 usable header GPIO are earmarked for a 4-position node-ID DIP switch, read directly by the ESP32 rather than through the PCA9555 — Domain B (and the PCA9555 on it) only has power when harness A+ is present, so a PCA9555-hosted switch would be unreadable during USB-only bench operation. Budget: 13 usable − SDA/SCL (2) − TWAI TX/RX (2, once the CAN firmware swap lands) − 4 node-ID = **5 still free**. Silkscreen labels each position by decimal weight (1/2/4/8), not bit index — sum the ON positions for the node ID. Specific pin numbers not yet picked — deferred to the same "buy a specific listing" step above, since the exact broken-out pinout isn't nailed down until then. DIP switch part itself also not yet sourced (Gate 1 pending).

<details><summary>Superseded S3-module candidate research (2026-08-01, before the wireless/chip-family decision) — kept for record, not actionable</summary>

| Candidate | Antenna | USB | Flash/PSRAM | Size/mount | Buttons | Confidence |
|---|---|---|---|---|---|---|
| Unexpected Maker TinyS3 | ✅ Confirmed — u.FL on product page | USB-C, native | 8MB / 8MB PSRAM | 35×17.8mm, 4.3mm max thickness, pin-header pads | RESET + BOOT confirmed | High — cleared every requirement under the old (S3, external-antenna) constraints |
| Olimex ESP32-S3-DevKit-Lipo-EA | ✅ Confirmed on manufacturer page | USB-C ×2 | 8MB / 8MB | 27.94×55.88mm | RESET + BOOT/USER | Medium-High |
| Adafruit ESP32-S3 Feather 8MB w/ w.FL Antenna | ⚠️ w.FL/MHF3, not u.FL — pigtail mismatch | USB-C, native | 8MB / no PSRAM | Feather ~51×23mm | RESET + BOOT | Out of stock at Adafruit |
| Espressif ESP32-S3-DevKitC-1U-N8R8 | ✅ Best-documented | ❌ Micro-USB, disqualifying | 8MB / 8MB | 70×28mm THT header | RESET + BOOT | Disqualified on USB connector |

Superseded because the S3-and-external-antenna requirements that drove this table no longer apply.
</details>

**Side effect, unchanged from before**: dropping the custom USB-C ingress removes the VBUS-vs-DC-terminal source-select stage (Q1/Q3/Q4/Q5, still legacy-root-only) entirely — including Q4/Q5's gate-drive chain, which has been an unresolved electrical gap since early in the project. Domain A power ingress simplifies to just the DC terminal path, same shape as Domain B. SW1/SW2 (EN/BOOT buttons) likely become redundant too — C3 Super Mini boards typically have their own onboard RST/BOOT buttons, pending confirmation from the in-progress sourcing pass.

## Housekeeping — found in the 2026-08-01 documentation audit, not yet resolved

- **Catch diode value — resolved 2026-08-01: SS34 (C8678) is correct.** Root schematic's D2/D3 still show "SS56" (stale, predates the correction) — harmless since root is legacy and not sourced from, but don't let it confuse a future read of root.
- **D1 (SMBJ26CA, load-dump TVS) is still placed in root** even though the design decision to drop it was made and documented — the symbol was never deleted. Low priority since root is legacy, but noted so it isn't mistaken for a live requirement if root gets referenced.
- **Root's `J2` symbol carries "(C2827883)" embedded in its description field.** As of the latest Gate 1 pass this number is confirmed correct: DORABO DB128L-5.08-4P-GN-S, 5.08mm 4-pos screw terminal, C2827883 (Extended, 16A/300V, 28.9k stock). It's now carried in J6's LCSC column in Block 4.
- **Root has a stray `R4 22kΩ`** with no documented function anywhere in current or historical BOM content. Possibly a leftover from the dropped TVS/load-dump analysis network. Needs a function check before it's either sourced or written off as dead.
- **PCB is significantly out of sync with the current schematic** (checked 2026-08-01): the entire Power Side B subcircuit (U10/R26/L4/R27/D6) is placed in the schematic but absent from the PCB; U7 (ESP32) and U9 (ADuM1250) are also missing from the PCB despite their support passives being placed. What *is* on the PCB uses stale ref numbers from before the last schematic re-annotation (`U3`→`U8`, `R18`/`R19`→`R24`/`R25` are the same physical parts). Needs a `pcb_sync_from_schematic` pass once the schematic itself is further along — resyncing now would just need to happen again.
- **Still unresolved from the prior pass**: J5 (Communications, 2-pin header — unclear role), R13 (Communications, 10kΩ 0.1% — unclear role), and a bare `GND` power symbol on the MCU sheet where everywhere else uses `GND_A` specifically. (SW1's function is resolved — see MCU sheet — and it's the same part as the still-to-be-placed SW2/BOOT button.)
- **kicad-mcp-pro `sch_update_properties`/`sch_modify_property` write-guard false positive, found 2026-08-02**: on `power_side_a.kicad_sch`, both tools refused *every* write attempt (even a plain Value-only change) with "the schematic mutation dropped structure (wire 12→10)". Root cause: the sheet has two places where three collinear wire segments meet at a T-junction (a pass-through point plus a stub) — the tool's own serializer merges each collinear pair into one wire on round-trip (12→10, exactly two merges), then its own safety check flags that merge as data loss and aborts the write. Not caused by the edit being attempted; happens on any write to this specific file via these two tools. Worked around by editing the `.kicad_sch` text directly (Read/Edit) instead of the MCP property tools — confirmed safe via `git diff` (only the intended property lines changed) and `sch_get_symbols` (values read back correctly). If this recurs on another sheet, check `sch_get_wires` for collinear T-junctions before assuming the requested edit is the problem.

---

## Power Side A — `power_side_a.kicad_sch`

**Purpose**: Domain A buck regulator, VIN → 3V3_A.

**IC: LMR50410XDBVR, SOT-23-6, LCSC C2841056** — confirmed live via `lib_get_component_details`: Extended tier, 4,643 in stock, $0.43/unit. Synchronous (integrated low-side FET, no external catch diode), fixed 700kHz switching, PFM light-load mode. Picked over the FPWM variant (LMR50410XFDBVR, C5219371) — its stock was checked and found too low for this build. Full sourcing history and the board-space rationale for moving off the original LM2596S-ADJ/TO-263 design: circuit-draft.md, "Buck IC switched to LMR50410."

**KiCad sheet not yet redrawn against this decision** — U8/L3/D5/R24/R25 as currently placed are the retired LM2596S-ADJ design and need replacing with the parts below.

**Parts to place**:

| Ref | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| U8 | LMR50410XDBVR | Buck regulator | C2841056 | ✅ SOT-23-6, 4–36V, 1A, synchronous, confirmed live (Extended, 4,643 stock, $0.43) |
| L3 | **33µH, Chilisin LVS606045-330M-N** | Buck inductor | C285825 | ✅ confirmed live: SMD 6×6mm, 1.4A rated / 2.3A saturation, 165mΩ DCR, Extended, 1,052 stock, $0.069. **Same part reused for L4** (Power Side B) — user's explicit call for BOM line count. Sized against the coil rail's revised 0.7A max expected load (see L4's note); comfortably oversized for this 3.3V rail's own ~0.2-0.5A load. Stepped up from an earlier 4×4mm/840mA-rated pick (SNR4030-330MT, C5127398) once the coil-rail load estimate was revised — that part's margin got too thin at the new number, see L4's note and circuit-draft.md. |
| D5 | — | Catch diode | — | **Removed** — LMR50410 is fully synchronous, no external catch diode needed |
| R24 | 2.7kΩ ±1% 0603, UNI-ROYAL 0603WAF2701T5E | FB divider, bottom (RFBB) | C13167 | ✅ confirmed live: **Basic tier**, 1,404,867 stock, $0.0010. Re-sourced 2026-08-02 (was 43.2kΩ/C23053, Extended) to close every FB resistor at Basic tier — different RFBT/RFBB pair than the original computation, same ~3.3V target. |
| R25 | 6.2kΩ ±1% 0603, UNI-ROYAL 0603WAF6201T5E | FB divider, top (RFBT) | C4260 | ✅ confirmed live: **Basic tier**, 352,385 stock, $0.0010. Re-sourced 2026-08-02 (was 100kΩ/C22936) — no longer shared with R26, each instance now has its own RFBT/RFBB pair chosen from the Basic-tier value set. Vout = 1.00×(1+6.2k/2.7k) = **3.30V** (−0.11%). |
| — | 2.2µF X7R ceramic, ≥50V | CIN bulk | *not yet sourced* | ⚠️ open — value/type from LMR50410 datasheet (§9.2, direct PDF read) |
| — | 0.1µF X7R ceramic | CIN high-freq bypass | *not yet sourced* | ⚠️ open |
| — | Small tantalum or polymer, value TBD | CIN damping cap | *not yet sourced* | ⚠️ open — datasheet flags a long-input-lead resonance risk; this board's automotive harness feed is exactly that case, so a small damping cap stays even though CIN is otherwise all-ceramic. Sized for damping, not bulk storage — far smaller than the electrolytic can this design used to carry. |
| — | 22µF X7R ceramic | COUT | *not yet sourced* | ⚠️ open — single cap per datasheet Table 9-1, no bulk cap needed |
| — | 0.1µF X7R ceramic, ≥16V | CBOOT | *not yet sourced* | ⚠️ open |

**Wiring**: `VIN_BUCK_A` → U8 Vin; CIN (2.2µF + 0.1µF ceramic + damping cap) across Vin↔GND_A at the IC pins; switch node `SW_A` → L3 → `3V3_A` output; COUT (22µF ceramic) across `3V3_A`↔GND_A; CBOOT between U8's BOOT pin and `SW_A`; FB divider `3V3_A` → R25 (6.2kΩ) → `FB_A` → U8 FB, R24 (2.7kΩ) → `GND_A`. Vout = 1.00×(1+6.2k/2.7k) = **3.30V**.

---

## Power Side B — `power_side_b.kicad_sch`

**Purpose**: Domain B (coil-drive) buck regulator, V_COIL_IN → COIL_9V.

**Same IC as Power Side A**: LMR50410XDBVR, C2841056. Same sourcing/rationale — see Power Side A above and circuit-draft.md's "Buck IC switched to LMR50410."

**KiCad sheet not yet redrawn against this decision** — U10/L4/D6/R26/R27 as currently placed are the retired LM2596S-ADJ design and need replacing with the parts below.

**Parts to place**:

| Ref | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| U10 | LMR50410XDBVR | Buck regulator | C2841056 | ✅ same part as U8 |
| L4 | **33µH, Chilisin LVS606045-330M-N** | Buck inductor | C285825 | ✅ same part as L3 — consolidated 2026-08-02 for BOM line count, **re-sourced same day after the coil-rail max expected load was revised from 0.45A to 0.7A**. At 28.8V/9V/700kHz, 33µH gives ripple current ΔIL≈268mA (unchanged by the load revision — ΔIL depends on Vin/Vout/fsw/L, not Iout), KIND≈0.383 at the new 0.7A load (was 0.595 at 0.45A — actually more comfortable now, well inside the 0.2-0.6 range). The load revision instead changed the *current-rating* margin: the original 4×4mm pick (SNR4030-330MT, 840mA rated/1.1A sat, C5127398) only cleared 0.7A by ~1.2× rated/~1.3× saturation — too thin for a sealed 65-85°C enclosure per this project's own PTC-derating lesson. New part clears with real margin: 1.4A rated / 0.7A = **2.0× rated-current margin**; 2.3A saturation / 0.834A peak (0.7A + ΔIL/2) = **~2.76× saturation margin**. See circuit-draft.md for the full math. |
| D6 | — | Catch diode | — | **Removed** — same reasoning as D5. Note: D6 was also the flyback-diode candidate for the relay board itself — that's a separate, still-open question (bench inspection needed), unaffected by this IC's own catch-diode removal |
| R26 | 24kΩ ±1% 0603, UNI-ROYAL 0603WAF2402T5E | FB divider, top (RFBT) | C23352 | ✅ confirmed live: **Basic tier**, 450,302 stock, $0.00099. Re-sourced 2026-08-02 (was 100kΩ/C22936, shared with R25) — LMR50410's recommended RFBT range is 10-100kΩ, 24kΩ is inside it. |
| R27 | 3kΩ ±1% 0603, UNI-ROYAL 0603WAF3001T5E | FB divider, bottom (RFBB) | C4211 | ✅ confirmed live: **Basic tier**, 3,745,756 stock, $0.00099. Re-sourced 2026-08-02 (was 12.4kΩ/C22865, Extended). Vout = 1.00×(1+24k/3k) = **9.00V exact**. |
| — | 2.2µF X7R ceramic, ≥50V | CIN bulk | *not yet sourced* | ⚠️ open, same as Power Side A |
| — | 0.1µF X7R ceramic | CIN high-freq bypass | *not yet sourced* | ⚠️ open |
| — | Small tantalum or polymer, value TBD | CIN damping cap | *not yet sourced* | ⚠️ open, same long-input-lead reasoning as Power Side A |
| — | 22µF X7R ceramic | COUT | *not yet sourced* | ⚠️ open |
| — | 0.1µF X7R ceramic, ≥16V | CBOOT | *not yet sourced* | ⚠️ open |

**No feedforward cap** — unlike the old LM2596 design (which needed one on this 9V rail per TI's Table 9-6), LMR50410's datasheet doesn't mention Cff anywhere; internal compensation handles it unconditionally regardless of RFBT or Vout.

**Wiring**: `V_COIL_IN` → U10 Vin; CIN network as above; switch node `SW_B` → L4 → `COIL_9V`; COUT (22µF ceramic) across `COIL_9V`↔GND_B; CBOOT between U10's BOOT pin and `SW_B`; FB divider `COIL_9V` → R26 (24kΩ) → `FB_B` → U10 FB, R27 (3kΩ) → `GND_B`. Vout = 1.00×(1+24k/3k) = **9.00V**.

**Wiring (stale — LM2596-era)**: `V_COIL_IN` → U10 Vin, switch node `SW_B` → L4 → `COIL_9V`; D6 on `SW_B`↔`GND_B`; FB divider `COIL_9V` → R26 → `FB_B` → U10 FB, R27 → `GND_B`. Vout = 1.23×(1+7500/1200) = **8.92V** (LM2596-era math).

---

## MCU — `mcu.kicad_sch`

**Purpose**: ESP32-C3 "Super Mini" class module, plus its entrance decoupling. **Fully rebuilt 2026-08-02** to implement the 2026-08-01 architecture pivot (see OPEN ARCHITECTURE DECISION above) — the bare ESP32-S3-WROOM-1 + custom USB-C/ESD/CC-pulldown circuit (old U7, R15–R17, SW1/SW2, C15) is gone from this sheet entirely, replaced by a single pre-made module symbol. **Symbol widened and pin table corrected again later the same day** once real reference data arrived — see the two dated passes below.

**⚠️ Everything on this sheet is a placeholder pending a bought listing.** No specific "ESP32-C3 Super Mini" SKU has been purchased yet (see OPEN ARCHITECTURE DECISION's "Still open" line). The symbol and footprint are now built from real reference data (a pinout guide + a dimensioned product photo, both user-supplied), which is considerably better than yesterday's from-doc-prose reconstruction, but neither is tied to one specific bought listing — treat every number here as "real but generic," not "confirmed for this exact SKU," until one is bought and checked.

**Parts placed today**:

| Ref | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| U7 | **ESP32-C3-SuperMini** (custom symbol/footprint, `AramLib:ESP32-C3-SuperMini`) | MCU module | — (not on LCSC, see OPEN ARCHITECTURE DECISION — AliExpress/eBay hand-assembly line) | 🔧 placeholder part — see caveats below |
| C14 | 10µF/25V, Samsung | 3V3_A bulk decoupling, module entrance | C96446 | ✅ same part reused from the old S3 circuit — repurposed, not resourced |
| C16 | 0.1µF/50V, Samsung | 3V3_A local bypass, module entrance | C14663 | ✅ same part reused from the old S3 circuit — repurposed, not resourced |

**Removed from this sheet** (all obsolete under the new architecture, per OPEN ARCHITECTURE DECISION's explicit call): C15 (was an EN-RC cap / entrance-dup, ambiguous even before the pivot — dropped, not carried forward), R15/R16/R17 (bare-chip strap/EN pull-ups — the module handles its own strapping internally), SW1/SW2 (EN/BOOT buttons — module has onboard RST/BOOT buttons per the sourcing pass), and the bare ESP32-S3-WROOM-1 symbol/footprint/USB-C-adjacent nets. The USB_N/USB_P global labels are also gone — the module's USB-C connects to its own onboard chip's native USB internally, never reaching this board's headers. **No separate EN pin is exposed either** (see below) — the RST button ties EN to GND onboard, so a header-level EN connection was never actually needed.

**Symbol/footprint — built from scratch, no stock KiCad asset exists for this class of board.** Checked `RF_Module:ESP32-C3-DevKitM-1`, `RF_Module:ESP32-C3-WROOM-02(U)`, and `RF_Module:WEMOS_C3_mini` — the last is the closest real candidate but is a **different, larger, differently-labeled board** (official Wemos/LOLIN product, ~34×25mm, 2×8 pins with Arduino-style `D0`–`D10` aliases) — using it would have silently drawn the wrong physical footprint, defeating the whole point of this pivot (the 8.5mm clearance finding). Built a custom part instead, in two passes:

**Pass 1 (morning, 2026-08-02)** — from-doc-prose reconstruction: 16 pins, 4 left (5V/3V3/GND/EN) + 12 right (IO0–7 + 4 named), lopsided and cramped. Footprint used estimated 22.5×18mm body / 20.32mm row pitch (both guesses, and width/height were effectively transposed).

**Pass 2 (afternoon, 2026-08-02)** — corrected against real references the user supplied:
- [lastminuteengineers.com's ESP32-C3 Super Mini pinout reference](https://lastminuteengineers.com/esp32-c3-super-mini-pinout-reference/) confirmed: 2 rows of 8 pins (16 total) at 2.54mm pitch; the real GPIO set is **GPIO0–10 + GPIO20 + GPIO21 = 13 GPIO** (not the 12 Pass 1 had — Pass 1 dropped GPIO10 to force-fit a 3V3 pin into budget); **3V3 is genuinely bidirectional** ("if you have a highly stable 3.3V external battery or power supply, you can use the 3V3 pin as an input") — directly validates this sheet's `+3V3_A`-backfeeds-the-module wiring, upgrading it from an assumption to a documented board feature; and a **third strapping pin, GPIO2**, not previously analyzed (`firmware/README.md` only covered GPIO8/9) — GPIO2 is currently unused/spare in this design, flag for later if it's ever pressed into service.
- A real **dimensioned product photo** (18.00mm wide × 22.52mm tall, 15.24mm header row pitch) corrected the footprint's actual geometry — Pass 1's estimate had width/height essentially swapped and the pitch wrong by ~5mm. This was the single highest-priority number to get right, since the whole pivot exists because of an 8.5mm clearance problem.
- **Reconciled the pin budget cleanly this time, no invented tradeoffs**: 13 GPIO + 5V + 3V3 + GND = 16 exactly, **with no separate EN pin** — the module's onboard RST button already ties EN to GND, so EN doesn't need its own header connection at all. (Pass 1 had invented dropping a GPIO to make room for both 3V3 *and* EN; dropping the unnecessary EN pin instead means all 13 real GPIO fit with zero compromises.)
- **Symbol widened and rebalanced** per explicit request: body half-width doubled (5.08mm→10.16mm), pin length doubled (2.54mm→5.08mm), and the lopsided 4-left/12-right split rebalanced to a clean 8-left/8-right (matching the real board's own 2×8 physical count, though — important — **this schematic-diagram left/right split is a readability choice only and does not claim to reproduce the real board's physical silkscreen layout**; only the footprint's pad *positions* carry physical-accuracy weight, and that's a separate, still-open item below).
- **Footprint pad numbering reworked to match the symbol 1:1** (pad N = symbol pin N = the same net, guaranteed by construction) — pads 1–8 left column top-to-bottom, 9–16 right column top-to-bottom, at the confirmed real 18.00×22.52mm body / 15.24mm row pitch / 2.54mm pin pitch.
- **Still explicitly open**: the footprint's pad *arrangement* (which physical corner has which real pin) is not confirmed against the actual board. The photo shows `5V`/`G`/`3.3` at the top of the board's **right** column with `GPIO21`/`GPIO0` at the bottom-left/bottom-right corners — this footprint's pad 1 (`5V`) currently sits on the **left**, a real unresolved mismatch. Several pins (especially the left column's upper half) aren't legible in the one photo available. **Deliberately not guessed at further** — user has taken the mechanical/physical side of this work; what's fixed today is internal consistency and real dimensions, not real-silkscreen fidelity.
- **New library infrastructure** (unchanged from Pass 1, still in place): `hardware/fp-lib-table` (→ `hardware/AramLib.pretty/`) and `hardware/sym-lib-table` (→ `hardware/AramLib.kicad_sym`) — both resolvable, `lib_search_symbols`/`lib_search_footprints` find `AramLib:ESP32-C3-SuperMini` correctly. Pre-existing `AramLib:SS14`/`AramLib:TS-1187A-B-A-B` footprints still not backfilled into these tables — out of scope, infrastructure exists for later if wanted.

**Wiring**: `+3V3_A` → module 3V3 pin (bypasses the module's own onboard 5V→3.3V regulator entirely — **module 5V pin left unconnected**, since Power Side A's buck (U8) already regulates 3V3_A independently; **now a documented board feature per the pinout reference, not just an assumption**). `GND_A` → module GND pin. Both power taps and the C14/C16 decoupling pair each get their **own separate power-symbol instances** directly at their own pins (not chained through a shared rail) — avoids a wire-merge/junction pitfall hit twice already today (see logbook). `SDA_A`/`SCL_A`/`UART_RX_A`/`UART_TX_A` global labels → module `IO8_SDA`/`IO9_SCL`/`IO20_RX`/`IO21_TX` pins respectively (same net names as before, so I2C Isolator and Communications sheets need no changes). Module `IO0`–`IO7` and `IO10` (9 spare GPIO) left unconnected with explicit no-connect flags — genuinely spare, no function assigned yet.

**Nets present**: `+3V3_A`, `GND_A`, `SCL_A`, `SDA_A`, `UART_RX_A`, `UART_TX_A`.

**ERC status, 2026-08-02 (Pass 2)**: clean except for the same expected single-sheet-isolation artifacts as Pass 1 (`GND_A`/`IO20_RX` "not driven," 4 global labels "connected to only one pin," 2 cosmetic `AramLib`-library warnings from `kicad-cli` not yet picking up the lib tables). No real connectivity errors this pass — Pass 1 briefly hit a "pin not connected" false-negative from two collinear wires merging around a mid-path power symbol (fixed then with an explicit junction); Pass 2 avoided the whole class of bug by giving every power tap its own dedicated symbol instance instead of chaining wires.

**Open items**:
- Pick and buy a specific "ESP32-C3 Super Mini" listing, then reconcile the footprint's pad *arrangement* (not its dimensions, already real) against the real silkscreen — this is now the single biggest remaining gap on this sheet.
- Confirm native-USB vs. CH340 for whatever specific listing gets bought.
- `firmware/`'s pin assignment (GPIO8/9 I2C, GPIO20/21 UART) is unaffected by any of today's sheet rebuilds — those GPIO numbers are unchanged, just now expressed through the new module symbol's pins. GPIO2's newly-flagged strapping caution should get the same class of check GPIO8/9 already got, whenever GPIO2 is first used for something.
- **CAN node address — assigned 2026-08-04, corrected same day to match what was actually built, board already at fab.** A 4-bit configurable CAN node address (0-15) is wired directly to the MCU's own Domain A GPIOs rather than through the PCA9555 (which lives on the galvanically-isolated Domain B side — address-select logic belongs with the MCU that actually forms CAN arbitration IDs, not across the isolation barrier). **As-fabbed pins: `NODE_ID0`-`NODE_ID3` → `IO0`-`IO3`** (a contiguous block — confirmed against `hardware/mcu.kicad_sch`'s U7 pins 9-12), each with an external 10kΩ pulldown to `GND_A` and a switch/jumper to `+3V3_A` (open=0, closed=1), read as plain digital inputs by firmware. **This is not the pin set originally planned earlier the same day** (GPIO0/1/3/10, chosen specifically to avoid GPIO2 as a strapping pin) — the board went to fab with `IO0`-`IO3` instead, which includes GPIO2. `firmware/lib/orc_can_addr/orc_can_addr.h` and `firmware/README.md` have both been corrected to match the real board. **GPIO2 strapping safety re-verified given it's now load-bearing on a fabbed board, not a design choice**: Espressif's *ESP32-C3 Series Datasheet* v2.4 Table 3-3 ("Chip Boot Mode Control") shows GPIO2 = "Any value" (don't-care) whenever GPIO9 = 1, and this board's own I2C pull-up (R21, `SCL_A`) holds GPIO9 = 1 at every reset — so neither address-switch position on GPIO2 can affect boot mode. Table 3-3's own footnote states directly: "GPIO2 actually does not determine SPI Boot and Joint Download Boot mode." No published ESP32-C3 errata mention GPIO2. **Verdict: safe as fabbed, no rework needed.** `IO4`-`IO7` and `IO10` remain genuinely spare (5 GPIO left after this assignment) — `IO10` in particular was the original plan's bit3 pin but isn't wired to anything address-related on the real board.

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

**Resolved 2026-08-02, user confirmed — was flagged as a contradiction earlier the same day.** This table's `COIL_9V → 3.3V` wiring is correct: U6 powers the PCA9555 I2C GPIO expander and the ADuM1250 isolator's Domain-B-side. circuit-draft.md's topology diagram previously said this rail sourced from harness A+ directly — that diagram caption was the stale one and has been corrected to match this table. Not blocking regardless (the coil-supply-gating feature that would have made the earlier ambiguity dangerous was proposed and reverted the same day, so nothing currently depends on `COIL_9V` surviving an outage). **U6 now sourced too** (C6186, confirmed Basic tier — see table below) — this section is fully closed.

| Ref (legacy) | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| U6 | AMS1117-3.3 | Domain B logic LDO, COIL_9V → 3.3V | C6186 | ✅ confirmed live via `lib_get_component_details` (jlcsearch), 2026-08-02: SOT-223, **Basic tier**, 1,490,681 in stock, ~$0.151/unit |
| C14/C15 (legacy refs) | 10µF ×2 | LDO in/out | — | ⚠️ generic |

### PCA9555 I2C GPIO expander

| Ref (legacy) | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| U5 (legacy ref) | PCA9555PW,118 (NXP) | I2C GPIO expander, addr 0x20, 10-of-16 I/O used, 6 spare | C128392 | ⚠️ re-pulled live 2026-08-01, 8,773 in stock, TSSOP-24. Basic/Extended tier not confirmed (badge didn't render to automated fetch). **Temp margin thin**: datasheet rated −40 to +85°C against this enclosure's 65–85°C ambient — near-zero headroom for self-heating above bulk ambient, same class of concern as the PTC 85°C-derating finding. Address 0x20 is set by A0–A2 board strapping, not part-number-specific. |
| C16 (legacy ref) | 0.1µF | Decoupling | — | ⚠️ generic |

### Per-channel coil driver ×10 (channels 1–10)

Identical stage repeated for each relay channel: `RBn` (PCA9555 output → NPN base) → `QNn` (level-shift NPN) → `RPn` (gate pull-up) → `QPn` (high-side coil switch) → harness pin `n+2`.

**Digital-transistor (pre-biased NPN) option evaluated and rejected, 2026-08-01**: considered replacing QNn+RBn with a single "digital transistor" (built-in base + base-emitter bias resistors, e.g. onsemi MMUN2211LT3G, C150229, R1=13kΩ, 13,980 in stock) to eliminate RB1-10 entirely. **Rejected on economics**: MMUN2211LT3G's Basic/Extended tier never confirmed after checking directly (same recurring JLCPCB-badge issue), and if it's Extended that's a flat $3/order reel-loading fee to displace parts (MMBT3904 + discrete resistors) that are already Basic and already loaded. Not worth the risk for an unconfirmed area saving. Went with resistor arrays instead (below) — same area-saving goal, zero tier risk.

| Ref | Value / part | Function | LCSC | Status |
|---|---|---|---|---|
| RB1–RB10, RP1–RP10 | **UNI-ROYAL 4D03WGJ0103T5E**, isolated 4×10kΩ array, 0603x4 (8-pin, 4 independent 2-terminal resistors — confirmed not a bussed/shared-common design) | PCA9555→NPN base (RBn) and gate pull-up to V_COIL_IN (RPn) — 20 discrete 10kΩ resistors total, both banks share one part | C29718 | ✅ **decided 2026-08-01** — confirmed genuinely JLCPCB Basic via `lib_get_component_details` (jlcsearch live data: "Basic: yes, Preferred: no" — a direct structured field, better evidence than the CSV-listing method used earlier; worth using this tool first for future tier checks). 849,056 in stock (jlcsearch live figure). 20 resistors → 5 array packages (4 each), real part-count and board-area reduction with no Extended-tier risk. **KiCad symbol/footprint, both standard — nothing to import**: symbol `Device:R_Pack04_Split` (built-in multi-unit, 4 independent isolated resistors as separate schematic units — places/wires independently, one physical package), footprint `Resistor_SMD:R_Array_Concave_4x0603` (1.6×3.2mm body, 8 pads — dimensions match the part's real package). **One open caveat**: KiCad also ships a `Convex` variant (different pad-termination style, ~0.1mm pad-size difference); picked Concave as the more common convention for this part family, but UNI-ROYAL's actual termination style wasn't confirmable from the datasheet (PDF wouldn't parse as text) — verify against the LCSC product photo before fab. |
| ~~RB1–RB10~~ | ~~10kΩ ×10 discrete~~ | ~~PCA9555 output → NPN base~~ | — | Superseded by the array above |
| QN1–QN10 | **MMBT3904** (JSCJ) | Level-shift NPN | C20526 | ✅ **decided 2026-08-01** — Basic-tier preference over the literal MMBT2222A (which didn't appear on JLCPCB's Basic-parts lists). 253,450 in stock. Functionally interchangeable for this role (3.3V I2C-expander output → 10k base, no high-current/high-freq need). |
| ~~RP1–RP10~~ | ~~10kΩ ×10 discrete~~ | ~~Gate pull-up to V_COIL_IN~~ | — | Superseded by the array above |
| QP1–QP10 | AO3401A ×10 | High-side coil switch | C15127 | ✅ re-verified live 2026-08-01: 186,375 in stock, $0.10/pc single-unit. **Tier caveat applies to every C15127 line in this doc** (this bank plus the Domain A reverse-polarity/source-select FETs once migrated): LCSC's page shows no Basic/Extended badge, JLCPCB's own page didn't render one either — circumstantially Basic per third-party list cross-check, not a direct-page confirmation. **Footprint/LCSC applied in KiCad, 2026-08-02**: `relay_drive.kicad_sch`'s 3 currently-placed AO3401A instances (Q8, Q12, Q16 — refs from the hierarchical split's own numbering, not QP1-10; only 3 of the eventual 10 channels are drawn so far per the parallel session's in-progress build-out) had empty `Footprint` and no `LCSC` field at all. Set `Footprint` to `Package_TO_SOT_SMD:SOT-23` (matches the stock KiCad symbol's own default/`ki_fp_filters`, already used by this project's other SOT-23 parts) and added `LCSC` = `C15127` to all three. Remaining channels need the same two fields applied as they're drawn — not yet a project-wide default, just fixed on what exists today. |
| ~~D-flyback ×10~~ | ~~BAT54C (JSCJ), SOT-23-3L~~ | ~~Per-channel coil flyback~~ | ~~C2135~~ | **Reversed 2026-08-01** — see below |
| D-flyback ×10 (refs TBD) | **SS34** (MDD), SMA(DO-214AC) — reused, same part as D3/D5/D6 | Per-channel coil flyback | C8678 | ✅ **decided 2026-08-01, final call** — BAT54C dropped: neither candidate listing (onsemi C236933, JSCJ C2135) appears in either of two independently-maintained JLCPCB Basic-parts lists, and even the "obviously safe" fallback (SS14, C2480) turned up explicitly tagged "Extended Library" in one of them — none of today's diode tier checks landed clean. Reusing C8678 (already in the BOM for D3/D5/D6) carries **zero incremental reel-fee risk** regardless of its own exact tier, since that decision is already made and already paid for — unlike introducing any new diode part number. Trade-off accepted: gives up the SOT-23 footprint win, back to SMA(DO-214AC) at ×10 instances. |

**Open, design-level item**: flyback diode presence on the *relay board itself* is still unconfirmed — needs bench inspection or a continuity/diode check across a coil's harness pins. Whether ORC needs to supply this diode at all is the remaining open question — the donor Motorola board apparently used a per-channel diode alongside the P-ch FET and NPN (user's own observation; PMUN1046A_RE has no component-level schematic to independently confirm this against — its own docs state no netlist was ever found for the URC). If it turns out not to be needed, SS34/C8678 above just doesn't get placed for this role — no harm in having already committed to it, since it's already in the BOM regardless.

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
