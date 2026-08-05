// ORC shared helper: CANopen message/COB-ID encode-decode primitives.
//
// This is deliberately NOT a general-purpose CANopen stack -- per
// docs/can-protocol-research.md and .claude/can-application-firmware-prompt.md,
// ORC's message set is small and fully specified (RPDO1, TPDO1, TPDO2,
// Heartbeat, one SDO object). This lib holds the pure encode/decode pieces
// (COB-ID arithmetic, relay-state bit packing, TPDO2 payload packing, the
// one-object SDO expedited-transfer wire format) so firmware/src/canopen_app
// /main.cpp can stay readable; it does not touch the TWAI driver, I2C bus,
// or NVS itself -- those live in main.cpp, which owns the actual peripherals
// and the runtime loop/state.
//
// Cited throughout to docs/can-protocol-research.md's "Arbitration ID (COB-ID)
// allocation" and per-message sections -- read that doc for the why, not
// just the what.

#pragma once

#include <Arduino.h>
#include <string.h>
#include "driver/twai.h"

// --- COB-ID allocation, CiA 301 predefined connection set -----------------
// docs/can-protocol-research.md, "Arbitration ID (COB-ID) allocation" table.
// 11-bit standard CAN IDs, function code (4 bit) << 7 | node ID (7 bit) --
// expressed here as fixed bases; add NodeID (1-127) to get the real COB-ID.
static const uint32_t kOrcCobIdNmt = 0x000;          // host -> all nodes (broadcast), unused this pass
static const uint32_t kOrcCobIdEmcyBase = 0x080;     // ORC -> host, unused this pass (stretch goal per prompt)
static const uint32_t kOrcCobIdTpdo1Base = 0x180;    // ORC -> host, relay status
static const uint32_t kOrcCobIdRpdo1Base = 0x200;    // host -> ORC, relay command
static const uint32_t kOrcCobIdTpdo2Base = 0x280;    // ORC -> host, bus health/uptime
static const uint32_t kOrcCobIdSdoTxBase = 0x580;    // ORC -> host, SDO server response
static const uint32_t kOrcCobIdSdoRxBase = 0x600;    // host -> ORC, SDO client request
static const uint32_t kOrcCobIdHeartbeatBase = 0x700;  // ORC -> host, periodic liveness

inline uint32_t orcCobId(uint32_t base, uint8_t nodeId) { return base + nodeId; }

// --- RPDO1 / TPDO1 -- relay command / status, 2-byte payload --------------
// docs/can-protocol-research.md's RPDO1/TPDO1 sections: byte0 bits0-7 =
// channels 1-8, byte1 bits0-1 = channels 9-10, byte1 bits2-7 reserved (send
// 0). This is the WIRE PROTOCOL's channel numbering only -- it is NOT the
// same as the PCA9555's own register bit layout. An earlier version of this
// file assumed a direct 1:1 match (byte0 == PCA9555 Port0 register, etc.)
// -- that assumption was WRONG, caught 2026-08-05 by reading the real
// schematic (docs/subcircuit-capture-guide.md's "Channel mapping" table,
// read off hardware/i2c_expander.kicad_sch): the real per-channel bit
// mapping is routing-driven, not sequential, and spans both PCA9555 ports
// non-contiguously. The actual channel<->PCA9555-register translation now
// lives in firmware/lib/orc_relay_map/ -- these two functions only handle
// the wire protocol's 2-byte <-> 10-bit-channel-mask conversion; callers
// (canopen_app/main.cpp) compose this with orc_relay_map's
// orcRelayMaskToPca9555()/orcPca9555ToRelayMask() to get real register
// values. Keeping these as two separate translation steps (protocol bytes
// <-> channel mask <-> PCA9555 registers) rather than one combined function
// is deliberate: it's what let this bug be caught and fixed in one place
// (orc_relay_map) without touching the wire-protocol format at all.

// Unpack a received 2-byte RPDO1 (or TPDO1) payload into a 10-bit channel
// mask, bit (n-1) = channel n, 1=energize. Reserved bits in byte1 (2-7) are
// masked off defensively, regardless of what was actually sent/received.
inline uint16_t orcUnpackChannelMask(const uint8_t in[2]) {
  return (uint16_t)in[0] | ((uint16_t)(in[1] & 0x03) << 8);
}

// Pack a 10-bit channel mask into a 2-byte RPDO1/TPDO1 payload.
inline void orcPackChannelMask(uint16_t channelMask, uint8_t out[2]) {
  out[0] = (uint8_t)(channelMask & 0xFF);
  out[1] = (uint8_t)((channelMask >> 8) & 0x03);
}

// --- TPDO2 -- bus health / uptime, 8-byte payload --------------------------
// docs/can-protocol-research.md's TPDO2 section. Byte-0 enum and the
// TX/RX-error-counter byte width were both flagged there as unverified
// design sketches; both were checked against ESP-IDF's actual
// driver/twai.h this pass (see firmware/README.md's canopen_app section for
// the citation) and are implemented here as CONFIRMED, not speculative.

