# ORC firmware

**Three kinds of content live here now, added in three separate passes — don't conflate them.** The four `*_bringup`/`can_address_bringup` sketches exist to answer one narrow question — does the MCU pinout the hardware side has settled on actually work at the IO level? — before real application logic got written; see [`.claude/firmware-io-verification-prompt.md`](../.claude/firmware-io-verification-prompt.md) for that task. **`canopen_app` (added 2026-08-05) is different: it IS ORC's first real application firmware** — actual CANopen relay control, per [`docs/can-protocol-research.md`](../docs/can-protocol-research.md) and [`.claude/can-application-firmware-prompt.md`](../.claude/can-application-firmware-prompt.md) — built on top of the bring-up work, not replacing it. **`usb_bench` (added 2026-08-05) is a second piece of application firmware, but a bench/dev tool, not the production control path** — a USB-serial relay-control interface over the same native USB-C port already wired for flashing, per [`docs/usb-bench-interface-spec.md`](../docs/usb-bench-interface-spec.md) and [`docs/features/FR-001.md`](../docs/features/FR-001.md). See each one's own section below.

**No ORC board is in hand yet.** As of 2026-08-04 the board has been **sent out to fab** (per `hardware/mcu.kicad_sch`'s U7 — a real, built ESP32-C3-SuperMini symbol/footprint, not just a placeholder) but hasn't come back — nothing here has run against ORC's actual hardware, and no PCA9555 or SN65HVD230 has been tested. All four sketches build successfully (verified: `pio run -e i2c_scanner -e pca9555_bringup -e uart_can_bringup -e can_address_bringup`, all SUCCESS against the `esp32-c3-devkitm-1` stand-in board), and two of them have also run live on a bare, generic ESP32-C3 dev kit connected to this session's machine (nothing else attached to it) — see "Real-hardware run, 2026-08-01" below for exactly what that does and doesn't confirm. `can_address_bringup` and the address-print addition to the other three sketches are build-verified only, not yet re-flashed to real hardware — the dev kit used for the earlier run was no longer enumerated on this machine when this pass ran. **Firmware's pin assignment was corrected once already, same day, after the board had already gone to fab** — see "CAN node address, as-fabbed" below; the schematic is now the source of truth firmware must match, not the other way around. Treat any claim in this directory carefully: "runs on a generic ESP32-C3 dev kit" is not the same thing as "verified on ORC's board," since the PCA9555 and SN65HVD230 pieces of the design are still completely untested.

## Board stand-in

`platformio.ini` targets PlatformIO's `esp32-c3-devkitm-1` board definition. This is a **stand-in**, not the final hardware — the actual target is a generic, widely-cloned "ESP32-C3 Super Mini"-class board (bare ESP32-C3FN4 die, 4MB flash, no PSRAM, USB-C, 2×7-pin THT headers) with no single confirmed SKU yet. `esp32-c3-devkitm-1` is electrically close enough (same chip class) to compile and flash against for IO-level bring-up; swap it for a real board definition once a specific listing is bought and its exact silkscreen/pin breakout is confirmed.

## Pin assignment under test

Per `hardware/mcu.kicad_sch` (U7, as-fabbed) and `docs/subcircuit-capture-guide.md`'s MCU section — full 13-GPIO "Super Mini" header pinout (GPIO0-10, GPIO20, GPIO21):

| Function | Pins |
|---|---|
| I2C (SDA/SCL) | GPIO8 / GPIO9 |
| UART (TX/RX, to SN65HVD230) | GPIO21 / GPIO20 |
| CAN node address (NODE_ID0-3) | GPIO0 / GPIO1 / GPIO2 / GPIO3 |
| Spare (unused) | GPIO4-7 (JTAG-default), GPIO10 |

## Configurable CAN node address, as-fabbed

4-bit node address (0-15), read directly by the ESP32-C3 — deliberately **not** through the PCA9555, since address-select logic belongs on the MCU's own non-isolated Domain A side, not across the ADuM1250 isolation barrier onto Domain B where the PCA9555 lives. Shared logic lives in `lib/orc_can_addr/` and is linked into every sketch below, each printing the current reading at startup.

**Wiring**: external 10kΩ pulldown to GND per bit, switch/jumper to 3V3 per bit — open=0 (LOW), closed=1 (HIGH). Plain `INPUT` mode in firmware (no internal pull requested); the external 10k dominates the ESP32-C3's own internal weak pull resistors (~45kΩ typ either direction per the datasheet) regardless of their state, so there's nothing to fight.

**Pins actually built: GPIO0, GPIO1, GPIO2, GPIO3 — corrected after the fact, not the original plan.** The initial pick (documented earlier the same day, 2026-08-04) was GPIO0/1/3/10, specifically chosen to avoid GPIO2 since it's one of the ESP32-C3's three strapping pins. **That was never what got built.** The as-fabbed schematic (`hardware/mcu.kicad_sch`, U7) wires `NODE_ID0`..`NODE_ID3` to a contiguous `IO0`..`IO3` block — which includes GPIO2 and doesn't use GPIO10 at all. This firmware has been corrected to match the real board; the schematic is the source of truth here, not the firmware plan.

**GPIO2 strapping safety — re-verified with real rigor since it's now load-bearing on a board that's already at fab, not just a design preference to weigh.** The ESP32-C3's full strapping-pin set is exactly {GPIO2, GPIO8, GPIO9} (Espressif *ESP32-C3 Series Datasheet* v2.4, Table 3-1/3-2/3-3). Table 3-3 "Chip Boot Mode Control" has exactly two rows:

| Boot Mode | GPIO2 | GPIO8 | GPIO9 |
|---|---|---|---|
| SPI Boot (normal) | Any value | Any value | 1 |
| Joint Download Boot | 1 | 1 | 0 |

— with the table's own footnote stating plainly: *"GPIO2 actually does not determine SPI Boot and Joint Download Boot mode, but it is recommended to pull this pin up due to glitches."* This board's I2C pull-up (R21, `SCL_A`, 10kΩ — see `docs/subcircuit-capture-guide.md`'s I2C Isolator section) holds **GPIO9 = 1 at every reset**, which is the sole determinant of the SPI-Boot row. Both address-switch positions on GPIO2 (LOW via the external 10k pulldown when open, HIGH when closed) land in that same SPI-Boot row regardless — GPIO2's column is "Any value" whenever GPIO9=1, and GPIO9=1 is guaranteed here. No published ESP32-C3 errata mention GPIO2 (checked directly against Espressif's chip errata index, not assumed absent by analogy to GPIO8/9). **Verdict: safe as fabbed, no rework needed.** Espressif's own generic recommendation to add a pull-up on GPIO2 (to guard against glitches on a *floating* GPIO2 in designs where GPIO9 isn't independently pinned) doesn't apply here in the same way, since this design's own pull-down plus GPIO9's independent pull-up together determine the outcome regardless of GPIO2's transient state.

GPIO0/1/3 are also ADC1_CH0/CH1/CH3 — irrelevant here since they're used purely as digital inputs, not sampled by the ADC. GPIO10, spare in this design, was the original plan's bit3 pin but isn't actually wired to anything address-related on the real board.

## The GPIO9 strapping question — resolved

GPIO9 is the ESP32-C3's BOOT strapping pin (default weak pull-up = normal SPI-flash boot) and is also assigned as I2C SCL. **Datasheet analysis verdict: safe, no conflict.** Full reasoning and citations now live in `docs/subcircuit-capture-guide.md`'s MCU section; summary:

- The boot-strap read is a one-shot latch completing within a fixed **t_H ≥ 3 ms** window anchored to `CHIP_EN` release (Espressif *ESP32-C3 Series Datasheet* v2.4, §3, Table 3-2, Figure 3-1) — before firmware runs and before the IO MUX/GPIO Matrix has routed the I2C peripheral onto GPIO8/9 at all (same datasheet, §4.1.3.1; Table 2-1 shows GPIO9's at-reset/after-reset state as plain `IE, WPU`, not an I2C pin).
- An idle-high I2C bus **reinforces** GPIO9's documented default pulled-up strap value (Table 3-1: GPIO9 default = weak pull-up, bit=1=SPI boot); external pull-ups are the datasheet's own documented mechanism for setting strap state and are not flagged as a hazard when they match the default direction.
- The only way to disturb the strap read is to actively drive GPIO9 **low** during the ~3 ms window — ordinary I2C only pulls SCL low during active clock pulses initiated by firmware, which cannot run before the strap read has already completed and been latched.
- No published ESP32-C3 errata mentions GPIO8/GPIO9/strapping/I2C (checked against Espressif's official chip errata index).
- GPIO8 (SDA) is also a strapping pin (chip-boot-mode + UART-print-control), defaults to **floating** (not pulled), and is safe by the same timing argument. One real caveat carried into the docs: GPIO8's floating default means if some other I2C slave on the bus holds SDA low during its own power-on reset at the same instant as the ESP32-C3's `CHIP_EN` release, that could unintentionally affect the GPIO8 strap read (Table 3-3: only matters if GPIO9 also reads 0 at that instant) — a general I2C power-sequencing caution, not specific to this design, and not a reason to reassign pins.

No pin reassignment was made. If a strapping-pin-free I2C pair is ever wanted for unrelated reasons, GPIO4/5 or GPIO6/7 avoid the {GPIO2, GPIO8, GPIO9} strap set but carry JTAG (MTMS/MTDI/MTCK/MTDO) as their IO-MUX default association — a different caution category, and not verified against this design's specific "Super Mini" header breakout.

## SN65HVD230 UART baud rate

The SN65HVD230 CAN transceiver has **no UART framing requirement of its own** — it's a transparent ISO 11898-2 physical-layer line driver, not a UART part (TI *SN65HVD230/1/2* datasheet, SLOS346O, rev. April 2018: §7 Pin Functions, §10.1 Overview). Its datasheet gives a "designed for data rates up to 1 Mbps" target (Features, p.1) and hard propagation-delay/rise-time limits in high-speed mode (§8.7–8.9, tens-to-~135 ns depending on the parameter) — not a baud spec. `uart_can_bringup` picks 500 kbps as a conventional round number comfortably under that ceiling; it is a firmware-side choice, not a transceiver requirement. **Caveat**: those propagation-delay numbers assume RS (slope-control) tied to ground for high-speed mode — if ORC's board leaves RS in slope-control mode via a resistor instead, propagation delay balloons (up to ~1200 ns typ at 100 kΩ RS per the datasheet's switching-characteristics table) and would cap the practical rate well below 1 Mbps. Confirm ORC's actual RS pin configuration before assuming the 1 Mbps ceiling applies.

## Real-hardware run, 2026-08-01

A bare, generic ESP32-C3 dev kit (chip ID reported by `esptool`: **ESP32-C3 QFN32, silicon revision v0.4**, 4MB embedded flash, native USB-Serial/JTAG, MAC `3c:84:27:af:48:fc`) was connected to the machine this session ran on, with **nothing else wired to it** — no PCA9555, no SN65HVD230, no I2C pull-ups beyond whatever the bare board itself has. This is not ORC's board and does not exercise the PCA9555 or CAN-transceiver bring-up logic; it confirms the ESP32-C3 IO-level pieces (I2C peripheral init, UART peripheral init, USB console) actually run on real silicon of the correct chip family.

**Bug found and fixed by this test, not cosmetic**: the stock `esp32-c3-devkitm-1` board definition does not enable USB-CDC-on-boot, so Arduino's default `Serial` console silently bound to UART0 on **GPIO21(TX)/GPIO20(RX) — the exact pins this design assigns to the CAN-transceiver UART** — instead of the native USB-Serial/JTAG port. First flash produced zero console output for this reason. Fixed in `platformio.ini` with `-D ARDUINO_USB_MODE=1 -D ARDUINO_USB_CDC_ON_BOOT=1`, which routes `Serial` onto native USB and frees GPIO21/20 exclusively for the CAN UART (`Serial1`) as intended. Worth carrying into whatever final board definition replaces the `esp32-c3-devkitm-1` stand-in — the same collision would otherwise resurface silently on real ORC hardware.

- **`i2c_scanner`**: flashed and ran live. Correctly reported no ACKs on the bus (expected — nothing is wired to GPIO8/9 on this bare dev kit) and printed the idle-bus-level check without error. Confirms the I2C peripheral initializes and runs on real hardware; does not confirm anything about the PCA9555 (not attached).
- **`uart_can_bringup`**: flashed and ran live. Baseline (nothing jumpered) correctly showed "RX: nothing received" every cycle. With GPIO21 (TX) physically shorted to GPIO20 (RX) by a jumper wire, **loopback verified end-to-end**: every `ORC-BRINGUP-<counter>` pattern sent came back byte-for-byte (22 bytes including `\r\n`), one-to-one, no drops or corruption, at 500 kbps. Confirms UART1 on these exact pins transmits and receives correctly on real silicon; does not confirm anything about the SN65HVD230 or a real CAN bus (neither present).
- **`pca9555_bringup`**: not run this session — no PCA9555 is attached to this dev kit. By design it halts cleanly at the "PCA9555 did not ACK" check rather than doing anything unsafe without the part present; still only build-verified, not run.

## Sketches

Each sketch is its own PlatformIO environment (separate, individually-flashable — not one monolithic test). Build/flash one at a time:

```
pio run -e i2c_scanner -t upload -t monitor
pio run -e pca9555_bringup -t upload -t monitor
pio run -e uart_can_bringup -t upload -t monitor
pio run -e can_address_bringup -t upload -t monitor
pio run -e canopen_app -t upload -t monitor
pio run -e usb_bench -t upload -t monitor
```

(Run from this `firmware/` directory. `-t monitor` opens the serial console at 115200 baud after flashing; omit it to just build+flash.)

### `i2c_scanner` ([src/i2c_scanner/main.cpp](src/i2c_scanner/main.cpp))

Scans the I2C bus (addresses 0x03–0x77), reports any device that ACKs — specifically flags 0x20, the PCA9555's address (A0–A2 strapped low). Before starting the I2C peripheral, reads SDA/SCL as plain GPIO to report whether the bus idles HIGH (normal) or LOW (a real fault — stuck bus, missing pull-up, or a device holding the line; the classic symptom anyone chasing a strapping-conflict theory would look for, though per the analysis above it isn't expected to be one here).

### `pca9555_bringup` ([src/pca9555_bringup/main.cpp](src/pca9555_bringup/main.cpp))

Configures the PCA9555 as 10 outputs and walks a single HIGH bit across channels 1–10 in sequence, 500 ms per step, using the **real, schematic-confirmed channel-to-register mapping** in `lib/orc_relay_map/`. Intended for a scope probe or a per-channel LED jig to visually confirm addressing and pin mapping once hardware exists. **Corrected 2026-08-05**: an earlier version of this sketch assumed channels 1-8 were sequential on Port0 (whole byte) with channels 9-10 on Port1's low 2 bits — that assumption was wrong. `docs/subcircuit-capture-guide.md`'s "Channel mapping" table (read directly off `hardware/i2c_expander.kicad_sch`) shows the real mapping is routing-driven and non-sequential, spanning both ports (e.g. channel 1 = Port1 bit0, channel 2 = Port0 bit7, channel 10 = Port0 bit3). "Channel 3" in this sketch's output now means the schematic's actual channel 3.

### `uart_can_bringup` ([src/uart_can_bringup/main.cpp](src/uart_can_bringup/main.cpp))

Configures UART1 at 500 kbps on GPIO21(TX)/GPIO20(RX) — the pins wired to the SN65HVD230 — sends a counted test pattern (`ORC-BRINGUP-<counter>`) once per second, and reports whatever arrives on RX within a 500 ms window. Useful for a bench loopback test (jumper TX to RX, or through the transceiver with CAN_H/CAN_L looped/terminated) before a real bus exists, and for testing against a second node once one does. This is raw UART bytes through the transceiver, not CAN-protocol-framed traffic.

### `can_address_bringup` ([src/can_address_bringup/main.cpp](src/can_address_bringup/main.cpp))

Reads the 4-bit CAN-address bank (GPIO0/1/2/3, see "Configurable CAN node address, as-fabbed" above) and prints the resulting 0-15 value once per second, along with each individual bit. Intended for a bench check: walk all 16 switch combinations and confirm each one reads back correctly before trusting the scheme in the field. The other bring-up sketches also call the same `orcPrintCanAddress()` helper (from `lib/orc_can_addr/`) once at startup, so whichever one is running, the console banner always states which node address the board is currently set to — useful as soon as more than one ORC unit might be on a bench or bus at once.

## `canopen_app` — application firmware: CANopen relay control

**This is real relay-control firmware, not a bring-up test.** Implements the full protocol specified in `docs/can-protocol-research.md`: real CAN (TWAI, not the `uart_can_bringup` UART scheme), 125 kbit/s, node-ID-based addressing (reuses `lib/orc_can_addr` unchanged), and four messages — RPDO1 (relay command), TPDO1 (relay status), TPDO2 (bus health/uptime), Heartbeat — plus one minimal SDO object for TPDO2's configurable send interval. Same "compiles cleanly, ready to flash, not tested on hardware" bar as everything else in this directory — no ORC board is in hand yet.

```
pio run -e canopen_app -t upload -t monitor
```

**Transport**: ESP-IDF/Arduino-ESP32's native TWAI driver (`driver/twai.h`), `tx_io=GPIO21`, `rx_io=GPIO20` — the same physical pins `uart_can_bringup` uses, but a genuinely different on-chip peripheral (TWAI vs UART1), per `can-protocol-research.md`'s UART-vs-TWAI resolution: TWAI has no dedicated IO_MUX pin on the ESP32-C3, it's GPIO-Matrix-routable to any pin, so this is a firmware-only change with zero schematic impact. Given separate `ORC_TWAI_TX_PIN`/`ORC_TWAI_RX_PIN` build flags (same values as `ORC_UART_TX_PIN`/`RX_PIN`) rather than reusing the UART-named constants, since reusing them here would misleadingly suggest this sketch is still UART-based. **Bitrate: 125 kbit/s, locked** (`TWAI_TIMING_CONFIG_125KBITS()`) — per `can-protocol-research.md`'s "Bus bitrate" section; don't change this without updating that doc and coordinating with `rigos-core`'s FR-059, which is being built against this exact value from the host side.

**Node ID**: read via `lib/orc_can_addr` at startup, unchanged from the bring-up sketches. **If the address reads 0** (all 4 DIP-switch positions open — not a valid CANopen node ID, CiA 301 requires 1-127), the firmware refuses to join the bus and loops printing an error instead of silently operating non-conformantly.

**Messages implemented** (COB-ID = base + NodeID, see `can-protocol-research.md` for the full spec):

| Message | COB-ID | Direction | What it does |
|---|---|---|---|
| RPDO1 | `0x200+NodeID` | host → ORC | 2-byte relay command (byte0=channels 1-8, byte1 bits0-1=channels 9-10) — this is the **wire protocol's** channel numbering, translated to real PCA9555 register bits via `lib/orc_relay_map/`'s schematic-confirmed, routing-driven mapping (channels are NOT sequential Port0-then-Port1 bits — see `pca9555_bringup`'s section above for the same correction). `orc_canopen.h` only handles the 2-byte-payload ↔ 10-bit-channel-mask conversion; `orc_relay_map.h` handles channel-mask ↔ PCA9555-register-value, kept as two separate, composable translation steps. |
| TPDO1 | `0x180+NodeID` | ORC → host | 2-byte applied relay status, sent after every RPDO1. Read back from the PCA9555's Input Port registers (which reflect actual driven pin level even for output-configured pins) rather than just echoing the command — real command-vs-applied confirmation, even without current sensing. |
| TPDO2 | `0x280+NodeID` | ORC → host, periodic (default 1000ms, SDO-configurable) | 8-byte bus health/uptime: byte0 CAN-controller-health enum, bytes1-2 TWAI TX/RX error counters, bytes3-6 uptime-seconds (`uint32` little-endian). Byte-0 enum and error-counter width were both flagged in `can-protocol-research.md` as unverified design sketches — **both confirmed this pass against ESP-IDF's actual `driver/twai.h`** (see that doc's TPDO2 section for the citation), implemented in `lib/orc_canopen/orc_canopen.h`. **Uptime bug found and fixed in 2026-08-05 review**: the field was originally computed from `millis()`, which wraps at ~49.7 days and would have silently reset the reported uptime on any continuously-powered unit — now uses `esp_timer_get_time()` (64-bit microseconds-since-boot), which doesn't have that problem. |
| Heartbeat | `0x700+NodeID` | ORC → host, periodic, fixed 1000ms | 1-byte NMT state. Sends Boot-up (0) once at the end of `setup()`, then Operational (5) on every subsequent periodic send — a fixed "Boot-up once, then always Operational" NMT posture, no Stopped/Pre-operational transitions implemented, stated explicitly rather than silently skipped. |
| SDO (minimal) | rx `0x600+NodeID`, tx `0x580+NodeID` | host ↔ ORC | Recognizes exactly one (index, sub-index) pair — `1801h`/`5`, TPDO2's event timer in milliseconds — and aborts (standard CiA 301 abort codes) anything else. Not a general SDO server, per the task's explicit scope. A successful write applies the new interval immediately and persists it to the ESP32-C3's NVS (`Preferences`, namespace `orc_cfg`, key `tpdo2_ms`) so it survives a power cycle; default 1000ms if nothing's been persisted yet. |

**RPDO1-timeout fail-safe**: if no RPDO1 arrives within 5000ms, all channels are de-energized rather than holding last state — per `can-protocol-research.md`'s explicit safety recommendation. **5000ms is a firmware default, not a cited spec value** — there's no documented host command cadence anywhere in this project (RPDO1 is event-driven, sent only on change, not periodic), so this was picked as 5x the TPDO2/Heartbeat default period, giving margin against normal idle gaps while still failing safe within a few seconds if the host or bus genuinely disappears.

**TWAI bus-off recovery**: added in the 2026-08-05 review pass — was missing entirely from the original implementation. ESP-IDF's TWAI driver does not recover from bus-off on its own: `twai_initiate_recovery()` must be called explicitly, and even after recovery completes the driver lands in `STOPPED`, not `RUNNING` — `twai_start()` must be called again to actually resume. Without this, a bus fault would be correctly *reported* via TPDO2's health byte (`BUS_OFF`/`RECOVERING`) but never actually recovered from, needing a physical power-cycle. `checkTwaiRecovery()` runs on a fixed 1-second cadence (independent of TPDO2's own SDO-configurable interval, deliberately) and handles both the `twai_initiate_recovery()` call and the follow-up `twai_start()`.

**Not implemented, by explicit scope choice, not oversight**: EMCY (`0x080+NodeID`) — flagged in the spec as a stretch goal, skipped this pass. Full `1010h`/`1011h` CANopen store/restore objects — the NVS auto-persist-on-write above satisfies "survives a power cycle" without them; a reasonable later addition if ORC ever needs to interoperate with a generic CANopen config tool expecting the standard mechanism specifically. No general-purpose CANopen stack or object dictionary — the message set is small and fully specified, hand-rolled directly on the TWAI driver per the task's explicit instruction not to pull in a library (e.g. CANopenNode) for this.

**Shared libraries**: `lib/orc_canopen/orc_canopen.h` — pure wire-protocol encode/decode helpers (COB-ID arithmetic, RPDO1/TPDO1 2-byte-payload ↔ 10-bit-channel-mask, TPDO2 payload packing, the one-object SDO wire format). `lib/orc_relay_map/orc_relay_map.h` — the real, schematic-confirmed channel-mask ↔ PCA9555-register-value mapping, shared with `pca9555_bringup` (see its section above). Splitting these two translation steps (protocol bytes ↔ channel mask, channel mask ↔ PCA9555 registers) into separate libs is deliberate — it's what let the routing-driven mapping bug get caught and fixed in one place without touching the wire-protocol format at all. Neither lib makes TWAI/I2C/NVS calls of its own; `canopen_app/main.cpp` owns the actual peripherals and runtime loop. Both header-only, no `.cpp` — everything's a small `inline` function.

## `usb_bench` — application firmware: USB bench/debug interface

**A second piece of real relay-control firmware, but explicitly a bench/dev tool, not the production control path.** ORC's resolved control-path decision (`docs/design-inputs.md`) is CAN, exclusively — this doesn't change that. It reuses the ESP32-C3's native USB-C port, already wired for programming/flashing, as a plain-ASCII CDC-ACM serial interface, per [`docs/usb-bench-interface-spec.md`](../docs/usb-bench-interface-spec.md) (full protocol spec/rationale) and [`docs/features/FR-001.md`](../docs/features/FR-001.md) (this repo's feature-request bookkeeping, format borrowed from the sibling `rigos-core` repo's `docs/features/` convention). Motivating case: `rigos-core`'s `io_relay` daemon needs CAN transceiver hardware wired correctly before ORC's own firmware can be exercised at all — this gives a transceiver-free path to drive/observe relay logic over the same cable used to flash it, during bring-up or as an ongoing bench tool.

```
pio run -e usb_bench -t upload -t monitor
```

**Transport**: native USB CDC-ACM (`Serial`), same `ARDUINO_USB_MODE`/`ARDUINO_USB_CDC_ON_BOOT` build flags every other sketch here already sets — no new build flags needed. Point-to-point by construction (one USB cable = one ORC unit) — unlike CAN, no NodeID concept here.

**Framing**: plain ASCII, one command/response per line, `\n`-terminated (`\r` tolerated/stripped), 128-byte max line length (`ERR LINE_TOO_LONG` + discard-until-`\n` past that), case-insensitive command words.

**Commands** (exactly per the spec):

| Command | Response | Notes |
|---|---|---|
| `PING` | `PONG <fw_version>` | Liveness + identify. |
| `VERSION` | `VERSION <fw_version> <build_date>` | Fuller build info. |
| `SET <ch> ON\|OFF` | `OK SET <ch> ON\|OFF` or `ERR <code>` | `<ch>` is 1-10. |
| `GET <ch>` | `STATE <ch> ON\|OFF` or `ERR <code>` | Applied state, read back from the PCA9555's Input Port registers — same "confirmed, not optimistic echo" pattern `canopen_app`'s TPDO1 already uses — not a repeat of the last commanded value. |
| `STATUS` | `STATUS <10-char 0/1 string>` | Channel 1 first, e.g. `STATUS 0010000000` = channel 3 on. |

**Unsolicited lines**: `HB <uptime_ms>` every 1000ms; `STATE <ch> ON\|OFF` once, immediately, on every actual applied-state change (SET-driven or fail-safe-driven) — closes the same "fresh listener can't learn current state" gap the spec flags CAN's event-only TPDO1 as having, which USB has no bandwidth reason to repeat.

**Fail-safe — FR-001's own decision, the spec explicitly left this open**: idle-timeout de-energizes all channels after **30000ms** of no host activity (any received line, valid or malformed, resets the timer), for philosophical consistency with CAN's own RPDO1-timeout fail-safe (`can-protocol-research.md`, implemented in `canopen_app`) — but 6x longer than CAN's 5000ms, since this is an interactive human-typed interface where a person pausing to read output shouldn't trip the same threshold a vanished CAN host should.

**Relay I/O**: goes through `lib/orc_relay_map/` — the same real, schematic-confirmed channel-mask↔PCA9555-register mapping `pca9555_bringup` and `canopen_app` already share. No third hand-rolled copy of that table.

**Not implemented, by explicit scope choice**: a `rigos-core`-side `orc_usb.py` bus backend — the spec calls this a natural follow-up, not needed for this firmware to be useful on its own (a human with a serial terminal is a complete consumer). Multi-unit addressing — point-to-point only, no NodeID concept.

**Bug found and fixed on real hardware, 2026-08-05 — `docs/features/FR-001.md` was reopened over this**: the first real-hardware test (a Raspberry Pi building/flashing `usb_bench` onto a bare ESP32-C3 devkit over USB, no ORC PCB, no PCA9555 attached) found `setup()` halted the entire sketch — `while (true) delay(1000)` — if the PCA9555 didn't ACK, which meant `PING`/`VERSION`/basic framing were unreachable too, not just the commands that actually need relay hardware. That defeated this firmware's own stated purpose: usable *before* full ORC hardware exists. Fixed: `setup()` never halts now. A `g_pca9555Present` flag tracks whether relay hardware has been seen; `PING`/`VERSION`/`HB`/framing errors work with zero I2C hardware present; `SET`/`GET`/`STATUS` make one lazy re-probe attempt if the flag is false, then return `ERR NO_RELAY_HARDWARE` if that also fails — so a PCA9555 hot-plugged mid-session is picked up on the next relay command, no reboot needed. A write/read failure after the PCA9555 was previously seen (e.g. unplugged mid-session) also drops the flag, so the next relay command re-probes instead of repeating the same failing I2C transaction. Also removed a misleading boot-log `PONG ...` line that looked like a live response to a `PING` command but was printed before `loop()` (and the real command interface) ever started.

## What this is not

- **The four bring-up sketches** (`i2c_scanner`, `pca9555_bringup`, `uart_can_bringup`, `can_address_bringup`) are not application firmware — no relay control logic, no real CAN message parsing/handling, no product behavior. `canopen_app` and `usb_bench` are different — see their sections above — but neither is a claim of hardware verification (next point).
- Not a claim of ORC-hardware verification — `i2c_scanner` and `uart_can_bringup` have run live on a generic ESP32-C3 dev kit (see above), which confirms the MCU-side IO logic on real silicon of the right chip family, but the PCA9555 and SN65HVD230 remain completely untested, and no ORC board exists to test them on. `pca9555_bringup` is still build-verified only, never run. `canopen_app` is build-verified only — no real TWAI frame, no real PCA9555, no real bus has touched it. `usb_bench` is a partial exception: a Raspberry Pi has built/flashed it onto a bare ESP32-C3 devkit over real USB and confirmed the build+flash pipeline works end-to-end (auto-reset via native USB, no manual BOOT button) — that run is also what found and drove the 2026-08-05 `g_pca9555Present` fix above. The fix itself (`PING`/`VERSION`/`HB` working with zero I2C hardware present, `ERR NO_RELAY_HARDWARE` on `SET`/`GET`/`STATUS`) is build-verified only so far, not yet re-flashed and re-confirmed live — see `docs/features/FR-001.md`'s acceptance criteria for exactly what's still open.
- Not a final board declaration — `esp32-c3-devkitm-1` in `platformio.ini` is an explicit stand-in; see above.
- `usb_bench` is not the production control path — CAN stays the one real transport in the field. See `docs/usb-bench-interface-spec.md`'s Scope section for the full "why this doesn't contradict the CAN-exclusive decision" reasoning.
