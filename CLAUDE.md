# ORC — agent instructions

Open replacement controller for the Motorola PMUN1046A relay box. See [README.md](README.md) for project overview.

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

`docs/` = specs and decisions. `hardware/` = KiCad project. `firmware/` = PlatformIO/Arduino. `PMUN1046A_RE` is a separate, archival repo — don't pull its content in here, cite it.
