// ORC application firmware: CANopen relay control
//
// This IS application-level firmware, unlike everything else in firmware/ --
// it's the real protocol ORC will run in the field, per
// docs/can-protocol-research.md and .claude/can-application-firmware-prompt.md.
// Built on top of, not replacing, the existing IO-level bring-up sketches:
// - lib/orc_can_addr's node-ID read is reused unchanged.
// - Real channel-to-PCA9555-register mapping, confirmed 2026-08-05 against
//   the actual schematic (docs/subcircuit-capture-guide.md's "Channel
//   mapping" table, read off hardware/i2c_expander.kicad_sch) and
//   implemented in firmware/lib/orc_relay_map/ -- routing-driven, NOT the
//   sequential "channels 1-8 = Port0 whole byte, 9-10 = Port1 bits 0-1"
//   layout pca9555_bringup originally assumed (that assumption was wrong;
//   pca9555_bringup has been corrected to match, same lib, same day).
// - uart_can_bringup's raw-UART transport is SUPERSEDED, not extended: this
//   firmware uses the ESP-IDF/Arduino-ESP32 TWAI driver on the same
//   GPIO21(TX)/GPIO20(RX) pins instead, per can-protocol-research.md's
//   UART-vs-TWAI resolution (TWAI has no dedicated IO_MUX pin -- it's
//   GPIO-Matrix-routable to any GPIO, so this is a firmware peripheral
//   change with zero schematic/pinout impact).
//
// No ORC board is in hand yet (board is at fab, see firmware/README.md).
// This compiles cleanly and is ready to flash; nothing here has run against
// real hardware, real TWAI frames, or a real PCA9555/SN65HVD230.

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include "driver/twai.h"
#include "orc_can_addr.h"
#include "orc_canopen.h"
#include "orc_relay_map.h"

// --- Pin assignment ---------------------------------------------------------
// Separate ORC_TWAI_* build flags from the existing ORC_UART_TX_PIN/RX_PIN,
// even though both name the same physical GPIO21/GPIO20 -- uart_can_bringup
// and this firmware drive genuinely different on-chip peripherals (UART1 vs
// TWAI) on those pins, and reusing the UART-named constant here would read
// as if this were still UART-based. See platformio.ini for the values.
static const gpio_num_t kTwaiTxPin = (gpio_num_t)ORC_TWAI_TX_PIN;
static const gpio_num_t kTwaiRxPin = (gpio_num_t)ORC_TWAI_RX_PIN;
static const uint8_t kPca9555Addr = ORC_PCA9555_ADDR;
static const uint8_t kI2cSdaPin = ORC_I2C_SDA_PIN;
static const uint8_t kI2cSclPin = ORC_I2C_SCL_PIN;

// --- PCA9555 register map (NXP datasheet command bytes) -- same as pca9555_bringup ---
static const uint8_t kRegOutputPort0 = 0x02;
static const uint8_t kRegOutputPort1 = 0x03;
static const uint8_t kRegInputPort0 = 0x00;
static const uint8_t kRegInputPort1 = 0x01;
static const uint8_t kRegPolarityPort0 = 0x04;
static const uint8_t kRegPolarityPort1 = 0x05;
static const uint8_t kRegConfigPort0 = 0x06;
static const uint8_t kRegConfigPort1 = 0x07;
// Config values: kOrcPca9555ConfigPort0/Port1, from lib/orc_relay_map/ --
// the real schematic's spares are 3 non-contiguous bits per port (Port0
// bits0-2, Port1 bits5-7), not the old "channels 9-10 only used on Port1"
// assumption these two masks used to encode locally.

// --- Timing defaults ---------------------------------------------------------
static const uint16_t kTpdo2DefaultIntervalMs = 1000;
static const uint32_t kHeartbeatIntervalMs = 1000;  // fixed, not SDO-configurable per the spec's scope
// RPDO1-timeout fail-safe: de-energize all channels if no RPDO1 arrives in
// this window. Not derived from any documented host command cadence (none
// specified anywhere in this project yet -- RPDO1 is event-driven, sent
// only when the host changes something, not periodic) -- picked as 5x the
// TPDO2/Heartbeat default period (1000ms), giving comfortable margin against
// normal idle gaps between commands while still failing safe within a
// human-noticeable few seconds if the host or bus genuinely disappears. A
// firmware default, adjustable, not a cited spec value.
static const uint32_t kRpdo1TimeoutMs = 5000;

