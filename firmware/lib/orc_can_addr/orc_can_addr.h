// ORC shared helper: read the 4-bit configurable CAN node address.
//
// Address-select hardware lives on the MCU's own (non-isolated, Domain A)
// side -- not on the PCA9555, which sits on the galvanically-isolated
// Domain B side of the ADuM1250 barrier. Four GPIOs (see pin numbers below)
// each read one address bit via an external 10k pulldown to GND, with a
// switch/jumper to 3V3 per bit: open = LOW (0), closed = HIGH (1).
//
// Pins, AS-FABBED (corrected 2026-08-04, later same day, after the board
// was already sent out): GPIO0, GPIO1, GPIO2, GPIO3 on the ESP32-C3,
// matching hardware/mcu.kicad_sch's real NODE_ID0..NODE_ID3 nets (U7 pins
// 9-12). An earlier same-day plan called for GPIO0/1/3/10 specifically to
// avoid GPIO2 (a strapping pin) -- that was never what got built. The real
// schematic uses a contiguous GPIO0-3 block, which includes GPIO2, and does
// not use GPIO10 at all. Firmware corrected to match the as-fabbed board.
//
// GPIO2 strapping safety, re-verified specifically because it's now
// load-bearing on a board that can't be changed: the ESP32-C3's full
// strapping-pin set is {GPIO2, GPIO8, GPIO9} (Espressif ESP32-C3 Series
// Datasheet v2.4, Table 3-1/3-2/3-3). Table 3-3 "Chip Boot Mode Control"
// has exactly two rows -- SPI Boot (GPIO2 = "Any value", GPIO9 = 1) and
// Joint Download Boot (GPIO2 = 1 required, GPIO9 = 0 required) -- and its
// own footnote states plainly: "GPIO2 actually does not determine SPI Boot
// and Joint Download Boot mode." This board's I2C pull-up (R21, SCL_A, see
// docs/subcircuit-capture-guide.md) holds GPIO9 = 1 at every reset, which
// is the sole determinant of the SPI-Boot row -- so GPIO2's level (LOW via
// the external 10k pulldown when the address switch is open, or HIGH when
// closed) never changes the boot outcome in either switch position. No
// published ESP32-C3 errata mention GPIO2 (checked directly against
// Espressif's chip errata index, not assumed absent by analogy to GPIO8/9).
// GPIO0/1/3 are also ADC1_CH0/CH1/CH3, irrelevant for plain digital-input
// use. See firmware/README.md and docs/subcircuit-capture-guide.md's MCU
// section for the full citation trail.
//
// Nothing here has been run against real hardware with the address-select
// switches actually wired -- compiles cleanly, ready to flash.

#pragma once

#include <Arduino.h>

// Bit order: bit0 = LSB. Address value = bit0 | (bit1<<1) | (bit2<<2) | (bit3<<3), range 0-15.
static const uint8_t kOrcCanAddrBit0Pin = ORC_CAN_ADDR_BIT0_PIN;  // GPIO0, NODE_ID0
static const uint8_t kOrcCanAddrBit1Pin = ORC_CAN_ADDR_BIT1_PIN;  // GPIO1, NODE_ID1
static const uint8_t kOrcCanAddrBit2Pin = ORC_CAN_ADDR_BIT2_PIN;  // GPIO2, NODE_ID2 (strapping pin, verified safe -- see above)
static const uint8_t kOrcCanAddrBit3Pin = ORC_CAN_ADDR_BIT3_PIN;  // GPIO3, NODE_ID3

// Configure the four address-select pins as plain digital inputs. Call once
// from setup() before orcReadCanAddress(). No internal pull configuration is
// requested -- the design uses an external 10k pulldown per bit, which
// dominates the ESP32-C3's own internal weak pull resistors (~45k typ
// either direction) regardless of their state, so plain INPUT is correct
// and simplest.
void orcInitCanAddrPins();

// Read the 4 address-select pins and return the resulting 0-15 node address.
uint8_t orcReadCanAddress();

// Print the current address reading to Serial, in the format:
//   "CAN node address: 5 (bits 3210 = 0101)"
// Intended as a one-line startup banner addition for every bring-up sketch,
// not just the dedicated can_address_bringup sketch -- knowing which
// physical node you're looking at matters as soon as more than one ORC unit
// might be on a bench/bus at once.
void orcPrintCanAddress();
