#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Aram Dergevorkian
"""ORC bench test 1: passive CAN bus monitor.

Listens on the bus and decodes anything that looks like ORC traffic --
Heartbeat, TPDO1 (relay status), TPDO2 (bus health/uptime) -- without sending
anything itself. This is the "is my adapter wired correctly and is the board
alive" first test to run: no need to know the board's node ID in advance,
since it auto-discovers node IDs from whatever COB-IDs it actually sees.

Does NOT command anything -- see relay_toggle_test.py for a test that
actually exercises RPDO1/TPDO1 by driving a relay channel.

Setup: see tools/README.md for adapter firmware (candleLight/gs_usb vs
slcan) and the --interface/--channel values that go with each. ORC's bus is
locked at 125 kbit/s (docs/can-protocol-research.md) -- don't override
--bitrate unless you know what you're doing, every node must agree.

Example (slcan, adapter enumerated as COM5 on Windows):
    python can_monitor.py --interface slcan --channel COM5

Example (gs_usb / candleLight, first device found):
    python can_monitor.py --interface gs_usb --channel 0

Real-hardware finding, 2026-08: a board whose CAN interface has never been
addressed (e.g. fresh boot, node ID 0 / DIP switches all open) has been
observed staying silent -- no Heartbeat -- until the FIRST time something is
actually sent to it. This script now sends one status poll (an SDO read of
ORC's TPDO2-interval object) at startup for exactly that reason, addressed
to --node-id (default 0, matching that all-DIP-switches-open state). If your
board's node ID is something else, pass --node-id to match, or the poll
won't reach it (though the passive listen loop below will still pick up
whatever it does see, on any node ID).
"""

from __future__ import annotations

import argparse
import sys
import time

import can

from orc_canopen import (
    build_sdo_read_request,
    BUS_BITRATE,
    COB_ID_HEARTBEAT_BASE,
    COB_ID_SDO_RX_BASE,
    COB_ID_SDO_TX_BASE,
    COB_ID_TPDO1_BASE,
    COB_ID_TPDO2_BASE,
    cob_id,
    decode_heartbeat,
    decode_sdo_response,
    decode_tpdo2,
    drain_stale_frames,
    format_channel_mask,
    node_id_from_cob_id,
    unpack_channel_mask,
)

POLL_RESPONSE_TIMEOUT_S = 2.0


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--interface", required=True, help="python-can interface, e.g. slcan or gs_usb (see tools/README.md)")
    p.add_argument("--channel", required=True, help="e.g. a COM port for slcan (COM5), or a device index for gs_usb (0)")
    p.add_argument("--bitrate", type=int, default=BUS_BITRATE, help=f"default {BUS_BITRATE} (locked bus bitrate, don't change without reason)")
    p.add_argument("--timeout", type=float, default=None, help="stop after this many CONSECUTIVE seconds of no traffic (not a total run-time cap); default: run until Ctrl+C")
    p.add_argument("--node-id", type=int, default=0, help="node ID to send the startup status poll to (default 0, matching all-DIP-switches-open); doesn't limit what the passive listen loop reports")
    p.add_argument("--no-poll", action="store_true", help="skip the startup status poll, just listen passively")
    return p.parse_args()


def poll_status(bus: can.Bus, node_id: int) -> None:
    """Sends one SDO read (of ORC's TPDO2-interval object) to node_id and
    waits briefly for a response, printing the outcome either way. This is
    deliberately not fatal on failure/timeout -- its real job is just to put
    a first frame on the wire addressed to the board, since real hardware
    has been observed staying silent until that happens; getting an actual
    SDO response back is a bonus confirmation, not the point."""
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
            print("no response (proceeding anyway -- the poll's job is just to wake the interface)")
            return
        msg = bus.recv(timeout=remaining)
        if msg is None:
            print("no response (proceeding anyway -- the poll's job is just to wake the interface)")
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
        # Something else arrived while waiting (e.g. a Heartbeat that started
        # right after the poll) -- not the response we're waiting for, but
        # not a reason to stop waiting for it either.


def main() -> int:
    args = parse_args()

    print(f"Opening {args.interface}:{args.channel} @ {args.bitrate} bit/s ...")
    try:
        bus = can.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate)
    except Exception as exc:  # noqa: BLE001 -- report and exit cleanly, not a stack trace, for a bench tool
        print(f"ERROR: could not open the CAN interface: {exc}", file=sys.stderr)
        print("Check --interface/--channel against tools/README.md, and that nothing else "
              "(another script, a serial monitor on the same port) already has it open.", file=sys.stderr)
        return 1

    stale = drain_stale_frames(bus)
    if stale:
        print(f"Drained {stale} stale buffered frame(s) left over from an earlier session.")

    if not args.no_poll:
        poll_status(bus, args.node_id)

    seen_nodes: set[int] = set()
    print("Listening for ORC traffic (Heartbeat, TPDO1, TPDO2). Ctrl+C to stop.\n")

    try:
        while True:
            msg = bus.recv(timeout=args.timeout)
            if msg is None:
                if args.timeout is not None:
                    print(f"\nNo traffic in {args.timeout}s, stopping.")
                    break
                continue

            node_id = node_id_from_cob_id(msg.arbitration_id, COB_ID_HEARTBEAT_BASE)
            if node_id is not None:
                if node_id not in seen_nodes:
                    print(f"*** New node seen: NodeID {node_id} ***")
                    seen_nodes.add(node_id)
                print(f"[Heartbeat] node {node_id}: {decode_heartbeat(bytes(msg.data))}")
                continue

            node_id = node_id_from_cob_id(msg.arbitration_id, COB_ID_TPDO1_BASE)
            if node_id is not None:
                mask = unpack_channel_mask(bytes(msg.data))
                print(f"[TPDO1]     node {node_id}: {format_channel_mask(mask)} (raw mask 0x{mask:03X})")
                continue

            node_id = node_id_from_cob_id(msg.arbitration_id, COB_ID_TPDO2_BASE)
            if node_id is not None:
                status = decode_tpdo2(bytes(msg.data))
                if status is None:
                    print(f"[TPDO2]     node {node_id}: malformed frame (DLC {msg.dlc} < 8)")
                else:
                    print(
                        f"[TPDO2]     node {node_id}: health={status.health_name} "
                        f"tx_err={status.tx_error_count} rx_err={status.rx_error_count} "
                        f"uptime={status.uptime_seconds}s"
                    )
                continue

            # Not one of ORC's message types -- print raw so the user can still
            # see it (useful for spotting other traffic, or a COB-ID mismatch
            # if ORC's node ID isn't what was expected).
            print(f"[other]     id=0x{msg.arbitration_id:03X} data={bytes(msg.data).hex()}")

    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        bus.shutdown()

    if not seen_nodes:
        print("\nNo ORC heartbeat ever seen. Check: adapter wiring (CAN_H/CAN_L, termination), "
              "board is powered and running canopen_app (not one of the bring-up sketches), "
              "and --bitrate matches the board's locked 125000.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