// --- Runtime state -----------------------------------------------------------
static uint8_t g_nodeId = 0;
static uint8_t g_relayPort0 = 0;  // last-applied PCA9555 Output Port 0 (channels 1-8)
static uint8_t g_relayPort1 = 0;  // last-applied PCA9555 Output Port 1 bits 0-1 (channels 9-10)
static bool g_deenergizedByTimeout = false;
static unsigned long g_lastRpdo1Millis = 0;
static unsigned long g_lastTpdo2Millis = 0;
static unsigned long g_lastHeartbeatMillis = 0;
static uint16_t g_tpdo2IntervalMs = kTpdo2DefaultIntervalMs;

static Preferences g_prefs;
static const char *kPrefsNamespace = "orc_cfg";
static const char *kPrefsTpdo2Key = "tpdo2_ms";

// --- PCA9555 I2C helpers, same pattern as pca9555_bringup --------------------
static bool pca9555WriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kPca9555Addr);
  Wire.write(reg);
  Wire.write(value);
  uint8_t result = Wire.endTransmission();
  if (result != 0) {
    Serial.printf("PCA9555 I2C write FAILED (reg 0x%02X, val 0x%02X): endTransmission=%u\n",
                  reg, value, result);
    return false;
  }
  return true;
}

// Reads back the PCA9555's Input Port registers -- for a pin configured as
// an output, this reflects the actual driven logic level, not just the
// commanded value, so it's a genuine (if not current-sensed) "applied
// state" confirmation, not just an echo of what was written.
static bool pca9555ReadInputPorts(uint8_t &port0, uint8_t &port1) {
  Wire.beginTransmission(kPca9555Addr);
  Wire.write(kRegInputPort0);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)kPca9555Addr, 2) != 2) return false;
  port0 = Wire.read();
  port1 = Wire.read();
  return true;
}

// --- TWAI helpers -------------------------------------------------------------
static bool twaiSend(uint32_t cobId, const uint8_t *data, uint8_t dlc) {
  twai_message_t msg = {};
  msg.identifier = cobId;
  msg.extd = 0;
  msg.rtr = 0;
  msg.data_length_code = dlc;
  memcpy(msg.data, data, dlc);
  esp_err_t result = twai_transmit(&msg, pdMS_TO_TICKS(100));
  if (result != ESP_OK) {
    Serial.printf("TWAI transmit FAILED (COB-ID 0x%03lX): esp_err=%d\n", (unsigned long)cobId, (int)result);
    return false;
  }
  return true;
}

static void sendTpdo1() {
  uint8_t inPort0 = 0, inPort1 = 0;
  uint8_t payload[2];
  if (pca9555ReadInputPorts(inPort0, inPort1)) {
    uint16_t channelMask = orcPca9555ToRelayMask(inPort0, inPort1);
    orcPackChannelMask(channelMask, payload);
  } else {
    // Read-back failed -- fall back to the last-commanded value rather than
    // sending garbage, but this is a real fault condition worth knowing
    // about (I2C bus problem), not silently swallowed.
    Serial.println("WARNING: PCA9555 input-port read-back failed; TPDO1 reflects last-commanded state, not confirmed-applied state.");
    uint16_t channelMask = orcPca9555ToRelayMask(g_relayPort0, g_relayPort1);
    orcPackChannelMask(channelMask, payload);
  }
  twaiSend(orcCobId(kOrcCobIdTpdo1Base, g_nodeId), payload, 2);
}

static void applyRelayCommand(uint8_t port0, uint8_t port1) {
  g_relayPort0 = port0;
  g_relayPort1 = port1;
  pca9555WriteReg(kRegOutputPort0, port0);
  pca9555WriteReg(kRegOutputPort1, port1);
  sendTpdo1();
}

static void sendTpdo2() {
  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK) {
    Serial.println("WARNING: twai_get_status_info() failed; skipping this TPDO2 send.");
    return;
  }
  uint32_t uptimeSeconds = millis() / 1000;  // known limitation: millis() rolls over ~49 days; accepted for this scope, see README
  uint8_t payload[8];
  orcPackTpdo2(status.state, status.tx_error_counter, status.rx_error_counter, uptimeSeconds, payload);
  twaiSend(orcCobId(kOrcCobIdTpdo2Base, g_nodeId), payload, 8);
}

