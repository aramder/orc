#!/usr/bin/env python3
"""ORC bench test 2: relay channel round-trip test.

Sends RPDO1 to energize a channel, waits for TPDO1 to confirm it applied,
then de-energizes it and confirms that too. Exercises the real command path
end-to-end (host -> RPDO1 -> ORC -> PCA9555 -> TPDO1 readback -> host) --
the most direct "does my board actually work" test available without a
scope or an LED jig on the relay outputs themselves.

Run can_monitor.py first if you haven't already confirmed the board is on
the bus and alive -- this script requires knowing (or guessing and having it
fail loudly) the target's --node-id, unlike the monitor's auto-discovery.

Setup: see tools/README.md. Example (slcan, adapter on COM5, ORC node 1):
    python relay_toggle_test.py --interface slcan --channel COM5 --node-id 1

Test a single channel:
    python relay_toggle_test.py --interface slcan --channel COM5 --node-id 1 --channel-num 3

Test all 10 channels in sequence (default):
    python relay_toggle_test.py --interface slcan --channel COM5 --node-id 1
"""

from __future__ import annotations

import argparse
import sys
import time

import can

from orc_canopen import (
    BUS_BITRATE,
    channel_is_set,
    channel_mask_set,
    cob_id,
    COB_ID_RPDO1_BASE,
    COB_ID_TPDO1_BASE,
    pack_channel_mask,
    unpack_channel_mask,
)

# How long to wait for TPDO1 to confirm a commanded state before declaring
# that channel a failure. canopen_app sends TPDO1 immediately after applying
# every RPDO1 (see firmware/src/canopen_app/main.cpp's applyRelayCommand()),
# so this is generous margin, not a tuned real-time deadline.
CONFIRM_TIMEOUT_S = 2.0


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--interface", required=True, help="python-can interface, e.g. slcan or gs_usb (see tools/README.md)")
    p.add_argument("--channel", required=True, help="e.g. a COM port for slcan (COM5), or a device index for gs_usb (0)")
    p.add_argument("--node-id", type=int, required=True, help="ORC unit's node ID (1-15, read off its DIP switch)")
    p.add_argument("--bitrate", type=int, default=BUS_BITRATE, help=f"default {BUS_BITRATE} (locked bus bitrate)")
    p.add_argument("--channel-num", type=int, default=None, help="test only this channel (1-10); default: all 10 in sequence")
    p.add_argument("--hold-seconds", type=float, default=0.5, help="how long to leave a channel energized before de-energizing it (default 0.5s -- keep this short, it's audibly/visibly cycling a real relay)")
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


def test_one_channel(bus: can.Bus, node_id: int, channel: int, hold_seconds: float) -> bool:
    """Energize, confirm, de-energize, confirm. Returns True if both
    confirmations matched what was commanded."""
    print(f"  Channel {channel}: energizing ... ", end="", flush=True)
    on_mask = channel_mask_set(channel, True)
    send_rpdo1(bus, node_id, on_mask)
    applied = wait_for_tpdo1(bus, node_id, CONFIRM_TIMEOUT_S)
    if applied is None:
        print("FAIL (no TPDO1 received)")
        return False
    if not channel_is_set(applied, channel):
        print(f"FAIL (commanded ON, TPDO1 reports mask 0x{applied:03X} -- channel still off)")
        return False
    print("OK (TPDO1 confirms ON)")

    time.sleep(hold_seconds)

    print(f"  Channel {channel}: de-energizing ... ", end="", flush=True)
    off_mask = channel_mask_set(channel, False, current_mask=on_mask)
    send_rpdo1(bus, node_id, off_mask)
    applied = wait_for_tpdo1(bus, node_id, CONFIRM_TIMEOUT_S)
    if applied is None:
        print("FAIL (no TPDO1 received)")
        return False
    if channel_is_set(applied, channel):
        print(f"FAIL (commanded OFF, TPDO1 reports mask 0x{applied:03X} -- channel still on)")
        return False
    print("OK (TPDO1 confirms OFF)")
    return True


def main() -> int:
    args = parse_args()
    channels = [args.channel_num] if args.channel_num is not None else list(range(1, 11))

    print(f"Opening {args.interface}:{args.channel} @ {args.bitrate} bit/s, target NodeID {args.node_id} ...")
    try:
        bus = can.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate)
    except Exception as exc:  # noqa: BLE001 -- report and exit cleanly for a bench tool
        print(f"ERROR: could not open the CAN interface: {exc}", file=sys.stderr)
        return 1

    results: dict[int, bool] = {}
    try:
        print(f"Testing {len(channels)} channel(s), {args.hold_seconds}s hold between on/off.\n")
        for ch in channels:
            results[ch] = test_one_channel(bus, args.node_id, ch, args.hold_seconds)
    except KeyboardInterrupt:
        print("\nInterrupted.")
    finally:
        bus.shutdown()

    passed = [ch for ch, ok in results.items() if ok]
    failed = [ch for ch, ok in results.items() if not ok]
    print(f"\n{len(passed)}/{len(results)} channel(s) passed.")
    if failed:
        print(f"FAILED: {failed}")
        print("If every channel failed with 'no TPDO1 received': check --node-id matches the "
              "board's DIP switch, and that canopen_app (not a bring-up sketch) is running.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
