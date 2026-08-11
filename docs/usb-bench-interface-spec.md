# USB bench/debug interface — spec for firmware implementation

Status: **implemented, real-hardware confirmed, and consolidated into
`canopen_app`** (2026-08-05, FR-004 — see
[`docs/features/FR-004.md`](features/FR-004.md)). This protocol originally
shipped as its own standalone `usb_bench` sketch/environment
([`docs/features/FR-001.md`](features/FR-001.md), now superseded); it now
lives in [`firmware/src/canopen_app/main.cpp`](../firmware/src/canopen_app/main.cpp)
alongside the CAN protocol, driving the same PCA9555 state, per the user's
explicit ask to be able to drive relays from either CAN or USB. This
document remains the wire-protocol/rationale source of truth — the
implementation tracks it, doesn't fork it. Real-hardware confirmed
2026-08-05: all 10 relays driven ON over this protocol on a real, powered
ORC PCB, 0.47A real coil current observed (matches the ~0.45A design
target).

## Scope — read this before anything else

**This is a bench/development interface, not a production control path.**
`docs/design-inputs.md`'s "Primary control path" section resolved
2026-08-01: *"CAN, exclusively. No wireless (WiFi/BLE) at all."* That
resolution is about the ESP32-C3 module's **radio** losing to CAN for
application control — it says nothing about the module's native USB-C,
which survives on the board regardless, wired only for
programming/flashing. This spec proposes using that same, already-present
USB-C port for a serial *bench* interface — no new connector, no BOM
change, no board respin, and no contradiction of the resolved CAN-exclusive
decision. In the field, on a vehicle, ORC is still commanded over CAN only.
This interface exists for firmware bring-up and bench diagnostics before
CAN hardware is soldered/wired, and as an optional bench tool afterward.

**Do not treat this as a reason to relax the CAN-exclusive decision.** If
that ever needs revisiting, it's its own decision with its own tradeoffs
(mechanical clearance, connector count) — this document takes it as given.

## Why rigOS wants this

rigOS's `io_relay` daemon (in the sibling `rigos-core` repo, FR-059) already
treats the relay bus as a pluggable transport — I2C expanders and, as of
FR-059, ORC over CAN (`services/io_relay/orc_can.py`). That CAN backend is
validated end-to-end against real electrical hardware (two USB-CAN
adapters, real bus, 0 errors), but bringing up ORC's *own* firmware still
needs CAN transceiver hardware wired correctly before anything on the ORC
side can be exercised at all. A USB-serial bench interface lets rigOS (or
a human at a terminal) drive and observe ORC's relay-channel logic directly
over the same cable used to flash it — no transceiver, no second CAN
adapter, no bus wiring — while that firmware is still being written and
debugged. It's the direct firmware-side analog of what
`tests/test_orc_can_bench_hardware.py` already proved works on the
rigos-core side once real CAN hardware exists.

It is *not* meant to become a second thing rigOS has to support in
production — CAN stays the one real transport there. A future rigos-core
bus backend for this (e.g. `orc_usb.py`, `transport: usb`) would be
explicitly scoped as **bench-only**, mirroring how `orc_can.py` is already
the real thing. That rigos-core-side backend is not part of this spec and
is not needed for this spec to be useful on its own (a human can talk to
this protocol directly with a serial terminal).

## Transport

