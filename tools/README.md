# ORC host-side CAN test tools

Bench test scripts for talking to a real ORC board (running `firmware/src/canopen_app`) over a real USB-CAN adapter, from a PC. These are **not** part of `rigos-core`'s eventual integration (see that sibling repo's FR-059) — they're small, dependency-light scripts anyone can run on a bench to sanity-check a board without standing up the full rigOS stack.

**Setup assumed**: an ESP32-C3 running `canopen_app`, connected to your PC over USB (for power + serial monitor), with its TWAI pins wired through the SN65HVD230 transceiver to CAN_H/CAN_L, which is in turn wired to a **separate USB-CAN adapter** also connected to your PC (e.g. the Jhoinrch RH-02 / CANable-clone noted in `rigos-core`'s FR-059). Two USB connections to the same PC, two different jobs — the ESP32's own USB is just power/serial, not the CAN link; the adapter is the actual CAN link these scripts use.

## Install

```
pip install -r requirements.txt
```

Just `python-can` — see [`requirements.txt`](requirements.txt).

## Which `--interface`/`--channel` to use

**This depends on which firmware your specific adapter shipped with — check before assuming.** CANable-clone boards (like the Jhoinrch RH-02) commonly ship with one of two firmwares, and `rigos-core`'s FR-059 flagged this as genuinely unconfirmed for that specific board:

- **candleLight (`gs_usb`)** — the adapter enumerates as a native USB-CAN device. On Windows this needs the `gs_usb`-compatible WinUSB driver bound via [Zadig](https://zadig.akeo.ie/) before python-can can see it (a one-time setup step, not something these scripts do for you). Once set up:
  ```
  python can_monitor.py --interface gs_usb --channel 0
  ```
  (`0` = first `gs_usb` device found; python-can enumerates by index.)

- **slcan** — the adapter enumerates as a plain serial port (a COM port on Windows, `/dev/ttyACM*` on Linux). No special driver needed beyond the OS's own USB-serial driver. Check Device Manager for the COM port number, then:
  ```
  python can_monitor.py --interface slcan --channel COM5
  ```

If you don't know which firmware your board has: try `slcan` first (needs a COM port to show up in Device Manager, easy to check, no driver install) and fall back to `gs_usb` if the port never enumerates as serial.

**Bitrate**: ORC's bus is locked at 125 kbit/s (`docs/can-protocol-research.md`). Both scripts default `--bitrate` to that — don't override it unless every node on your bench bus (including the adapter itself) is deliberately set to something else.

## The two tests

### 1. `can_monitor.py` — passive listener, run this first

Listens for Heartbeat/TPDO1/TPDO2 traffic and decodes it, without sending anything. Auto-discovers node IDs from whatever it actually sees on the bus — you don't need to already know the board's DIP-switch setting to run this one. Good first check that the adapter is wired correctly and the board is alive and running `canopen_app` (as opposed to one of the IO-level bring-up sketches, which don't speak CANopen at all — see `firmware/README.md`).

```
python can_monitor.py --interface slcan --channel COM5
```

Ctrl+C to stop, or pass `--timeout <seconds>` to auto-stop after a quiet period.

### 2. `relay_toggle_test.py` — sweep + all-on demo/test

Runs a fixed sequence over CAN, confirming every step via TPDO1 readback: sweeps channels 1 through 10 one at a time (1s each, exclusive — only that channel is on at each step), pauses with everything off, turns all 10 channels on at once, pauses again, then cleans up (all off) on exit. This is the real end-to-end path (host → RPDO1 → ORC → PCA9555 → TPDO1 readback → host), and it exercises both a single-channel command and an all-channels-at-once command — a real distinction on this hardware, since RPDO1 always sends the whole 10-bit state, not a per-bit toggle.

Needs the board's **node ID** (read directly off its DIP switch, weight-labeled `1 2 4 8` — see `docs/circuit-draft.md`'s "Node-ID address input" section).

```
python relay_toggle_test.py --interface slcan --channel COM5 --node-id 1
```

Tune the timing with `--sweep-seconds` (default 1.0s per channel) and `--pause-seconds` (default 2.0s, used both for the all-off pause before the all-on step and as the all-on hold duration):

```
python relay_toggle_test.py --interface slcan --channel COM5 --node-id 1 --sweep-seconds 0.5 --pause-seconds 3
```

Exit code is 0 if every step's TPDO1 confirmation matched what was commanded, 1 otherwise — usable in a simple pass/fail bench check, not just interactively. Ctrl+C at any point still runs the all-off cleanup before exiting.

## `orc_canopen.py`

Shared wire-protocol constants/encode-decode helpers, imported by both scripts above — the Python-side counterpart to `firmware/lib/orc_canopen/orc_canopen.h`. **The two are hand-maintained in parallel, not generated from one source** — if ORC's protocol ever changes (a new message, a COB-ID change, a payload layout change), both need updating together, along with `docs/can-protocol-research.md` (the actual source of truth) and `rigos-core`'s `orc_can.py` (the sibling repo's own independent implementation of this same protocol).

## What these are not

- Not a replacement for `rigos-core`'s eventual `orc_can.py` integration (FR-059) — that's the production daemon-integrated backend; these are standalone bench scripts.
- Not hardware-verified by anyone yet as of this writing — written against the documented protocol and `canopen_app`'s actual implementation, but not yet run against a real board (no ORC board is in hand at time of writing — see `firmware/README.md`'s status line). Treat a first real run as the actual test of both the board *and* these scripts.
