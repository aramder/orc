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

### `gs_usb` on Windows — two extra install steps beyond Zadig

Confirmed against a real Jhoinrch RH-02 (`canable gs_usb`, VID_1D50:PID_606F) on Windows, 2026-08. Binding the WinUSB driver via Zadig (as the section above describes) is necessary but **not sufficient** — `pyusb` (which `gs_usb` depends on) also needs the actual `libusb-1.0.dll` *runtime library*, which Windows doesn't ship and Zadig doesn't install:

```
pip install gs_usb libusb
```

`pip install libusb` bundles a working `libusb-1.0.dll`, but `pyusb`'s default search won't find it automatically — put its directory on `PATH` before running either script:

```
set PATH=%USERPROFILE%\AppData\Local\Programs\Python\Python313\Lib\site-packages\libusb\_platform\windows\x86_64;%PATH%
```

(Adjust the Python version/path to match your own install — find it with `python -c "import libusb, os; print(os.path.dirname(libusb.__file__))"` and append `\_platform\windows\x86_64`.) Symptom without this: `python -c "import usb.core; print(len(list(usb.core.find(find_all=True))))"` reports `0` devices even though Windows Device Manager shows the adapter present and its driver bound correctly.

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

Also holds `drain_stale_frames()` (the one function here that does touch a bus — see its own docstring) and the SDO read/response codec used for the startup status poll below.

## Startup status poll — why both scripts send one

Real-hardware finding, 2026-08: a board addressed at node ID 0 (all four DIP-switch positions open) was observed staying completely silent on the bus — no Heartbeat, nothing — until the *first* time something was actually sent to it. Both scripts now send one SDO read (of ORC's TPDO2 event-timer object, `1801h`/`5`) at startup specifically to provoke that first response, in addition to it being a genuine, harmless read (doesn't touch relay state). Not fatal if it times out — its job is putting a frame on the wire, not necessarily getting an answer. Skip it with `--no-poll` if you don't want it.

Related: both scripts also drain any stale buffered frames immediately after opening the bus (`drain_stale_frames()`) — real hardware showed that a fresh session opened against an adapter that had a prior session killed uncleanly (e.g. by a hard timeout instead of `bus.shutdown()`) can inherit a backlog that makes a 2-second wait loop take much longer than 2 seconds to actually reach its deadline, since it burns through old buffered frames first.

## Known open item: TPDO1 confirmation can lag or drop under real bus conditions

Real-hardware finding, 2026-08, **not yet root-caused**: a full 1-10 channel sweep against real hardware showed roughly every other TPDO1 missing entirely, and the ones that did arrive consistently reported the *previous* step's commanded mask (a one-cycle lag) — while the relays themselves were independently confirmed, by directly watching/listening to the board, to click through correctly and in real time. So the RPDO1 command path works; TPDO1's confirmation channel is what's unreliable, at least under the bus conditions observed.

**Ruled out**: a stale frame sitting in the host's own receive queue from the previous step. `relay_toggle_test.py` already drains immediately before every send (see `command_and_confirm()`), and it made no difference — same lag, same drop pattern. That means the cause is either in `canopen_app`'s actual TPDO1-send timing relative to RPDO1 receipt, or genuine frame-level loss/retry on the bus itself (the same run also showed an elevated TWAI TX error counter climbing back down over time, consistent with the bus still settling) — both are firmware/hardware questions, not something fixable from the host side. `relay_toggle_test.py` deliberately reports these as honest `FAIL`s rather than adding retry-tolerant matching that would mask whichever of those two it turns out to be — don't be alarmed by a sweep that doesn't hit 12/12 confirmed; check whether the relays audibly/visibly did the right thing before assuming something's actually broken.

## What these are not

- Not a replacement for `rigos-core`'s eventual `orc_can.py` integration (FR-059) — that's the production daemon-integrated backend; these are standalone bench scripts.
- **Hardware-verified as of 2026-08**: both scripts have been run against a real ORC board (Jhoinrch RH-02 adapter, real `canopen_app` firmware) — the startup poll, bus liveness, and the RPDO1 relay-command path (physically confirmed) all work. TPDO1 confirmation reliability is the one open item, documented above, not swept under the rug.
