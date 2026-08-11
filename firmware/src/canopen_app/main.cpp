// ORC application firmware: CANopen relay control, driven from CAN or USB
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
// FR-004 (2026-08-05): USB control consolidated in from the now-retired
// usb_bench sketch. Real ORC PCBs are in hand and under active bench test
// as of this pass -- see docs/features/FR-004.md and firmware/README.md's
// "Real-hardware run, 2026-08-05" section. This firmware now drives relays
// from CAN (RPDO1) or USB (plain-ASCII protocol, docs/usb-bench-interface-spec.md)
// against the SAME PCA9555 state, with a single read-back feeding both
// CAN's TPDO1 broadcast and USB's unsolicited STATE lines -- a change from
// either interface is visible on both. See docs/features/FR-004.md for the
// full design writeup, including why a naive shared fail-safe clock would
// NOT correctly implement "any activity resets both" (the tighter of two
// thresholds always wins on a shared clock, making the looser one dead
// code) and what was actually built instead (context-sensitive threshold
// based on which interface supplied the most recent activity).
//
// KNOWN LIMITATION, documented not silently accepted (FR-004 Scope item 5):
// this file's Serial output was always a human debug log (RPDO1 dumps,
// TWAI errors, SDO writes, etc.), not the strictly line-clean protocol
// usb_bench alone was. That debug prose is now interleaved with the real
// PONG/STATE/ERR/HB protocol lines on the same port. A host-side parser
// MUST tolerate/ignore unrecognized lines -- the spec already required
// this for interleaved unsolicited lines; it now also covers diagnostic
// prose, not just other protocol lines.

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <ctype.h>
#include "driver/twai.h"
#include "esp_timer.h"
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
// Control-activity fail-safe: de-energize all channels if neither interface
// has produced a real command in this window. Two thresholds, not one --
// see the file header and docs/features/FR-004.md for why a single shared
// threshold doesn't actually implement "any activity resets both" the way
// it needs to. 5000ms (CAN) is not derived from any documented host command
// cadence (RPDO1 is event-driven, not periodic) -- picked as 5x the
// TPDO2/Heartbeat default period, comfortable margin against normal idle
// gaps while still failing safe within a human-noticeable few seconds.
// 30000ms (USB) is generous specifically because a human typing commands by
// hand pauses far longer than a machine's command cadence -- ported
// unchanged from usb_bench/FR-001's own reasoning.
static const uint32_t kCanActivityTimeoutMs = 5000;
static const uint32_t kUsbIdleTimeoutMs = 30000;

// --- USB protocol constants (docs/usb-bench-interface-spec.md) -------------
static const size_t kMaxLineLen = 128;
static const char *kFwVersion = "orc-canopen-fr004";
static const char *kBuildDate = __DATE__ " " __TIME__;

// --- Runtime state -----------------------------------------------------------
static uint8_t g_nodeId = 0;
static bool g_canAvailable = false;  // true once TWAI installs and starts successfully -- no
                                      // longer gated on node ID being nonzero, see setup()
static uint8_t g_relayPort0 = 0;  // last-commanded PCA9555 Output Port 0 (channels 1-8)
static uint8_t g_relayPort1 = 0;  // last-commanded PCA9555 Output Port 1 bits 0-1 (channels 9-10)
static uint16_t g_lastAppliedMask = 0;  // last channel mask actually observed via PCA9555 read-back
static bool g_deenergizedByTimeout = false;
// BUG-001 fix, still in force: whether the PCA9555 ACKed. false means relay
// I/O (RPDO1/TPDO1 on CAN, SET/GET/STATUS on USB) is unavailable, but
// whichever interface(s) are otherwise up keep working -- see
// requireRelayHardware() and probeAndConfigurePca9555() below.
static bool g_pca9555Present = false;

// Which interface most recently produced real activity, and when -- see
// checkControlActivityTimeout() for how this picks the applicable
// fail-safe threshold. kNone is the boot default (channels already off, so
// a timeout firing before anything ever arrives is harmless).
enum class ActivitySource { kNone, kCan, kUsb };
static ActivitySource g_lastActivitySource = ActivitySource::kNone;
static unsigned long g_lastActivityMillis = 0;

static unsigned long g_lastTpdo2Millis = 0;
static unsigned long g_lastHeartbeatMillis = 0;
static uint16_t g_tpdo2IntervalMs = kTpdo2DefaultIntervalMs;
static bool g_busOffRecoveryInitiated = false;
static unsigned long g_lastHealthCheckMillis = 0;
// Fixed cadence, deliberately independent of g_tpdo2IntervalMs -- that value
// is SDO-configurable by the host and could be set far higher than is
// reasonable for noticing/recovering from a bus fault promptly.
static const uint32_t kHealthCheckIntervalMs = 1000;

