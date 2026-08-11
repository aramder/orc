# CAN protocol research — transport architecture and application-layer format

Research pass, 2026-08-01. Answers two questions gating ORC's CAN work: (1) is the
current GPIO21/GPIO20 UART-through-SN65HVD230 bring-up path actually capable of
real CAN, and (2) what should ride on top of it once it is. **No firmware or
schematic changes were made by this pass** — see `firmware/README.md` and
`docs/subcircuit-capture-guide.md`'s Communications/MCU sections for the current
state this builds on, and `docs/design-inputs.md`'s "Primary control path" entry
(resolved 2026-08-01: CAN, exclusively) for why this matters at all.

## 1. UART-vs-TWAI: the transport question, resolved

### Can the ESP32-C3's native TWAI controller reach GPIO21/GPIO20?

**Yes, and it isn't even a routing compromise — it's the more natural fit.** The
ESP32-C3's TWAI (Espressif's name for their CAN 2.0B-compatible protocol
controller) has **no dedicated IO_MUX pin at all**. Per the *ESP32-C3 Series
Datasheet* (v2.4) Table 3-2 and the *ESP32-C3 Technical Reference Manual*'s "IO MUX
and GPIO Matrix" chapter (Peripheral Signal List), TWAI TX/RX are GPIO-Matrix-only
signals — every GPIO-capable pin is an equally valid TWAI pin, selected purely in
firmware (`twai_general_config_t.tx_io` / `.rx_io` in ESP-IDF, or the Arduino-ESP32
`TWAIBus`/`twai.h` wrapper). GPIO21/GPIO20 are ordinary GPIOs with no competing
IO_MUX peripheral assignment that would block this. **This is a firmware
peripheral-configuration change, not a hardware redesign.** The SN65HVD230's
TXD/RXD pins are electrically identical 3.3 V logic inputs/outputs regardless of
which on-chip peripheral drives them — UART1 or TWAI — so no schematic or pinout
change follows from this finding. Confidence: high, primary source (Espressif
datasheet + TRM, both cited above).

### What does a plain UART through a CAN transceiver actually produce?

Not CAN, and not "simplified CAN" — a materially different scheme that happens to
reuse CAN-transceiver silicon for its differential electrical levels.

ISO 11898-1 defines the CAN data link layer (MAC), and the specific things it
requires are exactly the things a UART peripheral does not do:

- **Bitwise, non-destructive arbitration.** Every transmitting node samples the bus
  during the identifier field while it transmits and drops out the instant it
  sends a recessive (1) bit but reads back a dominant (0) — the classic
  wired-AND CAN arbitration mechanism (ISO 11898-1, arbitration/frame-format
  sections). This requires the transmitting peripheral to compare its own output
  against the bus state bit-by-bit, in real time, mid-frame. A UART has no
  concept of this: it transmits a byte and is done: it cannot inspect the bus
  and yield mid-byte. If two UART-driven nodes key up SN65HVD230 TXD pins at
  overlapping times, the result is electrical contention/corruption on
  CAN_H/CAN_L, not resolved arbitration.
- **Bit stuffing.** Real CAN inserts a complementary bit after five consecutive
  identical bits, for clock recovery and to make the frame boundary
  (unambiguous EOF/error-frame sequences) reliable. UART framing (start bit,
  8 data bits, stop bit) has no equivalent and does not stuff.
- **CRC + ACK slot.** Every CAN frame carries a 15-bit CRC and a dedicated ACK
  bit that any *listening* node (not just the sender) drives dominant to
  confirm receipt, independent of the sender's own framing. A UART's parity
  bit (if used at all) is a much weaker per-byte check with no receiver
  acknowledgment mechanism.
- **Error signalling/retransmission.** CAN controllers detect the above
  failures and actively flag them on the bus (error frames), forcing
  retransmission and tracking per-node error counters that can auto-bus-off a
  faulty node. None of this exists at the UART level.

