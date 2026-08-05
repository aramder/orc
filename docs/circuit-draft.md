# ORC circuit draft — parts and topology

Working document for parts selection and topology, ahead of schematic capture in `orc-hardware`. Carries forward from [design-inputs.md](design-inputs.md); does not repeat facts already established there.

**Status: draft, not locked.** Several items below are flagged *unresolved* — do not order parts against those without a follow-up pass.

**For manual schematic capture, parts, and LCSC numbers**, see [subcircuit-capture-guide.md](subcircuit-capture-guide.md) — the decisions below organized by KiCad sheet with real net-level wiring, pulled from the live schematic's connectivity graph rather than this document's chronological decision log. (`hardware/BOM.md` was retired 2026-08-01 — redundant with that doc once it existed.)

## Decisions made this session

| Decision | Choice | Consequence |
|---|---|---|
| Primary control path | **USB (native ESP32-S3-WROOM-1U USB)** | No CAN transceiver on BOM. WiFi remains config/monitoring only, per design-inputs.md. |
| Isolation | **Keep it** — MCU/USB domain galvanically isolated from coil-drive domain | Needs an isolated logic supply + an isolator. Costs board area and BOM lines. |
| Isolator part | **ADuM1250** (isolated I2C buffer), not a bank of discrete-line isolators | Barrier crossing collapses to 2 wires (SDA/SCL). Everything that drives the 10 channels — I2C GPIO expander, gate-drive stage — moves entirely to the coil-drive-domain side, addressed over that one bus. |
| Coil switching topology | **Positive-switched (high-side) — locked in, not a design choice** | Relay coil returns are common/ground on the relay board; the switching element sits between +9V and the top of each coil. Rules out ULN2803A (low-side sink) outright, regardless of isolator choice. |
| Per-channel current sensing | **Disregarded — not physically possible** | Dropped entirely, not deferred. No shunt/Hall element anywhere in this design. |
| Domain A power isolation | **Isolated 5V→5V DC-DC dropped** — ADuM1250 alone carries the barrier | Domain A power is now a single non-isolated buck, no transformer-isolated stage. Removes the Mornsun B0505S-1W line item entirely. |
| Coil supply input range | **Widened to handle up to 32V** (was 12V-only), later unified to **28.8V ceiling** across both buck instances (see below) | Covers both 12V and 24V vehicle electrical systems in one board design. Measured coil current: **45mA/coil at 9V, room temperature** (user-measured, reliable across all 10 channels) × 10 = 0.45A total (~4.05W) — size part for real margin above that. |
| Domain A 3.3V regulator | **Single-stage wide-input buck, not LDO cascade** — one DC-DC takes either USB VBUS (5V) or vehicle rail straight to 3.3V | Removes both the intermediate 5V buck and the AMS1117 LDO — one power supply instead of two in series. Sidesteps the LDO's sealed-enclosure thermal problem via switcher efficiency instead of fighting it with copper pour. |
| Input voltage ceiling | **Unified to 28.8V (continuous) for both buck instances** — was 36V (1a) / 32V (1c) | 28.8V is a real number, not an arbitrary derate: it's both a 24V lead-acid float voltage and an 8S LiFePO4 full-charge voltage, covering both 24V chemistries this design supports. Eases the catch-diode requirement back to TI's literal 40V bracket; transient spikes above this are the front-end TVS's job, not the buck's. |
| Diode part count | **Down to 2 distinct types**: SS34 (buck catch diode ×2, and coil flyback if ORC needs to supply it), USBLC6-2SC6 (USB ESD) | Consolidated this pass: CAN ESD dropped (SN65HVD230 has built-in ±16kV bus ESD), input load-dump TVS removed (transient protection now an open item, see below), and both catch diodes plus the flyback candidate moved from SS56 to a single board-wide SS34 (C8678). |
| Harness connector pinout | **Measured: 14 positions**, not the guessed 12-13 (design-inputs.md) — see below | Confirms high-side switching and A+-on-harness independently; pitch resolved (0.1" unshrouded vertical THT). |
| CAN interface | **Locked as the sole primary control path** — no longer provisional | Needs a transceiver, termination, and a field-wireable connector — all already sourced (SN65HVD230/C12084, R14 120Ω/C17909, J6 terminal/C2827883). Protocol-format and transport-architecture research: [docs/can-protocol-research.md](can-protocol-research.md) (2026-08-01) — recommends CANopen (CiA 301/401-style) for the application layer, and finds the ESP32-C3's native TWAI controller can be routed onto the already-wired GPIO21/GPIO20 pins via the GPIO Matrix (Espressif TRM), so the current `firmware/src/uart_can_bringup` sketch's plain-UART transport should be swapped for TWAI in the next firmware pass — a firmware-only change, no schematic/pinout impact. |
| Primary control path — **resolved 2026-08-01, CAN-only, no wireless** | Mechanical finding (only ~8.5mm clearance between stacked PCBs, not enough for both USB-C and the DC+CAN screw terminal) plus a scope decision (this board's actual job — CAN in, drive the I2C GPIO expander out — doesn't need WiFi/BLE or dual-core compute). **Both wireless config/monitoring and the dual-core Xtensa S3 are dropped, not just USB.** MCU moves from bare ESP32-S3-WROOM-1U + custom USB-C circuit to a pre-made **ESP32-C3** module (USB-C for programming only). Exact board TBD — see subcircuit-capture-guide.md's MCU section. | Retires U7/J4/U5/R11/R12 as designed-on-ORC's-board parts (replaced by one module part number), eliminates the Q1/Q3/Q4/Q5 source-select stage entirely (Q4/Q5's gate-drive chain was an unresolved electrical gap since early in the project), and retires design-inputs.md's external-antenna requirement — radio is never enabled in firmware now, so the sealed-enclosure PCB-antenna problem is moot. |
| Domain A power entry | **Dedicated hardwired DC terminal added, alongside USB-C** — decoupled from the harness's A+ tap | Fixes a latent isolation gap: Domain A's power was implicitly going to share the harness A+ with Domain B, which would have re-connected the two "isolated" grounds through a common supply return, defeating the ADuM1250 barrier. Now Domain A's only power inputs are USB-C or this terminal — both genuinely separate from Domain B/vehicle-chassis-referenced power. ~0.2A max draw (ESP32 + CAN transceiver only, no coil current on this rail). |
| CAN termination + ESD protection | **120Ω 0805 resistor (C17437) via a 2-pin header/shunt jumper**, plus USB (C7519) and CAN (C15771) ESD arrays | Termination now field-toggleable rather than hardwired, since whether ORC is end-of-bus depends on install. Closes two previously-unaddressed protection gaps (USB connector, CAN field terminal) — both are real transient-exposure points a technician handles directly. |
| USB + DC-terminal source arbitration | **Active P-FET disconnect (source-select), not passive diode-OR** — DC terminal path is switched off by a FET when USB VBUS is present | Closes a gap where both sources tied to one buck Vin node could backfeed into each other (DC terminal backfeeding a USB host is the bad case). Diode-OR was evaluated and **rejected on hard numbers**: USB-IF worst-case device-end VBUS (4.4V) is already below the buck's 4.5V minimum input with *zero* diode drop added — a passive diode-OR has negative margin at spec corners, not just a thin one. |
| Node-ID address input (multi-unit fleet addressing) | **Corrected 2026-08-02 — 4-position DIP switch, read directly by 4 ESP32-C3 GPIOs on Domain A, NOT the PCA9555.** Labeling corrected same day: weight-labeled (1/2/4/8), not bit-numbered. Design decision only, not yet sourced or drawn | Answers the open item flagged in [can-protocol-research.md](can-protocol-research.md)'s COB-ID section ("ORC's board currently has no DIP switch or equivalent"). **Critical reason for the Domain A correction**: the PCA9555 (Domain B) is powered only from harness A+ — if ORC is bench-connected over USB alone (no vehicle harness), Domain B has no power at all, and the ESP32 could never read a PCA9555-hosted switch to learn its own node ID. Direct-to-MCU wiring works in every power state Domain A itself can be in. See "Node-ID address input" section below for the full correction, labeling convention, and pin-budget accounting. |
| ~~Coil-supply (9V) power gating~~ | ~~PCA9555-controlled enable, default OFF~~ — **reverted 2026-08-02, same session**, coil supply runs continuously instead | User call: quiescent draw isn't significant against this design's actual power budget, not worth the complexity. Also retires the Domain-B power-source contradiction this idea surfaced as a blocking dependency — no longer relevant to this feature, though the underlying doc disagreement (below) is still worth fixing on its own merits. See "Coil-supply power gating — reverted" below. |
| Buck CIN/COUT part count | ~~Consolidated to one SMD Al-electrolytic part number~~ — **SUPERSEDED 2026-08-02, same session, by the LMR33630 IC switch below** | See "Buck IC switched to LMR33630" — the whole electrolytic-cap question is moot once the regulator IC changed; COUT goes all-ceramic. Kept for record only. |
| **Buck IC (both instances)** | **LM2596S-ADJ (TO-263) → LMR33630 (HSOIC-8, C841384)** — real board-space problem, not a cost/thermal one this time | User reported the TO-263 + 12×12mm inductor + SMA diode solution doesn't physically fit. Re-sourced for small-package synchronous bucks rated ≥28.8V, found LMR33630 already had a live LCSC listing from an earlier (rejected-on-stale-thermal-grounds) pass. Datasheet-confirmed (direct PDF read, not a mirror): all-ceramic COUT with no bulk cap required at all; CIN wants a small damping cap given this board's long automotive-harness input leads, but far smaller than the electrolytic can it replaces. See "Buck IC switched to LMR33630" below for the full writeup. |

## Buck IC switched to LMR50410 — board space, 2026-08-02 (corrected same session, LMR33630 → LMR50410)

**LMR33630 (below) was the first pick, same session — superseded within the same session by a smaller candidate the user found.** Kept for the board-space rationale and the datasheet-confirmed ceramic-cap findings (both still apply, same class of part), but every specific part number/value in that section is replaced by the table below.

**TI LMR50410 (LMR50410XDBVR), LCSC C2841056** — confirmed live (`lib_get_component_details`): Extended tier, 4,643 in stock, $0.43/unit. SOT-23-6 (JEDEC DBV0006A, 2.9×1.6mm body) — roughly **1/5 the board area of LMR33630's HSOIC-8**, and the real reason to prefer it over the first pick. Datasheet SLUSDW3A, direct PDF read (not a mirror), all 35 pages.

| Property | LMR33630 (superseded pick) | **LMR50410 (final)** |
|---|---|---|
| Package | HSOIC-8, ~4.9×3.9mm | **SOT-23-6, 2.9×1.6mm** |
| Vin | 3.8–36V | 4–36V (abs max 38V) — same margin over 28.8V ceiling |
| Iout | 3A | 1A — still 2–5× margin over our 0.2–0.5A/0.45A loads |
| Switching freq | RT-pin adjustable, not yet picked | **Fixed 700kHz** — one less open design decision |
| COUT | 4×22µF ceramic bank (88µF) | **Single 22µF ceramic** (per Table 9-1, both the 3.3V and 5V rows) |
| CIN | 10µF + 220nF ceramic | 2.2µF + 0.1µF ceramic |
| Feedforward cap | Not needed only *conditionally* (RFBT ≤ 1MΩ rule) | **Never needed — not mentioned anywhere in the datasheet**, internal compensation handles it unconditionally |
| CIN long-lead caveat | Yes, same class of warning | Same caveat present (§10, p.23) — this board's automotive-harness input still wants a small damping cap, same reasoning as before |
| VREF / FB equation | 1V, Vout=VREF×(1+RFBT/RFBB) | **Same equation, same VREF (1.00V)** — the FB resistor values computed below carry over unchanged |
| VOUT range | Fine for both targets | 1–28V confirmed (§7.3) — both 3.3V and 9V well inside |
| CBOOT | 100nF | 0.1µF, X7R/X5R, ≥16V — same value, restated from LMR50410's own datasheet (§8.3.5/§9.2.2.7) |

**FB divider — unchanged from the LMR33630 pass, since VREF is identical:**

| Instance | Target Vout | RFBT | RFBB (computed) | Nearest E96 | Resulting Vout |
|---|---|---|---|---|---|
| 1a (Domain A) | 3.3V | 100kΩ | 43.48kΩ | 43.2kΩ | 3.31V (+0.4%) |
| 1c (coil supply) | 9V | 100kΩ | 12.5kΩ | 12.4kΩ | 9.06V (+0.7%) |

(LMR50410's datasheet recommends RFBT in the 10–100kΩ range rather than LMR33630's single 100kΩ recommendation — 100kΩ is still valid and inside that range, so no change needed.)

**Inductor — derived directly from LMR50410's own design equation (§9.2.2.4, p.18), not copied from a table row**: LMIN = [(VIN_MAX−VOUT)/(IOUT×KIND)] × [VOUT/(VIN_MAX×fSW)], evaluated at our real Vin_max=28.8V, real per-instance load, fSW=700kHz, KIND=0.4 (mid-range of TI's stated 0.2–0.6):

| Instance | Vout | Iout (design) | LMIN computed | Round up to standard value |
|---|---|---|---|---|
| 1a | 3.3V | 0.5A | 20.9µH | 22µH or 33µH |
| 1c | 9V | 0.45A | 49.1µH | 56µH or 68µH |

**Open items before this is schematic-ready:**
- [x] **Inductor sourced, 2026-08-02, consolidated to one part for both instances (user's explicit call, BOM line count)** — **re-sourced same day after the coil-rail load estimate was revised from 0.45A to 0.7A max expected**. **Final pick: Chilisin LVS606045-330M-N, LCSC C285825**, 33µH, SMD 6×6mm, 1.4A rated / 2.3A saturation, 165mΩ DCR, Extended, 1,052 stock, $0.069, confirmed live via `lib_get_component_details`.
  - **1a (3.3V, ~0.2-0.5A load, unaffected by the revision)**: 33µH clears the ~21µH minimum with real margin, well-oversized current-wise.
  - **1c (9V, revised to 0.7A max)**: ripple current ΔIL≈268mA is fixed by Vin/Vout/fsw/L (doesn't depend on Iout) — at the new 0.7A load, KIND≈0.383, comfortably inside LMR50410's 0.2-0.6 range (was 0.595, tighter, at the old 0.45A estimate). The load revision instead broke the *current-rating* margin on the original pick: **SNR4030-330MT (C5127398, 4×4mm, 840mA rated/1.1A sat)** only cleared 0.7A by ~1.2× rated/~1.3× saturation — too thin for a part in this sealed, 65-85°C enclosure, per this project's own PTC-derating lesson (a part rated fine at 20-25°C derated well below its intended load at 85°C). New pick clears with real margin instead: 1.4A/0.7A = **2.0× rated-current margin**, 2.3A/0.834A-peak = **~2.76× saturation margin**.
  - Retired parts, both superseded same session: SNR4030-330MT (C5127398, the first single-part consolidation pick) and SNR6045-680MT (C5127420, the original non-consolidated 9V-rail pick, retired even earlier).
- [x] **PFM vs. FPWM resolved, 2026-08-02**: staying with **LMR50410XDBVR (PFM, C2841056)** — the FPWM alternative (XFDBVR, C5219371) was checked and its stock is too low for this build. Not revisited further.
- [x] **FB divider resistors sourced, 2026-08-02, re-sourced same day for all-Basic tier.** First pass (shared 100kΩ RFBT + 43.2kΩ/12.4kΩ RFBB) got two Extended-tier resistors — no Basic-tier 0603 1% option exists at 43.2kΩ or 12.4kΩ in any brand checked. **Re-sourced against JLCPCB's live Basic-tier 0603 ±1% value set** (pulled directly from jlcsearch's resistor list, 80 confirmed-Basic values spanning 0Ω–10MΩ) rather than picking round numbers and hoping — swept that value set for the pair closest to each target ratio (RFBT/RFBB = Vout/VREF − 1). **User authorized different RFBT per instance** rather than forcing a shared value, which found a better fit than the original scheme:
  - **1a (3.3V): RFBT = 6.2kΩ (C4260), RFBB = 2.7kΩ (C13167)** — both confirmed **Basic tier** live. Vout = 1.00×(1+6.2k/2.7k) = **3.30V** (−0.11%, tighter than the original 43.2k/100k pair's +0.4%).
  - **1c (9V): RFBT = 24kΩ (C23352), RFBB = 3kΩ (C4211)** — both confirmed **Basic tier** live. Vout = 1.00×(1+24k/3k) = **9.00V exact**. 24kΩ RFBT sits inside LMR50410's datasheet-recommended 10-100kΩ range with lower divider standing current than a smaller-value alternative also considered (12kΩ/1.5kΩ, same ratio).
  - All four resistors: UNI-ROYAL 0603WAF-series, deep stock (352k–3.7M units), ~$0.001/unit. Retires C22936/C23053/C22865 from the first pass entirely.
- [ ] Source live LCSC parts: CIN 2.2µF X7R ceramic, CIN 0.1µF X7R ceramic bypass, COUT 22µF X7R ceramic (×1 per instance), CIN damping/bulk cap (tantalum or small polymer, value TBD, sized for damping not storage), CBOOT 0.1µF/≥16V.
- [x] **Catch diode removal confirmed**: LMR50410 is fully synchronous (integrated low-side FET) — no external catch diode on either instance. Applied in subcircuit-capture-guide.md.
- [ ] Quantify actual board-area win against the user's real space constraint once parts are placed — qualitative case is very strong (SOT-23-6 + 4×4mm/6×6mm inductors vs. TO-263 + 12×12mm is a large win) but no real footprint total computed yet.

---

## Buck IC switched to LMR33630 — SUPERSEDED same session by LMR50410 above, board space, 2026-08-02

**Trigger: real physical fit problem, reported by user** — the LM2596S-ADJ (TO-263) + 68µH 12×12mm SMD inductor + SS34 (SMA/DO-214AC) catch diode solution does not fit in the available board space, in either buck instance. This is a different failure mode than the earlier cost/thermal tradeoffs this doc already worked through — those were about which *part* to use within the TO-263-class footprint; this is "that footprint class doesn't fit at all."

**Re-sourced for small-package synchronous bucks, ≥28.8V Vin, live on LCSC.** Full comparison (bare small-package ICs, fully-integrated inductor-in-package modules, and smaller discrete inductors) is in the session log; bottom line:

- No genuinely tiny package (SOT23-6/-8, WSON, small QFN) clears the 28.8V Vin ceiling — every candidate that small tops out well below it (e.g. MPS MP2315GJ-Z, TSOT23-8, 24V max — rejected outright).
- **HSOIC-8/SO-8-class synchronous bucks are the real floor for this Vin range** — smaller than TO-263 (roughly half the board area) but not as small as hoped. Two live candidates: **LMR33630** (C841384, 3.8–36V, 3A) and **LM5164** (C477928, 4.5–100V, 1A). **User picked LMR33630** — cheaper, well-vetted in this project's earlier research (previously rejected only on a now-stale WiFi-thermal assumption, see below), simpler FB network (no mandatory ripple-injection network the way LM5164's COT topology requires).
- Fully-integrated modules (inductor-in-package, e.g. LMZM33603) exist but came back with inconsistent stock/price between sources (~$5.62 vs $12+) and cost ~13× the bare IC — not pursued further this pass.

**Why LMR33630 was rejected once already, and why that reason no longer applies:** this project evaluated LMR33630 vs. LM2596S-ADJ back in the original Domain-A-supply decision and picked LM2596 specifically for TO-263's larger thermal mass, sized against a **355mA WiFi TX peak**. WiFi/BLE was dropped from this design entirely on 2026-08-01 (CAN-only control path) — real Domain A load is now ~0.2A, no radio bursts. The thermal objection that ruled out LMR33630 the first time was already stale before this board-space problem even came up; re-evaluating now just closes that loop.

**Ceramic-cap capability — confirmed by direct PDF read of TI's SNVSAN3F datasheet (not a mirror or search snippet), per this project's own "re-verify load-bearing figures against the primary source" rule:**

| Cap role | Datasheet guidance | Bulk/electrolytic required? |
|---|---|---|
| COUT | 4×22µF X7R/X5R ceramic (typical-app example); min 52µF / max ESR 0.11Ω per the datasheet's own Eq.6 | **No — datasheet states no bulk cap needed, ever.** Real, unconditional area win over the retired electrolytic COUT scheme. |
| CIN | 10µF ceramic (X7R or better) + 220nF high-freq bypass ceramic | **Conditional.** Datasheet's Power Supply Recommendations (p.32) warns ceramic-only CIN can ring with an underdamped LC resonance when the input source has long leads/high impedance — framed as "if long input leads/traces are used," not a blanket requirement the way LM2596's ceramic warning was. |

**This board's actual install is exactly the conditional case the datasheet flags** — the input is fed through a multi-meter automotive wiring harness, not a benchtop supply a few cm away. **Decision: keep a small damping/bulk cap at CIN, sized for damping, not for storage** — a modest tantalum or polymer part, single-digit-to-tens of µF, in a case size far smaller than the ~D6.3×7.7mm electrolytic can it replaces (that part was sized to hold ~94-100µF of bulk charge; a damping cap's job is different and needs much less capacitance). **Specific part not yet sourced — open item below.**

**FB divider, both outputs** — LMR33630 uses VREF = 1V, RFBT recommended 100k�text (max 1MΩ), Vout = VREF×(1+RFBT/RFBB):

| Instance | Target Vout | RFBT | RFBB (computed) | Nearest E96 | Resulting Vout |
|---|---|---|---|---|---|
| 1a (Domain A) | 3.3V | 100kΩ | 43.48kΩ | 43.2kΩ | 3.31V (+0.4%) |
| 1c (coil supply) | 9V | 100kΩ | 12.5kΩ | 12.4kΩ | 9.06V (+0.7%) |

At RFBT=100kΩ, the datasheet's own Cff rule (needed only above ~1MΩ RFBT, confirmed directly against the real threshold, not VOUT) means **no feedforward cap is required on either instance** — simpler than the old LM2596 scheme, which needed one on the 9V rail per TI's Table 9-6.

**Other confirmed requirements from the direct datasheet read**: CBOOT = 100nF (bootstrap, standard), CVCC = 1µF, inductor per the typical-app example is 8µH at 400kHz switching frequency — a real change from LM2596's 68µH/150kHz, since switching frequency and topology both changed. **Inductor value not yet re-derived for this design's actual Vin range/load** — TI's 8µH example was for a 5V-output, fixed-frequency design point, not directly transferable to our 3.3V/9V outputs without redoing the standard buck inductor sizing math (L = (Vin−Vout)×Vout / (Vin×Fsw×ΔIL)) at whatever switching frequency gets set via the RT pin. Flagged as an open item, not guessed at here.

**Open items before this is schematic-ready:**
- [ ] Re-derive inductor value/current rating for both instances at LMR33630's actual switching frequency (RT-pin-selectable, 200kHz–2.2MHz; TI's own example uses 400kHz) — the old 68µH/12×12mm part was sized for LM2596's 150kHz and is not a valid carryover.
- [ ] Source live LCSC parts for: CIN 10µF X7R ceramic, CIN 220nF X7R ceramic, COUT ceramic bank (value TBD — right-size against our real ~0.2–0.5A/0.45A loads rather than copying TI's 3A-class 4×22µF example, same discipline already applied once to the LM2596's CIN sizing), CIN damping/bulk cap (tantalum or small polymer, value TBD), CBOOT 100nF, CVCC 1µF, RFBT/RFBB pairs (100kΩ/43.2kΩ for 1a, 100kΩ/12.4kΩ for 1c).
- [ ] Confirm LMR33630 (C841384) Basic/Extended tier live — earlier note says "Basic: no" (Extended) from a `lib_get_component_details` pull; re-confirm before lock, same as any other Extended part.
- [ ] Quantify the actual board-area win once the above parts are sized — the qualitative case (SO-8 vs TO-263, ceramic vs electrolytic) is strong, but no real footprint total has been computed against the user's actual space constraint yet.
- [ ] D5/D6 catch-diode question: LMR33630 is fully synchronous (integrated low-side FET) — **confirm the external SS34 catch diode (D5/D6) is no longer needed at all**, not just smaller. This wasn't explicitly re-confirmed this pass; synchronous bucks normally don't need one, but say so directly before deleting two BOM lines.