// Byte-0 CAN-controller-health enum. ESP-IDF's twai_state_t (legacy
// driver/twai.h, what Arduino-ESP32 currently wraps) has exactly 4 states --
// STOPPED, RUNNING, BUS_OFF, RECOVERING -- with NO separate error-warning or
// error-passive state of its own. This enum layers the standard CAN
// error-confinement thresholds (ISO 11898-1 / SJA1000-lineage convention:
// warning >=96, passive >=128 on either TX or RX error counter) on top of
// RUNNING, exactly as the original design sketch proposed -- confirmed
// correct against the real driver, not a guess.
enum OrcTwaiHealth : uint8_t {
  ORC_TWAI_HEALTH_STOPPED = 0,
  ORC_TWAI_HEALTH_ACTIVE = 1,          // RUNNING, both error counters < 96
  ORC_TWAI_HEALTH_WARNING = 2,         // RUNNING, either counter in [96, 128)
  ORC_TWAI_HEALTH_PASSIVE = 3,         // RUNNING, either counter >= 128 (driver flips to BUS_OFF before either counter reaches 256)
  ORC_TWAI_HEALTH_BUS_OFF_RECOVERING = 4,  // twai_state_t == BUS_OFF or RECOVERING
};

inline uint8_t orcClassifyTwaiHealth(twai_state_t state, uint32_t txErr, uint32_t rxErr) {
  if (state == TWAI_STATE_BUS_OFF || state == TWAI_STATE_RECOVERING) {
    return ORC_TWAI_HEALTH_BUS_OFF_RECOVERING;
  }
  if (state == TWAI_STATE_STOPPED) {
    return ORC_TWAI_HEALTH_STOPPED;
  }
  // state == TWAI_STATE_RUNNING from here.
  uint32_t worst = (txErr > rxErr) ? txErr : rxErr;
  if (worst >= 128) return ORC_TWAI_HEALTH_PASSIVE;
  if (worst >= 96) return ORC_TWAI_HEALTH_WARNING;
  return ORC_TWAI_HEALTH_ACTIVE;
}

// Pack the full 8-byte TPDO2 payload. `txErr`/`rxErr` come straight from
// twai_status_info_t (declared uint32_t there, but the underlying hardware
// register is 0-255 -- masked to a single byte here defensively rather than
// assumed, per the real driver-struct check this pass). `uptimeSeconds` is
// little-endian per CANopen's standard multi-byte convention.
inline void orcPackTpdo2(twai_state_t state, uint32_t txErr, uint32_t rxErr,
                          uint32_t uptimeSeconds, uint8_t out[8]) {
  out[0] = orcClassifyTwaiHealth(state, txErr, rxErr);
  out[1] = (uint8_t)(txErr & 0xFF);
  out[2] = (uint8_t)(rxErr & 0xFF);
  out[3] = (uint8_t)(uptimeSeconds & 0xFF);
  out[4] = (uint8_t)((uptimeSeconds >> 8) & 0xFF);
  out[5] = (uint8_t)((uptimeSeconds >> 16) & 0xFF);
  out[6] = (uint8_t)((uptimeSeconds >> 24) & 0xFF);
  out[7] = 0;  // reserved
}

// --- Heartbeat --------------------------------------------------------------
// docs/can-protocol-research.md's Heartbeat section, NMT state byte values
// per CANopenNode's reference values (already cited there): 0=Boot-up,
// 4=Stopped, 5=Operational, 127=Pre-operational. ORC implements a fixed
// "Boot-up once, then always Operational" NMT posture -- no
// Stopped/Pre-operational transitions are triggered by anything external in
// this firmware, stated explicitly here per the prompt's instruction not to
// silently skip NMT state handling.
enum OrcNmtState : uint8_t {
  ORC_NMT_BOOTUP = 0,
  ORC_NMT_STOPPED = 4,
  ORC_NMT_OPERATIONAL = 5,
  ORC_NMT_PRE_OPERATIONAL = 127,
};

// --- SDO -- one object, index 1801h sub-index 5 (TPDO2 event timer, ms) ---
// docs/can-protocol-research.md's TPDO2 "Transmission interval" section.
// This is intentionally NOT a general SDO server -- it recognizes exactly
// one (index, sub-index) pair and aborts everything else. Wire format is
// CiA 301's standard SDO expedited-transfer encoding, widely published in
// free/open CANopen primers (e.g. Vector Informatik, Kvaser, Lely
// application notes) independent of the paywalled CiA 301 spec text itself
// -- the bit layout is public, standard, and consistent across every
// independent source checked.
static const uint16_t kOrcSdoObjectIndex = 0x1801;
static const uint8_t kOrcSdoObjectSubIndex = 0x05;

