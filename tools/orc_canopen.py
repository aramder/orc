"""ORC CANopen wire-protocol constants and encode/decode helpers.

Host-side (Python/python-can) counterpart to firmware/lib/orc_canopen/orc_canopen.h.
Keep the two in sync by hand -- there's no code generation between them, so if
the protocol changes in one, it needs to change in the other. Source of truth
for the actual protocol: docs/can-protocol-research.md.

This module has no side effects and does not touch a CAN bus itself -- it's
pure wire-format encode/decode, imported by the test scripts in this directory.
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

# --- Heartbeat: NMT state byte values ---------------------------------------
# Per CANopenNode's reference values, cited in can-protocol-research.md.
NMT_STATE_NAMES = {
    0x00: "Boot-up",
    0x04: "Stopped",
    0x05: "Operational",
    0x7F: "Pre-operational",
}


def cob_id(base: int, node_id: int) -> int:
    """CiA 301 predefined-connection-set COB-ID: base + NodeID."""
    return base + node_id


def node_id_from_cob_id(msg_id: int, base: int) -> int | None:
    """Inverse of cob_id() -- returns the NodeID if msg_id is base+N for a
    plausible node ID (1-127), else None. Used by the passive monitor to
    recognize traffic without already knowing which node is on the bus."""
    candidate = msg_id - base
    if 1 <= candidate <= 127:
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


@dataclass
class Tpdo2Status:
    health_code: int
    health_name: str
    tx_error_count: int
    rx_error_count: int
    uptime_seconds: int


def decode_tpdo2(data: bytes) -> Tpdo2Status | None:
    """8-byte TPDO2 payload -> Tpdo2Status, or None if the frame is too
    short to be a real TPDO2 (a malformed/foreign frame sharing the COB-ID
    space, which shouldn't happen but is worth guarding against rather than
    raising on a bench with unknown other traffic on the bus)."""
    if len(data) < 8:
        return None
    health_code = data[0]
    uptime_seconds = int.from_bytes(data[3:7], byteorder="little", signed=False)
    return Tpdo2Status(
        health_code=health_code,
        health_name=_TWAI_HEALTH_NAMES.get(health_code, f"unknown (0x{health_code:02X})"),
        tx_error_count=data[1],
        rx_error_count=data[2],
        uptime_seconds=uptime_seconds,
    )


def format_channel_mask(channel_mask: int) -> str:
    """Render a 10-bit channel mask as 'ch1=on ch2=off ...' for readable
    console output."""
    parts = []
    for ch in range(1, 11):
        state = "on" if channel_is_set(channel_mask, ch) else "off"
        parts.append(f"ch{ch}={state}")
    return " ".join(parts)