## Harness connector — measured pinout

Per [design-inputs.md](design-inputs.md#harness-header--the-critical-interface), 14 positions, **0.1" (2.54mm) pitch, unshrouded, vertical, THT male header** (LCSC C2977586, breakable strip, Extended — see BOM #7):

| Pin | Function |
|---|---|
| 1 | Coil common return ("coil −", shared across all 10 channels) |
| 2 | A+ |
| 3–12 | Coil 1+ … Coil 10+ (one per channel) |
| 13, 14 | Chassis (relay-board side) |

This measurement independently confirms two things already assumed elsewhere in this doc: **high-side switching** (individual "Coil N+" per channel, one shared common return — matches the positive-switched decision above) and **A+ is carried by the harness** (pin 2), so ORC's connector needs to carry that feed directly rather than requiring a separate lug/tap.

Pin 1 (coil common) vs. pins 13/14 (chassis) are three separate wires, not obviously one net — flagged as a possible star-grounding question, but **dismissed: tie them together as one common ground, no special handling needed.**

## Topology — two ground domains

```
                    ┌─────────────────────────────────────────┐
                    │  DOMAIN A — MCU / USB (isolated ground)  │
                    │                                          │
  USB-C ────────────┤  ESP32-S3-WROOM-1U (native USB, D+/D-    │
  (VBUS 5V,         │  on GPIO20/19)                            │
   host-referenced  │       │                                  │
   ground)          │  CAN transceiver (SN65HVD230) ───────────┼── CAN_H/CAN_L
                    │       │                                  │   (4-pin terminal,
  DC terminal (2 of │       │                                  │    shared w/ DC in)
  the 4 positions,  │       │                                  │
  hardwired alt to  │       │ I2C (SDA/SCL only — 2 wires)      │
  USB-C, ~0.2A,     │       │                                  │
  logic-only) ──────┤       │                                  │
                    └───────┼──────────────────────────────────┘
                            │
                     ┌──────▼──────┐
                     │  ADuM1250   │   isolated I2C buffer,
                     │  I2C barrier│   2 ch (SDA/SCL), not a
                     └──────┬──────┘   discrete-line isolator
                            │
                    ┌───────┼──────────────────────────────────┐
                    │  DOMAIN B — coil-drive (vehicle chassis)  │
                    │       │                                  │
                    │  PCA9555 I2C GPIO expander (16-bit,       │
                    │  10 used) — lives entirely on this side   │
                    │       │                                  │
                    │  NPN level-shift (×10) → high-side        │
                    │  P-ch MOSFET (×10) — coils are            │
                    │  positive-switched, locked in             │
                    │       │                                  │
  12V vehicle ──────┤  reverse-polarity protect                │
  A+ (harness pin 2, │       │                                  │
  per design-inputs. │  9V coil supply (buck, sized to          │
  md) ───────────────┤  measured total coil current — TBD)      │
                    │       │                                  │
                    │  → relay board (existing, passive)        │
                    └────────────────────────────────────────────┘

  Domain A logic supply (single-stage buck, no LDO cascade,
  fed ONLY by sources genuinely separate from Domain B/harness A+):
  DC terminal (2 pins, hardwired alt) ── P-FET source-select ──┐
  (gated OFF when USB VBUS present —                           ├── single
   see "Source arbitration" below)                             │  wide-input
  USB VBUS (5V, when connected) ─────────────────────────────────┘  buck
                                                               ── 3.3V ──
                                                          ESP32-S3, CAN xcvr
  One DC-DC handles both source cases — no separate 5V rail, no LDO.
  Reverse-polarity on the DC terminal reuses AO3401A (#5a) — same
  ~1-2A-rated part, this input only ever sees ~0.2A. Source-select FET
  is a SEPARATE P-FET from the reverse-polarity one, in series after it.

  Domain B logic supply (PCA9555 + I2C isolator secondary side):
  COIL_9V -> 3.3V via U6 (AMS1117-3.3 LDO) -- confirmed 2026-08-02,
  see "Domain B logic supply" section below. Domain B is already
  chassis-ground referenced, same as the coil supply, so no separate
  isolation is needed for this rail.
```

### Why Domain A's power can't come from harness A+ (fixed this session)

Domain A is supposed to be galvanically isolated from Domain B via ADuM1250 — but until this session, Domain A's buck was implicitly drawing from the same harness A+ tap that feeds Domain B's coil supply. **A shared power source re-connects two "isolated" domains through the supply's common return, regardless of what the signal isolator does** — so that would have silently defeated the isolation barrier's entire purpose. The new dedicated DC terminal (below) fixes this: Domain A's only power inputs are now USB-C or this terminal, both genuinely decoupled from Domain B/vehicle-chassis-referenced power.

### Fault/interrupt line: poll, don't add a channel

PCA9555 has an open-drain `INT` pin for input-side change notification, but that's an input use case — this design only drives outputs, so `INT` isn't in play. If a fault/status readback across the barrier is ever wanted, **poll it over the existing I2C link rather than adding a second isolator channel** — simpler BOM, and ADuM1250 only isolates SDA/SCL, so any additional line would need its own isolator part.

### Domain A power — collapsed to a single-stage buck, no LDO

Two corrections from the user, taken together: (1) no isolated 5V→5V DC-DC is required — the barrier between Domain A and Domain B is carried entirely by ADuM1250 (I2C signal only); (2) rather than cascading a 12V/24V→5V buck into a separate 5V→3.3V LDO, use **one wide-input buck that goes directly to 3.3V**, fed by either USB VBUS (5V) or the vehicle rail (up to ~36V worst-case). This removes both the Mornsun B0505S-1W isolated module (LCSC C16787) and the AMS1117 LDO from the BOM — one power supply instead of two in series.

This also sidesteps a real problem the LDO would have had: at the verified WiFi TX peak of 355mA (Table 6-4 — a continuous-duty rating, not a brief burst), an AMS1117-3.3 dropping 1.7V would dissipate ~0.6W, which runs marginal-to-unsafe in this design's **sealed enclosure** (design-inputs.md — heat only leaves through the chassis) at automotive ambient (65–85°C). A switcher dissipates a small fraction of that for the same current, so the thermal problem goes away by design rather than needing a copper-pour workaround.

**Part candidates** (JLCPCB/LCSC sourcing pass):
- **LMR33630** (TI, SO-8, synchronous) — Vin **3.8–36V** (datasheet-confirmed, clears the full USB-to-24V-vehicle range), adjustable output via FB divider (3.3V trivial), 3A capable. LCSC **C1355645** (SO-8, ~$2.32/1-10pcs, ~$1.37/1000+) or **C841384** (WSON, ~$0.72/1-10pcs, ~$0.45/1000+), both Extended — **verify stock before committing**, C1355645 showed low/out-of-stock at last check.
- **LM2596S-ADJ** (TO-263, non-synchronous) — Vin **4.5–40V** (datasheet-confirmed), adjustable via FB divider, 3A. LCSC **C963385** (~$0.62/1-10pcs, ~$0.47/1000+), Extended. Larger/taller TO-263 footprint than LMR33630 — check enclosure Z-height clearance.
- **XL4015E1 (C51661) does not work here** — datasheet minimum input is **8V**, so it cannot regulate from bare USB 5V. Excluded from this single-stage role (it's still fine for the 9V coil supply below, which never sees 5V input).

**Decision: LM2596S-ADJ selected**, despite lower rated efficiency, for two reasons that both trace back to the sealed-enclosure risk this design keeps running into:
1. **TO-263's exposed tab has real, well-characterized thermal capacity** for shedding several watts into a copper spreader — a much bigger safety margin against the sealed-enclosure/high-ambient risk than SO-8's small thermal mass, regardless of the exact efficiency number.
2. **Price**: ~3-4× cheaper than the SO-8 LMR33630 at every quantity break; roughly parity with the WSON LMR33630 variant, which buys back none of the thermal margin (no leads, less thermal mass than TO-263).

**Efficiency — flagged as not fully characterized, verify on bench.** TI's LMR33630 datasheet (SNVSAN3F, Fig 9-8) shows ~80-86% at 12V-in/3.3V-out at this design's actual light-load range (200-500mA), dropping to ~55-65% at 24V-in — buck efficiency degrades hard as Vin/Vout ratio grows even for a synchronous part. TI's LM2596S-ADJ datasheet (SNVS124G, Table 7.8) **only publishes efficiency at the 3A rated point (73% typ @ 12V-in, 3.3V-out)** — no light-load figure exists, and non-synchronous bucks (no light-load PFM) typically run worse than their rated-load efficiency at a few hundred mA, not better. **Don't assume a specific dissipation number for LM2596S-ADJ at the ~300mA operating point — confirm with a bench measurement once a prototype exists**, and size the TO-263 copper spreader with that uncertainty in mind rather than assuming best-case efficiency.

### Coil supply DC-DC — widened for 24V-system flexibility

The 9V coil supply must now handle **up to 32V input** (not just 12V), so a single ORC board design covers both 12V and 24V vehicle electrical systems with margin. Measured coil current: **45mA per coil at 9V, room temperature — user-measured, reliable across all 10 channels** × 10 = 0.45A total at 9V (~4.05W) — size the part with real margin above that to cover simultaneous-switching transients. Output voltage is 9V per Motorola's original coil-heating rationale (design-inputs.md, service manual 3.7.4).

Note: coil resistance rises with temperature (copper's positive tempco), so at a fixed 9V a *hotter* coil draws *less* current than at room temp — the 45mA figure is likely near the higher end of the coil's real operating range, not an underhood-worst-case-low figure. Good margin behavior, but worth confirming if a true worst-case (cold-start) number is ever needed.

**Decision: locked to LM2596S-ADJ (C963385), same part as #1a.** One fewer distinct BOM line to stock/qualify; XL4015E1's only edge (5A vs 3A rating) isn't needed at 0.45A, and it's already excluded from #1a's role anyway.

### Node-ID address input — design discussion, 2026-08-02, corrected twice same day

**Scope note: this section is discussion/decision-log only.** Per explicit instruction this pass, Gate 1 (part sourcing) and Gate 2 (schematic capture) are both deliberately skipped here — no switch/header part numbers pulled, nothing drawn in KiCad.

**Original decision (superseded within the same session): 4 bits off 4 of the PCA9555's spare I/O pins.** Reconsidered and corrected — **PCA9555 is the wrong side of the isolation barrier for this.** Per this doc's own topology diagram, Domain B's logic supply (which powers the PCA9555) is sourced from harness A+, full stop — it has no independent path, unlike Domain A which can run from USB alone with no vehicle harness connected at all (see "Domain A power entry" decision, above). A unit sitting on a bench for firmware flashing, testing, or field diagnosis over USB-only power would have **zero way for the ESP32 to learn its own node ID** if that value lives behind an unpowered PCA9555 — the I2C read would simply never complete. This isn't a hypothetical corner case; USB-only bench operation is exactly the scenario firmware development and field troubleshooting happen in.

**Corrected decision: 4 address bits, read directly by 4 ESP32-C3 GPIOs on Domain A** — no PCA9555, no I2C read, no dependency on Domain B power at all. Works identically whether ORC is bench-powered over USB, running on the DC terminal alone, or fully installed on vehicle power. Gives node IDs 1–15 (0 reserved/unset), same as before. Firmware reads the 4 GPIOs directly at boot (plain digital read, no bus transaction) and uses that value as CANopen `NodeID` in the COB-ID scheme already documented in can-protocol-research.md.

**Component choice, same day**: user picked a **4-position DIP switch** over a 2.54mm header+shunt-jumper block — same electrical function (4 independent lines, each pulled to a defined state and shorted to the opposite state to set a bit), but a DIP switch avoids loose shunt jumpers that can be lost or dropped inside a sealed enclosure during field service, and is flip-to-set rather than pull-and-place. Not yet sourced (Gate 1 pending) — any real 4-position through-hole DIP switch (e.g. the common Copal/CHERRY/Nidec-Copal SPST 2.54mm-pitch style already ubiquitous on JLCPCB) is a reasonable starting search, nothing locked.

**Labeling convention, same day**: bit-numbered labeling (`BIT0`–`BIT3`) was flagged as needlessly complex for an installer setting an address in the field — it requires translating bit position to decimal value in your head. **Decision: label each switch position by its decimal weight, not its bit index** — silkscreen (and the DIP switch's own printed numbering, most of these parts ship pre-numbered 1–4 anyway, so this maps directly) reads `1  2  4  8`, and the installer sets a node ID by summing the ON positions (e.g. ID 5 = positions `1` and `4` ON). Standard, well-precedented convention — the same pattern used for decades on RS-485/Modbus slave-address DIP switches and old ISA-card IRQ jumpers, chosen specifically because addition is easier than binary-to-decimal translation. Internal net/schematic names stay bit-indexed (`NODE_ID0`–`NODE_ID3`, matching firmware's bit positions) since that's an engineering reference, not something the installer reads — only the physical silkscreen uses the weight labels.

**Side benefit**: this also removes the collision risk with `hardware/relay_drive.kicad_sch` (where the PCA9555 lives, and where a parallel session has had live uncommitted edits throughout this project) — the switch now belongs on `mcu.kicad_sch`, which that session hasn't touched. Physical part selection and net-to-pin assignment are still deferred to the Gate 1/2 pass.

**Resolved 2026-08-04 — GPIO pins picked, board sent to fab, then a same-day correction.** Superseding the open items below, most of which this closes:

- **Component**: real `Switch:SW_DIP_x04` (4-gang DIP switch) placed on `hardware/mcu.kicad_sch`, wired `NODE_ID0`–`NODE_ID3`, each with an external 10kΩ pulldown to `GND_A` and the switch leg to `+3V3_A` (open=0, closed=1) — matches the weight-labeled-silkscreen intent above; exact distributor part number is still a Gate 1 item (see open items).
- **Pins, first pass**: GPIO0/1/3/10 — deliberately avoided GPIO2 (a known ESP32-C3 strapping pin), all four confirmed non-strapping.
- **Pins, as actually fabbed, same day**: the real schematic used a contiguous `IO0`–`IO3` block instead — includes GPIO2, drops GPIO10 — discovered only *after* the board had already been sent to JLC. Since the PCB isn't editable at that point, this was treated as a safety-critical re-verification rather than a design tradeoff: Espressif's *ESP32-C3 Series Datasheet* v2.4 Table 3-3 ("Chip Boot Mode Control") reportedly shows GPIO2 = "Any value" (don't-care) in the SPI-Boot row, whose sole determinant is GPIO9 = 1 — and this board's own I2C pull-up (R21, `SCL_A`) holds GPIO9 = 1 at every reset regardless of GPIO2's state, so neither DIP-switch position on GPIO2 should affect boot mode. Firmware (`firmware/lib/orc_can_addr/`, `platformio.ini`, `can_address_bringup`) and `firmware/README.md` corrected to the real GPIO0–3 pins same day; all four build environments re-verified.
- **Independent re-check of that GPIO2 claim, this session**: attempted to directly confirm Table 3-3's exact text against Espressif's primary PDF and found the tooling available couldn't render/parse that specific table (PDF page-render unavailable, extracted text was compressed streams only). Secondary sources corroborate the *load-bearing* part — GPIO9 alone gates SPI-boot vs. download-boot, matching this project's own already-cited Table 3-1 analysis from the GPIO8/9 work — but none available to me independently reproduced the specific "GPIO2 = don't-care" table cell or footnote text. One low-quality secondary source (espboards.dev) even had GPIO9's polarity backwards relative to every other source here, which is a reason to distrust casual mirrors generally, not evidence the original claim is wrong. **Net assessment: plausible and consistent with the well-corroborated GPIO9-dominant mechanism, not independently re-confirmed at the literal datasheet-table level this pass.** Doesn't change anything actionable — the board is already at fab either way — but the real, decisive check is still the one already flagged as outstanding below: flash `can_address_bringup` to real hardware and confirm the unit boots normally in **both** switch positions on the GPIO2/`NODE_ID2` bit, not just build-verify.

**Open items:**
- [x] PCA9555 pin budget: reverted to 10-of-16 used, 6 spare, per the correction above — subcircuit-capture-guide.md's PCA9555 line already reflects this.
- [ ] Source a specific 4-position DIP switch part on a live distributor catalog (Gate 1) — the `SW_DIP_x04` symbol is placed and wired, but no real LCSC/Digi-Key part number is pinned to it yet; check actuation force/cycle life for a sealed-enclosure field-serviceable input.
- [x] GPIO pins for `NODE_ID0`–`NODE_ID3`: resolved as-fabbed to GPIO0/1/2/3 (see above) — not the originally-planned GPIO0/1/3/10.
- [ ] **Real-hardware confirmation of the GPIO2 boot-safety claim** — static analysis says safe (see above, itself not fully independently re-verified this pass due to tooling limits), but the decisive check is flashing real hardware once the board arrives from fab and testing both DIP-switch positions on the GPIO2 bit against actual boot behavior, not just a clean build.
- [ ] Confirm the placed `SW_DIP_x04` footprint's real silkscreen matches the weight-labeled (`1 2 4 8`) convention decided above, once the board is in hand — schematic-level component choice is done, physical silkscreen fidelity to that specific convention hasn't been independently checked.

### Coil-supply (9V) power gating — proposed, then reverted, same session, 2026-08-02

Original ask: default the coil supply (#1c, LM2596S-ADJ generating `COIL_9V`/`V_COIL_IN`) OFF, ESP32-enabled over I2C on relay-switching activity, 60s idle auto-disable, to cut standing buck losses. While designing the control path, this surfaced a real blocking contradiction: `circuit-draft.md`'s topology diagram states Domain B logic (which powers the PCA9555 itself) is sourced from harness A+ directly, independent of the switched coil rail — but `subcircuit-capture-guide.md`'s Domain B logic-supply BOM table (U6, AMS1117-3.3) still reads `COIL_9V → 3.3V`. If that second wiring were what actually got built, gating `COIL_9V` off would have stranded the PCA9555 with no way to ever re-assert its own enable pin — a self-bricking feature.

**Reverted, not resolved-and-built**: user judgment call — quiescent draw on this buck isn't significant against the design's actual power budget (10 coils × 45mA/coil dominates), not worth the added complexity or the risk of shipping the deadlock above if the contradiction went unnoticed. Coil supply runs continuously, no enable/gating hardware, no PCA9555 pin spent on it. (The node-ID address input has since moved off the PCA9555 entirely too, see below — Domain B's pin budget is back to 10-of-16 used, 6 spare, untouched by either feature.)

**The underlying Domain-B power-source contradiction is still real and still open** — it just isn't a hard blocker for this particular feature anymore, since there's no gating hardware to strand. Still worth fixing on its own merits (see subcircuit-capture-guide.md's flag banner and the open item below), since an ambiguous power source for a logic-critical rail is a latent problem regardless of whether anything gates it.

**Open items:**
- [x] **Resolved 2026-08-02, user confirmed**: Domain B logic supply is `COIL_9V → 3.3V` via U6 (AMS1117-3.3 LDO) — this rail powers the PCA9555 I2C GPIO expander and the ADuM1250 I2C isolator's Domain-B-side, matching `subcircuit-capture-guide.md`'s U6 line. Topology diagram caption corrected to match (was stale, said "harness A+ direct"). Dissipation math for this LDO: (9V−3.3V)×I — see the MCU-power discussion in this session's log for the general treatment; not a concern raised for this specific rail since it's IO-expander/isolator logic current, not the MCU's own supply.
- [x] **U6 sourced, same day**: **AMS1117-3.3, LCSC C6186**, SOT-223, confirmed live via `lib_get_component_details` (jlcsearch) — **Basic tier**, 1,490,681 in stock, ~$0.151/unit. No reel-loading fee risk. See subcircuit-capture-guide.md's "Domain B logic supply" section.

### Support passives for both LM2596S-ADJ instances — SUPERSEDED 2026-08-02, TI's actual selection framework

**Superseded by "Buck IC switched to LMR33630" above** — the regulator IC changed for board-space reasons, so every LM2596-specific passive value below (inductor E·T sizing, catch diode, feedback resistors under the old part's VREF/equations) is stale. Kept for record only, per this doc's own convention for superseded decisions.

Pulled directly from TI's SNVS124G PDF (the real ti.com file, downloaded and read directly — not a third-party mirror; an earlier research pass used a mirror with the right general structure but unconfirmed figure numbers). **Both 1a and 1c are the adjustable version**, so the design path below (Figure 9-8, E·T-based) applies to both — the fixed-voltage nomographs (Figures 9-5/9-6/9-7) don't apply here.

**The real constraint neither instance escapes: both operating points are below what TI's own reference tables cover.** Figure 9-8's load-current axis runs 0.6–3A; Table 9-3/9-6's quick-design rows are all built for ≥2A design loads. 1a's ~0.2–0.5A and 1c's ~0.45A both sit at or under the chart's floor. That's not a research gap — it's what TI's own **Section 8.4.1 (Discontinuous Mode Operation)** describes directly: "most switcher designs will operate in the discontinuous mode when the load current is low," and a discontinuous design needs only **½ to ⅓ the inductance** of a continuous-mode design at the same point. Reading a value straight off Figure 9-8 for a sub-0.6A load isn't a valid table lookup; the discontinuous-mode adjustment is the applicable path, and TI's own recommendation for an exact number here is **WEBENCH Power Designer** (confirmed linked in the datasheet's Features section and Section 9.2.1.2.1/10.1.2.1) — enter real Vin/Vout/Iload, it returns a bill of materials, not a chart read.

**Inductor value (both instances)** — TI's E·T design equation, Section 9.2.2.2.2 Eq. 8, evaluated at each instance's worst-case (max) Vin:

```
E·T = (Vin − Vout − Vsat) × (Vout + Vd) / (Vin − Vsat + Vd) × (1000 / 150kHz)
      where Vsat = 1.16V (switch saturation), Vd = 0.5V (catch diode drop)
```

- **1a** (Vin_max ≈ 36V, Vout = 3.3V): E·T ≈ 22.6 V·µs
- **1c** (Vin_max ≈ 32V, Vout = 9V): E·T ≈ 30 V·µs

Both E·T values land in a real region of Figure 9-8's vertical axis, but both instances' load currents fall below the chart's 0.6A horizontal floor — confirming the discontinuous-mode note above applies to both. **This is not a device limitation — TI's own Electrical Characteristics tables (Sections 7.5–7.8) test VOUT accuracy at "0.2A ≤ ILOAD ≤ 3A," so 0.2–0.5A is squarely inside the IC's guaranteed spec range.** The LM2596 has no light-load skip/PFM mode to fail at — it's a plain fixed-150kHz PWM controller, and discontinuous conduction at light load is, in TI's words, "a perfectly acceptable mode of operation," not a workaround. The only real consequence is that Figure 9-8 (a graphical selection aid) and the quick-design tables simply don't extend down to this current — a chart-coverage gap, not an operating limit.

**Decision: use TI's own generic values as the starting point, not a WEBENCH/bench-tuned custom set.** These ICs are the exact basis for the ubiquitous cheap hobbyist LM2596 buck modules, which run fine at light load on generic component choices — no need to over-engineer this. TI's **Figure 9-13 "Adjustable Output Voltage Version"** application circuit is itself a generic reference used across TI's whole family of adjustable-output examples — that's the "one set of robust values, works across a wide range" starting point. **Table below reflects the final state after two later corrections** (Vin ceiling unified to 28.8V, and CIN right-sized off actual load current rather than copied from TI's 3A example — both covered in their own sections below):

| Component | TI Fig 9-13 generic value | ORC value (both instances) | Why |
|---|---|---|---|
| L1 (inductor) | 68µH, L38 code (~3.1A rated) | **68µH**, same | TI's own generic pick, huge margin over our 0.2–0.5A load |
| CIN | 470µF, 50V aluminum electrolytic | **~94µF (2×47µF), 63V SMD polymer** | Right-sized for our real ~0.2–0.5A load via TI's own RMS-ripple-current rule, not copied from their 3A example — see "CIN right-sized" section below |
| COUT | 220µF, 35V aluminum electrolytic | **220µF, 35V SMD polymer**, same value | Clears the 1.5×Vout floor with room to spare for both 3.3V and 9V outputs; matches TI's practice of over-rating for ESR |
| D1 (catch diode) | 1N5825, 5A/40V Schottky | **SS34, 3A/40V Schottky (C8678)** | 28.8V Vin ceiling requires ≥36V reverse rating (TI's literal 40V bracket) — SS34's 40V meets it with ~11% headroom; 3A rating is far above the 0.2–0.5A buck load. Chosen as the single board-wide diode type. |
| CFF | 1.5nF (Table 9-6, 9V row) | **1c only: 1.5nF. 1a: none** | 1c's 9V output matches Table 9-6's row directly; 1a's 3.3V is well clear of the >10V trigger |
| R1 / R2 (feedback divider) | R1=1kΩ 1% | **R1=1kΩ 1% both.** R2: **1a → 1.69kΩ, 1c → 6.34kΩ** | From TI's own Eq. 5/6, VREF=1.23V: R2=R1×(Vout/Vref − 1). 3.3V→1683Ω, 9V→6317Ω, rounded to nearest 1% E96 values |

This is an intentionally approximate, generic-values pick rather than a precision WEBENCH run — consistent with how this same IC performs in the mass-market hobbyist modules it's the basis for. Final inductor/cap LCSC part numbers still need picking (values are locked, specific stocked parts aren't yet), but the values themselves are no longer an open question.

**Output capacitor (both instances)** — ESR-gated, not just a capacitance number:
- **82–820µF low-ESR electrolytic, or 10–470µF solid tantalum.** TI: "Do not use capacitors larger than 820µF."
- **Voltage rating ≥ 1.5× Vout is the stated floor** — but TI's own worked tables use far higher voltage ratings than that floor requires (their 3.3V-output rows use 25–35V-rated caps despite the 1.5× rule only demanding ~5V), because **higher voltage rating buys lower ESR** (Figure 9-2), and ESR is what actually gates loop stability, not the bare multiplier. Extremely low-ESR ceramic-only output caps are explicitly flagged as a stability risk — this isn't a "any 3.3V-rated 100µF ceramic works" part.
- **Feedforward cap (CFF)** — TI's prose rule requires it when Vout > 10V or Cout has very low ESR. 1c's 9V output is just under that stated threshold, but **TI's own Table 9-6 lists a CFF value (1.5nF) at the 9V row anyway** — their reference table doesn't skip it at 9V despite the >10V prose rule. Include CFF on **1c**, skip it on **1a** (3.3V, well clear of both the rule and the table's practice).

**Catch diode (both instances)** — reverse-voltage rating is the real driver at these input voltages, not current rating (current at 0.2–0.5A load is trivial for any Table 9-4 part):
- Current rating ≥ 1.3× max load current — easily met by anything in Table 9-4.
- **Reverse voltage rating ≥ 1.25× Vin(max)** is the actual constraint. **1a needs ≥45V** (1.25 × 36V), **1c needs ≥40V** (1.25 × 32V). TI's Table 9-4 tops its explicit bracket at "50V or more" (SK35, MBRS360, 1N5825-class, 30WQ05, etc.) — both instances land in that top bracket, not the 20–40V brackets used in TI's own 12V/20V worked examples elsewhere in the datasheet. Schottky preferred over ultra-fast recovery per TI's standard guidance (lower drop, no instability from abrupt turnoff).

**Input capacitor (both instances)** — not the binding constraint at these load currents, included for completeness:
- Low-ESR aluminum electrolytic or tantalum, **voltage rating ≈ 1.5× Vin(max)** (electrolytic) or **2× Vin(max)** (tantalum, TI recommends surge-current-tested parts).
- RMS ripple current rating ≥ ~50% of DC load current (conservative: 75% at higher ambient) — trivially satisfied at 0.2–0.5A.
- **TI explicitly warns ceramic-only input bypassing can cause severe ringing at Vin** — electrolytic/tantalum primary, ceramic only as supplement.

**Bottom line: don't hand-pick exact part numbers/values for these four passives from this doc.** The framework above is real and datasheet-sourced, but both instances' actual operating points (sub-0.6A load, wide Vin) sit outside every one of TI's own quick-reference tables — that's a WEBENCH run or bench-iteration task, not a lookup.

### Input voltage ceiling lowered to 28.8V — unifies both instances, eases the diode

**Decision: continuous/steady-state Vin ceiling lowered from 36V (1a) / 32V (1c) to a single 28.8V for both.** 28.8V is a real, deliberately-chosen number, not an arbitrary derate — it's simultaneously a 24V lead-acid system's float voltage (2×14.4V) and an 8S LiFePO4 pack's full-charge voltage, so it genuinely covers both 24V chemistries this design already commits to supporting. This is a **continuous rating only** — transient spikes above it (load dump, etc.) are handled separately by the front-end TVS (BOM #4), which the buck doesn't need to survive continuously, just be protected from.

Consequence: **catch diode reverse-voltage requirement drops from ≥45V to ≥36V** (1.25×28.8V), landing back inside TI's own literal Fig 9-13 bracket (1N5825-class, 40V). The board-wide SS34 (40V, C8678) meets this ≥36V requirement with ~11% headroom. **Caveat:** with the front-end TVS now removed, load-dump transients above 40V are no longer clamped ahead of the diode — the 28.8V continuous ceiling holds, but transient survival is currently unaddressed (open item).

CIN's voltage-rating requirement drops from ≥54V to ≥43.2V (50V standard step) — **checked whether this unlocks a single higher-capacitance SMD polymer part and it does not**: pulled Nichicon's own PCR-series datasheet directly, and the entire polymer family tops out at 180µF at 50V (lower than hoped, and that specific part is out of stock at LCSC anyway) — capacitance drops as voltage rating rises across the whole technology, so lowering Vin doesn't rescue the "big single polymer cap" approach. See below for how CIN actually got resolved instead.

### CIN/COUT consolidated to one SMD Al-electrolytic part number, 2026-08-02 (corrected same day — cost) — SUPERSEDED, same session, board-space/IC switch

**Superseded by "Buck IC switched to LMR33630" above** — LMR33630 goes all-ceramic on COUT (no bulk cap at all) and needs only a small damping cap on CIN, not a sized-for-storage electrolytic bank. Everything below (including the KNSCHA C3445238 pick) is stale. Kept for record only.

**First pass (superseded within the same session):** consolidated CIN+COUT onto Nichicon PCR1J470MCL1GS (C3274436), a 47µF/63V SMD polymer part, ×2 for CIN / ×3 for COUT. **User flagged this as too expensive** and supplied a cheap SMD Al-electrolytic alternative (Jieerrui MA35V100M6X8, C46550467, 100µF/35V, ~$0.17–0.23/unit) as the style of part to re-source against.

**Voltage-rating check on the user's candidate — it fails the CIN role.** TI's own SNVS124G datasheet (§9.2.2.2.6, adjustable-output worked example) states the input-cap voltage rating must be **≥1.5×Vin(max)**, rounded up to the next standard voltage step — TI's own example: 28V ceiling → 42V required → next step is 50V, so "a 50-V capacitor must be used." At our 28.8V ceiling, that's ≥43.2V → also the 50V step. **A 35V-rated part cannot serve CIN at this design's Vin ceiling, only COUT** (COUT's floor is ≥1.5×Vout = 13.5V for the 9V instance, which 35V clears easily). This is TI's own worked-example arithmetic, not just our restated rule.

**Re-sourced for a cheap 50V+ part that still allows single-part consolidation.** Found several D6.3×L7.7mm SMD Al-electrolytic candidates at 50V — cheaper per-unit than even the user's 35V find:

| MPN | LCSC | Cap/V | Price (1 unit) | ESR / ripple confirmed? |
|---|---|---|---|---|
| DMBJ RVT1H470M0607 | C970679 | 47µF/50V | ~$0.03 | No — catalog-only, ripple listed as 66mA@120Hz, ESR not listed |
| **KNSCHA RX100UF50V90RV0105 (selected)** | **C3445238** | **100µF/50V** | **~$0.11** | No — catalog-only, neither ESR nor ripple listed |
| Panasonic EEEFTH101XAP | C165974 | 100µF/50V | ~$0.30 | **Yes** — 340mΩ ESR, 350mA ripple, real datasheet |

**Decision: KNSCHA RX100UF50V90RV0105, LCSC C3445238, 100µF/50V.** User's explicit call after being shown the gap: neither cheap 50V candidate has a fetchable manufacturer datasheet (both resolve to LCSC catalog-page-only data; DMBJ has no independent datasheet site, KNSCHA's corporate site doesn't publish a part-specific PDF) — ESR and rated ripple current are **unconfirmed** for the selected part, a real Gate 1 gap per hardware-workflow.md ("don't lock BOM while any line carries an open verification item"), accepted anyway on cost grounds rather than paying ~3× for the datasheet-confirmed Panasonic part. Flagged as a bench-verify item, consistent with how this doc already treats other unconfirmed-but-accepted numbers (LM2596S-ADJ light-load efficiency, USB/DC-terminal handoff behavior).

**Sizing, both roles, both instances (1a and 1c):**

| Role | Qty parallel | Total cap | vs. requirement |
|---|---|---|---|
| CIN | ×1 | 100µF | Clears the ~78µF scaled-from-TI target (see "CIN right-sized" below) in a single unit — fewer parts than the old 2× scheme |
| COUT | ×2 | 200µF | Within TI's 82–820µF window, closer to the original 220µF target than the polymer scheme's 141µF |

Net result vs. the first-pass polymer plan: 3 physical caps per buck instance instead of 5 (6 total on the board instead of 10), same single BOM line, and roughly 3-5× cheaper per unit. Voltage margin at CIN (50V vs. a 43.2V requirement, ~16% headroom) is real but tighter than the polymer part's 63V — worth keeping in mind if the Vin ceiling assumption (28.8V) ever moves.

**Open flags, not blockers, both carried to bench test:**
- ESR unconfirmed for C3445238 — TI's qualitative warning against ceramic-only ultra-low-ESR instability doesn't give a hard minimum, so this isn't confirmed problematic, just unverified.
- Rated ripple current unconfirmed for C3445238 — CIN role now runs on a single unit (no parallel-current-sharing margin the way the old ×2/×3 polymer scheme had); confirm the part's ripple rating clears our ~0.1–0.4A rms requirement before this is considered closed.

### CIN right-sized for actual load — not copied from TI's 3A reference example — SUPERSEDED 2026-08-02 (LM2596-specific reasoning; CIN is now a small ceramic + damping cap under LMR33630, see "Buck IC switched to LMR33630")

**The real fix wasn't sourcing harder, it was noticing 470µF was never derived for this design.** That value came from TI's generic Figure 9-13 circuit, sized for their 3A reference load. TI's own input-cap sizing rule is RMS-ripple-current-driven, not a fixed universal number: "select a capacitor with a ripple current rating of approximately 50% of the DC load current" (75% at higher ambient). At 3A that demands real bulk capacitance; at our actual ~0.2–0.5A load, the same rule asks for a cap rated for only ~0.1–0.4A RMS ripple — trivial, and nowhere near what 470µF's worth of bulk is sized to buy. Scaling TI's reference value by load current (470µF × 0.5A/3A ≈ 78µF) lands right around what's already confirmed and in stock.

**Decision: CIN = 2× 47µF/63V in parallel (~94µF total)**, using the already-confirmed **Nichicon PCR1J470MCL1GS, LCSC C3274436** (D10×L8mm SMD polymer, 3,977 in stock). Small footprint, no stock risk, no absurd parallel count, genuinely derived for this load rather than copied from a 3A example. Supersedes both the single 470µF/63V low-stock wet-electrolytic (Lelon C249674, only 10 units) and the earlier 10-caps-in-parallel option (which would have cost ~4× more board area than a single large cap — confirmed by direct footprint math, splitting into many small units is not automatically a space win for cylindrical polymer/electrolytic technology).

### Sourced parts — LM2596S-ADJ passives — SUPERSEDED 2026-08-02, see "Buck IC switched to LMR33630"

Live LCSC/JLCPCB sourcing pass against the locked values above. **Caps are SMD polymer per explicit preference — no THT radial caps below.**

- **L1, 68µH (both instances)**: **KOHERelec MDA1870-680M**, LCSC **C3015595**. 8A rated, 66mΩ DCR, SMD pad-mount 17.8×16.9mm — large-footprint SMD, not a small chip inductor, but board has room. 193 in stock. **Tier unmarked on the fetched page — treat as Extended until verified.**
- **CIN and COUT, single part, both instances**: **KNSCHA RX100UF50V90RV0105**, LCSC **C3445238**, 100µF/50V SMD aluminum electrolytic, D6.3×L7.7mm, ~$0.11/unit, 41,982 in stock at last check, Extended tier. **CIN role: ×1 parallel (100µF)**. **COUT role: ×2 parallel (~200µF)**. Corrected 2026-08-02 (same session) from an earlier Nichicon polymer pick (C3274436, 47µF/63V) on cost grounds — see "CIN/COUT consolidated" decision above for the full re-sourcing pass, the TI voltage-rule check that ruled out a cheaper 35V candidate for the CIN role, and the open ESR/ripple-current verification flag. Supersedes the earlier 470µF-target CIN approach, both original COUT candidates (NJCON 2210350810R00/C5243827, ROQANG RVT1V221M0810/C72498), and the Nichicon polymer consolidation — all retired.
- **CIN bypass, 1µF/100V X7R (both instances)**: **Samsung CL31B105KCHNNNE**, LCSC **C13832**, 1206, 1,204,700 in stock. Added in parallel with the CIN polymer pair for HF decoupling right at the IC pin — standard practice, negligible board cost, does not replace the bulk polymer caps (TI explicitly warns ultra-low-ESR ceramic-only bypassing can cause instability/ringing on this IC family).
- **COUT bypass, 1µF/50V X7R (both instances)**: **Samsung CL21B105KBFNNNE**, LCSC **C28323**, 0805, Basic tier, 880,220 in stock. Same role, output side.
- **D1, catch diode (both instances)**: **MDD (Microdiode Semiconductor) SS34**, LCSC **C8678**, 40V/3A Schottky, SMA(DO-214AC) SMD package, JLCPCB Basic, 2,374,628 in stock. Confirmed live 2026-08-01. Meets the ≥36V requirement (1.25×28.8V) with ~11% headroom and is the exact TI Fig 9-13 bracket part; 3A is far above the 0.2–0.5A buck load. Chosen as the single board-wide diode type (also covers coil flyback if needed), replacing SS56/C65009 — trades SS56's extra reverse margin for one-part-number simplicity and deeper stock. History: earlier used High Diode C466505, then SS56 C65009.
- **Feedback resistors — revised to close-standard E24 values, not the exact-computed 1.69kΩ/6.34kΩ** (those never resolved to a live LCSC listing across two passes). Recomputed Vout for each confirmed-live combo rather than guessing a C-number for an unconfirmed exact value:
  - **R1 = 1.2kΩ, UNI-ROYAL 0805W8F1201T5E, LCSC C17379**, 75,300 in stock. **Used for both dividers** (supersedes the earlier plan to use 1kΩ/C17513 — the 1.2kΩ-based combos below land closer to target on both rails and share one R1 part instead of needing separate values).
  - **1a (3.3V) divider: R2 = 2.0kΩ, UNI-ROYAL 0805W8F2001T5E, LCSC C17604**, 2,515,900 in stock. Vout = 1.23×(1 + 2000/1200) = **3.28V** (0.6% low of 3.3V target — well within margin).
  - **1c (9V) divider: R2 = 7.5kΩ, UNI-ROYAL 0805W8F7501T5E, LCSC C17807**, 378,900 in stock. Vout = 1.23×(1 + 7500/1200) = **8.92V** (0.9% low of 9V target).
  - Both R2 values and the shared R1 are confirmed live with deep stock — no unconfirmed C-numbers left in the feedback network. (The original 1kΩ R1, C17513, is no longer used here, but stays valid as the CAN-termination-resistor's neighbor in the same series if needed elsewhere.)
- **CFF, revised to 1000pF (1nF) C0G, 0805, 50V (1c only)**: **Fenghua 0805CG102J500NT, LCSC C29925**, 117,200 in stock, confirmed live (product page fetched directly, not a search snippet). Replaces the 1500pF target that never resolved to a live LCSC listing after three passes — 1500pF sits in a genuinely thin niche between well-stocked E-series neighbors; 1000pF and 1200pF both resolved immediately once tried. Picked 1000pF over the 1200pF alternative (also confirmed, IHHEC C0805N122J050T, LCSC C105920) for deeper stock (117,200 vs. 4,040) from a more established brand. Slightly below TI's Table 9-6 recommendation for a 9V output, but CFF is a phase-lead stability aid, not a precision-critical value — TI's own table only offers a handful of coarse output-voltage buckets to begin with, so a nearby value is consistent with how imprecise the "requirement" already is. No unconfirmed C-numbers remain in this passive set.

### USB + DC-terminal source arbitration — active disconnect, not diode-OR

Both Domain A power entries (USB-C VBUS and the hardwired DC terminal) land on the same buck's Vin node. If both are connected at once with nothing between them, the higher-voltage source backfeeds into the lower one — DC terminal (12–36V) backfeeding a USB host port through VBUS is the failure case that matters, and it's a real USB-spec violation, not a theoretical one.

**Passive diode-OR was evaluated and rejected**, on numbers rather than a design preference:
- USB-IF's guaranteed device-end VBUS floor under load (~500mA draw point) is **4.4V** — below the LM2596S-ADJ's **4.5V minimum input**, with *zero* diode drop yet subtracted. Margin is already negative at the spec's worst case, before any diode is added.
- Adding a realistic Schottky drop (BAT54C-class part, ~0.6–0.9V at this board's ~0.2–0.3A load — its datasheet only tabulates Vf up to 100mA, so this current point is a graph read, not a tabulated figure) would put the buck's Vin around **3.5–3.8V** on the USB leg — well under the 4.5V floor. A passive diode-OR would only work on typical bench slack (ports commonly deliver closer to 5.0–5.1V), not against the spec guarantee.
- The obvious active fallback, an ideal-diode controller (TI LM74610QDGKTQ1, LCSC C202259), has only 142 units in stock at last check and unconfirmed Basic/Extended tier — not a clean turnkey answer either.

**Decision: active P-FET source-select instead.** A P-channel MOSFET sits in series with the DC terminal's feed into the buck's Vin (downstream of the existing reverse-polarity FET, #5a — a separate FET, not reused, since the two jobs are distinct). Its gate is driven by USB-presence detection: a resistor divider off VBUS into an NPN level-shift stage (the same MMBT2222A + P-FET pattern already qualified elsewhere on this board for the ten coil drivers, not a new topology) pulls the gate toward the DC-side rail and turns the FET off whenever VBUS is present; with no USB connected, a pull-down resistor holds the FET on and the DC terminal feeds the buck normally.

This sidesteps the margin problem entirely rather than working around it — there's no diode Vf to budget for on either leg, because only one source is ever electrically connected to Vin at a time. Trade-off: needs the detection/gate-drive components diode-OR wouldn't have (though built from parts already on this BOM), and introduces a genuine **open question — hot-swap behavior**. If the board is already running from the DC terminal and USB gets plugged in mid-operation, the DC path switches off as VBUS comes up; whether the buck's output capacitance holds 3.3V through that handoff without a brownout is an assumption, not a verified fact, and belongs on the bench-test list before this is considered closed.

### CAN termination and ESD/TVS protection — parts identified this session

Scope for this pass: 120Ω CAN termination resistor, a way to make termination selectable, USB D+/D- ESD protection, and CAN_H/CAN_L ESD/TVS protection. **Note: the authoritative BOM is generated by the KiCad agent from the schematic — the sourcing notes below are inputs to that process, not a BOM table to maintain by hand.** Several tier badges below came back ambiguous from automated fetch ("Economic and Standard" text instead of a clean Basic/Extended badge) — treat those as **unconfirmed**, not Basic, until read live off the JLCPCB part page or BOM-upload tool, per docs/hardware-workflow.md gate 1.

**120Ω CAN termination resistor** — Nexperia SN65HVD230 is one node on the bus; whether ORC is the end-of-bus node depends on install, so termination should be cut-in/out-able rather than hardwired.
- **UNI-ROYAL 0805W8F1200T5E, 120Ω ±1%, 0805** — LCSC **C17437**, **Basic, high confidence** (appears on multiple independently-compiled JLCPCB Basic-parts lists). 555,400 in stock at last check. This is the pick.
- 0603 equivalent exists (LCSC C22787) but pricing/stock weren't independently re-pulled — no reason to prefer it over the confirmed 0805 part.

**Termination jumper mechanism** — two options evaluated:
- **(a) 2-pin header + shunt, in series with the 120Ω resistor.** Header candidate: generic 2.54mm 1×2 THT, LCSC **C36717**, Basic (moderate confidence — tier badge not cleanly re-verified). Shunt candidate: BOOMELE 2.54mm open-top 1×2 jumper cap, LCSC **C5305**, **tier unconfirmed**. Lets a technician physically pull the jumper in the field without a rework station — matches the field-serviceable posture of the rest of this board (screw terminals, etc.).
- **(b) Solder-jumper (SJ) pads, no part.** Standard low-risk PCB practice. **JLCPCB-specific gotcha**: JLCPCB's automated assembly/test does not solder-bridge or verify-by-continuity a cut/bridge SJ pattern as a normal process step — it's a manual field or bench operation either way, so this doesn't get exercised by their standard QC. Doesn't disqualify the approach, just means "populate/cut this pad" has to live in assembly notes, not the BOM.
- **Leaning toward (a)** given the header/shunt parts are as cheap as the SJ savings would be marginal, and it's a cleaner in-field toggle than a solder job on an already-sealed-enclosure product — not locked, revisit once board area is known.

**USB D+/D- ESD protection** — ESP32-S3-WROOM-1U's native USB currently has no protection between the USB-C connector (#6) and GPIO19/20.
- **ST USBLC6-2SC6**, SOT-23-6, LCSC **C7519**. Industry-standard part for this exact role: 2-channel array, 3.5pF max line cap (won't degrade USB 2.0 signaling), 5.25V standoff, 17V clamp, IEC 61000-4-2 Level 4 (15kV air/8kV contact) — appropriate for a connector a technician handles directly. **Tier unconfirmed**, part-selection confidence high.

**CAN_H/CAN_L ESD/TVS protection** — SN65HVD230's bus-side pins currently have no protection at the field-wireable screw terminal (#9), which is a real transient-exposure point (cable run into vehicle chassis), not theoretical.
- **Nexperia PESD1CAN,215**, SOT-23, LCSC **C15771**. Confirmed CAN-specific (not generic TVS) — datasheet explicitly names it for automotive CAN bus line protection: 11pF capacitance (low enough not to hurt 1Mbps signal integrity), 200W peak pulse (8/20µs), 3A peak pulse current, 24V working voltage, 70V clamp, -65 to +150°C. **Tier unconfirmed**, part-selection confidence high.

## MCU: ESP32-S3-WROOM-1U bring-up facts

Cited to Espressif's WROOM-1/1U datasheet and ESP32-S3 Hardware Design Guidelines (schematic checklist + PCB layout sections).

- **-1U-N8**: 8MB flash, no PSRAM (inferred from suffix convention — **verbatim ordering-table row not yet pulled, re-confirm**).
- **External antenna**: U.FL/IPEX (Gen-1 monopole), not the -1's onboard PCB antenna. No PCB-antenna keepout applies to this variant, but keep ground/components clear under and around the U.FL launch and route the feed as controlled-impedance 50Ω. **Exact numeric keepout not extracted — pull Figure 10 / land-pattern dimensions directly before layout.**
- **USB pins**: GPIO19 = USB_D-, GPIO20 = USB_D+. Reserve unpopulated series resistors (22–33Ω) and ground caps near the module; don't attach other circuitry to these pins. Documented ~60µs power-up glitches on both lines (2–3.2ms window) — relevant only if something else pulls on these pins.
- **Decoupling**: 0.1µF at each VDD3P3_CPU/VDD3P3_RTC pin; 10µF at the main 3.3V entrance plus 1µF elsewhere on that rail; VDD_SPI gets its own 0.1µF + 1µF.
- **Strapping**: GPIO0 pull-up (normal boot), GPIO46 pull-down (SPI boot). **GPIO45's required strap level not confirmed this pass — do not assign it to relay/isolator logic until checked**, since it affects VDD_SPI voltage selection at boot.
- **EN/CHIP_PU**: external RC, R=10kΩ/C=1µF typical, must not float, ≥50µs assert/deassert.

### ESP32-S3 current draw — verified, for Domain-A supply sizing

Cited to the ESP32-S3-WROOM-1/1U module datasheet v1.8, Tables 6-2, 6-4, 6-5 (3.3V, 25°C unless noted):

| Figure | Value | Source |
|---|---|---|
| WiFi TX peak, worst case | **355 mA** (802.11b, 1 Mbps, @20.5 dBm) | Table 6-4 — highest of all listed TX rows (297 mA @54Mbps 802.11g, 285–286 mA 802.11n) |
| WiFi RX | 95–97 mA | Table 6-4 |
| Bluetooth LE TX peak | 344 mA (@20 dBm) | Table 6-5 |
| Bluetooth LE RX | 93 mA | Table 6-5 |
| Espressif's own supply floor | **≥0.5 A** (`IVDD` min) | Table 6-2, Recommended Operating Conditions |

Espressif's stated floor (500 mA) already clears the worst documented single-radio peak (355 mA) by ~145 mA. Both buck candidates below (3A rating) give 8.5×+ margin over that peak — comfortable regardless of which one is finalized.

## BOM — sourcing pass results

All parts below came from a live JLCPCB/LCSC catalog search. Basic/Extended status matters for turnkey cost and lead time; several are unconfirmed and flagged accordingly — **do not lock BOM until flagged items are re-verified on the live JLCPCB part page.**

| # | Function | Part | LCSC | Basic/Extended | Confidence |
|---|---|---|---|---|---|
| 1a | Domain A supply, Vin (up to 28.8V) → 3.3V | ~~LM2596S-ADJ~~ → ~~LMR33630~~ → **LMR50410** (final, 2026-08-02, board space) | C2841056 | Extended, confirmed live: 4,643 in stock, $0.43/unit | SOT-23-6 (2.9×1.6mm — ~1/5 the area of the superseded LMR33630 HSOIC-8 pick), 4–36V, 1A, synchronous, single 22µF ceramic COUT (no bulk cap, no feedforward cap ever), fixed 700kHz. Replaces both LM2596S-ADJ (didn't fit) and LMR33630 (fit but not as well as this part). See "Buck IC switched to LMR50410" for the full writeup and open items (part sourcing for CIN/COUT/inductor, PFM-vs-FPWM variant choice not yet resolved). |
| 1c | Coil supply, Vin (up to 28.8V) → 9V | ~~LM2596S-ADJ~~ → ~~LMR33630~~ → **LMR50410** (same part as #1a, final 2026-08-02) | C2841056 | Extended, confirmed live: 4,643 in stock, $0.43/unit | Same part as #1a, one BOM line for both instances. FB divider computed: RFBT=100kΩ/RFBB=12.4kΩ → 9.06V (same VREF as the superseded LMR33630 pick, so this math carried over unchanged). See "Buck IC switched to LMR50410". |
| 2 | I2C isolator (SDA/SCL, 1 part covers the whole barrier) | ADUM1250ARZ-RL7 | C13839 | Extended | High confidence it's the right *kind* of part — 2-ch isolated I2C buffer, open-drain both sides, hot-swap safe, ~2500Vrms per datasheet mirror. Medium confidence on exact isolation-voltage/data-rate figures — full ADI Rev. L table not pulled yet, confirm before lock. |
| 3a | I2C GPIO expander, 16-bit (10 used), Domain B side | PCA9555PW | *verify LCSC #, prior pull gave TSSOP-24 C9900150829 — recheck, that's an unusual LCSC number format* | Extended | Open-drain outputs can't source the P-FET gate directly — needs level-shift stage below. |
| 3b | Per-channel NPN level-shift/gate driver (×10) | MMBT2222A | *not yet verified on live catalog* | Likely Basic, unconfirmed | Pulls high-side P-FET gate low to switch each channel on. |
| 3c | Per-channel high-side P-ch MOSFET, positive-switched coil (×10) | AO3401A | C15127 (same part as #5a — separate line item, separate qty) | Basic, confirmed | ~4A rated, comfortably covers the measured 45mA/coil (room temp). Same reasoning as #5a but this is a distinct application/quantity — don't merge the two BOM lines. |
| 4 | ~~Input protection (TVS, then PTC fuse)~~ | ~~various~~ | ~~—~~ | — | **Removed from scope entirely.** First the TVS (never cleanly re-specified after the two-power-entry split), then the PTC fuse (couldn't find a part clearing both the 28.8V ceiling and real 85°C-derated current margin without a values-vs-values trade-off) — rather than carry either forward as compromised, both are dropped. Input protection on these two DC nodes is now **out of scope for this board** unless revisited later. |
| 5a | Reverse-polarity, logic/isolated side (~1–2A) | AO3401A P-ch MOSFET | C15127 | **Basic, confirmed** | High. |
| 5b | Reverse-polarity, coil-drive side (~1A+ total, all 10 coils) | *not yet selected* | — | — | **Open — AO3401A is undersized for sustained full-coil current in a SOT-23; need a DPAK/SO-8-class automotive P-ch part.** |
| 6 | USB-C connector | Hroparts TYPE-C-31-M-12 | C165948 | Likely Basic (community-cited), **unconfirmed via live catalog** | Medium. |
| 7 | Harness connector, 14-position, board side | 0.1" (2.54mm) unshrouded vertical THT male header | C2977586 (ZHOURI 2.54-1×40, breakable strip) | Extended | Pitch/mount confirmed (see Harness connector section above). Sold as a 1×40 strip, snap to 14 positions. 2.5A/-40 to +105°C rated — clears automotive temp range with margin. |
| 8 | CAN transceiver (Domain A, 3.3V native) | **SN65HVD230** | C12084 | Likely Basic (widely used, **badge not independently confirmed live this pass**) | ISO 11898-2, 3.0-3.6V native supply (no level shifter needed), up to 1Mbps. **Bus operating bitrate locked 2026-08-05: 125 kbit/s** (can-protocol-research.md) — well within this part's 1 Mbps ceiling, chosen for harness noise margin on a low-traffic accessory bus, not a transceiver limitation. Note: this is a high-speed (ISO 11898-2) transceiver run at a low bitrate, not ISO 11898-3 low-speed/fault-tolerant CAN — different physical layer, same number coincidentally. Support components: 100nF bypass at VCC, Rs pin tied per desired slope-control mode, 120Ω bus termination (or split termination: 2× 60Ω to a common node + small cap to ground, per TI app notes, for better common-mode EMI). MCP2551 excluded — 5V-only, would need a level shifter. |
| 9 | DC + CAN terminal block, 4-position, right-angle THT screw terminal, top-accessible screws | **DORABO DB128L-5.08-4P-GN-S** (selected) | C2827883 | Extended, confirmed | **Pivoted away from push-in spring entirely** — after two rejected spring-terminal attempts (side-actuator R-mount, then top-actuator-but-vertical-wire-entry V-mount), neither matched "right-angle wire entry + top-accessible actuator." Screw terminals sidestep the problem: screwdriver access from above regardless of wire-entry direction. **Mechanically verified directly from DORABO's own datasheet drawing** (not just the listing): body flat/horizontal, screw heads face up for top-down access, wire entry through the front face parallel to the board, pins bend to enter the PCB at a right angle — matches the requirement exactly. 5.08mm pitch, 16A/300V, 12-22AWG wire, -40 to +105°C, M2 slotted/Phillips screws, 28k+ units in stock. **No Basic-tier right-angle fixed screw terminal block exists at all** — checked ~25 parts across DORABO/DEGSON/Kangnex, every one in this category is Extended; not a gap specific to this part. |

## Open items before schematic capture

- [ ] **Verify SN65HVD230 (#8) Basic/Extended status directly on the live JLCPCB catalog** — not confirmed this pass. Terminal block #9 (DB128L-5.08-4P-GN-S) is confirmed Extended — no Basic option exists in this connector category at all.
- [ ] **CAN is provisional — descope if it doesn't comfortably fit the board** (user's own caveat). Revisit once board area is known from layout.
- [ ] Flyback diode presence on the relay board — still open, harness pinout doesn't resolve this; needs bench inspection or a continuity/diode check across a coil's harness pins. **If ORC needs to supply them: reuse SS56 (C65009, already qualified for the buck catch-diode role)** rather than sourcing a new part — its 60V/5A rating is far more than the ~9V/45mA-per-coil flyback duty needs, and reusing it keeps the design at 4 distinct diode types instead of 5. Trade-off: SS56's SMA(DO-214AC) package is larger than a minimal SOD-123 part would need for this current level — accepted for one fewer BOM line, not free board-area-wise.
- [x] Coil current — **measured: 45mA/coil at 9V, room temperature, reliable across all 10 channels** (user-measured). Implies coil resistance ≈ 200Ω. Total at 9V with all ten energized: 0.45A. Coil resistance rises with temp, so this room-temp figure likely sits near the higher end of the real current range, not a worst-case-low estimate — reasonable working number, see design-inputs.md.
- [ ] Tyco relay part number and whether all ten are identical — carried from design-inputs.md.
- [ ] Board outline, mounting holes, Z-height clearance, gland positions — carried from design-inputs.md, unaffected by this pass.
- [ ] Confirm ADuM1250 exact isolation-voltage and max-data-rate figures against ADI's Rev. L datasheet table (partial fetch only so far).
- [ ] Verify PCA9555PW's LCSC number directly on the live catalog — the number returned in an earlier pass doesn't match LCSC's normal format, treat as unverified.
- [ ] Verify MMBT2222A's LCSC number and Basic/Extended status.
- [ ] Decide gate-drive sequencing/pull-up needs for the PCA9555 open-drain outputs → NPN → P-FET chain, once the coil current figure above is confirmed measured (affects turn-on time, not just steady-state rating).
- [x] Input TVS (load dump) removed from scope — see BOM #4. No longer tracked as an open item.
- [ ] Select the coil-drive-side reverse-polarity MOSFET (DPAK/SO-8 class, sized to total measured coil current once that's known).
- [ ] Confirm USB-C connector Basic/Extended status.
- [ ] Pull exact U.FL keepout / land-pattern dimensions (Figure 10, WROOM-1/1U datasheet) and GPIO45 strap requirement before assigning GPIOs or laying out the antenna area.
- [ ] Re-confirm -1U-N8 = 8MB flash / no PSRAM against the literal ordering-table row.
- [ ] Confirm LM2596S-ADJ's real-world dissipation at the ~0.5A operating point on the bench once a prototype exists — no light-load efficiency figure exists in TI's datasheet, don't assume best-case.
- [ ] Verify Basic/Extended tier live on JLCPCB for all four new parts this pass: 120Ω resistor (C17437, likely Basic — high confidence), 2-pin header (C36717) + shunt (C5305), USB ESD array (C7519), CAN ESD array (C15771) — automated fetch returned ambiguous tier text for the latter three, don't assume Basic.
- [ ] Decide termination-jumper mechanism: header+shunt (C36717/C5305) vs. solder-jumper pads — leaning header+shunt for field-serviceability, not locked.
- [ ] Confirm CAN termination is only populated/needed if ORC is the end-of-bus node — install-dependent, affects whether the jumper ships populated or open by default.
- [ ] **Bench-verify the USB/DC-terminal handoff doesn't brownout the 3.3V rail** — the P-FET source-select disconnects the DC terminal path the instant VBUS is detected; whether the buck's output cap holds 3.3V through that transition (or the reverse: USB unplugged while DC terminal is present) is assumed, not measured.
- [ ] Size the gate-drive divider/NPN stage for the USB-presence-detect source-select FET — same topology as the coil-driver chain (MMBT2222A + P-FET) but a new instance, values not yet worked out.
- [ ] Re-confirm USB-IF's 4.4V worst-case VBUS floor against the primary usb.org spec text (§7.2.2) — the figure used to reject diode-OR came from secondary sources, not the spec PDF itself (fetch was blocked this pass).
- [x] Source live LCSC/JLCPCB parts for L1, D1, R1, and COUT (with a polymer/wet-electrolytic choice) — see "Sourced parts" above. **Still open**: pin live LCSC C-numbers for R2 (1.69kΩ and 6.34kΩ, 0805 1%) and CFF (1.5nF C0G 0805) — both confirmed to exist as parts but not yet confirmed as live LCSC listings after two search passes; needs a direct LCSC.com check, not another automated pass.
- [ ] **Confirm ESR and rated ripple current for C3445238 (KNSCHA 100µF/50V)** before this line is considered closed — both are catalog-page-only, no independently fetchable manufacturer datasheet found this pass. Ripple current matters most for the CIN role, which now runs on a single unit with no parallel-current-sharing margin. Try requesting the datasheet directly from KNSCHA (contacts found: cathy@knscha.com, Vivan.liu@knscha.com) or pulling LCSC's PDF via a browser session rather than programmatic fetch. See "CIN/COUT consolidated" decision, 2026-08-02.
- [x] CIN stock risk resolved — superseded the 10-unit-stock 470µF/63V part entirely by right-sizing CIN to ~94µF (2× the deep-stock 47µF/63V C3274436, 3,977 units) once it was clear 470µF was copied from TI's 3A reference example rather than derived for our actual ~0.2–0.5A load. No further action needed here.
- [ ] Verify Basic/Extended tier for L1 (C3015595) and all newly-sourced passives above — none had a clean tier badge in this pass's automated fetch.
- [x] Input protection (TVS and PTC fuse both) — removed from scope entirely, see BOM #4. The 85°C-derating chase (2920L075/60 → 2920L110/60 → 2920L150) is documented in hardware-workflow.md as a general lesson even though the part itself didn't ship.
