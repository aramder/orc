# ORC — Open Relay Controller

A drop-in replacement controller board for the **Motorola PMUN1046A Universal Relay Controller** (URC), reusing that unit's relay board, wire harness, and sealed enclosure while replacing everything Motorola put on the controller PCB.

**ORC** is a deliberate one-letter riff on Motorola's **URC** — same form factor and job, open design.

## Why

The PMUN1046A is a sealed 12 V automotive relay box — ten relay outputs at 15 A each, per-channel fusing, a busbar, and a 60 A power feed, in a trunnion-mounted weatherproof chassis. Surplus units are common and cheap. Everything about it is desirable *except* that it only takes commands from a Motorola APX radio over a proprietary GCAI/USB link.

We spent two bench sessions trying to bring up and reflash the stock controller, and stopped — the full record, including what was ruled out and why, is in
[PMUN1046A_RE](https://github.com/aramder/PMUN1046A_RE). The short version: nothing the stock firmware could have told us wasn't obtainable with a multimeter, so the firmware was never load-bearing.

ORC replaces the controller board outright and makes the box a general-purpose, openly-controlled ten-channel power distribution module.

## What it is

- **Ten switched channels**, 15 A each, inherited from the stock relay board and its fusing
- **ESP32-S3** class controller (native USB; `-1U` variant for an external antenna)
- Reuses the **relay board**, the **wire harness (0975931M01)**, and the **sealed chassis**
- Rebuilds the **9 V coil supply**, **ten relay drivers**, and **automotive input protection**

Category-wise this is a **PDM** (power distribution module) — the sPOD / Switch-Pros class of device — with an open control interface.

## Repo structure

This repo holds project-level design inputs, specifications, and decisions. Implementation is split out as it starts:

| Repo | Contents | Status |
|---|---|---|
| `orc` (this one) | Design inputs, specs, architecture decisions | Active |
| `orc-hardware` | KiCad project — schematic, PCB, BOM | Not yet created |
| `orc-firmware` | PlatformIO / Arduino firmware | Not yet created |
| [`PMUN1046A_RE`](https://github.com/aramder/PMUN1046A_RE) | Reverse-engineering record of the donor unit | Closed, archival |

## Status

**Pre-schematic.** Gathering design inputs — see [docs/design-inputs.md](docs/design-inputs.md) for what carries forward from the teardown, what has to be rebuilt, and the bench measurements still outstanding before layout can start.

The gating item is the harness header: pitch, pin count, and pinout mapping.
