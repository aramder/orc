// ORC firmware bring-up: PCA9555 I2C GPIO expander
//
// NOT application firmware -- no relay control logic here. Configures the
// PCA9555 (NXP, address 0x20, A0-A2 strapped low) as 10 outputs matching
// docs/subcircuit-capture-guide.md's documented usage (10 of 16 I/O used),
// then walks a single HIGH bit across those 10 channels in sequence so a
// scope or a per-channel LED jig can visually confirm addressing and pin
// mapping once real hardware exists.
//
// Channel-to-register mapping, per the ERC findings logged in
// .claude/kicad-mcp-logbook.md (2026-08-01, "ERC 46->4" entry): PCA9555's
// unused pins are ~INT and IO1_2..IO1_7 (6 pins) -- meaning the 10 used
// pins are IO0_0..IO0_7 (Port 0, all 8 bits) plus IO1_0 and IO1_1 (Port 1,
// low 2 bits). This sketch assumes channel 1..10 maps to that pin order;
// it has not been cross-checked against a specific schematic net-to-pin
// table for the coil-driver stages (RBn/QNn/RPn/QPn), since that level of
// detail isn't in the docs this sketch was written against. Re-verify the
// mapping against the schematic before trusting channel numbers to mean
// specific relay channels.
//
// Nothing here has been run against real hardware. Compiles cleanly against
// the esp32-c3-devkitm-1 stand-in board definition; ready to flash once a
// real "Super Mini"-class board and a PCA9555 are in hand.

#include <Arduino.h>
#include <Wire.h>
#include "orc_can_addr.h"

static const uint8_t kSdaPin = ORC_I2C_SDA_PIN;
static const uint8_t kSclPin = ORC_I2C_SCL_PIN;
static const uint8_t kAddr = ORC_PCA9555_ADDR;

// PCA9555 register map (NXP datasheet, command byte values)
static const uint8_t kRegInputPort0 = 0x00;
static const uint8_t kRegInputPort1 = 0x01;
static const uint8_t kRegOutputPort0 = 0x02;
static const uint8_t kRegOutputPort1 = 0x03;
static const uint8_t kRegPolarityPort0 = 0x04;
static const uint8_t kRegPolarityPort1 = 0x05;
static const uint8_t kRegConfigPort0 = 0x06;
static const uint8_t kRegConfigPort1 = 0x07;

// Config register: bit = 0 -> output, bit = 1 -> input (NXP default = 0xFF,
// all inputs, on power-up).
static const uint8_t kConfigPort0Outputs = 0x00;  // all 8 bits: outputs (channels 1-8)
static const uint8_t kConfigPort1Mask = 0xFC;     // bits 0,1: outputs (channels 9-10); bits 2-7: left as inputs (unused, per docs)

static const uint8_t kNumChannels = 10;

static bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kAddr);
  Wire.write(reg);
  Wire.write(value);
  uint8_t result = Wire.endTransmission();
  if (result != 0) {
    Serial.printf("  I2C write FAILED (reg 0x%02X, val 0x%02X): endTransmission=%u\n",
                  reg, value, result);
    return false;
  }
  return true;
}

// Drive channel `ch` (1-10) HIGH, all other 9 channels LOW.
static void setChannel(uint8_t ch) {
  uint8_t port0 = 0x00;
  uint8_t port1 = 0x00;

  if (ch >= 1 && ch <= 8) {
    port0 = (uint8_t)(1u << (ch - 1));       // channels 1-8 -> IO0_0..IO0_7
  } else if (ch == 9) {
    port1 = 0x01;                             // channel 9 -> IO1_0
  } else if (ch == 10) {
    port1 = 0x02;                             // channel 10 -> IO1_1
  }

  writeReg(kRegOutputPort0, port0);
  writeReg(kRegOutputPort1, port1);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("=== ORC bring-up: PCA9555 walking-pattern test ===");
  Serial.println("Bring-up/verification sketch -- not application firmware.");
  Serial.printf("SDA=GPIO%u  SCL=GPIO%u  PCA9555 addr=0x%02X\n", kSdaPin, kSclPin, kAddr);

  orcInitCanAddrPins();
  orcPrintCanAddress();

  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(100000);

  // Confirm the part is present before configuring it.
  Wire.beginTransmission(kAddr);
  uint8_t probe = Wire.endTransmission();
  if (probe != 0) {
    Serial.printf("PCA9555 did not ACK at 0x%02X (endTransmission=%u). Halting -- "
                  "run the i2c_scanner sketch first to confirm the bus and address.\n",
                  kAddr, probe);
    while (true) delay(1000);
  }
  Serial.println("PCA9555 ACKed. Configuring 10 channels as outputs...");

  // All outputs start LOW before switching direction, to avoid a glitch
  // driving an indeterminate level on power-up (device resets to inputs,
  // 0xFF, so this write only takes effect once direction flips below).
  writeReg(kRegOutputPort0, 0x00);
  writeReg(kRegOutputPort1, 0x00);

  // No polarity inversion.
  writeReg(kRegPolarityPort0, 0x00);
  writeReg(kRegPolarityPort1, 0x00);

  // Direction: channels 1-10 as outputs; unused IO1_2..IO1_7 left as inputs
  // (config bit = 1), matching the "10 of 16 used" note in
  // docs/subcircuit-capture-guide.md rather than driving unconnected pins.
  writeReg(kRegConfigPort0, kConfigPort0Outputs);
  writeReg(kRegConfigPort1, kConfigPort1Mask);

  Serial.println("Configured. Starting walking-single-HIGH pattern across channels 1-10.");
}

void loop() {
  for (uint8_t ch = 1; ch <= kNumChannels; ch++) {
    Serial.printf("Channel %u HIGH (all others LOW)\n", ch);
    setChannel(ch);
    delay(500);
  }
}
