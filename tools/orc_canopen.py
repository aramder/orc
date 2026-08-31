# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Aram Dergevorkian
"""ORC CANopen wire-protocol constants and encode/decode helpers.

Host-side (Python/python-can) counterpart to firmware/lib/orc_canopen/orc_canopen.h.
Keep the two in sync by hand -- there's no code generation between them, so if
the protocol changes in one, it needs to change in the other. Source of truth
for the actual protocol: docs/can-protocol-research.md.

Mostly pure wire-format encode/decode, no CAN bus I/O of its own -- with one
deliberate exception, drain_stale_frames(), kept here specifically because
both test scripts in this directory need identical behavior from it (see its
own docstring for why it exists).
"""

from __future__ import annotations

from dataclasses import dataclass

# --- COB-ID allocation, CiA 301 predefined connection set -------------------
# docs/can-protocol-research.md, "Arbitration ID (COB-ID) allocation".
COB_ID_TPDO1_BASE = 0x180  # ORC -> host, relay status
COB_ID_RPDO1_BASE = 0x200  # host -> ORC, relay command
COB_ID_TPDO2_BASE = 0x280  # ORC -> host, bus health/uptime
COB_ID_SDO_TX_BASE = 0x580  # ORC -> host, SDO server response
COB_ID_SDO_RX_BASE = 0x600  # host -> ORC, SDO client request
COB_ID_HEARTBEAT_BASE = 0x700  # ORC -> host, periodic liveness

# Locked 2026-08-05 -- docs/can-protocol-research.md's "Bus bitrate" section.
# Every node on the bus must agree on this value.
BUS_BITRATE = 125_000

# All 10 channel bits set (bits 0-9) -- the "all relays on" RPDO1/TPDO1 mask.
ALL_CHANNELS_MASK = (1 << 10) - 1

# --- Heartbeat: NMT state byte values ---------------------------------------
# Per CANopenNode's reference values, cited in can-protocol-research.md.
NMT_STATE_NAMES = {
    0x00: "Boot-up",
    0x04: "Stopped",
    0x05: "Operational",
    0x7F: "Pre-operational",
}


def drain_stale_frames(bus) -> int:
    """Non-blocking drain of anything already sitting in the bus's receive
    queue, returning the count discarded. Call this immediately after
    opening a bus and before any timeout-based wait.

    Real bench finding, 2026-08: a fresh python-can session opened against
    an adapter that's had earlier sessions opened/killed against it (e.g. a
    prior run stopped by a hard timeout instead of a clean shutdown()) can
    inherit a backlog of already-buffered frames from those earlier
    sessions. Draining them looks instantaneous in host wall-clock time
    (they're already sitting in a queue, not arriving live), but each one
    still satisfies a `recv(timeout=remaining)` call in a wait loop -- so an
    un-drained backlog can make a 2-second timeout loop take much longer
    than 2 seconds to actually reach its deadline, burning through the
    backlog first. This isn't a python-can or gs_usb bug, just something
    bench tooling needs to account for explicitly. Takes an already-open
    can.Bus (typed loosely here to avoid this module importing `can` for a
    type hint alone -- see the module docstring's "no side effects" note;
    this function is the one deliberate exception, kept here because both
    scripts need identical behavior)."""
    count = 0
    while bus.recv(timeout=0) is not None:
        count += 1
    return count


def cob_id(base: int, node_id: int) -> int:
    """CiA 301 predefined-connection-set COB-ID: base + NodeID."""
    return base + node_id


def node_id_from_cob_id(msg_id: int, base: int) -> int | None:
    """Inverse of cob_id() -- returns the NodeID if msg_id is base+N for a
    plausible node ID (0-127), else None. Used by the passive monitor to
    recognize traffic without already knowing which node is on the bus.

    0 is technically not a valid CiA 301 node ID (valid range is 1-127) --
    but a board with every DIP-switch position open reads 0, and real bench
    hardware has been observed running that way (its CAN interface stays
    dormant until first polled, per the "status poll" mechanism these tools
    now use at startup -- see poll_status() in the test scripts). Accepting
    0 here is a pragmatic bench-tooling choice, not a claim that 0 is a
    conformant node ID to actually ship a fleet on."""
    candidate = msg_id - base
    if 0 <= candidate <= 127:
        return candidate
    return None


def decode_heartbeat(data: bytes) -> str:
    """1-byte NMT state -> human-readable name (or a hex fallback for an
    unrecognized value -- ORC's own firmware should never send one, but a
    third-party CANopen node sharing the bus might use a state this doesn't
    list)."""
    if len(data) < 1:
        return "malformed (empty)"
    return NMT_STATE_NAMES.get(data[0], f"unknown (0x{data[0]:02X})")


def pack_channel_mask(channel_mask: int) -> bytes:
    """10-bit channel mask (bit n-1 = channel n, 1=energize) -> 2-byte
    RPDO1/TPDO1 payload. Matches firmware's orcPackChannelMask()."""
    return bytes([channel_mask & 0xFF, (channel_mask >> 8) & 0x03])


def unpack_channel_mask(data: bytes) -> int:
    """Inverse of pack_channel_mask(). Matches firmware's
    orcUnpackChannelMask() -- reserved bits (byte1 bits 2-7) are masked off
    defensively regardless of what was actually sent."""
    if len(data) < 2:
        return 0
    return data[0] | ((data[1] & 0x03) << 8)