static void sendHeartbeat(uint8_t nmtState) {
  uint8_t payload[1] = {nmtState};
  twaiSend(orcCobId(kOrcCobIdHeartbeatBase, g_nodeId), payload, 1);
}

static void handleRpdo1(const twai_message_t &msg) {
  if (msg.data_length_code < 2) {
    Serial.println("WARNING: RPDO1 received with DLC < 2, ignoring malformed frame.");
    return;
  }
  uint16_t channelMask = orcUnpackChannelMask(msg.data);
  uint8_t port0 = 0, port1 = 0;
  orcRelayMaskToPca9555(channelMask, port0, port1);
  Serial.printf("RPDO1: channel mask=0x%03X -> PCA9555 Port0=0x%02X Port1=0x%02X\n",
                channelMask, port0, port1);
  applyRelayCommand(port0, port1);
  g_lastRpdo1Millis = millis();
  g_deenergizedByTimeout = false;
}

static void handleSdoRequest(const twai_message_t &msg) {
  uint16_t newValue = 0;
  bool changed = false;
  uint8_t resp[8];
  if (!orcHandleSdoRequest(msg.data, msg.data_length_code, g_tpdo2IntervalMs, newValue, changed, resp)) {
    return;  // malformed request (DLC < 8), no response per CiA 301 convention
  }
  if (changed) {
    g_tpdo2IntervalMs = newValue;
    g_prefs.putUShort(kPrefsTpdo2Key, g_tpdo2IntervalMs);
    Serial.printf("SDO write: TPDO2 event timer (1801h sub5) set to %u ms, persisted to NVS.\n", g_tpdo2IntervalMs);
  }
  twaiSend(orcCobId(kOrcCobIdSdoTxBase, g_nodeId), resp, 8);
}

static void handleIncomingFrame(const twai_message_t &msg) {
  if (msg.identifier == orcCobId(kOrcCobIdRpdo1Base, g_nodeId)) {
    handleRpdo1(msg);
  } else if (msg.identifier == orcCobId(kOrcCobIdSdoRxBase, g_nodeId)) {
    handleSdoRequest(msg);
  }
  // Anything else on the bus is not addressed to this node's known message
  // set -- ignored, not an error (a shared bus legitimately carries traffic
  // for other nodes).
}

