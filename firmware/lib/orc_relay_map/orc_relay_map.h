// ORC shared helper: relay-channel <-> PCA9555 register-bit mapping.
//
// Canonical source: docs/subcircuit-capture-guide.md, "Channel mapping —
// read directly off hardware/i2c_expander.kicad_sch (U1), 2026-08-04" table
// (lines 239-268 as of that write-up). That table is explicit that the
// mapping is **routing-driven, not a numbering scheme** -- channels are NOT
// sequential bits on Port 0 followed by Port 1 (an earlier, wrong assumption
// that both firmware/src/pca9555_bringup/main.cpp and
// firmware/lib/orc_canopen/ originally carried, corrected here 2026-08-05
// once the real schematic was checked). Spares are the low 3 bits of Port 0
// and the high 3 bits of Port 1 -- not a contiguous block either.
//
// This lib is the SINGLE place this mapping is encoded. Both
// pca9555_bringup (bring-up/verification) and canopen_app (application
// firmware) use it -- don't hand-roll a second copy.

#pragma once

#include <Arduino.h>

// Config register values (NXP/PCA9555 command bytes 0x06/0x07). Bit=1 means
// input, bit=0 means output (device convention, same as pca9555_bringup's
// original comment). Per the schematic table: Port0 bits 0-2 are unused
// spares left as inputs, bits 3-7 are outputs (channels). Port1 bits 0-4 are
// outputs (channels), bits 5-7 are unused spares left as inputs.
static const uint8_t kOrcPca9555ConfigPort0 = 0x07;  // 0b00000111 -- bits0-2 input(spare), bits3-7 output
static const uint8_t kOrcPca9555ConfigPort1 = 0xE0;  // 0b11100000 -- bits0-4 output, bits5-7 input(spare)

struct OrcRelayChannelMap {
  uint8_t port;  // 0 = Port 0, 1 = Port 1
  uint8_t bit;   // bit position within that port's register
};

// Index 0 = channel 1 ... index 9 = channel 10. Values transcribed directly
// from docs/subcircuit-capture-guide.md's table -- channel order (10, 8, 6,
// 4, 2 on Port0 descending; 1, 3, 5, 7, 9 on Port1 ascending) is exactly as
// documented there, not re-derived or assumed regular.
static const OrcRelayChannelMap kOrcRelayChannelMap[10] = {
    /* channel 1  */ {1, 0},
    /* channel 2  */ {0, 7},
    /* channel 3  */ {1, 1},
    /* channel 4  */ {0, 6},
    /* channel 5  */ {1, 2},
    /* channel 6  */ {0, 5},
    /* channel 7  */ {1, 3},
    /* channel 8  */ {0, 4},
    /* channel 9  */ {1, 4},
    /* channel 10 */ {0, 3},
};

// channelMask: bit (n-1) = channel n's commanded/observed state, n=1..10
// (bits 10-15 unused/ignored). Converts to real PCA9555 Output Port0/Port1
// register values per the map above.
inline void orcRelayMaskToPca9555(uint16_t channelMask, uint8_t &port0, uint8_t &port1) {
  port0 = 0;
  port1 = 0;
  for (uint8_t ch = 1; ch <= 10; ch++) {
    bool on = (channelMask >> (ch - 1)) & 0x01;
    if (!on) continue;
    const OrcRelayChannelMap &m = kOrcRelayChannelMap[ch - 1];
    if (m.port == 0) {
      port0 |= (uint8_t)(1u << m.bit);
    } else {
      port1 |= (uint8_t)(1u << m.bit);
    }
  }
}

// Inverse: real PCA9555 Port0/Port1 register values (as read back, e.g. from
// the Input Port registers) -> a channel bitmask, bit (n-1) = channel n.
inline uint16_t orcPca9555ToRelayMask(uint8_t port0, uint8_t port1) {
  uint16_t mask = 0;
  for (uint8_t ch = 1; ch <= 10; ch++) {
    const OrcRelayChannelMap &m = kOrcRelayChannelMap[ch - 1];
    bool on = (m.port == 0) ? ((port0 >> m.bit) & 0x01) : ((port1 >> m.bit) & 0x01);
    if (on) mask |= (uint16_t)(1u << (ch - 1));
  }
  return mask;
}
