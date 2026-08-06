// ORC application firmware: USB bench/debug interface
//
// Implements docs/usb-bench-interface-spec.md and docs/features/FR-001.md.
// A bench/dev tool, NOT the production control path -- ORC is still
// CAN-only in the field (docs/design-inputs.md's "Primary control path"
// resolution). This reuses the ESP32-C3's native USB-C port -- already
// wired for programming/flashing -- as a plain-ASCII CDC-ACM serial
// interface, so relay-channel logic can be driven and observed from a
// terminal or a script before any CAN transceiver hardware is wired up.
//
// Real relay I/O goes through firmware/lib/orc_relay_map/ -- the same real,
// schematic-confirmed channel<->PCA9555-register mapping pca9555_bringup
// and canopen_app already share. This sketch does not hand-roll a third
// copy of that table.
//
// No ORC board is in hand yet (see firmware/README.md). This compiles
// cleanly and is ready to flash; nothing here has run against real
// hardware or a real PCA9555.

#include <Arduino.h>
#include <Wire.h>
#include <ctype.h>
#include "orc_relay_map.h"

// --- Pin assignment -----------------------------------------------------
// Same I2C pins/address every other sketch in this directory uses --
// no new build flags needed for this sketch.
static const uint8_t kPca9555Addr = ORC_PCA9555_ADDR;
static const uint8_t kI2cSdaPin = ORC_I2C_SDA_PIN;
static const uint8_t kI2cSclPin = ORC_I2C_SCL_PIN;

// --- PCA9555 register map (NXP datasheet command bytes) -- same as pca9555_bringup/canopen_app ---
static const uint8_t kRegOutputPort0 = 0x02;
static const uint8_t kRegOutputPort1 = 0x03;
static const uint8_t kRegInputPort0 = 0x00;
static const uint8_t kRegInputPort1 = 0x01;
static const uint8_t kRegPolarityPort0 = 0x04;
static const uint8_t kRegPolarityPort1 = 0x05;
static const uint8_t kRegConfigPort0 = 0x06;
static const uint8_t kRegConfigPort1 = 0x07;
// Config values: kOrcPca9555ConfigPort0/Port1, from lib/orc_relay_map/.

// --- Protocol constants (docs/usb-bench-interface-spec.md) ---------------
static const size_t kMaxLineLen = 128;
static const uint32_t kHeartbeatIntervalMs = 1000;
// Idle-timeout fail-safe -- FR-001's own decision, spec left this open.
// De-energize all channels for philosophical consistency with CAN's
// RPDO1-timeout fail-safe (can-protocol-research.md, implemented in
// canopen_app), but with a much longer window than CAN's 5000ms: this is
// an interactive human-typed interface, not a machine command stream --a
// person pausing to read STATUS output shouldn't trip a safety timeout the
// way a genuinely vanished CAN host should. ANY received line (valid or
// malformed) counts as activity and resets this timer -- a malformed
// command is still evidence a host is present and typing.
static const uint32_t kIdleTimeoutMs = 30000;

static const char *kFwVersion = "orc-usb-bench-fr001";
static const char *kBuildDate = __DATE__ " " __TIME__;

// --- Runtime state --------------------------------------------------------
static char g_lineBuf[kMaxLineLen + 1];
static size_t g_lineLen = 0;
static bool g_lineOverflow = false;

static uint16_t g_lastAppliedMask = 0;  // last channel mask observed via PCA9555 read-back
static unsigned long g_lastActivityMillis = 0;
static unsigned long g_lastHeartbeatMillis = 0;

// True once a PCA9555 has ACKed and been configured. FR-001 REOPENED
// 2026-08-05: this used to be implicit in "setup() halted if not" -- real
// hardware showed that gate also silently killed PING/VERSION/framing,
// none of which touch the PCA9555 at all. Now setup() never halts; this
// flag is what SET/GET/STATUS check instead.
static bool g_pca9555Present = false;

// --- PCA9555 I2C helpers, same pattern as canopen_app/pca9555_bringup ----
static bool pca9555WriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kPca9555Addr);
  Wire.write(reg);
  Wire.write(value);
  uint8_t result = Wire.endTransmission();
  if (result != 0) {
    Serial.printf("ERR I2C write failed (reg 0x%02X, val 0x%02X): endTransmission=%u\n", reg,
                  value, result);
    // A write failure after the PCA9555 was previously seen means it's
    // gone missing mid-session (unplugged, power loss on Domain B, etc.) --
    // drop the flag so the next SET/GET/STATUS re-probes instead of
    // repeating the same failing I2C transaction and error message.
    g_pca9555Present = false;
    return false;
  }
  return true;
}