static Preferences g_prefs;
static const char *kPrefsNamespace = "orc_cfg";
static const char *kPrefsTpdo2Key = "tpdo2_ms";

// --- USB line-reader state ---------------------------------------------------
static char g_lineBuf[kMaxLineLen + 1];
static size_t g_lineLen = 0;
static bool g_lineOverflow = false;

// --- PCA9555 I2C helpers, same pattern as pca9555_bringup --------------------
static bool pca9555WriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kPca9555Addr);
  Wire.write(reg);
  Wire.write(value);
  uint8_t result = Wire.endTransmission();
  if (result != 0) {
    Serial.printf("PCA9555 I2C write FAILED (reg 0x%02X, val 0x%02X): endTransmission=%u\n",
                  reg, value, result);
    // Gone missing mid-session (unplugged, isolated-side power loss,
    // etc.) -- drop the flag so the next relay-touching call (either
    // interface) re-probes instead of repeating the same failing
    // transaction. See probeAndConfigurePca9555()/requireRelayHardware().
    g_pca9555Present = false;
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
  if (Wire.endTransmission(false) != 0) {
    g_pca9555Present = false;
    return false;
  }
  if (Wire.requestFrom((int)kPca9555Addr, 2) != 2) {
    g_pca9555Present = false;
    return false;
  }
  port0 = Wire.read();
  port1 = Wire.read();
  return true;
}

// Re-issues the PCA9555's Output, Polarity, and Config registers using the
// last-commanded state (g_relayPort0/g_relayPort1) -- self-heals a PCA9555
// that lost its configuration to a reset originating on the isolated
// (Domain B) side, e.g. a brief brownout of its own supply during a
// coil-switching event, WITHOUT needing a full ORC power cycle. Real
// hardware finding, 2026-08-05: chased on pca9555_bringup first (see that
// sketch's setChannel() for the full story) -- a PCA9555 power-on-reset
// reverts Config to 0xFF (all-input) and Output to its own 0xFF default
// (NXP datasheet POR value), silently, with I2C otherwise still ACKing
// fine.
//
// Write ORDER matters: Output MUST be re-written BEFORE Polarity/Config
// restore direction, not after. If direction flips back to output while
// the PCA9555's Output register still holds its stale post-reset 0xFF
// value, every Port0-mapped output channel would briefly drive HIGH the
// instant direction changes -- a real glitch on production relay-control
// firmware. Writing Output first means the register already holds the
// correct last-commanded value by the time direction is restored, so
// there's nothing to glitch to.
//
// Safe to call whether or not a reset actually happened -- idempotent if
// the PCA9555's real state already matched. Called both periodically (see
// loop()'s health-check block, so an idle period with no incoming command
// on either interface still self-heals) and on every applyRelayCommand()
// (so an active command self-heals immediately).
static void reassertPca9555State() {
  if (!g_pca9555Present) return;
  pca9555WriteReg(kRegOutputPort0, g_relayPort0);
  pca9555WriteReg(kRegOutputPort1, g_relayPort1);
  pca9555WriteReg(kRegPolarityPort0, 0x00);
  pca9555WriteReg(kRegPolarityPort1, 0x00);
  pca9555WriteReg(kRegConfigPort0, kOrcPca9555ConfigPort0);
  pca9555WriteReg(kRegConfigPort1, kOrcPca9555ConfigPort1);
}

static bool readAppliedMaskFromHardware(uint16_t &maskOut) {
  uint8_t port0 = 0, port1 = 0;
  if (!pca9555ReadInputPorts(port0, port1)) return false;
  maskOut = orcPca9555ToRelayMask(port0, port1);
  return true;
}

// Probes for the PCA9555 and, if it ACKs, runs the same output-configuration
// sequence setup() used to run unconditionally. Called once at boot and
// lazily (on demand) from requireRelayHardware() whenever g_pca9555Present
// is false -- ported from usb_bench/FR-001, ALSO now applied to the CAN
// side (handleRpdo1()), which never had lazy re-probe before this
// consolidation (BUG-001 only stopped it from halting the whole sketch, it
// never re-probed after boot).
static bool probeAndConfigurePca9555() {
  Wire.beginTransmission(kPca9555Addr);
  uint8_t probe = Wire.endTransmission();
  if (probe != 0) {
    g_pca9555Present = false;
    return false;
  }
  g_pca9555Present = true;
  // g_relayPort0/g_relayPort1 already hold the last-commanded state (0 at
  // boot; whatever the last real command was if this is a hot-plug
  // re-probe mid-session) -- reassertPca9555State() writes Output before
  // Config/Polarity, same glitch-avoidance ordering as every other call.
  reassertPca9555State();
  if (!g_pca9555Present) return false;  // reassertPca9555State() can clear it again on a write failure
  readAppliedMaskFromHardware(g_lastAppliedMask);  // baseline, avoids a spurious STATE burst on first command
  return true;
}

