# ORC design inputs

What carries forward from the PMUN1046A teardown, what has to be built from scratch, and what still needs measuring before layout.

Source of truth for the teardown itself is the writeup in
[PMUN1046A_RE](https://github.com/aramder/PMUN1046A_RE) — specifically its
`docs/pmun1046a-relay-controller.md`. Facts are cited back to it rather than duplicated here; where a figure is **unverified**, it says so.

## Inherited from the donor unit

All of this is genuinely useful and is why the donor unit is worth building around.

| Item | Part | Notes |
|---|---|---|
| Relay board | PMLN5639_ | **Entirely passive.** Ten Tyco relays, ten 15 A fuses, terminal blocks, power lugs, busbar. No active parts, no drivers, no regulator. |
| Wire harness | 0975931M01 | Being reused as-is. Carries power and coil drive, not logic-level commands. |
| Chassis | — | Sealed, metal, trunnion-mounted. Cable glands with per-wire radial gaskets. |
| Channel rating | — | 15 A per output, matching the fuse and relay rating. |
| Input feed | — | Busbar, one or two cables, **60 A maximum per cable** — the capability of the busbar, lugs, and glands. |

### On the 60 A / 16 A / 15 A figures

These describe three different things and are often conflated. They are all correct:

| Figure | What it is |
|---|---|
| **60 A per cable, ×2** | Capability of the input path — busbar, lugs, glands |
| **16 A** | Motorola's *specified* installation breaker, part 40012006001 |
| **15 A per output** | Per-channel fuse on the relay board |

**The circuit breaker is external, vehicle-side hardware** — installed inline between the URC's power lug and the battery ("AUX" end to the URC, "BAT" end to the supply). There is no breaker inside the enclosure, and ORC does not need to provide one. Input protection remains an installation choice, sized to the actual load and cable, and is not an ORC board-level concern.

### ORC's board does not carry the high-current path

Worth stating plainly, because it substantially simplifies the design: the load current never touches the controller PCB. It runs **busbar → relay contact → fuse → output terminal**, entirely on the passive relay board.

ORC's board only needs a **low-current A+ tap** for the 9 V coil supply and its own logic. The current there is bounded by ten relay coils plus the ESP32 — not by the 60 A input capability or the 150 A theoretical contact total. The "automotive input transient protection" line below refers to that low-current control tap.

### Inherited per-channel capability, and what limits it

Bench inspection 2026-07-31. **Inspection findings and estimates, not instrumented measurements.**

| Element | Finding | Confidence |
|---|---|---|
| Tyco relays | **40 A rated parts** — far above the 15 A system rating | Observed (part rating) |
| A+ busbar | Good for roughly **150 A total in short bursts**, both cables fed | Estimate, not measured |
| PCB traces, fuse positions, terminal blocks | **The actual limiting factor**, not the contacts | Observed |
| Relays **9 and 10** | Only two channels with convenient room for heavier copper, by layout accident | Observed |

Uprating a channel is disproportionately expensive: IPC-2221's external relation (`I = 0.048 × ΔT^0.44 × A^0.725`) inverts to **area ∝ I^1.38**, so 15 A → 25 A needs ~2.0× the copper and 15 A → 40 A needs ~3.7×.

If a channel ever needs uprating, note that **solder lumping is weak and bonded wire is strong** — solder runs ~8× copper's resistivity (SAC305 ~13 µΩ·cm, Sn63Pb37 ~15, Cu 1.72), so a generous fillet buys only ~1.25× conductance, while 12 AWG (3.31 mm², ~5130 mils²) is ~14× a 2 oz/128 mil trace. Best is a conductor from busbar straight to the relay terminal, bypassing the trace.

Two caveats that make trace work pointless on its own:

- **The fuse clip and terminal block don't improve with copper.** The chain is busbar → trace → contact → trace → fuse clip → fuse → trace → terminal block → output. Verify those two are rated for the target before reinforcing anything upstream.
- **The enclosure is sealed**, so heat only leaves through the chassis. Validate any uprating with a **thermal soak, lid on** — not an open-bench measurement.

## Requirements this places on ORC

**Channel mapping must be configurable, not hardcoded.** The ten channels are *not* electrically identical — 9 and 10 have headroom the rest don't. Function-to-channel assignment therefore belongs in configuration, so a high-draw load can be placed on a capable channel without rewiring.

rigOS already has hooks for relay number assignment, so the platform side of this is solved; ORC's firmware needs to expose channel identity and accept the mapping rather than assuming a fixed layout.

Open, not yet a requirement: **per-channel current sensing.** If channels differ in capability, knowing actual draw would let firmware enforce limits rather than trusting configuration. Costs board area and BOM — worth deciding deliberately rather than defaulting either way.

## To be rebuilt

Everything below lived on Motorola's controller board and is lost when we replace it. This is a real power design, not a microcontroller swap.

- **9 V coil regulator**, sized for all ten coils energized simultaneously. Motorola ran coils at 9 V rather than 12 V specifically to cut coil heat dissipation (service manual 3.7.4) — worth preserving that choice.
- **Ten relay drivers**, plus flyback if the relay board does not already carry it.
- **Automotive input transient protection** on the 12 V feed. Load dump is not optional in a vehicle.
- **Isolation barrier, if retained** — see the open decision below.

## Open architecture decisions

### Isolation

Motorola isolated MCU logic from coil drive with ADuM1410 digital isolators. The reason is sound: coil-drive ground referenced the relay board and vehicle chassis, while MCU ground arrived from the radio over USB — two references that can differ meaningfully during high-current switching.

If ORC's controller and the relay board share a supply and a solid star ground, the case for isolation is weaker. This is a deliberate decision to make, **not one to skip by default**. Isolation also costs channel count, board area, and an isolated supply for the coil-side logic.

### Primary control path

The ESP32-S3 gives WiFi essentially for free, but that is not automatically the right primary link:

- **Warning/emergency lighting** → a wired primary path (**CAN** or **USB**) is defensible; association dropouts and latency are poor failure modes. WiFi carries configuration and monitoring.
- **Accessory/camp/work lighting** → that argument largely evaporates and WiFi-primary is reasonable.

Decide which this unit is for before committing, since it changes whether a CAN transceiver is on the BOM.

### Antenna

The chassis is metal and sealed, so an on-module PCB antenna will not radiate out. Routine to solve: use an external-antenna module (**ESP32-S3-WROOM-1U** or equivalent, U.FL/IPEX) with a pigtail to an **O-ring-sealed bulkhead SMA** at one of the existing gland positions.

Pick the module variant and bulkhead part **before layout** — U.FL placement constrains the board outline.

## Outstanding measurements

Nothing below is assumed. These gate the schematic.

### Harness header — the critical interface

**Resolved: plain 0.1" (2.54mm) pitch, unshrouded, vertical male header** — not the shrouded 3.96/4.20/5.08mm part guessed from the initial by-eye inspection. The candidate-family table below is now superseded; kept for record.

Superseded candidate table (initial by-eye guess, before the actual 0.1" header was confirmed):

| Pitch | Family | Notes |
|---|---|---|
| 3.96 mm (0.156") | Molex KK 396 / 5273 | Common at automotive coil-level currents |
| 4.20 mm | Molex Mini-Fit Jr | Higher current capability |
| 5.08 mm (0.2") | Various | Less likely at this pin count |

- [x] **Pitch.** **0.1" (2.54mm), unshrouded, vertical, THT male header.** LCSC **C2977586** (ZHOURI 2.54-1×40, breakable strip, snap to 14 positions), Extended, 2.5A rated, -40 to +105°C — clears automotive temp range with margin. See circuit-draft.md BOM #7.
- [x] **Pin count and pinout mapping.** **14 positions, confirmed by count**, not the guessed 12–13:

  | Pin | Function |
  |---|---|
  | 1 | Coil common return ("coil −", shared across all 10 channels) |
  | 2 | A+ |
  | 3–12 | Coil 1+ … Coil 10+ (one per channel) |
  | 13, 14 | Chassis (relay-board side) |

- [x] **High-side vs low-side switching.** **Confirmed high-side** — individual "Coil N+" per channel with one shared common return, matching the positive-switched decision already locked in circuit-draft.md independently of this measurement.
- [ ] **Flyback diode presence on the relay board.** Still open — the pinout doesn't resolve this; still needs bench inspection or continuity/diode check across a coil's harness pins.
- [x] **Does the harness carry A+ up to the controller board, or is A+ tapped separately from the busbar?** **Confirmed: pin 2 is A+, carried by the harness.** ORC's board connector needs to carry the A+ feed directly — no separate lug/tap required.

### Ground topology — dismissed, not a concern

Pin 1 (coil common) and pins 13/14 (chassis) are three separate wires on the connector, not one labeled net. Flagged earlier as a possible star-grounding question given the isolation architecture — **user call: not important, tie them together as one common ground.** No special handling needed on Domain B's ground plane.

### Coil and thermal

- [x] **Coil current — measured: 45mA per coil at 9V, room temperature, reliable across all 10 channels** (user-measured). Implies coil resistance ≈ 200Ω (not independently verified via direct DMM reading, backed out from V/I). Total current with all ten energized: 0.45A at 9V (~4.05W) — sizes the 9V switcher, see circuit-draft.md. Coil resistance rises with temperature, so this room-temp figure is likely near the higher end of the real operating current range, not a worst-case-low estimate.
- [ ] **Total coil current checked against harness and ground-path ratings** — the 0.45A figure above is well within any plausible harness rating, but hasn't been explicitly cross-checked against the harness pin gauge/rating.
- [ ] **Tyco relay part number**, and whether all ten are identical single-pole parts.

### Mechanical

- [ ] **Board outline, mounting hole positions, Z-height clearance.** The stock controller board is retained by two T10 Torx screws; ORC has to match that footprint and clear the chassis and thermal pads.
- [ ] Gland positions and which one can host the antenna bulkhead.

## Consumables

Single-use parts — order spares per unit opened:

- **Light Bar Gasket** 3278310A01 (×2 per unit)
- **Thermal pad** 75012026001 (×2 per unit)

Reassembly torques and the full disassembly sequence are in the PMUN1046A_RE writeup. The main O-ring must not be pinched or the enclosure loses its seal.
