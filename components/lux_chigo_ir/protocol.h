#pragma once
#include <cstdint>

namespace esphome {
namespace lux_chigo_ir {

// Wire timings, µs (means across 39 sample frames)
constexpr uint32_t HEADER_MARK_US      = 3145;
constexpr uint32_t HEADER_SPACE_US     = 1560;
constexpr uint32_t BIT_MARK_US         = 243;
constexpr uint32_t BIT_SPACE_ZERO_US   = 243;
constexpr uint32_t BIT_SPACE_ONE_US    = 1063;
constexpr uint32_t FRAME_GAP_US        = 200000;   // 200 ms A → B
constexpr uint32_t CARRIER_HZ          = 38000;

// Frame structure
constexpr int FRAME_BYTES = 14;
constexpr int PAYLOAD_BYTES = 13;   // bytes 0-12; byte 13 is the checksum

// Frame A: fixed preamble (see docs/protocol.md § Frame A (preamble)). Sent verbatim before every Frame B.
extern const uint8_t FRAME_A[FRAME_BYTES];

// Semantic enums — see docs/protocol.md § Byte 6 — mode + feature flags through § Byte 12 — swing-H + mode base
enum class Mode : uint8_t {
  COOL = 0xc0,
  HEAT = 0x80,
  DRY  = 0x40,
  FAN  = 0xe0,
  AUTO = 0x10,
};

enum class Fan : uint8_t {
  AUTO = 0x00,
  LOW  = 0x40,
  MED  = 0xc0,
  HIGH = 0xa0,
};

// State that fully describes a Frame B (except checksum).
struct LuxState {
  bool  power_on        = true;
  bool  eco             = false;
  Mode  mode            = Mode::COOL;
  bool  turbo           = false;
  bool  ifeel           = false;
  bool  health          = false;
  uint8_t temp_c        = 24;      // 16..31
  Fan   fan             = Fan::AUTO;
  bool  swing_v         = false;
  bool  swing_h         = false;
  bool  sleep           = false;
  uint8_t ifeel_temp_c  = 0;       // only used when ifeel=true; 0..127
  // Frame B byte 5 bit 0x04. Set in every captured frame (the original remote
  // always beeps). Cleared = the AC applies the command silently, confirmed
  // live 2026-11-24. Default true keeps encode/decode roundtrips exact.
  bool  beep            = true;
};

// Byte-level utilities
uint8_t bit_reverse(uint8_t b);
uint8_t checksum(const uint8_t payload[PAYLOAD_BYTES]);

// Encode LuxState into 14 wire bytes. Always fills out[13] with correct checksum.
// Returns false if state is inconsistent (e.g. temp_c out of 16..31 range).
bool encode(const LuxState& s, uint8_t out[FRAME_BYTES]);

// Decode 14 wire bytes into LuxState. Validates checksum and byte3==0x80
// (command frame, not preamble). Returns false on any validation failure.
bool decode(const uint8_t in[FRAME_BYTES], LuxState& out);

}  // namespace lux_chigo_ir
}  // namespace esphome