static void checkRpdo1Timeout() {
  if (g_deenergizedByTimeout) return;  // already handled, don't repeat every loop
  if (millis() - g_lastRpdo1Millis < kRpdo1TimeoutMs) return;
  Serial.printf("RPDO1 fail-safe: no command received in %lu ms, de-energizing all channels.\n",
                (unsigned long)kRpdo1TimeoutMs);
  applyRelayCommand(0x00, 0x00);
  g_deenergizedByTimeout = true;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("=== ORC application firmware: CANopen relay control ===");
  Serial.println("This IS application firmware -- real relay control, not a bring-up sketch.");

  orcInitCanAddrPins();
  g_nodeId = orcReadCanAddress();
  orcPrintCanAddress();

  if (g_nodeId == 0) {
    // CiA 301 valid node IDs are 1-127; 0 is not a legal node address (all
    // 4 DIP-switch positions open). Refuse to join the bus with an invalid
    // ID rather than silently operating non-conformantly -- loop, printing
    // periodically, so this is debuggable (e.g. via a live serial monitor
    // while adjusting the switch bank and resetting) instead of a silent
    // hang.
    while (true) {
      Serial.println("ERROR: CAN node address reads 0 -- not a valid CANopen node ID (1-127 "
                      "required, per CiA 301). Set at least one address-select switch and "
                      "reset. Halting, not joining the bus.");
      delay(2000);
    }
  }

  g_prefs.begin(kPrefsNamespace, false);
  g_tpdo2IntervalMs = g_prefs.getUShort(kPrefsTpdo2Key, kTpdo2DefaultIntervalMs);
  Serial.printf("TPDO2 event timer loaded from NVS (namespace \"%s\", key \"%s\"): %u ms "
                "(default %u ms if never previously written).\n",
                kPrefsNamespace, kPrefsTpdo2Key, g_tpdo2IntervalMs, kTpdo2DefaultIntervalMs);

  Wire.begin(kI2cSdaPin, kI2cSclPin);
  Wire.setClock(100000);
  Wire.beginTransmission(kPca9555Addr);
  uint8_t probe = Wire.endTransmission();
  if (probe != 0) {
    Serial.printf("PCA9555 did not ACK at 0x%02X (endTransmission=%u). Halting -- relay "
                  "control is this firmware's entire purpose, cannot proceed without it. "
                  "Run i2c_scanner first to confirm the bus and address.\n",
                  kPca9555Addr, probe);
    while (true) delay(1000);
  }
  // All channels off before switching direction (matches pca9555_bringup's
  // own glitch-avoidance reasoning): device resets to all-input/0xFF, so
  // this write only takes effect once direction flips to output below.
  pca9555WriteReg(kRegOutputPort0, 0x00);
  pca9555WriteReg(kRegOutputPort1, 0x00);
  pca9555WriteReg(kRegPolarityPort0, 0x00);
  pca9555WriteReg(kRegPolarityPort1, 0x00);
  pca9555WriteReg(kRegConfigPort0, kOrcPca9555ConfigPort0);
  pca9555WriteReg(kRegConfigPort1, kOrcPca9555ConfigPort1);
  Serial.println("PCA9555 configured: 10 channels as outputs (real routing-driven map, see "
                  "lib/orc_relay_map/), all off.");

  twai_general_config_t gConfig = TWAI_GENERAL_CONFIG_DEFAULT(kTwaiTxPin, kTwaiRxPin, TWAI_MODE_NORMAL);
  // 125 kbit/s, LOCKED -- docs/can-protocol-research.md's "Bus bitrate"
  // section, resolved 2026-08-05. Do not change without updating that doc
  // and coordinating with rigos-core's FR-059 (the sibling repo consuming
  // this exact bitrate from the host side).
  twai_timing_config_t tConfig = TWAI_TIMING_CONFIG_125KBITS();
  // Accept-all filter: this app only cares about 2 COB-IDs (RPDO1, SDO-rx),
  // filtered in software in handleIncomingFrame() -- message volume on a
  // 10-relay accessory bus is trivially low, so a hardware acceptance
  // filter buys nothing worth the complexity of computing its mask/ID
  // fields for two non-contiguous IDs.
  twai_filter_config_t fConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&gConfig, &tConfig, &fConfig) != ESP_OK) {
    Serial.println("ERROR: twai_driver_install() failed. Halting.");
    while (true) delay(1000);
  }
  if (twai_start() != ESP_OK) {
    Serial.println("ERROR: twai_start() failed. Halting.");
    while (true) delay(1000);
  }
  Serial.printf("TWAI started: TX=GPIO%d RX=GPIO%d, 125 kbit/s, Node ID %u.\n",
                (int)kTwaiTxPin, (int)kTwaiRxPin, g_nodeId);

  // CANopen boot-up convention: announce Boot-up (0) once, then move to
  // Operational for every subsequent periodic heartbeat. No
  // Stopped/Pre-operational transitions are triggered by anything in this
  // firmware -- a fixed "Boot-up once, then always Operational" NMT
  // posture is the entire NMT behavior implemented here, stated explicitly
  // per the task's instruction not to silently skip NMT handling.
  sendHeartbeat(ORC_NMT_BOOTUP);

  unsigned long now = millis();
  g_lastRpdo1Millis = now;  // starts the fail-safe timer; channels already off, so a
                            // timeout firing before any RPDO1 ever arrives is harmless
  g_lastTpdo2Millis = now;
  g_lastHeartbeatMillis = now;

  Serial.println("Initialization complete. Entering Operational.");
}

void loop() {
  twai_message_t msg;
  // Non-blocking receive -- 0 ticks timeout, drain whatever's queued this
  // pass without stalling the periodic TPDO2/Heartbeat/timeout checks below.
  while (twai_receive(&msg, 0) == ESP_OK) {
    handleIncomingFrame(msg);
  }

  checkRpdo1Timeout();

  unsigned long now = millis();
  if (now - g_lastTpdo2Millis >= g_tpdo2IntervalMs) {
    sendTpdo2();
    g_lastTpdo2Millis = now;
  }
  if (now - g_lastHeartbeatMillis >= kHeartbeatIntervalMs) {
    sendHeartbeat(ORC_NMT_OPERATIONAL);
    g_lastHeartbeatMillis = now;
  }
}
