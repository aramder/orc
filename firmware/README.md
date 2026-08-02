# ORC firmware — IO-level bring-up/verification sketches

**These are bring-up and verification sketches, not ORC's application firmware.** They exist to answer one narrow question — does the MCU pinout the hardware side has settled on actually work at the IO level? — before real application logic (relay control, CAN message handling) gets written. See [`.claude/firmware-io-verification-prompt.md`](../.claude/firmware-io-verification-prompt.md) for the task this directory was created to satisfy.

**No ORC board exists yet.** No ORC board has been fabricated and no specific "ESP32-C3 Super Mini" listing has been bought (see [`docs/subcircuit-capture-guide.md`](../docs/subcircuit-capture-guide.md)'s OPEN ARCHITECTURE DECISION section) — nothing here has run against ORC's actual hardware, and no PCA9555 or SN65HVD230 has been tested. All three sketches build successfully (verified: `pio run -e i2c_scanner`, `pio run -e pca9555_bringup -e uart_can_bringup`, all SUCCESS against the `esp32-c3-devkitm-1` stand-in board), and two of the three have now also run live on a bare, generic ESP32-C3 dev kit connected to this session's machine (nothing else attached to it) — see "Real-hardware run, 2026-08-01" below for exactly what that does and doesn't confirm. Treat any claim in this directory carefully: "runs on a generic ESP32-C3 dev kit" is not the same thing as "verified on ORC's board," since the PCA9555 and SN65HVD230 pieces of the design are still completely untested.

## Board stand-in

`platformio.ini` targets PlatformIO's `esp32-c3-devkitm-1` board definition. This is a **stand-in**, not the final hardware — the actual target is a generic, widely-cloned "ESP32-C3 Super Mini"-class board (bare ESP32-C3FN4 die, 4MB flash, no PSRAM, USB-C, 2×7-pin THT headers) with no single confirmed SKU yet. `esp32-c3-devkitm-1` is electrically close enough (same chip class) to compile and flash against for IO-level bring-up; swap it for a real board definition once a specific listing is bought and its exact silkscreen/pin breakout is confirmed.

## Pin assignment under test

Per `docs/subcircuit-capture-guide.md`'s MCU section (2026-08-01):

| Function | Pins |
|---|---|
| I2C (SDA/SCL) | GPIO8 / GPIO9 |
| UART (TX/RX, to SN65HVD230) | GPIO21 / GPIO20 |

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
```

(Run from this `firmware/` directory. `-t monitor` opens the serial console at 115200 baud after flashing; omit it to just build+flash.)

### `i2c_scanner` ([src/i2c_scanner/main.cpp](src/i2c_scanner/main.cpp))

Scans the I2C bus (addresses 0x03–0x77), reports any device that ACKs — specifically flags 0x20, the PCA9555's address (A0–A2 strapped low). Before starting the I2C peripheral, reads SDA/SCL as plain GPIO to report whether the bus idles HIGH (normal) or LOW (a real fault — stuck bus, missing pull-up, or a device holding the line; the classic symptom anyone chasing a strapping-conflict theory would look for, though per the analysis above it isn't expected to be one here).

### `pca9555_bringup` ([src/pca9555_bringup/main.cpp](src/pca9555_bringup/main.cpp))

Configures the PCA9555 as 10 outputs (IO0_0–IO0_7 + IO1_0/IO1_1, matching `docs/subcircuit-capture-guide.md`'s "10 of 16 I/O used" note; the 6 unused IO1_2–IO1_7 pins are left as inputs, not driven) and walks a single HIGH bit across channels 1–10 in sequence, 500 ms per step. Intended for a scope probe or a per-channel LED jig to visually confirm addressing and pin mapping once hardware exists. **Channel-to-net mapping is assumed from the ERC-findings note in `.claude/kicad-mcp-logbook.md`, not cross-checked against a specific schematic pin table for the RBn/QNn/RPn/QPn coil-driver stages** — re-verify before trusting "channel 3" in this sketch's output to mean a specific relay channel on the board.

### `uart_can_bringup` ([src/uart_can_bringup/main.cpp](src/uart_can_bringup/main.cpp))

Configures UART1 at 500 kbps on GPIO21(TX)/GPIO20(RX) — the pins wired to the SN65HVD230 — sends a counted test pattern (`ORC-BRINGUP-<counter>`) once per second, and reports whatever arrives on RX within a 500 ms window. Useful for a bench loopback test (jumper TX to RX, or through the transceiver with CAN_H/CAN_L looped/terminated) before a real bus exists, and for testing against a second node once one does. This is raw UART bytes through the transceiver, not CAN-protocol-framed traffic.

## What this is not

- Not application firmware — no relay control logic, no real CAN message parsing/handling, no product behavior.
- Not a claim of ORC-hardware verification — `i2c_scanner` and `uart_can_bringup` have run live on a generic ESP32-C3 dev kit (see above), which confirms the MCU-side IO logic on real silicon of the right chip family, but the PCA9555 and SN65HVD230 remain completely untested, and no ORC board exists to test them on. `pca9555_bringup` is still build-verified only, never run.
- Not a final board declaration — `esp32-c3-devkitm-1` in `platformio.ini` is an explicit stand-in; see above.
