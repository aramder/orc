// ORC firmware bring-up: I2C bus scanner
//
// NOT application firmware. Verifies IO-level pinout only:
//   - Confirms GPIO8(SDA)/GPIO9(SCL) actually function as an I2C bus.
//   - Reports whether the PCA9555 GPIO expander (address 0x20 with A0-A2
//     strapped low, per docs/subcircuit-capture-guide.md) ACKs.
//   - Detects a bus stuck low, which is the real, common symptom that would
//     show up here if the GPIO9-as-BOOT-strap concern were actually a live
//     conflict (it isn't, per the datasheet analysis in this repo's docs —
//     see firmware/README.md — but this sketch is the empirical check for
//     when real hardware exists).
//
// Nothing here has been run against real hardware. Compiles cleanly against
// the esp32-c3-devkitm-1 stand-in board definition; ready to flash once a
// real "Super Mini"-class board is in hand.

#include <Arduino.h>
#include <Wire.h>
#include "orc_can_addr.h"

static const uint8_t kSdaPin = ORC_I2C_SDA_PIN;
static const uint8_t kSclPin = ORC_I2C_SCL_PIN;
static const uint8_t kPca9555Addr = ORC_PCA9555_ADDR;

static void checkIdleBusLevels() {
  // Before Wire.begin() takes the pins over as I2C, read them as plain
  // GPIO inputs. A healthy idle I2C bus reads HIGH on both lines (pulled up
  // by the design's external pull-ups). A line stuck LOW here means either
  // a wiring fault, a missing/wrong pull-up, or a device holding the bus —
  // not a strapping conflict (that read completes ~3ms after CHIP_EN
  // release, long before this sketch's setup() runs), but worth reporting
  // plainly since a stuck-low bus is exactly the symptom anyone chasing a
  // strapping theory would look for.
  pinMode(kSdaPin, INPUT);
  pinMode(kSclPin, INPUT);
  delay(2);
  int sdaLevel = digitalRead(kSdaPin);
  int sclLevel = digitalRead(kSclPin);

  Serial.printf("Idle bus levels before Wire.begin(): SDA(GPIO%u)=%s  SCL(GPIO%u)=%s\n",
                kSdaPin, sdaLevel ? "HIGH" : "LOW",
                kSclPin, sclLevel ? "HIGH" : "LOW");

  if (sdaLevel == LOW || sclLevel == LOW) {
    Serial.println("WARNING: bus line reads LOW at idle. Real fault (missing/weak "
                    "pull-up, shorted trace, or a device holding the line), not a "
                    "boot-strap effect at this point in execution -- setup() only "
                    "runs after the ROM boot-mode strap read has already completed "
                    "and been latched.");
  } else {
    Serial.println("Both lines read HIGH at idle -- normal.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);  // let USB-CDC/serial-JTAG enumerate before printing
  Serial.println();
  Serial.println("=== ORC bring-up: I2C bus scanner ===");
  Serial.println("Bring-up/verification sketch -- not application firmware.");
  Serial.printf("SDA=GPIO%u  SCL=GPIO%u  expected PCA9555 addr=0x%02X\n",
                kSdaPin, kSclPin, kPca9555Addr);

  orcInitCanAddrPins();
  orcPrintCanAddress();

  checkIdleBusLevels();

  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(100000);  // conservative 100kHz standard mode for first bring-up
}

void loop() {
  Serial.println("\nScanning I2C bus (addresses 0x03-0x77)...");

  int found = 0;
  bool pca9555Found = false;
  for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    uint8_t result = Wire.endTransmission();
    if (result == 0) {
      Serial.printf("  ACK at 0x%02X%s\n", addr,
                     (addr == kPca9555Addr) ? "  <-- expected PCA9555 address" : "");
      found++;
      if (addr == kPca9555Addr) pca9555Found = true;
    }
  }

  if (found == 0) {
    Serial.println("No devices ACKed. If the bus read HIGH at idle above, this means "
                    "either nothing is attached yet, or a real addressing/wiring fault "
                    "-- not a boot-strap conflict.");
  } else {
    Serial.printf("%d device(s) found.\n", found);
  }

  if (!pca9555Found) {
    Serial.printf("PCA9555 NOT found at expected address 0x%02X.\n", kPca9555Addr);
  }

  delay(3000);
}
