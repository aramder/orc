// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Aram Dergevorkian
#include "orc_can_addr.h"

void orcInitCanAddrPins() {
  pinMode(kOrcCanAddrBit0Pin, INPUT);
  pinMode(kOrcCanAddrBit1Pin, INPUT);
  pinMode(kOrcCanAddrBit2Pin, INPUT);
  pinMode(kOrcCanAddrBit3Pin, INPUT);
}

uint8_t orcReadCanAddress() {
  uint8_t bit0 = digitalRead(kOrcCanAddrBit0Pin) ? 1 : 0;
  uint8_t bit1 = digitalRead(kOrcCanAddrBit1Pin) ? 1 : 0;
  uint8_t bit2 = digitalRead(kOrcCanAddrBit2Pin) ? 1 : 0;
  uint8_t bit3 = digitalRead(kOrcCanAddrBit3Pin) ? 1 : 0;
  return (uint8_t)(bit0 | (bit1 << 1) | (bit2 << 2) | (bit3 << 3));
}

void orcPrintCanAddress() {
  uint8_t bit0 = digitalRead(kOrcCanAddrBit0Pin) ? 1 : 0;
  uint8_t bit1 = digitalRead(kOrcCanAddrBit1Pin) ? 1 : 0;
  uint8_t bit2 = digitalRead(kOrcCanAddrBit2Pin) ? 1 : 0;
  uint8_t bit3 = digitalRead(kOrcCanAddrBit3Pin) ? 1 : 0;
  uint8_t addr = (uint8_t)(bit0 | (bit1 << 1) | (bit2 << 2) | (bit3 << 3));
  Serial.printf("CAN node address: %u (bits 3210 = %u%u%u%u, pins GPIO%u/%u/%u/%u)\n",
                addr, bit3, bit2, bit1, bit0,
                kOrcCanAddrBit3Pin, kOrcCanAddrBit2Pin, kOrcCanAddrBit1Pin, kOrcCanAddrBit0Pin);
}