def channel_mask_set(channel: int, on: bool, current_mask: int = 0) -> int:
    """Set/clear a single 1-indexed channel (1-10) bit in a mask, leaving
    every other channel's bit untouched."""
    if not 1 <= channel <= 10:
        raise ValueError(f"channel must be 1-10, got {channel}")
    bit = 1 << (channel - 1)
    return (current_mask | bit) if on else (current_mask & ~bit)


def channel_is_set(channel_mask: int, channel: int) -> bool:
    if not 1 <= channel <= 10:
        raise ValueError(f"channel must be 1-10, got {channel}")
    return bool((channel_mask >> (channel - 1)) & 0x01)


# --- TPDO2: bus health / uptime ----------------------------------------------
_TWAI_HEALTH_NAMES = {
    0: "stopped",
    1: "active (error-active, running normally)",
    2: "warning (error-warning)",
    3: "passive (error-passive)",
    4: "bus-off / recovering",
}


# FR-002: byte-7 relay-hardware fault bitmask (previously reserved/always 0).
# Mirror of OrcRelayFault in firmware/lib/orc_canopen/orc_canopen.h.
RELAY_FAULT_HARDWARE_ABSENT = 1 << 0


@dataclass
class Tpdo2Status:
    health_code: int
    health_name: str
    tx_error_count: int
    rx_error_count: int
    uptime_seconds: int
    relay_fault: int
    relay_hardware_absent: bool


def decode_tpdo2(data: bytes) -> Tpdo2Status | None:
    """8-byte TPDO2 payload -> Tpdo2Status, or None if the frame is too
    short to be a real TPDO2 (a malformed/foreign frame sharing the COB-ID
    space, which shouldn't happen but is worth guarding against rather than
    raising on a bench with unknown other traffic on the bus)."""
    if len(data) < 8:
        return None
    health_code = data[0]
    uptime_seconds = int.from_bytes(data[3:7], byteorder="little", signed=False)
    relay_fault = data[7]
    return Tpdo2Status(
        health_code=health_code,
        health_name=_TWAI_HEALTH_NAMES.get(health_code, f"unknown (0x{health_code:02X})"),
        tx_error_count=data[1],
        rx_error_count=data[2],
        uptime_seconds=uptime_seconds,
        relay_fault=relay_fault,
        relay_hardware_absent=bool(relay_fault & RELAY_FAULT_HARDWARE_ABSENT),
    )


# --- SDO -- one object, index 1801h sub-index 5 (TPDO2 event timer, ms) ----
# Matches firmware/lib/orc_canopen/orc_canopen.h's orcHandleSdoRequest() wire
# format exactly -- CiA 301 expedited-transfer encoding. This is also used
# as a "status poll": real bench hardware has been observed staying
# passive on the CAN bus (no Heartbeat, doesn't respond to RPDO1) until
# it's been sent *something* addressed to it first -- see poll_status() in
# the test scripts, which sends this SDO read at startup specifically to
# provoke that first response, in addition to it being a genuine read of
# ORC's configured TPDO2 interval.
SDO_OBJECT_INDEX = 0x1801
SDO_OBJECT_SUBINDEX = 0x05

_SDO_ABORT_CODES = {
    0x06020000: "Object does not exist",
    0x06090011: "Sub-index does not exist",
    0x05040001: "Command specifier not valid or unknown",
}


def build_sdo_read_request(index: int = SDO_OBJECT_INDEX, subindex: int = SDO_OBJECT_SUBINDEX) -> bytes:
    """8-byte CiA 301 'initiate upload' (read) request. ccs=2 (bits 7-5 of
    byte0 = 0b010 = 0x40), index little-endian, sub-index, rest reserved/0."""
    return bytes([0x40, index & 0xFF, (index >> 8) & 0xFF, subindex, 0, 0, 0, 0])


@dataclass
class SdoResponse:
    success: bool
    index: int
    subindex: int
    value: int | None = None  # only set on success -- 2-byte value (this object's only real width)
    abort_code: int | None = None  # only set on failure
    abort_name: str | None = None


def decode_sdo_response(data: bytes) -> SdoResponse | None:
    """Decodes an 8-byte SDO server response -- either a successful
    'initiate upload' response (ccs=2, byte0=0x4B for this object's fixed
    2-byte width) or an abort (byte0=0x80). Returns None if the frame is
    too short to be a real SDO response."""
    if len(data) < 8:
        return None
    index = data[1] | (data[2] << 8)
    subindex = data[3]
    if data[0] == 0x80:
        abort_code = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24)
        return SdoResponse(
            success=False, index=index, subindex=subindex,
            abort_code=abort_code,
            abort_name=_SDO_ABORT_CODES.get(abort_code, f"unknown (0x{abort_code:08X})"),
        )
    # Anything else is treated as a successful expedited-upload response --
    # this tooling only ever talks to the one object above, so byte0's
    # exact e/s/n bits aren't separately validated (matches the firmware
    # server's own "single fixed-width object" simplification).
    value = data[4] | (data[5] << 8)
    return SdoResponse(success=True, index=index, subindex=subindex, value=value)


def format_channel_mask(channel_mask: int) -> str:
    """Render a 10-bit channel mask as 'ch1=on ch2=off ...' for readable
    console output."""
    parts = []
    for ch in range(1, 11):
        state = "on" if channel_is_set(channel_mask, ch) else "off"
        parts.append(f"ch{ch}={state}")
    return " ".join(parts)
