// ORC firmware bring-up: configurable CAN node address
//
// NOT application firmware. Reads the 4-bit address-select bank (external
// 10k pulldown per bit, switch/jumper to 3V3, on GPIO0/1/3/10) and reports
// the resulting 0-15 node address continuously, so a bench check can walk
// all 16 switch combinations and confirm each one reads back correctly
// before trusting the scheme in the field.
//
// See firmware/lib/orc_can_addr/orc_can_addr.h for the pin pick and the
// strapping-pin-safety reasoning (short version: none of GPIO0/1/3/10 are
// ESP32-C3 strapping pins, so there's no boot-time interaction to check,
// unlike the GPIO8/9 I2C case).
//
// Nothing here has been run against real hardware with the address-select
// switches actually wired -- compiles cleanly, ready to flash.

#include <Arduino.h>
#include "orc_can_addr.h"

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("=== ORC bring-up: CAN node address read ===");
  Serial.println("Bring-up/verification sketch -- not application firmware.");
  Serial.printf("Address bits: bit0=GPIO%u  bit1=GPIO%u  bit2=GPIO%u  bit3=GPIO%u\n",
                kOrcCanAddrBit0Pin, kOrcCanAddrBit1Pin, kOrcCanAddrBit2Pin, kOrcCanAddrBit3Pin);
  Serial.println("Wiring assumed: external 10k pulldown to GND per bit, switch/jumper to "
                  "3V3 -- open=0, closed=1. Walk all 16 combinations on the bench and "
                  "confirm each reads back correctly before trusting this in the field.");

  orcInitCanAddrPins();
}

void loop() {
  orcPrintCanAddress();
  delay(1000);
}