// Standard CiA 301 SDO abort codes (also widely published, not proprietary).
static const uint32_t kOrcSdoAbortObjectDoesNotExist = 0x06020000;
static const uint32_t kOrcSdoAbortSubIndexDoesNotExist = 0x06090011;
static const uint32_t kOrcSdoAbortCommandSpecifierInvalid = 0x05040001;

// Processes one incoming SDO request frame (8-byte CiA 301 expedited
// transfer). `currentValue` is the live event-timer value (ms) before this
// request. On a successful write, sets `newValue` and `valueChanged=true` --
// caller (main.cpp) is responsible for actually applying and persisting it;
// this function only decodes the wire protocol and builds the response
// frame, it has no side effects of its own. Always writes exactly 8 bytes
// to `respData` (padded with 0 where the CiA 301 format doesn't use them)
// and returns true if a response should be sent at all (false only if the
// request frame itself was too short to parse, DLC < 8 -- CiA 301's SDO
// frames are always DLC 8, so a shorter frame is malformed and gets no
// response rather than a guessed one).
inline bool orcHandleSdoRequest(const uint8_t *reqData, uint8_t reqDlc,
                                 uint16_t currentValue,
                                 uint16_t &newValue, bool &valueChanged,
                                 uint8_t respData[8]) {
  valueChanged = false;
  if (reqDlc < 8) return false;

  memset(respData, 0, 8);

  uint16_t reqIndex = (uint16_t)(reqData[1] | (reqData[2] << 8));
  uint8_t reqSubIndex = reqData[3];
  uint8_t ccs = (uint8_t)(reqData[0] >> 5);  // client command specifier, bits 7-5

  if (reqIndex != kOrcSdoObjectIndex) {
    respData[0] = 0x80;
    respData[1] = reqData[1];
    respData[2] = reqData[2];
    respData[3] = reqSubIndex;
    respData[4] = (uint8_t)(kOrcSdoAbortObjectDoesNotExist & 0xFF);
    respData[5] = (uint8_t)((kOrcSdoAbortObjectDoesNotExist >> 8) & 0xFF);
    respData[6] = (uint8_t)((kOrcSdoAbortObjectDoesNotExist >> 16) & 0xFF);
    respData[7] = (uint8_t)((kOrcSdoAbortObjectDoesNotExist >> 24) & 0xFF);
    return true;
  }
  if (reqSubIndex != kOrcSdoObjectSubIndex) {
    respData[0] = 0x80;
    respData[1] = reqData[1];
    respData[2] = reqData[2];
    respData[3] = reqSubIndex;
    respData[4] = (uint8_t)(kOrcSdoAbortSubIndexDoesNotExist & 0xFF);
    respData[5] = (uint8_t)((kOrcSdoAbortSubIndexDoesNotExist >> 8) & 0xFF);
    respData[6] = (uint8_t)((kOrcSdoAbortSubIndexDoesNotExist >> 16) & 0xFF);
    respData[7] = (uint8_t)((kOrcSdoAbortSubIndexDoesNotExist >> 24) & 0xFF);
    return true;
  }

  if (ccs == 1) {
    // Initiate download (write). Read the new value from the low 2 bytes of
    // the data field (bytes 4-5, little-endian) -- this object's real width
    // is fixed at 2 bytes (UNSIGNED16, CiA 301's Communication Parameter
    // Record event-timer sub-index), so the request's own declared
    // size/n/e/s bits aren't separately validated here: a deliberate
    // simplification for a single fixed-width object, not a general SDO
    // download parser.
    newValue = (uint16_t)(reqData[4] | (reqData[5] << 8));
    valueChanged = true;
    respData[0] = 0x60;  // initiate download response, ccs=3
    respData[1] = reqData[1];
    respData[2] = reqData[2];
    respData[3] = reqSubIndex;
    return true;
  }
  if (ccs == 2) {
    // Initiate upload (read).
    respData[0] = 0x4B;  // ccs=2, e=1, s=1, n=2 (4-byte-count field unused for a 2-byte value)
    respData[1] = reqData[1];
    respData[2] = reqData[2];
    respData[3] = reqSubIndex;
    respData[4] = (uint8_t)(currentValue & 0xFF);
    respData[5] = (uint8_t)((currentValue >> 8) & 0xFF);
    return true;
  }

  // Unrecognized command specifier.
  respData[0] = 0x80;
  respData[1] = reqData[1];
  respData[2] = reqData[2];
  respData[3] = reqSubIndex;
  respData[4] = (uint8_t)(kOrcSdoAbortCommandSpecifierInvalid & 0xFF);
  respData[5] = (uint8_t)((kOrcSdoAbortCommandSpecifierInvalid >> 8) & 0xFF);
  respData[6] = (uint8_t)((kOrcSdoAbortCommandSpecifierInvalid >> 16) & 0xFF);
  respData[7] = (uint8_t)((kOrcSdoAbortCommandSpecifierInvalid >> 24) & 0xFF);
  return true;
}
