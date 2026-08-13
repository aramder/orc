<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/media/banner-dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="docs/media/banner-light.svg">
  <img alt="ORC — Open Relay Controller" src="docs/media/banner-light.svg" width="100%">
</picture>

[![status](https://img.shields.io/badge/status-real--hardware_bring--up-orange?style=flat-square)](docs/features/LOG.md)
[![controller](https://img.shields.io/badge/controller-ESP32--C3-blue?style=flat-square)](#what-it-is)
[![channels](https://img.shields.io/badge/channels-10_%C3%97_15A-2da44e?style=flat-square)](#what-it-is)
[![donor](https://img.shields.io/badge/donor_unit-PMUN1046A-6e7681?style=flat-square)](#why)

**A replacement controller board for surplus Motorola PMUN1046A relay boxes — same relays and enclosure; open CAN control over plain terminal blocks instead of the proprietary wired link to the radio.**

Independent open-source project. Not affiliated with, endorsed by, or connected to Motorola Solutions, Inc. — see [TRADEMARKS.md](TRADEMARKS.md).

**Safety:** this switches high-current DC in a vehicle. It is not safety qualified and must not be used for safety-critical functions. Read [DISCLAIMER.md](DISCLAIMER.md) before building anything.

</div>

---

## Why

The PMUN1046A **Universal Relay Controller (URC)** is a sealed 12 V automotive relay box — ten relay outputs at 15 A each, per-channel fusing, and a busbar power input rated for up to two cables at 60 A each (Motorola's install manual), in a trunnion-mounted weatherproof chassis. Power goes straight to the busbar through cable glands — no special harness needed on the input side. Surplus units are common and cheap. Everything about it is desirable *except* that it only takes commands from a Motorola APX radio over a proprietary GCAI/USB link.

ORC replaces the controller board outright and makes the box a general-purpose, openly-controlled ten-channel power distribution module. **ORC** stands for Open Relay Controller.

ORC does not implement, emulate, or interoperate with the GCAI link. The controller board is an independent design that replaces the donor unit's controller board entirely; control is plain CAN or USB.

<details>
<summary><b>About the donor unit</b> (PMUN1046A Universal Relay Controller)</summary>
<br>

Motorola's URC ships as part of the APX mobile radio ecosystem — a sealed relay box meant to switch scene lighting, sirens, and other accessory loads from the radio head. The relay board and chassis are generic 12 V automotive gear; only the controller board and its proprietary GCAI/USB command link to the radio are Motorola-specific. The relay board is entirely passive (relays, fuses, terminal blocks, busbar — no drivers or regulator of its own); a small internal harness carries switched 9 V coil drive between it and the controller board. Power input has no special harness at all — it lands straight on the busbar through the enclosure's cable glands. ORC keeps the relay board and chassis, and replaces the controller board and its proprietary radio link with plain CAN over terminal blocks — no proprietary connector anywhere in this design.

</details>

## What it is

<table>
<tr>
<td width="50%" valign="top">

**Inherited from the donor unit**
- Ten switched channels, 15 A each
- Per-channel fusing + busbar
- Board-to-board coil-drive harness (P/N `0975931M01`) — the internal cable between the relay board and the controller board, carrying switched 9 V coil drive; *not* a power-input harness
- Sealed, trunnion-mounted chassis

</td>
<td width="50%" valign="top">

**Rebuilt on the new controller board**
- ESP32-C3 controller (native USB for programming only; CAN-exclusive control, no wireless)
- 9 V relay coil supply
- Ten relay driver stages
- CAN terminal blocks (no proprietary connector)

</td>
</tr>
</table>

Category-wise this is a **PDM** (power distribution module) — the sPOD / Switch-Pros class of device — with an open CAN control interface instead of a proprietary wired link to the radio.

## Repo structure

One repo for design inputs, hardware, and firmware — no split-repo overhead for a project this size:

| Path | Contents | Status |
|---|---|---|
| `docs/` | Design inputs, specs, architecture decisions | Active |
| `hardware/` | KiCad project — schematic, PCB, BOM | Schematic captured, boards fabricated and populated, real-hardware bring-up in progress |
| `firmware/` | PlatformIO / Arduino firmware | `canopen_app` is real application firmware (CAN + USB relay control), under active real-hardware bring-up |
| `tools/` | Host-side CAN/USB bench test scripts | Used against real hardware |

## License

Different parts of the repo carry different licenses. Full texts are in [LICENSES/](LICENSES/); each directory also carries its own `LICENSE` file.

| Path | License |
|---|---|
| `hardware/` | [CERN-OHL-W-2.0](LICENSES/CERN-OHL-W-2.0.txt) |
| `firmware/`, `tools/` | [Apache-2.0](LICENSES/Apache-2.0.txt) |
| `docs/` | [CC-BY-4.0](LICENSES/CC-BY-4.0.txt) |

Hardware and software are licensed separately because they are separate works: under CERN-OHL's own definitions the ESP32-C3 is an Available Component, so a hardware license on the board does not reach the code running on it. See [NOTICE](NOTICE) for the summary and [TRADEMARKS.md](TRADEMARKS.md) for trademark and affiliation.

## Status

**Real hardware in hand and under active bring-up.** Boards were sent to fab 2026-08-02, came back populated, and have been bench-tested since 2026-08-10: the PCA9555/I2C-isolator chain, the full 10-channel relay-driver mapping, and the CAN (RPDO1/TPDO1/TPDO2/Heartbeat) and USB control paths have all been exercised on real hardware, including three real firmware bugs found and fixed along the way (see [docs/features/LOG.md](docs/features/LOG.md) for the full BUG-001/002/003 history). One real mechanical fault (a harness connector shorting against the enclosure chassis) was also found and fixed on the bench — see [docs/circuit-draft.md](docs/circuit-draft.md) for the follow-up enclosure-clearance item.

Working conventions for part selection, asset verification, and check gates: [docs/hardware-workflow.md](docs/hardware-workflow.md). Bench test tooling for talking to a real board over CAN/USB from a PC: [tools/](tools/).
