<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/media/banner-dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="docs/media/banner-light.svg">
  <img alt="ORC — Open Relay Controller" src="docs/media/banner-light.svg" width="100%">
</picture>

[![status](https://img.shields.io/badge/status-pre--schematic-orange?style=flat-square)](docs/design-inputs.md)
[![controller](https://img.shields.io/badge/controller-ESP32--S3-blue?style=flat-square)](#what-it-is)
[![channels](https://img.shields.io/badge/channels-10_%C3%97_15A-2da44e?style=flat-square)](#what-it-is)
[![donor](https://img.shields.io/badge/donor_unit-Motorola_PMUN1046A-6e7681?style=flat-square)](https://github.com/aramder/PMUN1046A_RE)

**A drop-in replacement controller for the Motorola PMUN1046A relay box — same relays, harness, and enclosure; open control instead of a proprietary wired link to the radio.**

</div>

---

## Why

The PMUN1046A **Universal Relay Controller (URC)** is a sealed 12 V automotive relay box — ten relay outputs at 15 A each, per-channel fusing, a busbar, and a 60 A power feed, in a trunnion-mounted weatherproof chassis. Surplus units are common and cheap. Everything about it is desirable *except* that it only takes commands from a Motorola APX radio over a proprietary GCAI/USB link.

ORC replaces the controller board outright and makes the box a general-purpose, openly-controlled ten-channel power distribution module. **ORC** is a deliberate one-letter riff on Motorola's **URC**.

<details>
<summary><b>About the donor unit</b> (PMUN1046A Universal Relay Controller)</summary>
<br>

Motorola's URC ships as part of the APX mobile radio ecosystem — a sealed relay box meant to switch scene lighting, sirens, and other accessory loads from the radio head. The hardware (relay board, harness, chassis) is generic 12 V automotive gear; only the controller board and its command protocol are Motorola-specific. Reverse-engineering notes on the donor unit live in the archival [`PMUN1046A_RE`](https://github.com/aramder/PMUN1046A_RE) repo.

</details>

## What it is

<table>
<tr>
<td width="50%" valign="top">

**Inherited from the donor unit**
- Ten switched channels, 15 A each
- Per-channel fusing + busbar
- Wire harness (P/N `0975931M01`)
- Sealed, trunnion-mounted chassis

</td>
<td width="50%" valign="top">

**Rebuilt on the new controller board**
- ESP32-S3 controller (native USB; `-1U` variant for external antenna)
- 9 V relay coil supply
- Ten relay driver stages
- Automotive-grade input protection

</td>
</tr>
</table>

Category-wise this is a **PDM** (power distribution module) — the sPOD / Switch-Pros class of device — with an open control interface instead of a proprietary wired link to the radio.

## Repo structure

One repo for design inputs, hardware, and firmware — no split-repo overhead for a project this size:

| Path | Contents | Status |
|---|---|---|
| `docs/` | Design inputs, specs, architecture decisions | Active |
| `hardware/` | KiCad project — schematic, PCB, BOM | Full schematic capture, not gate-verified |
| `firmware/` | PlatformIO / Arduino firmware | Not yet created |
| [`PMUN1046A_RE`](https://github.com/aramder/PMUN1046A_RE) *(separate repo)* | Reverse-engineering record of the donor unit | Closed, archival |

## Status

**Schematic drafted, not gate-verified.** A full scripted capture exists in `hardware/` — two ground domains, ADuM1250 barrier, ×10 coil-driver channels, CAN interface, active USB/DC-terminal source-select — but has **not** been through the [hardware-workflow.md](docs/hardware-workflow.md) Gate 1/2 discipline; several parts are placeholders pending live-catalog verification (see [hardware/BOM.md](hardware/BOM.md)). See [docs/design-inputs.md](docs/design-inputs.md) for what carries forward from the teardown and the bench measurements still outstanding.

The harness header — formerly the gating item — is **resolved**: 14-position, 0.1" THT, pinout measured and locked (LCSC C2977586). Remaining open items: flyback-diode presence on the relay board, Tyco relay part number, board outline/mounting, and the USB-presence-detect gate-drive stage for the new source-select FET (topology sketched, values/second-stage not worked out — see BOM.md).

Working conventions for part selection, asset verification, and check gates ahead of schematic capture: [docs/hardware-workflow.md](docs/hardware-workflow.md).