// Reads the Input Port registers -- for an output-configured pin this
// reflects the actual driven logic level, not just the last commanded
// value, so this is genuine "applied state" per the spec's GET/STATUS
// wording, not an optimistic echo.
static bool pca9555ReadInputPorts(uint8_t &port0, uint8_t &port1) {
  Wire.beginTransmission(kPca9555Addr);
  Wire.write(kRegInputPort0);
  if (Wire.endTransmission(false) != 0) {
    g_pca9555Present = false;  // gone missing mid-session -- see pca9555WriteReg's comment
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

static bool readAppliedMask(uint16_t &maskOut) {
  uint8_t port0 = 0, port1 = 0;
  if (!pca9555ReadInputPorts(port0, port1)) return false;
  maskOut = orcPca9555ToRelayMask(port0, port1);
  return true;
}

// Probes for the PCA9555 and, if it ACKs, runs the same output-configuration
// sequence setup() used to run unconditionally. Called once at boot and
// lazily (on demand) from SET/GET/STATUS whenever g_pca9555Present is false
// -- see the "re-probe policy" note below for why lazy-on-demand was chosen
// over a periodic background poll.
static bool probeAndConfigurePca9555() {
  Wire.beginTransmission(kPca9555Addr);
  uint8_t probe = Wire.endTransmission();
  if (probe != 0) {
    g_pca9555Present = false;
    return false;
  }

  // All channels off before switching direction (glitch-avoidance: device
  // resets to all-input/0xFF, so this write only takes effect once
  // direction flips to output below).
  pca9555WriteReg(kRegOutputPort0, 0x00);
  pca9555WriteReg(kRegOutputPort1, 0x00);
  pca9555WriteReg(kRegPolarityPort0, 0x00);
  pca9555WriteReg(kRegPolarityPort1, 0x00);
  pca9555WriteReg(kRegConfigPort0, kOrcPca9555ConfigPort0);
  pca9555WriteReg(kRegConfigPort1, kOrcPca9555ConfigPort1);

  // The writes above go through pca9555WriteReg(), which clears
  // g_pca9555Present on any failure -- if any of them failed, that's
  // already reflected. Only claim success if it's still standing.
  g_pca9555Present = true;
  readAppliedMask(g_lastAppliedMask);  // baseline, avoids a spurious STATE burst on first command
  return true;
}

// SET/GET/STATUS are the only commands that legitimately need the PCA9555.
// Called first thing inside each of their handlers: if hardware was already
// known-present, this is a no-op check; if not, it makes one lazy live
// attempt before giving up -- see "re-probe policy" below.
static bool requireRelayHardware() {
  if (g_pca9555Present) return true;
  if (probeAndConfigurePca9555()) return true;
  Serial.println("ERR NO_RELAY_HARDWARE");
  return false;
}

// Writes the given channel mask to the PCA9555 outputs, then reads back the
// actual applied state and emits an unsolicited "STATE <ch> <ON|OFF>" line
// for every channel that changed -- per the spec, sent regardless of
// whether the change came from a SET command or (here) the idle-timeout
// fail-safe. Command handlers still send their own direct response
// (e.g. "OK SET ...") separately -- these two lines can both appear for one
// SET, which is what the spec's "unsolicited, no command needed" wording
// calls for; host-side parsers are told to treat every line independently.
static void applyMaskAndEmitStateChanges(uint16_t commandedMask) {
  uint8_t port0 = 0, port1 = 0;
  orcRelayMaskToPca9555(commandedMask, port0, port1);
  pca9555WriteReg(kRegOutputPort0, port0);
  pca9555WriteReg(kRegOutputPort1, port1);

  uint16_t appliedMask = commandedMask;
  if (!readAppliedMask(appliedMask)) {
    Serial.println("ERR I2C read-back failed; STATE events not confirmed this cycle.");
    return;
  }
  uint16_t changed = appliedMask ^ g_lastAppliedMask;
  for (uint8_t ch = 1; ch <= 10; ch++) {
    if ((changed >> (ch - 1)) & 0x01) {
      bool on = (appliedMask >> (ch - 1)) & 0x01;
      Serial.printf("STATE %u %s\n", ch, on ? "ON" : "OFF");
    }
  }
  g_lastAppliedMask = appliedMask;
}

static void formatStatus(char out[11]) {
  uint16_t mask = g_lastAppliedMask;
  uint16_t readMask;
  if (readAppliedMask(readMask)) mask = readMask;
  for (uint8_t ch = 1; ch <= 10; ch++) {
    out[ch - 1] = ((mask >> (ch - 1)) & 0x01) ? '1' : '0';
  }
  out[10] = '\0';
}

// --- Command handling ------------------------------------------------------
static void toUpperInPlace(char *s) {
  for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static void handleLine(char *line) {
  // Any received line -- valid or not -- is evidence a host is present and
  // typing; resets the idle-timeout fail-safe. Done before parsing so even
  // a malformed command counts.
  g_lastActivityMillis = millis();

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
    if (!requireRelayHardware()) return;
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
    applyMaskAndEmitStateChanges(newMask);
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
    if (!requireRelayHardware()) return;
    uint16_t mask;
    if (!readAppliedMask(mask)) {
      Serial.println("ERR I2C_READ_FAILED");
      return;
    }
    bool on = (mask >> (ch - 1)) & 0x01;
    Serial.printf("STATE %d %s\n", ch, on ? "ON" : "OFF");

  } else if (strcmp(cmd, "STATUS") == 0) {
    if (!requireRelayHardware()) return;
    char buf[11];
    formatStatus(buf);
    Serial.printf("STATUS %s\n", buf);

  } else {
    Serial.println("ERR UNKNOWN_CMD");
  }
}

static void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      g_lineBuf[g_lineLen] = '\0';
      if (g_lineOverflow) {
        Serial.println("ERR LINE_TOO_LONG");
      } else if (g_lineLen > 0) {
        handleLine(g_lineBuf);
      }
      g_lineLen = 0;
      g_lineOverflow = false;
    } else if (c == '\r') {
      // Tolerated, stripped -- per spec's framing section (a \r\n host is
      // fine, the \n above is what actually terminates the line).
      continue;
    } else {
      if (g_lineLen < kMaxLineLen) {
        g_lineBuf[g_lineLen++] = c;
      } else {
        // Already over budget for this line -- keep consuming silently
        // until the terminating \n, then report ERR LINE_TOO_LONG once.
        g_lineOverflow = true;
      }
    }
  }
}