// SET/GET/STATUS (USB) and RPDO1 (CAN) are the only things that legitimately
// need the PCA9555. Called first thing by each: if hardware was already
// known-present, this is a no-op check; if not, it makes one lazy live
// attempt before giving up.
static bool requireRelayHardware() {
  if (g_pca9555Present) return true;
  return probeAndConfigurePca9555();
}

// --- Diagnostic-print helper ---------------------------------------------------
// BUG-003 round 2 (2026-08-10): the TX-queue-backlog fix below (twaiSend())
// was a real, correctly-reasoned mechanism but NOT the actual cause of the
// 0.5s-cadence failures -- confirmed directly by a same-session A/B
// real-hardware test: identical firmware, identical randomized-RPDO1 sweep
// at 0.5s cadence, the ONLY difference was whether something was actively
// draining canopen_app's own USB-serial console while the test ran.
// Nothing draining it: 1/15 confirmed, TPDO1 stale/missing, zero
// Heartbeat/TPDO2 observed for the whole run. Something draining it
// (a second script reading COM6 concurrently): 15/15 confirmed, TPDO1
// arriving within single-digit milliseconds of every RPDO1, every single
// round. Same binary, same cadence, same bus -- the entire difference was
// serial backpressure.
//
// Real mechanism, found by re-reading HWCDC.cpp with this new evidence in
// hand: BUG-002's `Serial.setTxTimeoutMs(20)` fix bounded *each retry
// interval*, but `max_consec_timeouts` (HWCDC.cpp) is a HARDCODED constant,
// 20, not something that call adjusts -- so a single blocking
// `Serial` write's real worst case is `20 x 20ms = 400ms`, not the "~200ms
// (2 retries)" this file's own setup() comment mistakenly claimed before
// this fix (corrected there too). `handleRpdo1()` and `applyRelayCommand()`
// between them can issue several such calls per RPDO1 -- the RPDO1 log
// line, one `STATE` line per changed channel (up to 10), the backlog
// notice below -- and if console backpressure is present, EACH one can hit
// close to that 400ms ceiling, compounding across a single command to
// multiple seconds, with no recovery for the rest of a run because nothing
// ever drains the buffer in a CAN-only test session.
//
// Fix: `printBestEffort()` checks `Serial.availableForWrite()` first and
// skips the print entirely rather than blocking if there's no room --
// removing the hazard rather than further bounding it. Applied to the
// specific hot-path calls implicated by the A/B test (see call sites
// below); not a blanket replacement of every `Serial` call in this file
// (that's still open, same as BUG-002 left it -- see that bug's "options
// 2/3" for the fuller audit if this ever resurfaces on an ungated call).
static void printBestEffort(const char *s) {
  if (Serial.availableForWrite() > 0) {
    Serial.print(s);
  }
}