ESP32-C3 native USB, CDC-ACM class (`TinyUSB`/Arduino-ESP32's built-in USB
CDC, or ESP-IDF's `tinyusb_cdcacm` component — implementation's choice).
Appears as `/dev/ttyACM0`-class device host-side; no baud rate actually
applies (USB CDC ignores it, but most serial libraries — including
`pyserial` — still require a value be passed; document that in the
firmware's own README so nobody chases a phantom baud-rate mismatch).

Point-to-point by construction (one USB cable = one ORC unit) — unlike the
CAN protocol, this interface has **no NodeID concept**. Channel numbering
(1-10) is identical to the CAN protocol's for consistency, but there's
nothing analogous to CAN's multi-unit fleet addressing to design here.

## Framing

Plain ASCII, one command or response per line, `\n`-terminated (`\r\n` from
a host should be tolerated — strip trailing `\r`). Deliberately not binary:
this needs to be usable by typing into a plain serial terminal
(`screen`/`minicom`/PlatformIO's own `-t monitor`, already used at
115200 baud per `firmware/README.md` for the UART bring-up sketches) during
firmware bring-up, not just by a script.

- Max line length: 128 bytes. Anything longer: respond `ERR LINE_TOO_LONG`
  and discard until the next `\n`.
- Case-insensitive commands recommended (easier to type by hand); firmware
  implementer's call.
- Every command produces exactly one response line (never zero, never
  multi-line) — keeps host-side parsing trivial, one `readline()` per
  request.

## Commands (host -> ORC)

| Command | Response | Notes |
|---|---|---|
| `PING` | `PONG <fw_version>` | Liveness + identify. `<fw_version>` is a short build identifier (git short-SHA or a firmware-defined version string), not a fixed format this spec dictates. |
| `VERSION` | `VERSION <fw_version> <build_date>` | Fuller build info than `PING`'s inline version, for diagnosing which build is flashed. |
| `SET <ch> <ON\|OFF>` | `OK SET <ch> <ON\|OFF>` or `ERR <code>` | `<ch>` is 1-10. Applies the channel state — same "energize/de-energize" semantics as CAN's RPDO1, just one channel per command instead of a bitmask (USB has no framing pressure forcing a coalesced format the way a single CAN frame's 8-byte payload does). |
| `GET <ch>` | `STATE <ch> <ON\|OFF>` or `ERR <code>` | Single-channel read of *applied* state (not requested-but-not-yet-applied — mirrors CAN's TPDO1 being the real read-back, not an optimistic echo). |
| `STATUS` | `STATUS <10-char 0/1 string>` | All 10 channels at once, channel 1 first, e.g. `STATUS 0010000000` means channel 3 on, everything else off. Fixed-width and trivially parseable. |

## Unsolicited lines (ORC -> host, no command needed)

This is the one place this spec deliberately does **more** than the CAN
protocol, based on a real gap FR-059 found and documented on the
rigos-core side: CAN's TPDO1 is event-driven only (sent once, on RPDO1
receipt), never periodic — so a host process that starts *after* the last
change has no way to learn current state short of re-commanding it. CAN
has real reasons to stay minimal (bus bandwidth, arbitration cost of
frequent frames); USB over a dedicated point-to-point link doesn't share
those constraints, so there's no reason to repeat that gap here:

- **`HB <uptime_ms>`** — sent every 1000 ms (tunable), unconditionally,
  whether or not anything changed. Lets a listener confirm liveness without
  polling, same role as CAN's heartbeat.
- **`STATE <ch> <ON\|OFF>`** — sent once, immediately, whenever a channel's
  *applied* state actually changes, regardless of whether the change came
  from a `SET` command or (if the firmware ever drives a channel from
  local logic, e.g. a fault condition) some other cause. This is the line
  that closes the "fresh listener can't learn current state" gap: a host
  that opens the port and waits briefly will see either a `STATE` line
  (something changed recently) or can just send `STATUS` itself for an
  immediate full snapshot — either path works, unlike CAN's.

Because unsolicited lines can interleave with command responses, host-side
parsers must treat every line independently (check its first token) rather
than assuming "the next line is always my command's response."

## Errors

`ERR <code>` on any malformed/out-of-range/failed command. Suggested codes
(firmware implementer may extend, but keep the shape `ERR <UPPER_SNAKE>`,
no spaces in the code itself, so it stays one grep-able token):

- `ERR BAD_CHANNEL` — channel outside 1-10.
- `ERR BAD_ARGS` — wrong number of arguments / unparseable state token.
- `ERR UNKNOWN_CMD` — unrecognized command word.
- `ERR LINE_TOO_LONG` — see Framing.

## Fail-safe behavior — decided during implementation, 2026-08-05

`can-protocol-research.md`'s own heartbeat section recommends CAN command
loss (RPDO1 timeout) de-energize all channels rather than hold last state.
Whether the *same* watchdog should apply to this USB interface was left
open here on purpose. **Decided in `docs/features/FR-001.md`**: yes, for
philosophical consistency with the CAN path — a bench tool that behaves
differently from the real control path on "commander went away" was judged
the wrong default. Implemented as a 30000ms idle-activity timeout (any
received line, valid or malformed, resets it) — 6x longer than CAN's
5000ms RPDO1 timeout, since this is an interactive human-typed interface
where a person pausing to read output shouldn't trip the same threshold a
genuinely vanished CAN host should. Full rationale and the code:
`firmware/src/usb_bench/main.cpp` and `firmware/README.md`'s `usb_bench`
section.

## Explicitly out of scope for this spec

- A rigos-core-side bus backend (`orc_usb.py`) consuming this protocol —
  natural follow-up once this firmware exists, not needed for this spec to
  be useful (a human with a serial terminal is a complete consumer on its
  own).
- Multi-unit addressing — point-to-point only, see Transport.
- Binary/compact framing — deliberately ASCII, see Framing's rationale.
- Any change to the CAN protocol or the resolved CAN-exclusive production
  control path.