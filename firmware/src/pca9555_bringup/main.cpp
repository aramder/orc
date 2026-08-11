// ORC firmware bring-up: PCA9555 I2C GPIO expander
//
// NOT application firmware -- no relay control logic here. Configures the
// PCA9555 (NXP, address 0x20, A0-A2 strapped low) as 10 outputs matching
// docs/subcircuit-capture-guide.md's documented usage (10 of 16 I/O used),
// then walks a single HIGH bit across those 10 channels in sequence so a
// scope or a per-channel LED jig can visually confirm addressing and pin
// mapping once real hardware exists.
//
// Channel-to-register mapping -- CORRECTED 2026-08-05 against the real
// schematic. An earlier version of this sketch assumed channels 1-8 were
// sequential on Port0 (whole byte) and channels 9-10 on Port1's low 2 bits,
// per a 2026-08-01 ERC-findings note that only established WHICH 10 pins
// were used, not which channel maps to which pin. That assumption was
// WRONG: docs/subcircuit-capture-guide.md's "Channel mapping" table (read
// directly off hardware/i2c_expander.kicad_sch, 2026-08-04) shows the real
// mapping is routing-driven, not sequential -- e.g. channel 1 is Port1 bit0,
// channel 2 is Port0 bit7, channel 10 is Port0 bit3, and so on, spanning
// both ports non-contiguously. The real mapping now lives in
// firmware/lib/orc_relay_map/ -- this sketch uses it directly rather than
// hand-rolling a second (and previously wrong) copy. If you're using this
// sketch's walking pattern with a scope or an LED jig, "channel 3" printed
// here now means the actual schematic's channel 3, not a guess.
//
// Nothing here has been run against real hardware. Compiles cleanly against
// the esp32-c3-devkitm-1 stand-in board definition; ready to flash once a
// real "Super Mini"-class board and a PCA9555 are in hand.

#include <Arduino.h>
#include <Wire.h>
#include "orc_can_addr.h"
#include "orc_relay_map.h"

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

// Config values: kOrcPca9555ConfigPort0/Port1, from lib/orc_relay_map/ --
// see that lib for the real bit-by-bit reasoning (3 non-contiguous spare
// bits per port, not "channels 9-10 only used on Port1" like this sketch
// used to assume).

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

// Writes direction (Config) and polarity registers -- the two registers
// that define "which pins are outputs" and "not inverted." Called once at
// boot AND on every setChannel() call below -- see that function's comment
// for why the repeat isn't paranoia.
static void configurePca9555Direction() {
  writeReg(kRegPolarityPort0, 0x00);
  writeReg(kRegPolarityPort1, 0x00);
  writeReg(kRegConfigPort0, kOrcPca9555ConfigPort0);
  writeReg(kRegConfigPort1, kOrcPca9555ConfigPort1);
}

// Drive channel `ch` (1-10) HIGH, all other 9 channels LOW. Uses the real
// routing-driven map from lib/orc_relay_map/, not a sequential assumption.
//
// Real-hardware finding and root-cause chase, 2026-08-10, first ORC PCB
// tested, once it was mounted in its enclosure:
// 1. Exactly one relay ever clicked, ever -- including on later loop
//    passes back through the same channel -- until the board was fully
//    power-cycled, at which point exactly one more click happened before
//    going silent again. I2C itself looked perfectly healthy throughout
//    (every write ACKed, zero "I2C write FAILED" prints) -- the working
//    theory at the time was a PCA9555 power-on-reset silently reverting
//    its direction back to all-input, with subsequent Output writes still
//    ACKing but having no physical effect. Re-issuing Config/Polarity
//    every step (this function, called from every setChannel()) was added
//    to self-heal that scenario if it recurred.
// 2. That change immediately surfaced a SECOND symptom: I2C writes started
//    failing with driver-level ESP_ERR_INVALID_STATE errors instead of
//    ACKing. A manual I2C bus-recovery routine (bit-banged SCL clock-out +
//    forced STOP + Wire re-init) was tried here and REMOVED again the same
//    session -- real-hardware testing showed every retry failed
//    identically right after "recovery," meaning Wire.end()/Wire.begin()
//    doesn't cleanly tear down and rebuild this Arduino-ESP32 core's newer
//    i2c_master ("i2c-ng") driver state. Left in, it would have been
//    broken code claiming success it didn't have -- worse than not having
//    it. If real I2C bus recovery is needed here in the future, it needs a
//    driver-level fix (or a full ESP.restart()), not this approach.
// 3. Root cause, found by physical inspection, same session: **the
//    board's output-connector through-hole pins were shorting to the
//    enclosure chassis.** That explains both symptoms as one electrical
//    fault, not two separate bugs: the short pulled down shared supply
//    rail(s) hard enough under coil-switching current to both starve the
//    coil drive (user-reported "very quiet actuations, not enough to close
//    the contacts") and glitch the PCA9555's own logic supply, corrupting
//    I2C transactions. Confirmed as the real cause, not firmware, not a
//    coil-supply sizing problem -- see docs/circuit-draft.md and
//    docs/design-inputs.md for the mechanical-clearance follow-up this
//    should turn into (standoff/insulation/enclosure-fit, not a schematic
//    change).
//
// configurePca9555Direction() being called every step is left in place as
// cheap, harmless defense-in-depth against a future transient brownout of
// whatever cause -- it did not cause and does not fix the short above.
static void setChannel(uint8_t ch) {
  configurePca9555Direction();

  uint16_t channelMask = (uint16_t)(1u << (ch - 1));
  uint8_t port0 = 0x00;
  uint8_t port1 = 0x00;
  orcRelayMaskToPca9555(channelMask, port0, port1);

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

  // Direction: channels 1-10 as outputs; the 6 unused/spare bits (3 per
  // port, non-contiguous -- see lib/orc_relay_map/) left as inputs.
  writeReg(kRegConfigPort0, kOrcPca9555ConfigPort0);
  writeReg(kRegConfigPort1, kOrcPca9555ConfigPort1);

  Serial.println("Configured. Starting walking-single-HIGH pattern across channels 1-10.");
}

void loop() {
  for (uint8_t ch = 1; ch <= kNumChannels; ch++) {
    Serial.printf("Channel %u HIGH (all others LOW)\n", ch);
    setChannel(ch);
    delay(500);
  }
}