// --- TWAI helpers -------------------------------------------------------------
// BUG-003 (2026-08-10): under sub-1Hz RPDO1 commanding, real-hardware
// tracing showed TPDO1 confirmations settling into a persistent "always one
// round behind, never catches up" pattern, with Heartbeat/TPDO2 -- ORC's
// own independent 1Hz periodic status broadcasts -- disappearing from the
// bus entirely for the rest of the run once it happened. **This TX-queue
// mechanism is real and worth keeping, but round 2 (see printBestEffort()'s
// comment above) found the actual dominant cause was blocking Serial
// writes, not this.** TWAI_GENERAL_CONFIG_DEFAULT's tx_queue_len is 5
// (confirmed in driver/twai.h). Every status message this firmware sends
// (TPDO1, TPDO2, Heartbeat, SDO response) reports CURRENT state -- a copy
// still sitting unsent in the 5-deep TX queue is not "delayed," it's
// flat-out WRONG the moment a fresher one becomes due. Nothing previously
// stopped one from clogging the queue and blocking everything queued
// behind it in FIFO order once send rate outpaced the bus's actual drain
// rate for any reason -- kept as real defense-in-depth even though it
// alone didn't clear BUG-003's symptom.
//
// Fix: before enqueueing anything, check twai_get_status_info()'s
// msgs_to_tx -- if nonzero, something older is still backlogged and hasn't
// left the transceiver yet. Clear it with twai_clear_transmit_queue()
// before enqueueing the fresh message, so the newest status always wins
// instead of queuing behind stale ones. Only acts when a backlog is
// actually detected (msgs_to_tx > 0) -- a no-op under normal healthy
// conditions, where the queue should already be empty by the time the next
// send happens.
//
// Known, accepted tradeoff: this can occasionally discard a message that
// was queued moments earlier in the SAME loop() pass by a different
// twaiSend() call (e.g. TPDO2 then Heartbeat both firing in one iteration)
// if it genuinely hasn't left the wire yet -- rare at 125 kbit/s (a short
// frame transmits in well under 1ms under normal conditions), and a single
// missed periodic broadcast self-corrects on the next 1s cycle regardless.
static bool twaiSend(uint32_t cobId, const uint8_t *data, uint8_t dlc) {
  if (!g_canAvailable) return false;

  twai_status_info_t status;
  if (twai_get_status_info(&status) == ESP_OK && status.msgs_to_tx > 0) {
    char msg[128];
    snprintf(msg, sizeof(msg),
             "TWAI TX backlog detected (%lu msg(s) still queued) before sending COB-ID "
             "0x%03lX -- clearing stale backlog so this status stays current (BUG-003).\n",
             (unsigned long)status.msgs_to_tx, (unsigned long)cobId);
    printBestEffort(msg);
    twai_clear_transmit_queue();
  }

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

// Records real command activity from either interface, and which one --
// see checkControlActivityTimeout() for why the source matters, not just
// the timestamp. Clears g_deenergizedByTimeout so a fresh command after a
// fail-safe trip is treated as a real recovery, not ignored.
static void noteActivity(ActivitySource src) {
  g_lastActivityMillis = millis();
  g_lastActivitySource = src;
  g_deenergizedByTimeout = false;
}

// Single choke point for every relay-state change, regardless of which
// interface caused it (CAN RPDO1, USB SET, or this file's own fail-safe
// de-energize). Re-asserts PCA9555 state, does ONE read-back, then feeds
// that single read-back to BOTH: a CAN TPDO1 broadcast and any USB STATE
// lines for channels that actually changed. FR-004: this is a real
// behavior upgrade over the pre-consolidation world, not just a code
// merge -- a change from either interface is now visible on both, not only
// echoed back on the interface that caused it.
static void applyRelayCommand(uint8_t port0, uint8_t port1) {
  g_relayPort0 = port0;
  g_relayPort1 = port1;
  reassertPca9555State();

  uint16_t appliedMask;
  uint8_t inPort0 = 0, inPort1 = 0;
  if (pca9555ReadInputPorts(inPort0, inPort1)) {
    appliedMask = orcPca9555ToRelayMask(inPort0, inPort1);
  } else {
    // Read-back failed -- fall back to the last-commanded value rather than
    // sending garbage, but this is a real fault condition worth knowing
    // about (I2C bus problem), not silently swallowed.
    Serial.println("WARNING: PCA9555 input-port read-back failed; TPDO1/STATE reflect "
                    "last-commanded state, not confirmed-applied state.");
    appliedMask = orcPca9555ToRelayMask(g_relayPort0, g_relayPort1);
  }

  // CAN side: TPDO1, on every real applied-state change, not just ones
  // RPDO1 itself caused -- a USB-originated change is now visible to any
  // CAN listener too. No-op (returns false, harmless) if CAN never joined.
  uint8_t tpdo1Payload[2];
  orcPackChannelMask(appliedMask, tpdo1Payload);
  twaiSend(orcCobId(kOrcCobIdTpdo1Base, g_nodeId), tpdo1Payload, 2);

  // USB side: unsolicited STATE lines for whatever actually changed, per
  // docs/usb-bench-interface-spec.md -- sent regardless of which interface
  // (or the fail-safe) caused the change. Best-effort (printBestEffort(),
  // see its own comment) -- can be up to 10 of these per RPDO1 on a
  // busy mask change; BUG-003 round 2 found exactly this loop, with no
  // console reader attached, compounding into multi-second stalls.
  uint16_t changed = appliedMask ^ g_lastAppliedMask;
  for (uint8_t ch = 1; ch <= 10; ch++) {
    if ((changed >> (ch - 1)) & 0x01) {
      bool on = (appliedMask >> (ch - 1)) & 0x01;
      char line[24];
      snprintf(line, sizeof(line), "STATE %u %s\n", ch, on ? "ON" : "OFF");
      printBestEffort(line);
    }
  }
  g_lastAppliedMask = appliedMask;
}

static void sendTpdo2() {
  if (!g_canAvailable) return;
  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK) {
    Serial.println("WARNING: twai_get_status_info() failed; skipping this TPDO2 send.");
    return;
  }
  // esp_timer_get_time() is a 64-bit microseconds-since-boot monotonic
  // counter -- deliberately NOT millis() (32-bit milliseconds, wraps at
  // ~49.7 days), which would silently reset this field to near-zero every
  // ~49.7 days on a continuously-powered unit and defeat the whole reason
  // this field is a uint32_t seconds count (~136 years of headroom) instead
  // of a 2-byte one. Found and fixed 2026-08-05 review -- see
  // docs/can-protocol-research.md's TPDO2 section.
  uint32_t uptimeSeconds = (uint32_t)(esp_timer_get_time() / 1000000);
  uint8_t payload[8];
  orcPackTpdo2(status.state, status.tx_error_counter, status.rx_error_counter, uptimeSeconds, payload);
  twaiSend(orcCobId(kOrcCobIdTpdo2Base, g_nodeId), payload, 8);
}

