// ORC shared helper: read the 4-bit configurable CAN node address.
//
// Address-select hardware lives on the MCU's own (non-isolated, Domain A)
// side -- not on the PCA9555, which sits on the galvanically-isolated
// Domain B side of the ADuM1250 barrier. Four GPIOs (see pin numbers below)
// each read one address bit via an external 10k pulldown to GND, with a
// switch/jumper to 3V3 per bit: open = LOW (0), closed = HIGH (1).
//
// Pin pick and rationale (2026-08-04): GPIO0, GPIO1, GPIO3, GPIO10 on the
// ESP32-C3. None of the four are strapping pins -- the ESP32-C3's full
// strapping-pin set is {GPIO2, GPIO8, GPIO9} (Espressif ESP32-C3 Series
// Datasheet v2.4, Table 3-1/3-2/3-3 -- the same tables already cited for
// this project's GPIO9 I2C-strapping analysis). Because these four pins
// carry no boot-time meaning at all, there is no strap-timing window to
// check against an external pulldown, unlike the GPIO9/I2C case -- a plain
// runtime GPIO-read problem, not a boot-strap problem. GPIO0/1/3 are also
// ADC1_CH0/CH1/CH3, which doesn't affect their use as plain digital inputs.
// GPIO10 has no ADC/strap function. See firmware/README.md and
// docs/subcircuit-capture-guide.md's MCU section for the full citation
// trail and the pins that were deliberately avoided (GPIO2: unclaimed
// strapping pin, GPIO4-7: JTAG-default, GPIO8/9: already I2C, GPIO18/19:
// native USB, not even broken out on this board's header, GPIO20/21:
// already the CAN UART).
//
// Nothing here has been run against real hardware with the address-select
// switches actually wired -- compiles cleanly, ready to flash.

#pragma once

#include <Arduino.h>

// Bit order: bit0 = LSB. Address value = bit0 | (bit1<<1) | (bit2<<2) | (bit3<<3), range 0-15.
static const uint8_t kOrcCanAddrBit0Pin = ORC_CAN_ADDR_BIT0_PIN;  // GPIO0
static const uint8_t kOrcCanAddrBit1Pin = ORC_CAN_ADDR_BIT1_PIN;  // GPIO1
static const uint8_t kOrcCanAddrBit2Pin = ORC_CAN_ADDR_BIT2_PIN;  // GPIO3
static const uint8_t kOrcCanAddrBit3Pin = ORC_CAN_ADDR_BIT3_PIN;  // GPIO10

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