The SN65HVD230 itself does none of this either — per TI's datasheet (SLOS346O,
rev. April 2018, §10.1 Overview), it is a transparent physical-layer line driver
only; arbitration/stuffing/CRC/ACK/error-handling are protocol-controller (MAC
layer) functions that live in the CAN controller silicon — TWAI on this chip — not
in the transceiver, and not obtainable by any transceiver alone. This is exactly
what `firmware/README.md` and `uart_can_bringup/main.cpp`'s own comments already
flag ("a raw UART byte stream through the transceiver, not framed CAN protocol
traffic") — this research confirms that caveat is not a rounding error, it is the
literal difference between "real multi-drop CAN" and "point-to-point differential
serial link that happens to use CAN-shaped voltage levels." Two nodes driving a
shared bus this way is not degraded CAN — arbitration on overlapping transmissions
is simply absent, and the result is electrical contention with no protocol-level
recovery path.

### Recommendation: use the native TWAI controller. Don't ship the UART scheme.

ORC's own docs already establish this isn't a single fixed point-to-point link:
`design-inputs.md`'s "Requirements this places on ORC" section requires
configurable channel mapping because rigOS already has relay-assignment hooks, and
this task's own framing (§3 below) raises multi-unit fleets as a real deployment
shape — i.e., a shared bus with a host/gateway (rigOS side) and potentially more
than one ORC unit, where more than one node can legitimately need to transmit
(command traffic one way, status/heartbeat traffic back, from potentially several
units) without a proprietary time-division scheme to keep them from colliding.
That is precisely the case real CAN arbitration exists to solve, and precisely the
case a UART-through-transceiver link cannot solve at all. Even in a
single-master/single-slave install, using the real TWAI controller costs nothing
extra (same pins, same transceiver, and ESP-IDF/Arduino-ESP32 TWAI drivers are
mature) while keeping the door open to more units later without redesigning the
transport.

**Verdict: real CAN, via the native TWAI controller on GPIO21/GPIO20, not the
UART scheme.** This is a firmware change for a later firmware pass — flagged, not
made by this research task. Concretely, that means: replace
`uart_can_bringup`'s `HardwareSerial(1)`-based transport with ESP-IDF's `twai.h` /
Arduino-ESP32's TWAI driver, configured `tx_io=GPIO21, rx_io=GPIO20`, and re-run
the same class of bring-up test (loopback, then two-node) against real TWAI frames
instead of raw bytes. No `hardware/*.kicad_sch` change is implied — the SN65HVD230
wiring is transport-agnostic between UART and TWAI. Because the fix is "use the
correct peripheral," not "the wiring was wrong," there is no need to rename
anything in the docs away from "CAN" — the hardware and the eventual firmware both
genuinely produce CAN; only the current bring-up sketch's *use* of a UART
peripheral was a stand-in, already labeled as such.

## 2. Application-layer standards survey

ORC's actual payload need, restated from `design-inputs.md`: command up to 10
relay-channel on/off states from a host (rigOS gateway) to one or more ORC units,
with channel-to-function mapping configurable rather than hardcoded, plus
(optionally, not yet a hard requirement per design-inputs.md's "per-channel
current sensing" open item) status/telemetry feedback. Every payload considered
below — a 10-bit channel mask plus a few status/heartbeat bytes — fits inside a
single classical CAN 2.0A data frame (max 8 bytes). **ISO-TP (ISO 15765-2)
segmentation is not needed**; nothing here exceeds one frame, so it is dismissed
from further consideration except to note why.

| Standard | What it specifies | OSS implementation for ESP32-C3? | Fit for ORC |
|---|---|---|---|
| **CANopen** (CiA 301 application/comms layer + CiA 401 generic I/O device profile) | 11-bit COB-ID scheme (`function code (4 bit) << 7 \| node-ID (7 bit)`, CiA 301's *predefined connection set*), NMT node-control/heartbeat, SDO (config/param access), PDO (real-time process data, exactly the "set N discrete outputs" case). CiA 401 layers a standard object dictionary for digital-I/O devices on top: object `6200h` "Write Output 8-Bit" family for outputs, `6000h` "Read Input 8-Bit" for inputs/feedback, mapped into PDOs. **Secondary-source caveat**: CiA specs are paywalled by CAN in Automation; details here are drawn from CAN in Automation's own public knowledge-base pages (`can-cia.org/can-knowledge/cia-401-series-i/o-device-profile`, `.../pdo-protocol-1`) and CANopenNode's public docs, not the purchased CiA 301/401 PDF text — flag confidence as **medium** on exact bit-level object-dictionary layout, **high** on the overall architecture (COB-ID scheme, PDO/SDO/NMT split), which is corroborated across multiple independent secondary sources. | **CANopenNode** (github.com/CANopenNode/CANopenNode) is a mature, actively maintained, MIT-licensed C stack with an existing ESP32 port (`CANopenNode_ESP32`, ESP-IDF-based) and community Arduino-ESP32 wrappers. No authoritative ROM/RAM figure was found even in the project's own issue tracker (maintainers describe it as "varies a lot" with feature/OD-size configuration, no published minimal-config numbers) — treat footprint as **plausible but unconfirmed** against the 4 MB flash/400 KB SRAM budget (`subcircuit-capture-guide.md` MCU section); a minimal single-node, small-OD, no-LSS build is a small fraction of either budget based on general embedded-CANopen-stack experience, but that is an estimate, not a citation. | **Best fit found.** Designed for exactly this device class (discrete digital outputs on a shared industrial/embedded bus), open spec, real OSS stack, and its node-ID/COB-ID addressing natively answers the "multi-unit fleet" deployment shape without inventing an ID-allocation scheme from scratch. Heartbeat (object `1017h`) gives a standard bus-liveness signal that maps directly onto "all relays off if the bus goes quiet" — a fail-safe ORC's firmware should implement regardless of format, but CANopen gives it a standard vocabulary instead of a bespoke one. |
| **SAE J1939** | 29-bit extended ID: 3-bit priority, 18-bit PGN (Parameter Group Number, itself split into data page/PDU format/PDU specific), 8-bit source address. PGN/SPN (Suspect Parameter Number) framing is built for heavy-vehicle broadcast telemetry with a large, standardized parameter catalog. | Several OSS partial stacks exist (mostly PGN decode/encode libraries), but nothing sized for a 10-channel accessory controller — J1939 stacks are typically sized around the standard's much larger SPN catalog and address-claim procedure. | **Poor fit, confirmed by the survey rather than assumed.** J1939's address-claim protocol and PGN catalog exist to support a large, heterogeneous, multi-OEM heavy-vehicle bus (engine, transmission, brakes, etc.) — overkill for a single accessory relay box, and there is no existing PGN for "N discrete relay outputs" worth borrowing; ORC would end up defining a private PGN anyway, at which point J1939's only remaining value is the 29-bit ID's priority field, which CAN 2.0B's plain extended ID already gives without the rest of the stack. Not recommended. |
| **NMEA 2000** | Built directly on CAN 2.0B (250 kbit/s, 29-bit ID, same physical layer family as J1939's PDU framing). Has two PGNs that are a close real-world analog to ORC's job: **PGN 127501** "Binary Switch Bank Status" (up to 28 binary channels per bank, reported on change and periodically) and **PGN 127502** "Switch Bank Control" (command version, write to the same field layout). | The most relevant OSS project is `ttlappalainen/NMEA2000` (Arduino-friendly, has ESP32 support via its CAN-bus abstraction layer), MIT-licensed, actively used in the marine hobbyist/DIY space — a closer cultural match to ORC's overlanding/RV use case than industrial CANopen tooling. | **Genuinely close functional analog** — PGN 127501/127502's bit-per-channel status/command layout is almost exactly ORC's "set channels 1–10" need, and the marine DIY ecosystem (Yacht Devices, Maretron, hobbyist NMEA2000 boards) is a real precedent for exactly this class of device (digital switch/relay banks on a small CAN bus). Weaker than CANopen on two points specific to ORC: NMEA 2000 has no equivalent to CiA 301's clean node-ID-based COB-ID/heartbeat addressing scheme for a "fleet" of like devices (NMEA 2000 device instancing exists but is oriented around one shared marine network of heterogeneous devices, not a family of identical accessory controllers), and the full NMEA 2000 spec requires paid ISO/NMEA membership for the address-claim (ISO 11783-5-derived) procedure's fine print, though PGN 127501/127502's layout is documented well enough in secondary sources (the OSS library's own header comments, forum/vendor documentation) to be usable without it. Confidence: medium — good functional match, weaker on the addressing/fleet story than CANopen. |
| **DeviceNet** (ODVA, CIP over CAN) | CAN 2.0A data-link layer with ODVA's Common Industrial Protocol (CIP) on top — object-oriented, connection-based, similar generation and purpose to CANopen. | ODVA membership (minimum ~$1,000/yr per historical pricing found — see sources) is needed for full spec/conformance-test access; vendor IDs are free but full documentation is not as openly available as CiA's knowledge-base or CANopenNode's OSS stack. No widely-used lightweight OSS DeviceNet slave stack sized for a hobbyist/small-embedded target was found in this search. | **Not recommended.** Functionally similar niche to CANopen but with a worse open-access story (paid ODVA membership gates the spec) and no equivalent to CANopenNode's ready-to-use OSS stack for this class of MCU. Included for completeness per the task's ask; doesn't change the outcome. |
| **ISO-TP (ISO 15765-2)** | Multi-frame transport for payloads >8 bytes (single/first/consecutive/flow-control frames). | N/A — not an application-layer format, a transport segmentation layer. | **Not needed.** Every payload identified for ORC (a 10-channel bitmask command, a status/heartbeat frame) fits in a single classic CAN 2.0 frame's 8-byte payload with room to spare (2 bytes covers 10 channels as a bitmask; see §3). Noted only to close out the task's explicit ask to evaluate it. |
| De facto RV/overlanding/marine conventions outside a formal standard | — | — | **None found worth citing.** The RV/overlanding accessory-control space (e.g. common relay/switch-panel products) is dominated by proprietary, undocumented protocols (vendor-specific CAN dongles, LIN-based panels) rather than open, documented de facto conventions — NMEA 2000's switch-bank PGNs (above) are the closest thing to an open standard actually used in adjacent (marine) accessory-control hardware, and are already captured in the NMEA 2000 row rather than as a separate finding. |

## 3. Recommendation: adopt CANopen, CiA 401-style digital-output profile

CANopen is the recommendation — it is the only candidate in the survey that pairs
a real fit for the payload (PDO-mapped discrete I/O, exactly CiA 401's purpose)
with both an open, freely-readable addressing scheme (CAN in Automation's public
knowledge-base pages cited above cover the predefined connection set in enough
detail to implement without buying the CiA 301 PDF) and an existing OSS stack
(CANopenNode) with a real ESP32 port. NMEA 2000's switch-bank PGNs are a closer
cultural match to the use case but weaker on the fleet-addressing story that this
task specifically raised as a real deployment shape; J1939 and DeviceNet are both
justified rejections, not un-investigated gaps (see table). This is **not** a
case where the survey came up empty — CANopen is a genuine, actionable fit, so no
custom frame format is proposed.

**Confidence note carried through the whole recommendation**: the COB-ID/predefined-
connection-set architecture (high confidence, corroborated across independent
secondary sources) is more solid than the exact CiA 401 object-dictionary index
numbers below (medium confidence, drawn from CiA's own public knowledge-base
summaries and CANopenNode's public docs, not the purchased CiA 301/401 spec text).
Before firmware implementation, cross-check the exact `6200h`/`6000h`
sub-index/bit-mapping conventions against CANopenNode's own example object
dictionaries (`CANopenNode/example` in the repo) or a purchased CiA 401 copy if the
project's budget allows — this doc is enough to design against, not a substitute
for that final check at implementation time.

### Bus bitrate — resolved 2026-08-05: 125 kbit/s

Surfaced from the other side of this integration: `rigos-core` (the Pi host
software that will command ORC) chose its physical CAN adapter for ORC's bus
— a Jhoinrch RH-02 USB-CAN adapter (CANable-clone, STM32G431CBT6), rated up to
1 Mbps — while drafting its own relay-control FR
([rigos-core FR-059](https://github.com/aramder/rigos-core) — cross-repo
reference, not duplicated here), and in doing so noticed **ORC had never
actually locked a bus bitrate anywhere in this repo.**

**Decision: 125 kbit/s**, user's call — this is a dedicated accessory-control
bus (10 relay channels, small periodic/event-driven message set, no telemetry
firehose), so headroom for a much higher bitrate buys nothing real here, and
a slower bitrate gives more margin against harness length, reflections, and
noise on a hand-run automotive/RV cable — the tradeoff the earlier open item
flagged as unanalyzed. All nodes on the bus (ORC units, `rigos-core`'s
adapter, any switch panel from the precedent survey above) must run this
same 125 kbit/s.

**Naming precision worth keeping straight, since it caused genuine confusion
when this was first discussed**: 125 kbit/s is *not* the same thing as
"ISO 11898-3 low-speed/fault-tolerant CAN," even though 125 kbit/s happens to
be that standard's rate ceiling. ISO 11898-3 is a *different physical layer*
(fault-tolerant, survives a single-wire short/open, used for body/comfort
buses like this truck's own 125 kbit/s MS-CAN). **ORC's SN65HVD230 is an
ISO 11898-2 (high-speed) transceiver** — this is a high-speed-CAN transceiver
simply run at a low bitrate, not a fault-tolerant low-speed-CAN physical
layer. Functionally fine for this application (no fault-tolerance
requirement identified for ORC's dedicated bus), but don't describe ORC's bus
as "ISO 11898-3" or "low-speed CAN" in any spec sheet — it isn't, even though
the number matches.

### Arbitration ID (COB-ID) allocation

Per CiA 301's predefined connection set (11-bit standard CAN ID = 4-bit function
code, shifted left 7 bits, OR'd with a 7-bit node ID):

| Message | Function code | COB-ID | Direction |
|---|---|---|---|
| NMT node control | `0000` | `0x000` | host → all nodes (broadcast) |
| Emergency (EMCY) | `0001` | `0x080 + NodeID` | ORC unit → host, fault/error report |
| RPDO1 — relay command | `0100` | `0x200 + NodeID` | host → ORC unit |
| TPDO1 — relay status | `0011` | `0x180 + NodeID` | ORC unit → host |
| TPDO2 — bus health / uptime | — | `0x280 + NodeID` | ORC unit → host, periodic, added 2026-08-05 (see below) |
| Heartbeat (NMT error control) | `1110` | `0x700 + NodeID` | ORC unit → host, periodic liveness |

`NodeID` is a per-unit integer, 1–127, assigned at commissioning. For a
single-unit install, default `NodeID = 1`. For multi-unit fleets, each ORC unit
needs its node ID set at install time. **Update, 2026-08-02**: this open item now
has a design direction — [circuit-draft.md](circuit-draft.md)'s "Node-ID
address input" section — a 4-position DIP switch, node IDs 1–15. **Corrected
twice same day**: (1) read directly by 4 ESP32-C3 GPIOs on Domain A, *not*
through the PCA9555 — the PCA9555 (Domain B) only has power when harness A+ is
present, so a PCA9555-hosted switch would be unreadable during USB-only bench
operation (firmware flashing, testing, field diagnosis with no vehicle harness
connected); (2) silkscreen labels each position by decimal weight (`1 2 4 8`),
not bit index (`BIT0-3`) — sum the ON positions for the node ID, same
convention long used on RS-485/Modbus slave-address DIP switches. Still
design-only, not yet sourced or drawn (Gate 1/2 deliberately deferred). (CiA
305 defines an over-the-bus LSS node-ID-assignment procedure as a more elegant
alternative to physical switches, at the cost of more firmware complexity —
worth considering once fleet deployment is a concrete requirement rather than
a hypothetical.)

### RPDO1 — "set relay channel states" (host → ORC unit)

COB-ID `0x200 + NodeID`, DLC 2 bytes, mapped as CiA 401's "Write Output 8 Bit"
objects (`6200h` sub-index 1 = byte 0, sub-index 2 = byte 1):

| Byte | Bits | Meaning |
|---|---|---|
| 0 | 0–7 | Channels 1–8, one bit per channel, 1 = energize |
| 1 | 0–1 | Channels 9–10, one bit per channel, 1 = energize |
| 1 | 2–7 | Reserved, send 0 |

**Recommended commanding interval: ~1 second or slower.** Real-hardware
testing (`docs/features/BUG-002.md`, `BUG-003.md`, 2026-08-10) confirmed a
1s/2s RPDO1 cadence works cleanly and reliably (12/12 confirmed on a real
board over real CAN). Below ~1s (tested down to 0.5s and 0.3s), TPDO1
confirmation reliability degrades — some commands go unconfirmed, some
TPDO1s report a stale prior state — and ORC's own periodic Heartbeat/TPDO2
can go fully silent for the rest of a fast-cadence session. Root-caused
down to two contributing factors, one fixed and one not: a blocking
USB-CDC diagnostic-print hazard in `canopen_app`'s hot RPDO1 path (fixed,
`printBestEffort()`) improved but did not fully resolve it; the remainder
correlates with real `CAN_ERR_CRTL` (RX+TX error-warning) frames recurring
every ~2-2.4s specifically under fast bidirectional traffic, evidence of a
genuine bus/controller-level condition rather than a firmware logic bug —
see `BUG-003.md` for the full investigation trail. **Not expected to
matter in practice**: a real host commanding relay changes in response to
user/vehicle events has no reason to approach sub-second RPDO1 cadence.
If a future use case genuinely needs faster commanding, this needs real
electrical-level investigation (oscilloscope/protocol analyzer on the
physical bus under the exact failing traffic pattern) before more firmware
changes are attempted — not undertaken as of this note.

### TPDO1 — "relay status / applied state" (ORC unit → host)

COB-ID `0x180 + NodeID`, DLC 2 bytes, same bit layout as RPDO1, mapped from CiA
401's "Read Input 8 Bit" (`6000h`) — reports the state ORC actually applied
(useful even without current sensing, as command-vs-applied confirmation). Per
this doc's original note, a second TPDO was the planned home for anything
beyond relay state — that's now TPDO2, below, though it carries bus-health
telemetry rather than per-channel fault bits since ORC still has no current
sensing (`design-inputs.md`'s open item, unresolved) — if that ever changes,
extend TPDO2 or add a TPDO3 rather than overloading TPDO1's simple 2-byte
relay-state layout.

### TPDO2 — bus health / uptime, added 2026-08-05

**Why this exists**: while discussing the heartbeat message, it became clear
"send some richer status periodically" is a real, separate need from
heartbeat's pure liveness byte — but mixing extra data into the heartbeat
frame itself would break CANopen conformance (CiA 301 defines heartbeat as
exactly 1 byte, DLC=1; a generic CANopen master or the switch panels surveyed
earlier expect that and nothing more). The **CANopen-native** answer is
already built into the spec: every node gets 4 predefined transmit-PDO slots,
and ORC has only used the first (TPDO1). TPDO2 is the second, purpose-built
for exactly this.

**COB-ID `0x280 + NodeID`, DLC 8, periodic** (interval configurable, see
below; default 1000&nbsp;ms / 1&nbsp;Hz, matching heartbeat's own stated
default). Content is deliberately limited to what ORC can actually measure
today — no invented fields:

| Byte | Meaning |
|---|---|
| 0 | CAN controller state — see enum below |
| 1 | TWAI TX error counter (0–255, raw hardware counter value) |
| 2 | TWAI RX error counter (0–255, raw hardware counter value) |
| 3–6 | Uptime, seconds since boot, `uint32` little-endian (CANopen's standard multi-byte convention) — ~136 years of headroom, chosen over a 2-byte field specifically so it never wraps during any realistic continuous-power deployment |
| 7 | Reserved, send 0 |

**Byte 0 state enum — confirmed 2026-08-05 against ESP-IDF's real
`driver/twai.h` (legacy API, what Arduino-ESP32 currently wraps), implemented
in `firmware/lib/orc_canopen/orc_canopen.h`.** `twai_state_t` has exactly
4 members — `TWAI_STATE_STOPPED`, `TWAI_STATE_RUNNING`, `TWAI_STATE_BUS_OFF`,
`TWAI_STATE_RECOVERING` (confirmed identical across ESP-IDF v4.4.x, v5.2.1,
and current `master`, cross-checked against 3 IDF revisions) — with **no
separate error-warning or error-passive state of its own**; the driver only
exposes those 4 coarse states, so the original design sketch's approach of
layering the standard ISO 11898-1/SJA1000-lineage error-confinement
thresholds (warning ≥96, passive ≥128 on either TX or RX error counter) on
top of `RUNNING` is not just reasonable, it's the *only* way to get that
granularity from this driver — confirmed correct, not just plausible. Final
mapping, unchanged from the original proposal: `0`=stopped, `1`=running/
error-active, `2`=error-warning, `3`=error-passive, `4`=bus-off/recovering.

**One version caveat worth carrying forward**: ESP-IDF's `master` branch has
since deprecated this legacy API in favor of a new node-based `esp_twai.h`
API with a *different* struct/enum (`twai_node_status_t`/`twai_error_state_t`)
that *does* expose explicit `TWAI_ERROR_ACTIVE`/`WARNING`/`PASSIVE`/`BUS_OFF`
states natively with `uint16_t` counters — Arduino-ESP32's current stable
releases still target the legacy API this firmware uses, but if the Arduino
core ever moves to a newer bundled IDF version that's switched over, this
mapping (and the packing code) would need revisiting.

**Implementation bug found and fixed, 2026-08-05 review, worth recording
against the "~136 years of headroom" claim above**: the first implementation
of the uptime field computed it as `millis() / 1000` — but `millis()` itself
is a 32-bit *milliseconds* counter that overflows at ~49.7 days, so on a
continuously-powered unit (the realistic deployment case) the reported
uptime would have silently reset to near-zero every ~49.7 days, regardless
of the wire field's own 136-year capacity. The 136-year headroom claim above
is only actually true because this was caught and fixed — `canopen_app` now
derives it from `esp_timer_get_time()` (ESP-IDF's 64-bit
microseconds-since-boot monotonic counter), which has no comparable
wraparound concern. Don't reintroduce `millis()` for this field if this code
is ever touched again.

**Transmission interval — configurable via standard CANopen object, not a
custom field.** CiA 301 already defines a Communication Parameter Record for
every TPDO; TPDO2's is object `1801h`:

| Sub-index | Field | ORC value |
|---|---|---|
| 1 | COB-ID | `0x280 + NodeID` (fixed) |
| 2 | Transmission type | `254` (timer-driven, asynchronous) |
| 3 | Inhibit time | `0` (no minimum gap enforced) |
| **5** | **Event timer** | **milliseconds between sends — the actual "frequency" knob, default `1000`** |

Set it with a plain SDO write: index `1801h`, sub-index `5`, value = desired
interval in **milliseconds**, not Hz — convert at the host
(`interval_ms = 1000 / Hz`). No custom protocol needed; any generic CANopen
configuration tool already knows how to write a TPDO's event timer.

**Persistence**: the request was for this to survive a power cycle.
CANopen's fully-conformant mechanism is object `1010h` ("Store parameters") —
writing a specific ASCII signature to a sub-index tells the device to save
its current Object Dictionary state to non-volatile memory, with `1011h`
("Restore default parameters") as the inverse. **Recommended for ORC's first
implementation: skip `1010h`/`1011h` for now and just auto-persist the
`1801h` sub-index 5 value to the ESP32-C3's NVS (flash-backed key-value
store) immediately on SDO write** — satisfies "persists across power cycles"
without the extra object-dictionary machinery. Flagged as a scope choice, not
a limitation: full `1010h`/`1011h` support is a reasonable later addition if
ORC ever needs to interoperate with a generic CANopen configuration tool that
expects the standard store/restore mechanism specifically, rather than just
persisting on every write.

**TX/RX error-counter width — confirmed 2026-08-05, with one honest residual caveat.**
`twai_status_info_t`'s `tx_error_counter`/`rx_error_counter` fields are
declared `uint32_t` in the struct itself (confirmed against the real ESP-IDF
header, same 3-revision cross-check as above) — but the underlying hardware
(an SJA1000-lineage TWAI peripheral per ISO 11898-1) implements TEC/REC as
8-bit registers, 0–255, entering bus-off (which flips `state` to
`TWAI_STATE_BUS_OFF` rather than letting the counter climb past 256) once
TEC would exceed that range. **No explicit ESP-IDF documentation states this
0–255 range as a guaranteed invariant of the `uint32_t` struct field itself**
— it's implied by the hardware register width and the bus-off transition
logic, not asserted outright. `firmware/lib/orc_canopen/orc_canopen.h`
masks both counters `& 0xFF` when packing into TPDO2's single-byte fields
regardless, so this is safe in practice even if the theoretical edge case
were ever real — implemented defensively rather than left as an open risk.

**Open items:**
- [x] Byte-0 controller-state enum — confirmed against ESP-IDF's real
  `twai_state_t`, see above. Implemented in `firmware/lib/orc_canopen/`.
- [x] TWAI TX/RX error-counter width — confirmed effectively 0–255 in
  practice (hardware register width), defensively masked in code regardless
  of the theoretical struct-field caveat above. See above.
- [x] NVS key name/namespace — `orc_cfg` / `tpdo2_ms`, implemented in
  `firmware/src/canopen_app/main.cpp` via the Arduino `Preferences` library.
- [ ] `1010h`/`1011h` full store/restore support — still deferred, not
  designed. `canopen_app` auto-persists on every SDO write instead, per the
  scope choice above.

### Heartbeat — bus-liveness / fail-safe trigger

COB-ID `0x700 + NodeID`, DLC 1 byte (NMT state), periodic (e.g. 1 Hz, tunable).
**Firmware safety recommendation — implemented 2026-08-05 in `canopen_app`**:
ORC's firmware treats loss of the expected command cadence (RPDO1 timeout,
using CANopen's own heartbeat/node-guarding concept) as a reason to
de-energize all channels rather than hold last state. `canopen_app`'s actual
timeout value (5000&nbsp;ms) is a firmware default, not derived from any
documented host command cadence — RPDO1 is event-driven, not periodic, so
there's no spec'd interval to time against. See
`firmware/src/canopen_app/main.cpp`'s `checkRpdo1Timeout()`.

**Related robustness item, found missing in the 2026-08-05 implementation
review and fixed same day**: detecting a dead bus (via this heartbeat/RPDO1
mechanism, or via TPDO2's health byte) is only half the job — ESP-IDF's TWAI
driver does not recover from a bus-off condition on its own.
`twai_initiate_recovery()` must be called explicitly, and even after recovery
completes the driver lands in `STOPPED`, not `RUNNING` — `twai_start()` must
be called again to actually resume. Without this, a single bus fault would be
correctly *reported* forever (TPDO2's health byte would show `BUS_OFF`/
`RECOVERING`) but never actually recovered from, requiring a physical
power-cycle. `canopen_app`'s `checkTwaiRecovery()` now handles both steps on
a fixed 1&nbsp;second cadence, independent of TPDO2's own (SDO-configurable)
interval.

### Why single-frame is enough, no ISO-TP

RPDO1/TPDO1's payload is 2 bytes (10 channels) plus optional expansion —
comfortably inside a classic CAN 2.0 frame's 8-byte DLC ceiling with 6 bytes of
headroom for anything not yet scoped (e.g. a firmware/config version byte, a
future 6-more-channel expansion). TPDO2 (added 2026-08-05) uses the full 8
bytes but is still a single classic CAN frame, DLC 8 — still no transport-layer
segmentation needed. No currently-known future requirement needs ISO-TP.

## Precedent survey — automotive/accessory-control ecosystems, 2026-08-02

Follow-up research, dispatched as two parallel passes (`can-interface` for
protocol precedent, `veh-lighting-controls` for buyable switch-panel hardware):
does real-world precedent in the automotive/emergency-vehicle/overland
accessory-control space change anything about the CANopen recommendation above,
and is there an existing physical switch panel ORC could point installers to
instead of a custom input device? Neither pass reopens the CANopen decision —
both corroborate it — but the switch-panel survey found a genuinely usable
lead.

### Protocol precedent — does anything beat CANopen for ORC's own format?

| Ecosystem | Open or closed | What's actually documented | Lesson for ORC |
|---|---|---|---|
| **Whelen WeCAN / WeCanX** (CenCom Core/Gold) | **Closed.** Real CAN 2.0 at the physical layer — Whelen's own CenCom Core install/harness docs confirm dedicated CAN-H/CAN-L pins, 20 AWG twisted pair — but no arbitration-ID scheme, payload layout, or message catalog is public anywhere. | Connector pinouts and wire colors only. No DBC, no protocol doc, and no legitimate public reverse-engineering write-up found (installer forums, GitHub, FCC filings all came up empty) despite 15+ years of deployment in a market full of technically capable installers. | **This is the precedent ORC exists to avoid, not to emulate.** The absence of any public teardown after 15+ years is itself evidence of how effectively a closed application-layer protocol locks a market to one vendor's control heads — directly analogous to Motorola's GCAI, the reason ORC exists. Worth citing in this doc as a concrete "why open matters" example, not a design source. |
| **RV-C** (RV Industry Association) | **Nominally open, gated in practice.** Marketed as an "open-source CAN protocol" (rv-c.com) built on J1939 physical/transport with its own PGN catalog, but the current spec text lives behind RVIA membership (manufacturer dues from ~$2,040/yr). | `thomasonw/RV-C` (an OSS Arduino library, itself flagged incomplete/untested by its author) confirms PGNs exist for dimmer/relay-class devices, but no clean "N-channel relay bank" PGN as tidy as NMEA 2000's 127501/127502 could be confirmed from public sources. | Closer *cultural* fit (RV/overland domain vs. CANopen's industrial-automation origin) but **not more open** than what's already recommended — trades CiA's freely-readable public knowledge-base for a membership-gated spec with no clearer message definition to show for it. Not a case for switching. |
| **sPOD (BantamX / SourceLT)** | Closed, and **not actually CAN** — the panel-to-controller link is Ethernet/CAT-6, per sPOD's own marketing. No public protocol spec found; a forum thread shows a user asking for third-party integration and getting no answer. | — | Same "sealed proprietary ecosystem" pattern as WeCAN, on a different bus entirely. Confirms the pattern, not a new data point, and rules sPOD out as an ORC-compatible input device (see hardware survey below — it was independently ruled out there too, for the same non-CAN reason). |
| **Blue Sea/BEP CZone** | **Proprietary application layer riding open NMEA 2000 transport** — the closest analog to what CANopen-on-open-CAN gives ORC. | Community project `canboat` (canboat.github.io) has reverse-engineered CZone's private PGNs — 65280 (Circuit Control), 65282 (Alarm Event), 65283 (Channel State), 65284 (Circuit Status heartbeat), 65290 (Module Announce) — through sustained OSS effort. | **Strongest real precedent for ORC's actual problem shape.** It demonstrates two things at once: (1) even a proprietary app layer on an open transport eventually gets cracked by a determined OSS community, which is a good citation for "closed doesn't stay closed forever, it just wastes years of collective effort getting there" — an argument *for* choosing open (CANopen) up front rather than relying on eventual reverse-engineering; and (2) PGN/circuit-style application framing is proven sufficient for exactly ORC's "control N discrete circuits, report status" semantics — mild corroboration that the CANopen/NMEA2000-shaped approach already chosen is the right shape, not just a defensible one. |

**Bottom line: no change to the CANopen recommendation.** WeCAN and CZone both
validate the "proprietary-on-CAN accessory controller" pattern ORC is explicitly
built to avoid — CZone's `canboat` reverse-engineering is documented proof that
installers/hobbyists do eventually crack these systems if forced to, which is
now cited here as the "why open matters" precedent RV-C and sPOD don't provide
(RV-C for being membership-gated despite the "open" label, sPOD for not being
CAN at all).

### Switch-panel hardware survey — is there a buyable CAN-native input device?

ORC's board itself has no local buttons (`design-inputs.md`'s "Primary control
path" decision — ORC is commanded entirely over CAN by an external node). The
donor Motorola unit's external node was an APX radio head; ORC drops that
requirement and just needs "some CAN node sends RPDO1 commands." This survey
asked whether an existing, buyable physical switch panel could serve as that
node, instead of a from-scratch button-panel design.

| Product | Buyable | Protocol | Verdict |
|---|---|---|---|
| **Whelen control heads** (CenCom Core/Gold) | Yes, as accessory line items (Sirennet, Ferno, Mega-Tech) | WeCanX — closed, pair-only with Whelen's own controllers, no public frame format | **Dead end** — right form factor, wrong (closed) protocol. Same finding as the precedent survey above. |
| **sPOD BantamX / SourceLT** | Yes | Proprietary, Ethernet/CAT-6 (not CAN) | **Dead end** — not even the right physical bus. |
| **Blue Sea CZone keypads** (6-/12-button) | Yes, ~$220 | Proprietary CZone PGNs (65280 etc.), not the open PGN 127501/127502 pair | **Dead end as a panel** — same closed layer as the precedent-survey finding, confirmed independently by this pass. |
| **Carling Technologies CKP-Series** (2×2–2×6 backlit keypad, IP69, laser-etched, per-button LED, ~1M actuation cycles) | Yes, live catalog part (Mouser/TTI/Littelfuse) | **SAE J1939-compliant CAN**, per-vendor interface specification exists (`carlingtech.com`/TTI-hosted PDF) | **Strongest lead found.** Real vehicle-grade hardware, standards-based (not a closed pairing scheme), available through ORC's normal live-catalog sourcing channel. See caveat below — the interface spec PDF didn't parse as machine-readable text this pass (image/encoded content stream, same recurring PDF-extraction limitation this project has hit before on datasheets), so exact PGN/SPN assignment for button state and LED control is **not yet independently confirmed**, only the vendor's own "J1939-compliant" claim. Needs a direct manual read of the interface spec before this is treated as locked. |
| **Carling VM-Series** (3-/6-switch multiplexed rocker pod, IP68, "Contura" switch feel) | Yes, live catalog part | Vendor states compatible with **both CANopen and J1939** networks, field-configurable via Carling's "LF Logik" tool | Same family as CKP-Series, rocker-switch form factor instead of keypad — worth considering as an alternative front panel style, same PGN-confirmation caveat applies. |
| **Yacht Devices YDSC-04N "Switch Control"** | Yes, ~$149 (Yacht Devices US) | Bridges physical momentary/rocker switch inputs onto standard **NMEA 2000 PGN 127502 (Binary Switch Control) / 127501 (Binary Status)** | Not a panel itself — a wiring-terminal-to-NMEA2000 bridge box. Directly matches the PGN pair already flagged in this doc's standards survey (above) as ORC's closest NMEA 2000 functional analog. Would need real switches/rockers wired to its terminals separately. |
| **Off-road switch pods** (Baja Designs, Rigid, sSwitch, etc.) | Yes | Simple relay-trigger wired panels, no CAN found | Confirms this doc's earlier standards-survey conclusion — no CAN-native option exists in this specific market segment. |
| **DIY/OSS fallback** | N/A | `ttlappalainen/NMEA2000` or `CANopenNode`, both ESP32-capable (already cited above) | No dedicated open-source button-panel hardware project found, but either stack makes a DIY panel (buttons + small ESP32 + SN65HVD230, emitting a CANopen RPDO or NMEA2000 PGN 127502) a modest build on top of stacks already evaluated in this doc — not a from-scratch protocol design. |

**Recommendation, reframed 2026-08-02**: the actual goal here isn't picking one
winning panel to lock ORC to — it's giving installers as many legitimate,
standards-based ways to command ORC over CAN as possible, the same way ORC
itself is meant to be an open alternative rather than a new closed one. Nothing
in this survey should read as "ORC only supports Carling." What this survey
established is that **CANopen's own RPDO1/COB-ID scheme (already the primary
interface) is protocol-agnostic on the input side** — anything that can put the
right bytes on the bus at `0x200+NodeID` counts, whether that's a hand-rolled
ESP32 button panel, a host application (rigOS or otherwise), or a bridged OEM
device. Carling's CKP-Series/VM-Series and Yacht Devices' YDSC-04N are each one
*option* among what should be a growing list, not a dependency: Carling for a
single-SKU vehicle-grade keypad/rocker (via a J1939-decode bridge), Yacht
Devices for a NMEA-2000-native bridge-plus-separate-switches path, and a DIY
ESP32+CANopenNode panel (both already cited above) for anyone who wants to
build their own. Document each real option as it gets confirmed; don't narrow
to one.

**Open items added by this survey:**
- [ ] Directly read Carling's CKP-Series interface specification (PDF at
  `carlingtech.com`/TTI, didn't parse as text via automated fetch this pass)
  to confirm real PGN/SPN assignment before documenting it as a confirmed
  option (not "the" option).
- [ ] Scope a J1939-decode-to-CANopen-RPDO1 bridge as a firmware task once
  Carling's frame format is confirmed — not yet designed, just identified as
  "likely straightforward, runs on ORC's existing MCU," and general enough to
  reuse for any other J1939-native panel that turns up later.
- [ ] Keep surveying — this list (Carling, Yacht Devices, DIY) is a starting
  point, not exhaustive; add more confirmed-open options as they're found
  rather than treating this pass as final.

## Sources

- Espressif, *ESP32-C3 Series Datasheet*, v2.4 — Table 3-2 (TWAI GPIO Matrix
  assignment), §3 (strapping/boot timing, cited for unrelated GPIO9 question in
  `subcircuit-capture-guide.md`, confirms same datasheet is the primary source
  used throughout this repo's ESP32-C3 work).
- Espressif, *ESP32-C3 Technical Reference Manual* — "IO MUX and GPIO Matrix"
  chapter, Peripheral Signal List; "Two-wire Automotive Interface (TWAI)"
  chapter.
- Texas Instruments, *SN65HVD230/1/2* datasheet, SLOS346O (rev. April 2018),
  §7 Pin Functions, §10.1 Overview — transceiver is a transparent physical-layer
  line driver, no MAC-layer function.
- ISO 11898-1 — CAN data link layer (arbitration, bit stuffing, CRC, ACK, error
  signalling); referenced via its well-established public description (full text
  is paywalled by ISO), corroborated by TI/Bosch/Microchip CAN primers using the
  same mechanism description.
- CAN in Automation (CiA), public knowledge-base: `can-cia.org/can-knowledge/cia-401-series-i/o-device-profile`,
  `can-cia.org/can-knowledge/pdo-protocol-1`, `can-cia.org/can-knowledge/generic-device-profiles`
  — secondary source for CiA 301/401 details; full CiA 301/401 spec text is
  paywalled, not consulted directly.
- CANopenNode project (github.com/CANopenNode/CANopenNode) and
  CANopenNode_ESP32 (github.com/sicrisembay/CANopenNode_ESP32) — OSS stack and
  ESP32 port; no authoritative ROM/RAM figure found even in the maintainers' own
  issue tracker (github.com/CANopenNode/CANopenNode/issues/302) — footprint
  claim in the table above is flagged as an estimate, not confirmed.
- NMEA 2000 PGN 127501/127502 — described via Victron Energy's public PGN
  reference (`victronenergy.com/live/ve.can:pgn_details`) and the
  `ttlappalainen/NMEA2000` OSS library; full NMEA 2000 spec text is paywalled.
- SAE J1939 structure — described via multiple independent secondary technical
  summaries (CSS Electronics, Kvaser, embeddeduse.com); full J1939 spec text is
  paywalled, not consulted directly.
- ODVA/DeviceNet — `odva.org`, `can-cia.org/can-knowledge/devicenet`; membership
  cost figure (~$1,000/yr) is from a historical (early-2000s) secondary source
  found via web search, not independently re-verified against current ODVA
  pricing — flagged as low-confidence and only used to support a "less open than
  CiA/CANopenNode" comparison, not a hard number relied on elsewhere.
- `docs/design-inputs.md` — "Primary control path" (resolved 2026-08-01) and
  "Requirements this places on ORC" sections, cited not restated.
- `docs/circuit-draft.md` — CAN interface/termination/ESD decision entries,
  cited not restated.
- `docs/subcircuit-capture-guide.md` — MCU section (ESP32-C3 pinout,
  GPIO21/20 UART assignment, 4 MB flash/400 KB SRAM budget) and Communications
  section (SN65HVD230 wiring), cited not restated.
- `firmware/README.md` and `firmware/src/uart_can_bringup/main.cpp` — existing
  bring-up work and its own "raw UART, not framed CAN" caveat, which this
  research resolves rather than repeats.

**Added 2026-08-02 (precedent survey):**
- Whelen CenCom Core install manual (`manualslib.com`) and CORE harness
  documentation (`commlineincupfit.com`) — confirms WeCanX's physical CAN
  pinout; no protocol/frame-format detail found in either.
- `rv-c.com` and `github.com/thomasonw/RV-C` — RV-C's public marketing/PGN
  scope; full spec text confirmed gated behind RVIA membership
  (`rvia.org/membership/membership-categories-and-dues`).
- `canboat.github.io/canboat/canboat.html` — community NMEA 2000/CZone
  reverse-engineering project, source for CZone's reverse-engineered PGN list.
- sPOD BantamX product page (`4x4spod.com/8-switch-systems`) — confirms
  Ethernet/CAT-6 panel-to-controller link, not CAN.
- Carling Technologies CKP-Series and VM-Series datasheets/product pages
  (`carlingtech.com`, `tti.com`, `mouser.com`) — J1939/CANopen compliance
  claims; the CKP-Series interface specification PDF was located but did not
  parse as extractable text this pass (image/encoded content stream) — **not
  yet independently verified**, flagged as an open item above.
- Yacht Devices YDSC-04N product page (`yachtd.com` / Yacht Devices US
  reseller) — NMEA 2000 PGN 127501/127502 switch-input bridge.
