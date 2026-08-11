#!/usr/bin/env python3
"""ORC bench test 2: relay sweep + all-on demo/test.

Runs a fixed sequence over CAN, confirming every step via TPDO1 readback:

  1. Sweep channels 1 through 10, one at a time, 1 second each (each step
     energizes exactly that channel and de-energizes every other one --
     a classic "walking" chase, same pattern firmware/src/pca9555_bringup
     uses for its own bring-up walk).
  2. All channels off, pause.
  3. All 10 channels ON at once, pause.
  4. All channels off (cleanup -- doesn't leave relays energized at exit).

Exercises the real command path end-to-end (host -> RPDO1 -> ORC -> PCA9555
-> TPDO1 readback -> host) across both a single-channel and an all-channels
command, which is a real distinction for this hardware: RPDO1 sends the
*whole* 10-bit state every time, not a per-bit toggle, so "all on" is one
command, not ten.

Run can_monitor.py first if you haven't already confirmed the board is on
the bus and alive -- this script requires knowing (or guessing and having it
fail loudly) the target's --node-id, unlike the monitor's auto-discovery.

Example (slcan, adapter on COM5, ORC node 1):
    python relay_toggle_test.py --interface slcan --channel COM5 --node-id 1
"""

from __future__ import annotations

import argparse
import sys
import time

import can

from orc_canopen import (
    ALL_CHANNELS_MASK,
    BUS_BITRATE,
    channel_mask_set,
    cob_id,
    COB_ID_RPDO1_BASE,
    COB_ID_TPDO1_BASE,
    format_channel_mask,
    pack_channel_mask,
    unpack_channel_mask,
)

# How long to wait for TPDO1 to confirm a commanded state before declaring
# that step a failure. canopen_app sends TPDO1 immediately after applying
# every RPDO1 (see firmware/src/canopen_app/main.cpp's applyRelayCommand()),
# so this is generous margin, not a tuned real-time deadline.
CONFIRM_TIMEOUT_S = 2.0


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--interface", required=True, help="python-can interface, e.g. slcan or gs_usb (see tools/README.md)")
    p.add_argument("--channel", required=True, help="e.g. a COM port for slcan (COM5), or a device index for gs_usb (0)")
    p.add_argument("--node-id", type=int, required=True, help="ORC unit's node ID (1-15, read off its DIP switch)")
    p.add_argument("--bitrate", type=int, default=BUS_BITRATE, help=f"default {BUS_BITRATE} (locked bus bitrate)")
    p.add_argument("--sweep-seconds", type=float, default=1.0, help="how long each channel stays on during the 1-10 sweep (default 1.0s)")
    p.add_argument("--pause-seconds", type=float, default=2.0, help="pause duration: all-off before the all-on step, and hold duration of the all-on step itself (default 2.0s)")
    return p.parse_args()


def send_rpdo1(bus: can.Bus, node_id: int, channel_mask: int) -> None:
    msg = can.Message(
        arbitration_id=cob_id(COB_ID_RPDO1_BASE, node_id),
        data=pack_channel_mask(channel_mask),
        is_extended_id=False,
    )
    bus.send(msg)


def wait_for_tpdo1(bus: can.Bus, node_id: int, timeout_s: float) -> int | None:
    """Blocks until a TPDO1 from the given node arrives, or timeout_s
    elapses. Returns the decoded channel mask, or None on timeout. Ignores
    any other traffic on the bus (Heartbeat, TPDO2, other nodes) while
    waiting -- this is a filter loop, not a single bus.recv()."""
    target_id = cob_id(COB_ID_TPDO1_BASE, node_id)
    deadline = time.monotonic() + timeout_s
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return None
        msg = bus.recv(timeout=remaining)
        if msg is None:
            return None
        if msg.arbitration_id == target_id:
            return unpack_channel_mask(bytes(msg.data))


def command_and_confirm(bus: can.Bus, node_id: int, mask: int, label: str) -> bool:
    """Sends one RPDO1 and waits for TPDO1 to confirm the exact mask was
    applied. Prints a one-line result either way. Returns True on a
    confirmed match."""
    print(f"  {label}: {format_channel_mask(mask)} ... ", end="", flush=True)
    send_rpdo1(bus, node_id, mask)
    applied = wait_for_tpdo1(bus, node_id, CONFIRM_TIMEOUT_S)
    if applied is None:
        print("FAIL (no TPDO1 received)")
        return False
    if applied != mask:
        print(f"FAIL (commanded 0x{mask:03X}, TPDO1 reports 0x{applied:03X})")
        return False
    print("OK")
    return True


def main() -> int:
    args = parse_args()

    print(f"Opening {args.interface}:{args.channel} @ {args.bitrate} bit/s, target NodeID {args.node_id} ...")
    try:
        bus = can.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate)
    except Exception as exc:  # noqa: BLE001 -- report and exit cleanly for a bench tool
        print(f"ERROR: could not open the CAN interface: {exc}", file=sys.stderr)
        return 1

    results: list[bool] = []
    try:
        print(f"\n--- Sweep: channels 1-10, one at a time, {args.sweep_seconds}s each ---")
        for ch in range(1, 11):
            mask = channel_mask_set(ch, True)  # exclusive -- this channel only
            results.append(command_and_confirm(bus, args.node_id, mask, f"Channel {ch}"))
            time.sleep(args.sweep_seconds)

        print(f"\n--- All off, {args.pause_seconds}s pause ---")
        results.append(command_and_confirm(bus, args.node_id, 0, "All off"))
        time.sleep(args.pause_seconds)

        print(f"\n--- All 10 channels ON at once, {args.pause_seconds}s pause ---")
        results.append(command_and_confirm(bus, args.node_id, ALL_CHANNELS_MASK, "All on"))
        time.sleep(args.pause_seconds)

    except KeyboardInterrupt:
        print("\nInterrupted.")
    finally:
        print("\n--- Cleanup: all off ---")
        command_and_confirm(bus, args.node_id, 0, "All off")
        bus.shutdown()

    passed = sum(1 for ok in results if ok)
    print(f"\n{passed}/{len(results)} step(s) confirmed.")
    if passed != len(results):
        print("If every step failed with 'no TPDO1 received': check --node-id matches the "
              "board's DIP switch, and that canopen_app (not a bring-up sketch) is running.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