static void sendHeartbeat(uint8_t nmtState) {
  if (!g_canAvailable) return;
  uint8_t payload[1] = {nmtState};
  twaiSend(orcCobId(kOrcCobIdHeartbeatBase, g_nodeId), payload, 1);
}

static void handleRpdo1(const twai_message_t &msg) {
  if (msg.data_length_code < 2) {
    Serial.println("WARNING: RPDO1 received with DLC < 2, ignoring malformed frame.");
    return;
  }
  // A syntactically valid RPDO1 counts as CAN activity regardless of
  // whether relay hardware is present to act on it -- the host did send a
  // real command.
  noteActivity(ActivitySource::kCan);

  if (!requireRelayHardware()) {
    Serial.println("RPDO1 received but no PCA9555 present -- relay command cannot be applied. "
                    "Node stays on the bus (heartbeat/TPDO2 unaffected); TPDO1 not sent.");
    return;
  }
  uint16_t channelMask = orcUnpackChannelMask(msg.data);
  uint8_t port0 = 0, port1 = 0;
  orcRelayMaskToPca9555(channelMask, port0, port1);
  // Diagnostic print moved AFTER applyRelayCommand() (which sends TPDO1),
  // not before -- BUG-003 round 2 found this exact line, positioned here,
  // was the single biggest contributor to the observed multi-second
  // stalls when nothing was draining the USB console. Belt-and-suspenders:
  // still best-effort (printBestEffort()) even in its new position, since
  // applyRelayCommand() itself issues further Serial calls (STATE lines)
  // that a later handleRpdo1() call could still queue behind.
  applyRelayCommand(port0, port1);
  char line[80];
  snprintf(line, sizeof(line), "RPDO1: channel mask=0x%03X -> PCA9555 Port0=0x%02X Port1=0x%02X\n",
           channelMask, port0, port1);
  printBestEffort(line);
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

// TWAI bus-off recovery -- found missing in 2026-08-05 review, per ESP-IDF's
// own docs the driver does NOT recover from bus-off on its own:
// twai_initiate_recovery() must be called explicitly, and even after
// recovery completes (128 bus-idle occurrences observed) the driver lands in
// STOPPED, not RUNNING -- twai_start() must be called again to resume.
// Without this, a single bus fault (short, glitch, anything that trips
// bus-off) would be correctly reported via TPDO2's health byte forever, but
// never actually recovered from -- exactly the "detects the fault but can't
// self-heal" trap this project's sibling repo (rigos-core) explicitly
// designed its own io_relay daemon to avoid (BUG-016).
static void checkTwaiRecovery() {
  if (!g_canAvailable) return;
  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK) return;

  if (status.state == TWAI_STATE_BUS_OFF) {
    if (!g_busOffRecoveryInitiated) {
      Serial.println("TWAI bus-off detected -- initiating recovery.");
      twai_initiate_recovery();
      g_busOffRecoveryInitiated = true;
    }
  } else if (status.state == TWAI_STATE_STOPPED && g_busOffRecoveryInitiated) {
    // Only treat STOPPED as "recovery complete" if we're the ones who
    // initiated it -- the driver also passes through STOPPED at normal
    // startup, before twai_start() is first called in setup().
    Serial.println("TWAI bus recovery complete -- restarting driver.");
    if (twai_start() == ESP_OK) {
      g_busOffRecoveryInitiated = false;
    } else {
      Serial.println("WARNING: twai_start() failed after recovery; will retry next check.");
    }
  }
}

