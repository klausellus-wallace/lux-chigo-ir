#include "protocol.h"

namespace esphome {
namespace lux_chigo_ir {

// Frame A adds this offset to the logical (bit-reversed) sum before the
// final bit reversal. Frame B uses no offset.
static constexpr uint8_t FRAME_A_CHECKSUM_OFFSET = 0x0f;

// See docs/protocol.md § Byte 7 — temperature (upper nibble) for full temperature table (temp 16..31 → nibble)
static constexpr uint8_t TEMP_NIBBLE_TABLE[16] = {
    0xf, 0x7, 0xb, 0x3,   // 16..19
    0xd, 0x5, 0x9, 0x1,   // 20..23
    0xe, 0x6, 0xa, 0x2,   // 24..27
    0xc, 0x4, 0x8, 0x0,   // 28..31
};

static uint8_t temp_nibble(uint8_t temp_c) {
  if (temp_c < 16 || temp_c > 31) return 0;
  return TEMP_NIBBLE_TABLE[temp_c - 16];
}

static uint8_t nibble_to_temp(uint8_t nibble) {
  for (uint8_t t = 16; t <= 31; t++) {
    if (TEMP_NIBBLE_TABLE[t - 16] == nibble) return t;
  }
  return 0;
}

uint8_t bit_reverse(uint8_t b) {
  uint8_t r = 0;
  for (int i = 0; i < 8; i++) {
    if (b & (1 << i)) r |= (1 << (7 - i));
  }
  return r;
}

uint8_t checksum(const uint8_t payload[PAYLOAD_BYTES]) {
  uint16_t sum = 0;
  for (int i = 0; i < PAYLOAD_BYTES; i++) {
    sum += bit_reverse(payload[i]);
  }
  return bit_reverse(static_cast<uint8_t>(sum & 0xff));
}

bool encode(const LuxState& s, uint8_t out[FRAME_BYTES]) {
  if (s.temp_c < 16 || s.temp_c > 31) return false;
  if (s.ifeel && s.ifeel_temp_c > 127) return false;

  // Bytes 0-3: fixed header for Frame B
  out[0] = 0xc4; out[1] = 0xd3; out[2] = 0x64; out[3] = 0x80;

  // Byte 4: always 0 for non-timer frames
  out[4] = 0x00;

  // Byte 5: power + eco + display-off + fixed bit 0x04. ECO and display-off
  // are only emitted when the unit is on, because 0x04 is the only power-off
  // value ever captured.
  uint8_t b5 = 0x04;
  if (s.power_on) {
    b5 |= 0x20;
    // Live-hardware observation (2026-07-23): the physical remote's ECO
    // press sends bit 0x01 in b5, not 0x02 as the reverse-engineering
    // session had labeled. Emit 0x01 so HA→AC ECO commands match what
    // the AC expects.
    if (s.eco) b5 |= 0x01;
    if (!s.display_on) b5 |= 0x02;
  }
  out[5] = b5;

  // Byte 6: mode nibble | health/turbo/ifeel bits
  uint8_t b6 = static_cast<uint8_t>(s.mode);
  if (s.health) b6 |= 0x08;
  if (s.turbo)  b6 |= 0x02;
  if (s.ifeel)  b6 |= 0x01;
  out[6] = b6;

  // Byte 7: temperature (turbo forces 16°C)
  uint8_t effective_temp = s.turbo ? 16 : s.temp_c;
  out[7] = temp_nibble(effective_temp) << 4;

  // Byte 8: bit-field. Fan speed uses top bits (0xe0 mask), swing-V uses
  // bits 2-4 (0x1c mask), sleep uses bit 7 alone (0x80 as an exclusive
  // sentinel). Fan and swing_v can be ORed together (observed live: b8=0x5c
  // = fan LOW + swing-V). Sleep remains exclusive since 0x80 collides with
  // fan MED (0xc0) after OR.
  uint8_t b8;
  if (s.sleep) {
    b8 = 0x80;
  } else {
    // Fan bits (turbo and FAN mode force HIGH regardless of s.fan)
    uint8_t fan_bits;
    if (s.turbo || s.mode == Mode::FAN) {
      fan_bits = 0xa0;
    } else {
      switch (s.fan) {
        case Fan::AUTO: fan_bits = 0x00; break;
        case Fan::LOW:  fan_bits = 0x40; break;
        case Fan::MED:  fan_bits = 0xc0; break;
        case Fan::HIGH: fan_bits = 0xa0; break;
        default:        fan_bits = 0x00; break;
      }
    }
    b8 = fan_bits | (s.swing_v ? 0x1c : 0x00);
  }
  out[8] = b8;

  // Bytes 9, 10: fixed 0x00 for non-timer frames
  out[9]  = 0x00;
  out[10] = 0x00;

  // Byte 11: I-FEEL sensor value, LSB-first (bit_reverse of °C)
  out[11] = s.ifeel ? bit_reverse(s.ifeel_temp_c) : 0x00;

  // Byte 12: mode base low-nibble | swing_h high-nibble
  uint8_t mode_base = (s.mode == Mode::HEAT || s.mode == Mode::AUTO) ? 0x00 : 0x01;
  out[12] = mode_base | (s.swing_h ? 0x10 : 0x00);

  // Byte 13: checksum
  out[13] = checksum(out);
  return true;
}

bool decode(const uint8_t in[FRAME_BYTES], LuxState& out) {
  // Validate fixed prefix + command marker
  if (in[0] != 0xc4 || in[1] != 0xd3 || in[2] != 0x64) return false;
  if (in[3] != 0x80) return false;   // 0x40 = preamble Frame A, we skip it
  if (in[4] != 0x00) return false;   // b4 != 0 => timer frame, not supported
  if (checksum(in) != in[13]) return false;
  // b5 accepted iff constant bit 0x04 is set (all observed on-state frames)
  // OR pure power-off 0x04. Reject timer-adjacent b5=0x36 (bit 0x10).
  if (in[5] != 0x04 && (in[5] & 0x24) != 0x24) return false;
  if (in[5] & 0x10) return false;

  LuxState s{};
  s.power_on = (in[5] & 0x20) != 0;
  s.eco      = (in[5] & 0x01) != 0;   // ECO bit — matches encoder
  s.display_on = (in[5] & 0x02) == 0;  // inverted: set = display off

  // Recover mode from top 4 bits
  uint8_t mode_bits = in[6] & 0xf0;
  switch (mode_bits) {
    case 0xc0: s.mode = Mode::COOL; break;
    case 0x80: s.mode = Mode::HEAT; break;
    case 0x40: s.mode = Mode::DRY;  break;
    case 0xe0: s.mode = Mode::FAN;  break;
    case 0x10: s.mode = Mode::AUTO; break;
    default:   return false;
  }
  s.health = (in[6] & 0x08) != 0;
  s.turbo  = (in[6] & 0x02) != 0;
  s.ifeel  = (in[6] & 0x01) != 0;

  uint8_t temp = nibble_to_temp(in[7] >> 4);
  if (temp == 0) return false;
  s.temp_c = temp;

  // Byte 8 is a bit-field: fan speed in top bits (mask 0xe0), swing-V in
  // bits 2-4 (mask 0x1c, all three bits set together = swing-V active),
  // sleep uses 0x80 as an exclusive sentinel. Observed live: 0x5c = fan
  // LOW (0x40) OR swing-V (0x1c). Unknown top-nibble → fall back to AUTO
  // so power/mode/temp still propagate on marginal captures.
  if (in[8] == 0x80) {
    s.sleep = true;
    s.fan = Fan::AUTO;
  } else {
    switch (in[8] & 0xe0) {
      case 0x00: s.fan = Fan::AUTO; break;
      case 0x40: s.fan = Fan::LOW;  break;
      case 0xc0: s.fan = Fan::MED;  break;
      case 0xa0: s.fan = Fan::HIGH; break;
      default:   s.fan = Fan::AUTO; break;
    }
    if ((in[8] & 0x1c) == 0x1c) s.swing_v = true;
  }

  // Bytes 9, 10: strict zero for supported frames. Timer frames have b9!=0
  // and are rejected here (docs/protocol.md § Byte 4 — timer flag; timer explicitly out of scope).
  if (in[9] != 0x00 || in[10] != 0x00) return false;

  if (s.ifeel) {
    s.ifeel_temp_c = bit_reverse(in[11]);
  }

  s.swing_h = (in[12] & 0x10) != 0;
  // Byte 12 low-nibble is mode-dependent; already implicit via s.mode. No check.

  out = s;
  return true;
}

static uint8_t frame_a_checksum(const uint8_t payload[PAYLOAD_BYTES]) {
  uint8_t sum = bit_reverse(checksum(payload));
  return bit_reverse(static_cast<uint8_t>(sum + FRAME_A_CHECKSUM_OFFSET));
}

void encode_frame_a(bool mute, uint8_t out[FRAME_BYTES]) {
  for (int i = 0; i < FRAME_BYTES; i++) out[i] = 0x00;
  out[0] = 0xc4; out[1] = 0xd3; out[2] = 0x64; out[3] = 0x40;
  out[5] = mute ? 0x06 : 0x02;
  out[8] = 0x01;   // Purpose unknown; present in every captured Frame A.
  out[13] = frame_a_checksum(out);
}

bool decode_frame_a(const uint8_t in[FRAME_BYTES], bool& mute) {
  if (in[0] != 0xc4 || in[1] != 0xd3 || in[2] != 0x64) return false;
  if (in[3] != 0x40) return false;
  if (frame_a_checksum(in) != in[13]) return false;
  if (in[5] != 0x02 && in[5] != 0x06) return false;
  mute = (in[5] & 0x04) != 0;
  return true;
}

}  // namespace lux_chigo_ir
}  // namespace esphome
