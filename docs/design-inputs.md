# ORC design inputs

What carries forward from the PMUN1046A teardown, what has to be built from scratch, and what still needs measuring before layout.

Source of truth for the teardown itself is the writeup in
[PMUN1046A_RE](https://github.com/aramder/PMUN1046A_RE) — specifically its
`docs/pmun1046a-relay-controller.md`. Facts are cited back to it rather than duplicated here; where a figure is **unverified**, it says so.

## Inherited from the donor unit

All of this is genuinely useful and is why the donor unit is worth building around.

| Item | Part | Notes |
|---|---|---|
| Relay board | PMLN5639_ | **Entirely passive.** Ten Tyco relays, ten 15 A fuses, lightbar terminal blocks, power lugs, busbar. No active parts, no drivers, no regulator. |
| Wire harness | 0975931M01 | Being reused as-is. Carries **switched 9 V coil drive**, not logic-level commands. |
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

Confirmed by inspection: **generic shrouded, non-keyed male header.** Pitch measured by eye as **>3 mm — not yet confirmed with calipers.**

Candidate families to check against:

| Pitch | Family | Notes |
|---|---|---|
| 3.96 mm (0.156") | Molex KK 396 / 5273 | Common at automotive coil-level currents |
| 4.20 mm | Molex Mini-Fit Jr | Higher current capability |
| 5.08 mm (0.2") | Various | Less likely at this pin count |

- [ ] **Pitch, with calipers.** Picks the connector family and the PCB footprint.
- [ ] **Pin count and pinout mapping.** Expect ten switched coil legs plus a common 9 V feed and return(s) — roughly 12–13 positions. Confirm by counting and identify each position.
- [ ] **High-side vs low-side switching.** Sets driver topology.
- [ ] **Flyback diode presence on the relay board.** Changes the driver design.
- [ ] **Does the harness carry A+ up to the controller board, or is A+ tapped separately from the busbar?** The 9 V rail derives from A+, but the route is not established. This determines whether ORC's board connector needs to carry the A+ feed or whether a separate lug/tap is required.

### Coil and thermal

- [ ] **Coil resistance**, measured directly across the harness — the relay board is passive, so this is a straight DMM reading. **This sizes the 9 V switcher. No value should be assumed for it.**
- [ ] **Total coil current with all ten energized**, checked against the harness and ground-path ratings. This is the worst case.
- [ ] **Tyco relay part number**, and whether all ten are identical single-pole parts.

### Mechanical

- [ ] **Board outline, mounting hole positions, Z-height clearance.** The stock controller board is retained by two T10 Torx screws; ORC has to match that footprint and clear the chassis and thermal pads.
- [ ] Gland positions and which one can host the antenna bulkhead.

## Consumables

Single-use parts — order spares per unit opened:

- **Light Bar Gasket** 3278310A01 (×2 per unit)
- **Thermal pad** 75012026001 (×2 per unit)

Reassembly torques and the full disassembly sequence are in the PMUN1046A_RE writeup. The main O-ring must not be pinched or the enclosure loses its seal.