// Unified control-activity fail-safe. FR-004: a naive single shared
// last-activity timestamp does NOT correctly implement "any activity
// resets both timers" -- since kCanActivityTimeoutMs (5000) < kUsbIdleTimeoutMs
// (30000), a shared clock means the 5s check always fires first, making
// the 30s USB grace period unreachable dead code. The actual fix: track
// WHICH interface supplied the most recent activity and pick the
// threshold based on that -- a pure-CAN session keeps the original strict
// 5s RPDO1 fail-safe; a pure-USB session gets the original 30s human-
// typing grace; either interface's activity postpones the other's
// countdown (since both funnel through the same g_lastActivityMillis via
// noteActivity()).
//
// Second real refinement (user request): the CAN-side 5s threshold should
// only apply once CAN traffic has actually addressed this node at least
// once -- g_lastActivitySource only ever becomes ActivitySource::kCan
// inside handleRpdo1(), which is only reached for a frame matching THIS
// node's own RPDO1 COB-ID (handleIncomingFrame's dispatch), so "source ==
// kCan has ever been set" already IS "CAN has addressed this node." Before
// that first contact (source == kNone, i.e. neither interface has ever
// produced real activity), there is nothing to time out FROM -- a node
// that has simply never been talked to yet is not the same situation as
// one that was talked to and then went quiet, and shouldn't force a
// same-boot de-energize (harmless when channels start at 0, but pointless
// I2C traffic and a confusing log line otherwise). The fail-safe now only
// arms once real activity -- CAN or USB -- has happened at least once.
static void checkControlActivityTimeout() {
  if (g_deenergizedByTimeout) return;  // already handled, don't repeat every loop
  if (g_lastActivitySource == ActivitySource::kNone) return;  // never addressed by either interface yet
  uint32_t threshold =
      (g_lastActivitySource == ActivitySource::kUsb) ? kUsbIdleTimeoutMs : kCanActivityTimeoutMs;
  if (millis() - g_lastActivityMillis < threshold) return;
  Serial.printf("Control-activity fail-safe: no command in %lu ms (last source: %s) -- "
                "de-energizing all channels.\n",
                (unsigned long)threshold,
                g_lastActivitySource == ActivitySource::kUsb
                    ? "USB"
                    : (g_lastActivitySource == ActivitySource::kCan ? "CAN" : "none"));
  applyRelayCommand(0x00, 0x00);
  g_deenergizedByTimeout = true;
}

