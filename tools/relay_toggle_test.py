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

Real-hardware finding, 2026-08: a board whose CAN interface has never been
addressed (e.g. fresh boot, node ID 0 / DIP switches all open) has been
observed staying silent until the FIRST time something is actually sent to
it. This script sends one status poll (an SDO read of ORC's TPDO2-interval
object) right after opening the bus, before the sweep begins, for exactly
that reason -- if your board is currently at node ID 0, pass --node-id 0 and
the poll (and everything after it) will address it correctly.
"""

from __future__ import annotations

import argparse
import sys
import time

import can

from orc_canopen import (
    ALL_CHANNELS_MASK,
    build_sdo_read_request,
    BUS_BITRATE,
    channel_mask_set,
    cob_id,
    COB_ID_RPDO1_BASE,
    COB_ID_SDO_RX_BASE,
    COB_ID_SDO_TX_BASE,
    COB_ID_TPDO1_BASE,
    decode_sdo_response,
    drain_stale_frames,
    format_channel_mask,
    pack_channel_mask,
    unpack_channel_mask,
)

# How long to wait for TPDO1 to confirm a commanded state before declaring
# that step a failure. canopen_app sends TPDO1 immediately after applying
# every RPDO1 (see firmware/src/canopen_app/main.cpp's applyRelayCommand()),
# so this is generous margin, not a tuned real-time deadline.
CONFIRM_TIMEOUT_S = 2.0
POLL_RESPONSE_TIMEOUT_S = 2.0


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--interface", required=True, help="python-can interface, e.g. slcan or gs_usb (see tools/README.md)")
    p.add_argument("--channel", required=True, help="e.g. a COM port for slcan (COM5), or a device index for gs_usb (0)")
    p.add_argument("--node-id", type=int, required=True, help="ORC unit's node ID (0-15 -- 0 matches all DIP-switch positions open, read the rest off the board's DIP switch)")
    p.add_argument("--bitrate", type=int, default=BUS_BITRATE, help=f"default {BUS_BITRATE} (locked bus bitrate)")
    p.add_argument("--sweep-seconds", type=float, default=1.0, help="how long each channel stays on during the 1-10 sweep (default 1.0s)")
    p.add_argument("--pause-seconds", type=float, default=2.0, help="pause duration: all-off before the all-on step, and hold duration of the all-on step itself (default 2.0s)")
    p.add_argument("--no-poll", action="store_true", help="skip the startup status poll")
    return p.parse_args()


def poll_status(bus: can.Bus, node_id: int) -> None:
    """Sends one SDO read (of ORC's TPDO2-interval object) to node_id and
    waits briefly for a response, printing the outcome either way. Not
    fatal on failure/timeout -- its real job is putting a first frame on
    the wire addressed to the board, since real hardware has been observed
    staying silent until that happens; an actual SDO response is a bonus
    confirmation, not the point of calling this before the sweep."""
    print(f"Sending startup status poll to NodeID {node_id} (SDO read, object 0x1801/5) ... ", end="", flush=True)
    req = can.Message(
        arbitration_id=cob_id(COB_ID_SDO_RX_BASE, node_id),
        data=build_sdo_read_request(),
        is_extended_id=False,
    )
    bus.send(req)

    target_id = cob_id(COB_ID_SDO_TX_BASE, node_id)
    deadline = time.monotonic() + POLL_RESPONSE_TIMEOUT_S
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            print("no response (proceeding anyway)")
            return
        msg = bus.recv(timeout=remaining)
        if msg is None:
            print("no response (proceeding anyway)")
            return
        if msg.arbitration_id == target_id:
            resp = decode_sdo_response(bytes(msg.data))
            if resp is None:
                print("malformed response")
            elif resp.success:
                print(f"OK (TPDO2 interval currently {resp.value}ms)")
            else:
                print(f"abort: {resp.abort_name}")
            return


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
    confirmed match.

    Real-hardware finding, 2026-08 -- root-caused and fixed as of 2026-08-10,
    see .claude/tpdo1-lag-investigation-prompt.md (gitignored, local) for the
    full trail: an early full 1-10 sweep showed roughly every other TPDO1
    missing entirely, with survivors reporting the *previous* step's mask (a
    one-cycle lag), while the relays themselves clicked through correctly in
    real time. Host-side stale-queue buildup was ruled out first (draining
    before every send here made no difference). A bus-health theory (TWAI
    hardware retrying on a not-yet-settled bus) was tested next and also
    ruled out -- re-running after tx_error_counter held at 0 for 10+ seconds,
    with termination confirmed correctly populated, still produced the
    identical pattern. Actual cause: canopen_app's handleRpdo1() logged the
    incoming RPDO1 via Serial.printf() *before* sending TPDO1, and native
    USB-CDC writes can block up to ~2000ms if the physical USB link is up but
    nothing is draining it -- longer than this function's confirm window.
    Fixed firmware-side (Serial.setTxTimeoutMs(20) in setup()), re-verified
    against real hardware at 12/12 confirmed. drain_stale_frames() is kept
    here regardless -- it's still correct/useful, just wasn't the fix."""
    drain_stale_frames(bus)
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

    stale = drain_stale_frames(bus)
    if stale:
        print(f"Drained {stale} stale buffered frame(s) left over from an earlier session.")

    if not args.no_poll:
        poll_status(bus, args.node_id)

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
