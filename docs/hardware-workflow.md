# Hardware workflow — gates, not automation

Practical subset of [PCB-Agent-Teams](https://github.com/Zane456/PCB-Agent-Teams)'s discipline, adapted for this repo: manual KiCad on Windows, US sourcing, no Claude Code skills. The value there isn't the automation — it's the checkpoints that stop a board from shipping on an invented part number or an unverified footprint. Apply these by hand.

## Why not the framework itself

- **Windows has no `pcbnew` Python interpreter path.** `draw-pcb`'s scripted board generation has nothing to call here. `kicad-cli`'s `sch erc` / `pcb drc` subcommands are plain CLI, not scripted-`pcbnew`, and do work cross-platform — that part is worth keeping (see Gate 3).
- **Part-selection locales are Japan and China-mainland only.** No US variant. Digi-Key/Mouser US sourcing stays manual.
- Schematic/PCB capture itself stays in the KiCad GUI, not source-generated from Python.

## Gate 1 — part selection: nothing from memory

Every part number in [circuit-draft.md](circuit-draft.md)'s BOM table already gets this treatment; keep doing it:

- Pull from a live Digi-Key / Mouser / LCSC catalog page, not recollection. Cite the distributor part number (LCSC `C######`, Digi-Key/Mouser SKU).
- Record Basic/Extended (or equivalent turnkey-cost) status, and flag it **unconfirmed** if the live page wasn't actually checked this pass.
- Tag every line with a confidence level (High / Medium / Low, or "not yet verified on live catalog") — don't let a guess read the same as a checked fact.
- **Don't lock the BOM** while any line carries an open verification item. Keep those in the "Open items before schematic capture" checklist until closed.

## Gate 1 in practice — the sourcing-pass pattern

Worked out over the LM2596 passives / diode / PTC sourcing rounds in circuit-draft.md. Keep using this shape:

- **Dispatch one focused sourcing task per part or tightly-related group**, not one giant "find everything" pass — narrow asks come back with real LCSC C-numbers and live stock counts; broad ones come back vague. Ask explicitly for: part number, manufacturer/MPN, exact spec match, package, Basic/Extended tier, live stock count.
- **LCSC's search is JS-rendered and doesn't scrape reliably.** When a sourcing pass comes back "not found" for an obviously-common part (a standard E96 resistor, a common ceramic cap value), retry once with a different search term or a nearby standard value — that resolved real gaps twice this session (1.69kΩ/6.34kΩ → 1.2kΩ/2.0kΩ/7.5kΩ; 1500pF → 1000pF). If a second attempt still comes up empty, **stop and flag it for a direct manual LCSC.com check** rather than burning further automated passes against the same wall — three failed attempts at the exact same value is a tooling limit, not a part that doesn't exist.
- **Basic/Extended tier badges specifically don't render to automated fetch, even when the rest of the page does.** Hit on 4 out of 4 parts in one sourcing round (2026-08-01: inductor, NPN transistor, both P-FETs, I2C expander) — stock count, spec, and part identity all came back clean, but the Basic/Extended badge itself was consistently missing from the fetched text on both LCSC's and JLCPCB's part-detail pages. Treat this as a standing tooling limit, not a one-off: **tier claims from an automated sourcing pass are circumstantial (inferred from price/stock patterns or third-party Basic-parts-list mirrors) until a human does one direct visual check on the actual page.** Don't let a circumstantial tier read as a confirmed one in the BOM.
- **"Not found" is a reportable outcome, not a failure to hide.** Every sourcing summary in this doc distinguishes confirmed-live parts from parts that are real (confirmed via manufacturer datasheet or third-party distributor) but not yet pinned to a live LCSC listing. Never let an unconfirmed C-number read the same as a checked one.
- **Re-verify against the primary datasheet directly when a figure is load-bearing**, not a mirror or an agent's paraphrase. The LM2596 inductor/cap selection guidance was first sourced from a third-party mirror with the right shape but unconfirmed figure numbers; downloading and reading TI's actual SNVS124G PDF directly caught the difference (adjustable-version nomograph is E·T-based, not raw-Vin-based) before it became a wrong component pick.
- **Don't copy a reference design's values past the range they were derived for.** TI's Figure 9-13 generic circuit is a legitimate starting point, but its 470µF input cap was sized for a 3A example load — copying it verbatim for a ~0.2–0.5A design would have forced an unsourceable 470µF/63V polymer part into existence. Re-deriving from TI's own sizing rule (RMS ripple current ≈ 50–75% of DC load) for the *actual* load gave a right-sized, easily-sourced value instead. When a generic circuit's component value was clearly picked for a specific operating point, check whether this design's operating point actually matches before reusing the value.
- **When a design decision earlier in the session leaves a BOM line stale, say so and fix it — don't leave it silently orphaned.** The load-dump TVS BOM line was written against a single-power-entry architecture; when the design later split into two separately-isolated power entries, nobody had gone back to re-specify which node the TVS protected. Caught it, flagged it explicitly as a gap (not just an omission), and it ended up removed by user decision rather than carried forward unresolved.

## Gate 1 — BOM line-count discipline

This project's scale (small-batch turnkey, not a cost-optimized high-volume design) makes fewer distinct part numbers worth real trade-offs elsewhere. Applied twice this session:

- **Consolidate when the reused part clears the new role's requirement with real margin to spare.** SS56 (60V/5A Schottky, already qualified as the buck catch diode) was reused for the coil flyback-diode role instead of sourcing a new part — a flyback diode across a 9V/45mA coil needs a fraction of SS56's rating, so reuse costs only some board area (SMA/DO-214AC is bigger than a minimal SOD-123 would need), not function.
- **Don't consolidate when the parts' selection criteria diverge enough to defeat the part's purpose.** Evaluated reusing one PTC resettable fuse across two DC input nodes with different steady-state currents (~0.2A and ~0.45A) — the lower-hold-current candidate (0.5A) was too tight against the higher-current node to avoid nuisance trips, so the shared part had to be picked to satisfy the *more demanding* requirement (0.75A hold), accepting looser fault-current precision on the lighter-loaded node as the explicit trade. Still one BOM line, but the choice of *which* part to standardize on was driven by the harder constraint, not convenience.
- When genuinely unsure which way a consolidation cuts, surface the trade-off (AskUserQuestion or a flagged note) rather than picking silently — the PTC case above was a real judgment call, not an obvious answer.

## Gate 1 — a rated spec at 25°C isn't the spec that matters in a sealed enclosure

This board lives in a sealed automotive enclosure (heat only leaves through the chassis, 65–85°C ambient expected — see design-inputs.md). Any component whose datasheet headline number is a 25°C (or 20°C) rating needs its actual temperature-derating curve checked before that number gets treated as real margin. This bit twice in one pass: the PTC resettable fuse initially selected (Littelfuse 2920L075/60MR, 0.75A hold at 20°C, 1.67× nominal margin over the 0.45A load) actually derates to **0.34A at 85°C — below the load it was supposed to protect**, per Littelfuse's own temperature-rerating table. The 25°C number alone would have shipped a part that nuisance-trips or fails to protect at the enclosure's actual worst-case operating temperature.

- **For any thermally-sensitive part** (PTCs, anything with a current or power rating that's fundamentally a self-heating limit — regulators, fuses, connectors with contact-resistance heating) **pull the manufacturer's actual temperature-derating table before treating a 25°C-rated margin as real.** Don't extrapolate a percentage — read the actual printed row for the actual part number; derating curves are not linear or consistent across a manufacturer's own family (confirmed directly this session: Littelfuse's 2920L075/60 and 2920L110/60 don't derate by the same fraction).
- **Voltage and current ratings trade off within a family, and relaxing one to fix the other is itself a decision to record, not a free upgrade.** Moving from a 60V-rated PTC to a 33V-rated sibling in the same series fixed the thermal-current margin (0.34A → 0.74A @85°C) but dropped the voltage margin over this design's 28.8V ceiling from 108% to ~15% — both numbers belong in the BOM note, not just the one that got fixed.
- This generalizes past PTCs: any time a spec-sheet number is a room-temperature figure and the part lives in this enclosure, ask "what does this number actually do at 85°C" before locking it.

## Gate 2 — nothing drawn until the asset is on disk

Before a part goes into schematic capture:

- Datasheet fetched and the number actually cited against it (not "seems about right").
- Footprint/symbol confirmed to exist and match the part's real package — THT vs. SMD, actual pin count vs. datasheet pinout, no generic-symbol-standing-in-for-a-real-part.
- Part number and package written into the BOM table before it's placed on the schematic, not entered from memory while drawing.

This is already how `design-inputs.md` §"Harness header" and `circuit-draft.md` §"MCU bring-up facts" operate — datasheet-cited, checkbox-gated. Extend that same treatment to every BOM line before schematic capture starts, not just the ones that already got scrutiny.

## Gate 3 — checked twice, two different ways

Once `hardware/` exists and schematic/PCB capture starts:

**Schematic stage**
- ERC clean via `kicad-cli sch erc` (CLI-only, no `pcbnew` needed — works on Windows).
- SPICE sanity check via ngspice where a subcircuit's behavior matters (e.g. the buck regulator's feedback divider, the coil-drive gate stage) — not just connectivity.
- Manual datasheet pin-cross-check: every net that touches a part's pin gets checked against that part's actual pinout table, not assumed from the symbol.

**PCB stage**
- DRC clean via `kicad-cli pcb drc` (same CLI-only path, Windows-safe).
- Manual schematic↔PCB cross-check before release: walk the netlist and confirm nothing shorted or dropped in layout. `draw-pcb`'s automated version needs `pcbnew`; the manual version is slower but does the same job.
- Thermal/EMC stays a design-inputs-style written analysis (see design-inputs.md §"Inherited per-channel capability") rather than the framework's automated 44-rule pass — no tooling for that exists here yet.

**Any gate failure rolls back.** Don't route around a failed ERC/DRC run or an unverified part by shipping anyway — fix it or explicitly re-open the item in the checklist.

## BOM split (for later — release stage)

Keep procurement and production BOM as separate artifacts once `orc-hardware`-equivalent (`hardware/`) work starts generating them:

- **Procurement BOM** — what you actually order, from the sourcing pass above.
- **Production BOM / CPL** — what goes to a fab/assembly house, generated from the final layout, not hand-copied from the procurement list.

Not relevant yet at the pre-schematic stage this repo is in, but worth keeping separate from day one rather than untangling later.

## Doc structure already in use here

This repo already runs something close to PCB-Agent-Teams' three-layer split, just without the name:

| This repo | Framework equivalent | Role |
|---|---|---|
| [design-inputs.md](design-inputs.md) | `Projects/<name>/CLAUDE.md` | Static compass — what's inherited, what's measured, what's still open |
| [circuit-draft.md](circuit-draft.md) | working schematic-stage notes | Live decisions log + BOM, updated per session |
| README.md status line | `STATUS.md` | One-line current phase |

No new file needed for this — keep using design-inputs.md for settled facts and circuit-draft.md for in-progress decisions, the way both already do.