// --- USB command handling ----------------------------------------------------
static void toUpperInPlace(char *s) {
  for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static void formatStatus(char out[11]) {
  uint16_t mask = g_lastAppliedMask;
  uint16_t readMask;
  if (readAppliedMaskFromHardware(readMask)) mask = readMask;
  for (uint8_t ch = 1; ch <= 10; ch++) {
    out[ch - 1] = ((mask >> (ch - 1)) & 0x01) ? '1' : '0';
  }
  out[10] = '\0';
}

static void handleUsbLine(char *line) {
  // Any received line -- valid or not -- is evidence a host is present and
  // typing; resets the control-activity fail-safe. Done before parsing so
  // even a malformed command counts, matching FR-001's original reasoning.
  noteActivity(ActivitySource::kUsb);

  char *cmd = strtok(line, " \t");
  if (!cmd) {
    Serial.println("ERR BAD_ARGS");
    return;
  }
  toUpperInPlace(cmd);

  if (strcmp(cmd, "PING") == 0) {
    Serial.printf("PONG %s\n", kFwVersion);

  } else if (strcmp(cmd, "VERSION") == 0) {
    Serial.printf("VERSION %s %s\n", kFwVersion, kBuildDate);

  } else if (strcmp(cmd, "SET") == 0) {
    char *chTok = strtok(NULL, " \t");
    char *stateTok = strtok(NULL, " \t");
    if (!chTok || !stateTok) {
      Serial.println("ERR BAD_ARGS");
      return;
    }
    int ch = atoi(chTok);
    if (ch < 1 || ch > 10) {
      Serial.println("ERR BAD_CHANNEL");
      return;
    }
    if (!requireRelayHardware()) {
      Serial.println("ERR NO_RELAY_HARDWARE");
      return;
    }
    toUpperInPlace(stateTok);
    bool on;
    if (strcmp(stateTok, "ON") == 0) {
      on = true;
    } else if (strcmp(stateTok, "OFF") == 0) {
      on = false;
    } else {
      Serial.println("ERR BAD_ARGS");
      return;
    }
    uint16_t newMask = g_lastAppliedMask;
    if (on) {
      newMask |= (uint16_t)(1u << (ch - 1));
    } else {
      newMask &= (uint16_t)~(1u << (ch - 1));
    }
    uint8_t port0 = 0, port1 = 0;
    orcRelayMaskToPca9555(newMask, port0, port1);
    applyRelayCommand(port0, port1);
    Serial.printf("OK SET %d %s\n", ch, on ? "ON" : "OFF");

  } else if (strcmp(cmd, "GET") == 0) {
    char *chTok = strtok(NULL, " \t");
    if (!chTok) {
      Serial.println("ERR BAD_ARGS");
      return;
    }
    int ch = atoi(chTok);
    if (ch < 1 || ch > 10) {
      Serial.println("ERR BAD_CHANNEL");
      return;
    }
    if (!requireRelayHardware()) {
      Serial.println("ERR NO_RELAY_HARDWARE");
      return;
    }
    uint16_t mask;
    if (!readAppliedMaskFromHardware(mask)) {
      Serial.println("ERR I2C_READ_FAILED");
      return;
    }
    bool on = (mask >> (ch - 1)) & 0x01;
    Serial.printf("STATE %d %s\n", ch, on ? "ON" : "OFF");

  } else if (strcmp(cmd, "STATUS") == 0) {
    if (!requireRelayHardware()) {
      Serial.println("ERR NO_RELAY_HARDWARE");
      return;
    }
    char buf[11];
    formatStatus(buf);
    Serial.printf("STATUS %s\n", buf);

  } else {
    Serial.println("ERR UNKNOWN_CMD");
  }
}

static void pollUsbSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      g_lineBuf[g_lineLen] = '\0';
      if (g_lineOverflow) {
        Serial.println("ERR LINE_TOO_LONG");
      } else if (g_lineLen > 0) {
        handleUsbLine(g_lineBuf);
      }
      g_lineLen = 0;
      g_lineOverflow = false;
    } else if (c == '\r') {
      continue;  // tolerated/stripped, \n terminates the line
    } else {
      if (g_lineLen < kMaxLineLen) {
        g_lineBuf[g_lineLen++] = c;
      } else {
        g_lineOverflow = true;  // keep consuming silently until the next \n
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  // TPDO1 lag/loss investigation, .claude/tpdo1-lag-investigation-prompt.md.
  // Native USB-CDC (HWCDC) writes have a documented worst-case block when
  // the port reports "connected" (driven by isPlugged(), i.e. the physical
  // USB link being up -- NOT by whether any application has actually opened
  // the port for reading) but nothing is draining the ring buffer.
  // CORRECTED, BUG-003 round 2 (2026-08-10): this call alone does NOT bound
  // that worst case to "~200ms (2 retries)" as originally claimed here --
  // `max_consec_timeouts` (HWCDC.cpp) is a HARDCODED constant, 20, not
  // something this call adjusts. The real worst case per blocking write is
  // `20 x tx_timeout_ms` = 20 x 20ms = 400ms, not 40-200ms. Still a real
  // improvement over the 2000ms stock default (BUG-002's original fix),
  // but round 2's real-hardware A/B test (identical firmware, identical 0.5s
  // RPDO1 cadence, only difference was whether something drained the USB
  // console: 1/15 confirmed with nothing reading it, 15/15 with a reader
  // attached) showed this timeout bound ALONE is insufficient once several
  // Serial calls stack up per RPDO1 (handleRpdo1()'s log line, up to 10
  // STATE lines, TPDO1-path diagnostics -- each up to 400ms if backpressure
  // is present). The actual fix is printBestEffort() (see its own comment,
  // near twaiSend()): skip the print entirely rather than block, applied to
  // the specific hot-path calls the A/B test implicated. This timeout
  // setting stays in place as defense-in-depth for the calls NOT yet gated
  // that way (WARNING/ERROR prints elsewhere in this file).
  Serial.setTxTimeoutMs(20);
  delay(1500);
  Serial.println();
  Serial.println("=== ORC application firmware: CANopen relay control (CAN + USB) ===");
  Serial.println("This IS application firmware -- real relay control, not a bring-up sketch.");

  orcInitCanAddrPins();
  g_nodeId = orcReadCanAddress();
  orcPrintCanAddress();

  g_prefs.begin(kPrefsNamespace, false);
  g_tpdo2IntervalMs = g_prefs.getUShort(kPrefsTpdo2Key, kTpdo2DefaultIntervalMs);
  Serial.printf("TPDO2 event timer loaded from NVS (namespace \"%s\", key \"%s\"): %u ms "
                "(default %u ms if never previously written).\n",
                kPrefsNamespace, kPrefsTpdo2Key, g_tpdo2IntervalMs, kTpdo2DefaultIntervalMs);

  Wire.begin(kI2cSdaPin, kI2cSclPin);
  Wire.setClock(100000);
  if (probeAndConfigurePca9555()) {
    Serial.println("PCA9555 configured: 10 channels as outputs (real routing-driven map, see "
                    "lib/orc_relay_map/), all off.");
  } else {
    Serial.printf("WARNING: PCA9555 did not ACK at 0x%02X -- no relay hardware present. CAN "
                  "heartbeat/TPDO2/SDO and USB PING/VERSION/HB still work; relay I/O "
                  "(RPDO1/TPDO1 on CAN, SET/GET/STATUS on USB) unavailable until a PCA9555 is "
                  "detected (re-probed automatically on the next relay-touching attempt on "
                  "either interface -- no reboot needed if hot-plugged later). Run i2c_scanner "
                  "to confirm wiring.\n",
                  kPca9555Addr);
  }

  // FR-004: applying the exact same lesson BUG-001/FR-001 already taught
  // twice (a fault on one domain shouldn't silently kill an unrelated
  // domain's independent function) proactively here, before it needed its
  // own bug report. A missing/invalid node ID used to halt this ENTIRE
  // sketch -- which would also have taken USB control down with it, the
  // one thing this consolidation exists to make independently available.
  // Now: TWAI bring-up is always attempted, regardless of the node ID
  // read; USB always continues into loop() either way.
  //
  // User request, follow-up same day: node ID 0 now ENABLES CAN rather
  // than disabling it. This is a deliberate departure from CiA 301, which
  // reserves node ID 0 (not a valid 1-127 node address) -- stated plainly,
  // not silently relaxed. With g_nodeId==0, orcCobId(base, 0) == base,
  // so every COB-ID this firmware uses collapses to its literal base value
  // (RPDO1 listens on 0x200, TPDO1 sends on 0x180, Heartbeat on 0x700,
  // etc.) -- fine for a single bench unit, but NOT a real multi-node
  // CANopen network address; a second ORC unit also left at node ID 0
  // would collide on every COB-ID.
  if (g_nodeId == 0) {
    Serial.println("NOTE: CAN node address reads 0 -- not a valid CANopen node ID per CiA 301 "
                    "(1-127 required for real multi-node interoperability). CAN is still ENABLED "
                    "at this point (user's explicit request) -- COB-IDs collapse to their literal "
                    "base values since base+0=base (RPDO1=0x200, TPDO1=0x180, Heartbeat=0x700, "
                    "etc.). Fine for a single bench unit; do NOT run two ORC units both at node ID "
                    "0 on the same bus, they will collide on every COB-ID. Set an address-select "
                    "switch for any real multi-unit deployment.");
  }

  {
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
      // FR-004: downgraded from halting the whole sketch -- USB still
      // needs to work even if TWAI install fails for some reason.
      Serial.println("ERROR: twai_driver_install() failed. CAN bus disabled for this session; "
                      "USB control still works.");
      g_canAvailable = false;
    } else if (twai_start() != ESP_OK) {
      Serial.println("ERROR: twai_start() failed. CAN bus disabled for this session; USB "
                      "control still works.");
      g_canAvailable = false;
    } else {
      g_canAvailable = true;
      Serial.printf("TWAI started: TX=GPIO%d RX=GPIO%d, 125 kbit/s, Node ID %u.\n",
                    (int)kTwaiTxPin, (int)kTwaiRxPin, g_nodeId);
      // CANopen boot-up convention: announce Boot-up (0) once, then move to
      // Operational for every subsequent periodic heartbeat. No
      // Stopped/Pre-operational transitions are triggered by anything in
      // this firmware -- a fixed "Boot-up once, then always Operational"
      // NMT posture is the entire NMT behavior implemented here.
      sendHeartbeat(ORC_NMT_BOOTUP);
    }
  }

  unsigned long now = millis();
  g_lastActivityMillis = now;  // placeholder value; checkControlActivityTimeout() doesn't act on
                                // it at all while g_lastActivitySource is kNone (see that
                                // function) -- the fail-safe only arms once either interface has
                                // produced real activity for the first time.
  g_lastActivitySource = ActivitySource::kNone;
  g_lastTpdo2Millis = now;
  g_lastHeartbeatMillis = now;
  g_lastHealthCheckMillis = now;

  Serial.println("Initialization complete. Commands: PING, VERSION, SET <ch 1-10> ON|OFF, "
                  "GET <ch>, STATUS. Entering Operational.");
}

