// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Aram Dergevorkian
// ORC firmware bring-up: UART -> SN65HVD230 CAN transceiver
//
// NOT application firmware -- no CAN message handling, no relay logic.
// This design drives the SN65HVD230 CAN transceiver's TXD/RXD pins from the
// ESP32-C3's plain UART peripheral (GPIO21 TX / GPIO20 RX), not the chip's
// native TWAI/CAN controller -- see docs/subcircuit-capture-guide.md's
// Communications sheet. The SN65HVD230 has no UART-specific framing
// requirement of its own (confirmed against TI's SLOS346O datasheet -- see
// firmware/README.md): it's a transparent differential line driver, so any
// baud rate is a firmware-side choice, bounded only by staying comfortably
// under the transceiver's ~1 Mbps design target and its propagation-delay/
// rise-time budget (tens-to-low-hundreds of ns in high-speed RS=0V mode,
// SLOS346O Section 8.7-8.9).
//
// This sketch sends a known test pattern out the UART and reports whatever
// comes back in on RX. Two uses once hardware exists:
//   1. Bench loopback: jumper TXD to RXD directly (or through the
//      transceiver with CANH/CANL looped or terminated) before a real CAN
//      bus or second node is available -- confirms the UART peripheral and
//      pin routing work at all.
//   2. Two-node test: run this on a second ORC (or any UART-to-transceiver)
//      board and confirm the test pattern arrives intact across a real
//      CAN_H/CAN_L pair.
//
// Nothing here has been run against real hardware. Compiles cleanly against
// the esp32-c3-devkitm-1 stand-in board definition; ready to flash once real
// hardware exists.

#include <Arduino.h>
#include "orc_can_addr.h"

static const uint8_t kTxPin = ORC_UART_TX_PIN;
static const uint8_t kRxPin = ORC_UART_RX_PIN;

// 500 kbps: a conventional automotive CAN bit rate, and comfortably under
// the SN65HVD230's ~1 Mbps design-target signaling rate with margin for the
// transceiver's own propagation delay/rise-time budget. Not itself a CAN
// bit rate in the protocol sense (this is a raw UART byte stream through
// the transceiver, not framed CAN) -- chosen as a realistic round number
// for a first bring-up test, not derived from a spec requirement.
static const unsigned long kBaud = 500000;

static HardwareSerial CanUart(1);  // ESP32-C3 UART1, distinct from Serial (console)

static uint32_t txCounter = 0;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("=== ORC bring-up: UART/CAN-transceiver test ===");
  Serial.println("Bring-up/verification sketch -- not application firmware.");
  Serial.printf("UART1 TX=GPIO%u  RX=GPIO%u  baud=%lu\n", kTxPin, kRxPin, kBaud);
  Serial.println("This is a raw UART byte stream through the SN65HVD230, not "
                  "framed CAN protocol traffic.");

  orcInitCanAddrPins();
  orcPrintCanAddress();

  CanUart.begin(kBaud, SERIAL_8N1, kRxPin, kTxPin);
}

void loop() {
  char msg[48];
  int len = snprintf(msg, sizeof(msg), "ORC-BRINGUP-%08lu\r\n", (unsigned long)txCounter);
  txCounter++;

  Serial.printf("TX: %s", msg);
  CanUart.write((const uint8_t *)msg, (size_t)len);

  // Listen for a reply/echo for up to 500ms before sending the next pattern.
  unsigned long start = millis();
  String received;
  while (millis() - start < 500) {
    while (CanUart.available()) {
      char c = (char)CanUart.read();
      received += c;
    }
    if (received.length() > 0 && received.endsWith("\n")) break;
    delay(5);
  }

  if (received.length() > 0) {
    Serial.printf("RX (%u bytes): %s", received.length(), received.c_str());
  } else {
    Serial.println("RX: nothing received in 500ms window (expected until TX/RX "
                    "are jumpered for loopback, or a second node is present).");
  }

  delay(1000);
}