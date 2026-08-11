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

**Ruled out**: a stale frame sitting in the host's own receive queue from the previous step. `relay_toggle_test.py` already drains immediately before every send (see `command_and_confirm()`), and it made no difference — same lag, same drop pattern.

**Update, code-review pass against `canopen_app` (post-FR-004), 2026-08-10 — not `canopen_app`'s fault, points at bus health.** Full writeup in `.claude/tpdo1-lag-investigation-prompt.md`'s "Findings" section (gitignored, local-only); summary: `handleRpdo1()` → `applyRelayCommand()` → `twaiSend()` is synchronous, straight-line code with nothing that could delay a send by anywhere near 1s — rules out a firmware-logic cause. But `twai_transmit()` returning `ESP_OK` only means *queued*, not *transmitted-and-ACKed*: the ESP-IDF TWAI controller retries an unACKed frame in hardware, invisibly, for as long as it takes, and `canopen_app`'s `twaiSend()` never retries a failed enqueue itself (`tx_queue_len = 5`, one attempt, log-and-drop on failure). On a bus with real ACK trouble, that alone reproduces both symptoms: a TPDO1 that's stuck retrying can physically leave the transceiver a cycle late (the "previous step's mask" pattern), and once the 5-deep queue backs up, a subsequent send is dropped outright (the "no TPDO1 at all" pattern). The session's own measured TX error counter (127, right at the error-passive boundary, recovering over ~10s) is independent evidence the bus *did* have real ACK problems at the time — consistent with, and sufficient to cause, this mechanism.

**Root-caused and fixed at 1s/2s cadence, 2026-08-10.** The bus-settling and termination theories were tested directly and both came back negative (TEC held at 0 for 10+ seconds through a full sweep, termination confirmed populated correctly both ends, and the sweep still went 0/12 with the identical lag/loss shape) — ruling out bus health entirely at this cadence. That reopened the investigation on the firmware side, which found the real cause: `handleRpdo1()` calls `Serial.printf()` to log the incoming RPDO1 *before* `applyRelayCommand()` sends TPDO1, and native USB-CDC (`HWCDC`) writes have a documented worst-case block of up to 2000ms (20 retries x 100ms default `tx_timeout_ms`) when the physical USB link is up but nothing is draining the ring buffer — comfortably longer than this script's 2s confirm window. Fix applied and flashed: `setup()` now calls `Serial.setTxTimeoutMs(20)`. Re-ran the identical sweep against the reflashed board: **12/12 steps confirmed** at the standard 1s/2s cadence.

**Sub-second cadence (BUG-003, same day, follow-up) — do not use below ~1s.** Randomized-mask stress testing at 0.5s/0.3s cadence found a second, distinct problem: TPDO1 lag/loss and total Heartbeat/TPDO2 silence, worse than the 1s-cadence symptom above. Chased two real contributing mechanisms: (1) a TWAI TX-queue backlog letting stale status frames block fresh ones (fixed, `twai_clear_transmit_queue()`); (2) the `setTxTimeoutMs(20)` fix above was itself under-specified — `HWCDC.cpp`'s retry *count* (20) is a hardcoded constant the timeout call doesn't adjust, so the real worst case is `20 x 20ms = 400ms` per blocking write, not "~200ms," and `canopen_app` issues several such calls per RPDO1 that compound. Fixed with `printBestEffort()` (skips a diagnostic print entirely rather than blocking if the console isn't being drained) — confirmed via a same-session A/B real-hardware test: identical firmware, identical 0.5s cadence, 1/15 confirmed with nothing reading the USB console vs. **15/15** with a reader attached. Real, substantial improvement (1/15 → 6/15 in the harder no-reader case) but **not a full fix**: watching the bus directly during a fresh run found real `CAN_ERR_CRTL` (RX+TX error-warning) CAN error frames recurring every ~2-2.4s, unaffected by the Serial fix, and Heartbeat/TPDO2 still fully silent over a full 20s run — evidence of a genuine bus/controller-level condition under fast bidirectional traffic, not further-fixable firmware logic. **Recommendation, and the actual resolution**: keep RPDO1 commanding at ~1s or slower — real hosts have no reason to approach sub-second relay-command cadence. Full investigation trail: `docs/features/BUG-003.md`.

See `.claude/tpdo1-lag-investigation-prompt.md` (gitignored, local) for the full blow-by-blow investigation trail if picking any of this up again.

## What these are not

- Not a replacement for `rigos-core`'s eventual `orc_can.py` integration (FR-059) — that's the production daemon-integrated backend; these are standalone bench scripts.
- **Hardware-verified as of 2026-08**: both scripts have been run against a real ORC board (Jhoinrch RH-02 adapter, real `canopen_app` firmware) — the startup poll, bus liveness, and the RPDO1 relay-command path (physically confirmed) all work. TPDO1 confirmation reliability is the one open item, documented above, not swept under the rug.