void loop() {
  pollUsbSerial();

  if (g_canAvailable) {
    twai_message_t msg;
    // Non-blocking receive -- 0 ticks timeout, drain whatever's queued this
    // pass without stalling the periodic TPDO2/Heartbeat/timeout checks below.
    while (twai_receive(&msg, 0) == ESP_OK) {
      handleIncomingFrame(msg);
    }
  }

  checkControlActivityTimeout();

  unsigned long now = millis();
  if (now - g_lastHealthCheckMillis >= kHealthCheckIntervalMs) {
    checkTwaiRecovery();
    // Self-heal a PCA9555 that lost its config to an isolated-side reset
    // during an idle period (no command on either interface to trigger the
    // same recovery via applyRelayCommand()) -- see reassertPca9555State().
    reassertPca9555State();
    g_lastHealthCheckMillis = now;
  }
  if (g_canAvailable && now - g_lastTpdo2Millis >= g_tpdo2IntervalMs) {
    sendTpdo2();
    g_lastTpdo2Millis = now;
  }
  // One shared 1000ms cadence for both heartbeat emitters: the CAN
  // heartbeat (only if g_canAvailable) and the USB HB line (always). Both
  // read/reset the SAME g_lastHeartbeatMillis in one block -- keeping them
  // in two separate if-blocks each gated on their own copy of the same
  // condition was a real bug caught before flashing: if g_canAvailable was
  // false, a CAN-only-gated block would never update the timestamp, and an
  // unconditional USB-only block reading that same stale timestamp would
  // fire on every single loop() iteration once 1000ms had first elapsed --
  // flooding "HB" lines instead of one per second.
  if (now - g_lastHeartbeatMillis >= kHeartbeatIntervalMs) {
    if (g_canAvailable) {
      sendHeartbeat(ORC_NMT_OPERATIONAL);
    }
    Serial.printf("HB %lu\n", now);
    g_lastHeartbeatMillis = now;
  }
}