static void checkIdleTimeout() {
  if (g_lastAppliedMask == 0) return;  // nothing energized, nothing to fail safe from
  if (millis() - g_lastActivityMillis < kIdleTimeoutMs) return;
  Serial.printf("ERR IDLE_TIMEOUT: no host activity in %lu ms, de-energizing all channels.\n",
                (unsigned long)kIdleTimeoutMs);
  applyMaskAndEmitStateChanges(0);
}

void setup() {
  Serial.begin(115200);  // USB CDC ignores the actual baud value; kept for
                          // pyserial-class libraries that require one be passed.
  delay(1500);
  Serial.println();
  Serial.println("=== ORC usb_bench: USB bench/debug interface ===");
  Serial.println("Bench/dev tool -- NOT the production control path. See "
                  "docs/usb-bench-interface-spec.md and docs/features/FR-001.md.");
  // Deliberately NOT printing a "PONG ..." line here -- FR-001's reopen
  // flagged this exact line as misleading on the original real-hardware
  // test: it read like a live response to a PING command, but was really
  // just boot-log text printed before loop() (and therefore the actual
  // command interface) ever started. Send an actual PING if you want a
  // PONG.

  Wire.begin(kI2cSdaPin, kI2cSclPin);
  Wire.setClock(100000);

  // FR-001 REOPENED 2026-08-05: this used to halt the whole sketch here if
  // the PCA9555 didn't ACK -- which meant PING/VERSION/framing, none of
  // which touch relay hardware, were unreachable on a bare bring-up devkit
  // with no PCA9555 attached. That defeated this firmware's own stated
  // purpose (usable *before* full ORC hardware exists). Now: probe once,
  // note the result, and always continue into loop() either way.
  if (probeAndConfigurePca9555()) {
    Serial.println("PCA9555 configured: 10 channels as outputs, all off.");
  } else {
    Serial.printf("WARNING: PCA9555 did not ACK at 0x%02X -- no relay hardware present. "
                  "PING/VERSION/HB and basic framing still work. SET/GET/STATUS will return "
                  "ERR NO_RELAY_HARDWARE until a PCA9555 is detected (re-probed automatically "
                  "on the next SET/GET/STATUS attempt -- no reboot needed if it's hot-plugged "
                  "onto the bus later in this session). Run i2c_scanner to confirm the bus.\n",
                  kPca9555Addr);
  }

  unsigned long now = millis();
  g_lastActivityMillis = now;
  g_lastHeartbeatMillis = now;

  Serial.println("Ready. Commands: PING, VERSION, SET <ch 1-10> ON|OFF, GET <ch>, STATUS.");
}

void loop() {
  pollSerial();
  checkIdleTimeout();

  unsigned long now = millis();
  if (now - g_lastHeartbeatMillis >= kHeartbeatIntervalMs) {
    // uptime_ms via millis() -- 32-bit, wraps ~49.7 days. Acceptable here
    // unlike canopen_app's TPDO2 uptime field: this is a bench tool a human
    // is actively watching, not a long-term unattended health counter: see
    // docs/features/FR-001.md if that assumption ever needs revisiting.
    Serial.printf("HB %lu\n", now);
    g_lastHeartbeatMillis = now;
  }
}
