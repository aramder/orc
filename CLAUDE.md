# ORC — agent instructions

Open replacement controller board for surplus Motorola PMUN1046A relay boxes. See [README.md](README.md) for project overview. **This repo is public.**

## Before touching hardware/BOM/schematic work

Read [docs/hardware-workflow.md](docs/hardware-workflow.md) first. It sets three gates:

1. **Part selection** — live distributor catalog only, never from memory. Confidence-tag every line. Don't lock the BOM on unverified parts.
2. **Asset-before-draw** — datasheet + footprint/pinout confirmed before a part goes on the schematic.
3. **Check twice** — `kicad-cli sch erc` / `pcb drc` clean, SPICE sanity where behavior matters, manual netlist cross-check before anything ships. A failed gate gets fixed or re-opened as a checklist item — never routed around.

## Source of truth

- [docs/design-inputs.md](docs/design-inputs.md) — settled facts: what's inherited from the donor unit, what's measured, what's still open. Cite it, don't restate it.
- [docs/circuit-draft.md](docs/circuit-draft.md) — live decisions log + BOM. Update in place as decisions land; don't fork a parallel notes file.
- README.md **Status** section — one-line current phase. Update it when phase changes.

## Repo layout

`docs/` = specs and decisions. `hardware/` = KiCad project. `firmware/` = PlatformIO/Arduino. `tools/` = host-side CAN/USB bench scripts. The teardown notes live in a separate **private** record — don't pull its content in here, don't link it, and don't name the repo; refer to it only as the private teardown record.

## Publishing rules

This repo is public and the design was reverse-engineered from a Motorola product. These are not style preferences — they are what keeps the project's legal posture clean.

- **Never reproduce Motorola content.** No manual text, tables, drawings, figures, or photographs, even with attribution. Cite facts by section (`service manual 3.7.4`) instead. Facts, specs, and part numbers are safe; expression is not.
- **Never use Motorola marks as branding.** No logo, no stylized wordmark, no Motorola styling in banners, badges, or social preview images. Naming the donor unit in body prose is nominative use and is fine; keep it to the minimum needed to identify the unit.
- **Don't imply affiliation or endorsement**, and don't name ORC's own parts or protocol after Motorola's.
- **Don't implement, document, or emulate the GCAI link.** Replacing it rather than speaking it is deliberate and load-bearing — see the provenance section in [docs/design-inputs.md](docs/design-inputs.md).
- **Don't accept any Motorola agreement** — no accessory-program enrollment, no CPS/firmware click-through, no NDA — without flagging it first. Doing so would import contract obligations the project currently does not have.
- Keep [TRADEMARKS.md](TRADEMARKS.md), [DISCLAIMER.md](DISCLAIMER.md), and [NOTICE](NOTICE) accurate when scope changes.

## Licensing

Split by directory; full texts in [LICENSES/](LICENSES/), plus a `LICENSE` in each directory.

| Path | License |
|---|---|
| `hardware/` | CERN-OHL-W-2.0 |
| `firmware/`, `tools/` | Apache-2.0 |
| `docs/` | CC-BY-4.0 |

New source files get an `SPDX-License-Identifier:` header matching their directory. Hardware and software stay separately licensed — the ESP32-C3 is an Available Component under CERN-OHL, so the hardware license does not reach firmware.

## Local-only, never commit

`.claude/` is gitignored in full (session prompts, permissions, the KiCad MCP logbook). Keep working notes there, not in `docs/`.
