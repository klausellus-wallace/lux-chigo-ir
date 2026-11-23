// Differential + unit tests for the pure-C++ protocol layer.
// Reads tests/fixtures.json (two sets):
//   "roundtrip"   — decode -> encode must reproduce the exact 14 bytes
//   "accept_only" — decode must return true and not crash, no reproduction
//
// Plus explicit unit tests for edge cases and encode-only pinned frames.
#include "../components/lux_chigo_ir/protocol.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace esphome::lux_chigo_ir;

static std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    out.push_back(std::stoi(hex.substr(i, 2), nullptr, 16));
  }
  return out;
}

// Naive JSON reader: extract every `"hex": "..."` value from the file.
// fixtures.json is a flat array of {hex, sources} entries.
static std::vector<std::string> read_all_hexes(const std::string& path) {
  std::ifstream f(path);
  if (!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); std::exit(2); }
  std::stringstream buf; buf << f.rdbuf();
  std::string s = buf.str();

  std::vector<std::string> hexes;
  size_t pos = 0;
  const std::string key = "\"hex\":";
  while ((pos = s.find(key, pos)) != std::string::npos) {
    pos = s.find('"', pos + key.size());
    if (pos == std::string::npos) break;
    size_t end = s.find('"', pos + 1);
    if (end == std::string::npos) break;
    hexes.push_back(s.substr(pos + 1, end - pos - 1));
    pos = end + 1;
  }
  return hexes;
}

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void expect_bytes(const uint8_t got[FRAME_BYTES],
                          const uint8_t want[FRAME_BYTES], const char* label) {
  if (std::memcmp(got, want, FRAME_BYTES) != 0) {
    std::fprintf(stderr, "FAIL %s\n  got:  ", label);
    for (int i = 0; i < FRAME_BYTES; i++) std::fprintf(stderr, "%02x ", got[i]);
    std::fprintf(stderr, "\n  want: ");
    for (int i = 0; i < FRAME_BYTES; i++) std::fprintf(stderr, "%02x ", want[i]);
    std::fprintf(stderr, "\n");
    failures++;
  }
}

static void test_bit_reverse_involution() {
  // bit_reverse(bit_reverse(x)) == x for all bytes
  for (int x = 0; x <= 255; x++) {
    uint8_t r = bit_reverse(static_cast<uint8_t>(x));
    uint8_t rr = bit_reverse(r);
    CHECK(rr == static_cast<uint8_t>(x));
  }
  // Spot checks against docs/protocol.md § Overview values
  CHECK(bit_reverse(0x00) == 0x00);
  CHECK(bit_reverse(0xff) == 0xff);
  CHECK(bit_reverse(0x80) == 0x01);
  CHECK(bit_reverse(0x1b) == 0xd8);
  CHECK(bit_reverse(0xd8) == 0x1b);
}

static void test_checksum_edges() {
  uint8_t all_zero[PAYLOAD_BYTES] = {0};
  CHECK(checksum(all_zero) == 0x00);
  uint8_t all_ff[PAYLOAD_BYTES];
  std::memset(all_ff, 0xff, PAYLOAD_BYTES);
  // sum of 13 * 0xff (bit-reversed = 0xff) = 13 * 255 = 3315, mod 256 = 243 = 0xf3
  // bit_reverse(0xf3) = bit_reverse(0b11110011) = 0b11001111 = 0xcf
  CHECK(checksum(all_ff) == 0xcf);
  uint8_t single_bit[PAYLOAD_BYTES] = {0};
  single_bit[0] = 0x01;  // wire bit 0 (LSB in LSB-first) -> reversed value 0x80
  // sum = 0x80, bit_reverse = 0x01
  CHECK(checksum(single_bit) == 0x01);
}

// Frame A literals are the two variants observed in tests/captures/capture2.log.
static const uint8_t CAPTURED_A_MUTE_OFF[FRAME_BYTES] = {
    0xc4,0xd3,0x64,0x40,0x00,0x02,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0xa7};
static const uint8_t CAPTURED_A_MUTE_ON[FRAME_BYTES] = {
    0xc4,0xd3,0x64,0x40,0x00,0x06,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0xa0};

static void test_frame_a_encode() {
  uint8_t got[FRAME_BYTES] = {0};
  encode_frame_a(false, got);
  expect_bytes(got, CAPTURED_A_MUTE_OFF, "Frame A mute off");
  encode_frame_a(true, got);
  expect_bytes(got, CAPTURED_A_MUTE_ON, "Frame A mute on");
}

static void test_frame_a_decode() {
  bool mute = true;
  CHECK(decode_frame_a(CAPTURED_A_MUTE_OFF, mute));
  CHECK(mute == false);
  CHECK(decode_frame_a(CAPTURED_A_MUTE_ON, mute));
  CHECK(mute == true);
}

static void test_frame_a_decode_rejects_invalid() {
  bool mute = true;
  // MUTE bit flipped without its checksum: a single-bit RX error.
  const uint8_t bad_checksum[FRAME_BYTES] = {
      0xc4,0xd3,0x64,0x40,0x00,0x06,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0xa7};
  CHECK(!decode_frame_a(bad_checksum, mute));
  // Corrupted header byte seen once in capture3.log.
  const uint8_t bad_header[FRAME_BYTES] = {
      0xc6,0xd3,0x64,0x40,0x00,0x02,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0xa7};
  CHECK(!decode_frame_a(bad_header, mute));
  // Checksum is valid, but byte 5 has a bit never observed in Frame A.
  const uint8_t unknown_b5[FRAME_BYTES] = {
      0xc4,0xd3,0x64,0x40,0x00,0x03,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0xa6};
  CHECK(!decode_frame_a(unknown_b5, mute));
  // A Frame B is not a Frame A.
  const uint8_t frame_b[FRAME_BYTES] = {
      0xc4,0xd3,0x64,0x80,0x00,0x24,0xc0,0xe0,0x00,0x00,0x00,0x00,0x01,0xc3};
  CHECK(!decode_frame_a(frame_b, mute));
  CHECK(mute == true);   // rejected frames must not touch the output
}

static void test_display_bit() {
  // Captured 0x24/0x26 pair. DISPLAY as the meaning of bit 0x02 is unconfirmed.
  const uint8_t display_on[FRAME_BYTES] = {
      0xc4,0xd3,0x64,0x80,0x00,0x24,0xc0,0xe0,0x00,0x00,0x00,0x00,0x01,0xc3};
  const uint8_t display_off[FRAME_BYTES] = {
      0xc4,0xd3,0x64,0x80,0x00,0x26,0xc0,0xe0,0x00,0x00,0x00,0x00,0x01,0xc0};
  uint8_t got[FRAME_BYTES];

  LuxState s{};
  CHECK(decode(display_off, s));
  CHECK(s.display_on == false);
  CHECK(s.eco == false);
  CHECK(encode(s, got));
  expect_bytes(got, display_off, "display off roundtrip");

  CHECK(decode(display_on, s));
  CHECK(s.display_on == true);
  CHECK(encode(s, got));
  expect_bytes(got, display_on, "display on roundtrip");

  // ECO (0x01) and display-off (0x02) are independent bits.
  const uint8_t eco_display_off[FRAME_BYTES] = {
      0xc4,0xd3,0x64,0x80,0x00,0x27,0xc0,0xe0,0x00,0x00,0x00,0x00,0x01,0xc1};
  CHECK(decode(eco_display_off, s));
  CHECK(s.eco == true);
  CHECK(s.display_on == false);
  CHECK(encode(s, got));
  expect_bytes(got, eco_display_off, "ECO + display off roundtrip");

  // Power-off frames stay 0x04: the only off value ever captured.
  LuxState off{};
  off.power_on = false;
  off.display_on = false;
  CHECK(encode(off, got));
  CHECK(got[5] == 0x04);
}

static void test_encode_pinned() {
  // Pin known-good encoded frames for states not necessarily in the fixture set.

  // Baseline: COOL, 24°C, fan AUTO, no aux, power on.
  {
    LuxState s{};
    s.mode = Mode::COOL; s.temp_c = 24; s.fan = Fan::AUTO;
    uint8_t got[FRAME_BYTES];
    CHECK(encode(s, got));
    const uint8_t want[FRAME_BYTES] = {
        0xc4,0xd3,0x64,0x80,0x00,0x24,0xc0,0xe0,0x00,0x00,0x00,0x00,0x01,0xc3};
    expect_bytes(got, want, "baseline COOL 24");
  }
  // Power-off frame — must byte-match capture.log's observed off-frame.
  {
    LuxState s{};
    s.power_on = false;
    s.mode = Mode::COOL; s.temp_c = 24; s.fan = Fan::AUTO;
    uint8_t got[FRAME_BYTES];
    CHECK(encode(s, got));
    const uint8_t want[FRAME_BYTES] = {
        0xc4,0xd3,0x64,0x80,0x00,0x04,0xc0,0xe0,0x00,0x00,0x00,0x00,0x01,0xfd};
    expect_bytes(got, want, "power-off COOL 24");
  }
  // TURBO forces temp=16 and fan=HIGH regardless of user request.
  {
    LuxState s{};
    s.mode = Mode::COOL; s.temp_c = 26; s.fan = Fan::LOW;
    s.turbo = true;
    uint8_t got[FRAME_BYTES];
    CHECK(encode(s, got));
    CHECK(got[6] == 0xc2);   // COOL + TURBO
    CHECK(got[7] == 0xf0);   // 16°C
    CHECK(got[8] == 0xa0);   // HIGH fan
  }
  // I-FEEL with sensor temp 27°C: byte 11 = bit_reverse(27) = 0xd8.
  {
    LuxState s{};
    s.mode = Mode::COOL; s.temp_c = 24; s.fan = Fan::AUTO;
    s.ifeel = true; s.ifeel_temp_c = 27;
    uint8_t got[FRAME_BYTES];
    CHECK(encode(s, got));
    CHECK(got[6] == 0xc1);   // COOL + I-FEEL
    CHECK(got[11] == 0xd8);
  }
  // Swing-H + HEAT (HEAT/AUTO base low-nibble = 0x00 per docs/protocol.md § Byte 12 — swing-H + mode base)
  {
    LuxState s{};
    s.mode = Mode::HEAT; s.temp_c = 22; s.fan = Fan::MED;
    s.swing_h = true;
    uint8_t got[FRAME_BYTES];
    CHECK(encode(s, got));
    CHECK((got[6] & 0xf0) == 0x80);
    CHECK(got[12] == 0x10);   // 0x00 (HEAT base) | 0x10 (swing_h)
  }
  // Encode-only priority pinning (never in fixtures):
  //   sleep=true + swing_v=true + fan=MED -> byte 8 = 0x80 (sleep wins).
  {
    LuxState s{};
    s.mode = Mode::COOL; s.temp_c = 24; s.fan = Fan::MED;
    s.sleep = true; s.swing_v = true;
    uint8_t got[FRAME_BYTES];
    CHECK(encode(s, got));
    CHECK(got[8] == 0x80);
  }
  // Byte 8 bit-field: fan LOW + swing-V simultaneously → 0x5c
  // (Live-hardware observation 2026-07-23.)
  {
    LuxState s{};
    s.mode = Mode::COOL; s.temp_c = 24; s.fan = Fan::LOW;
    s.swing_v = true;
    uint8_t got[FRAME_BYTES];
    CHECK(encode(s, got));
    CHECK(got[8] == 0x5c);
    // And the roundtrip: decode(encode(s)) preserves both fields.
    LuxState back{};
    CHECK(decode(got, back));
    CHECK(back.fan == Fan::LOW);
    CHECK(back.swing_v == true);
    CHECK(back.sleep == false);
  }
  // fan HIGH + swing-V → 0xa0 | 0x1c = 0xbc
  {
    LuxState s{};
    s.mode = Mode::COOL; s.temp_c = 24; s.fan = Fan::HIGH;
    s.swing_v = true;
    uint8_t got[FRAME_BYTES];
    CHECK(encode(s, got));
    CHECK(got[8] == 0xbc);
  }
  // Bare swing-V with fan AUTO.
  {
    LuxState s{};
    s.mode = Mode::COOL; s.temp_c = 24; s.fan = Fan::AUTO;
    s.swing_v = true;
    uint8_t got[FRAME_BYTES];
    CHECK(encode(s, got));
    CHECK(got[8] == 0x1c);
  }
}

static void test_encode_reject_out_of_range() {
  uint8_t buf[FRAME_BYTES];
  LuxState s{};
  s.mode = Mode::COOL; s.fan = Fan::AUTO;
  s.temp_c = 15;
  CHECK(!encode(s, buf));
  s.temp_c = 32;
  CHECK(!encode(s, buf));
  s.temp_c = 24;
  CHECK(encode(s, buf));   // sanity
}

static void test_decode_reject_bad_checksum() {
  uint8_t frame[FRAME_BYTES] = {
      0xc4,0xd3,0x64,0x80,0x00,0x24,0xc0,0xe0,0x00,0x00,0x00,0x00,0x01,0x00};
  LuxState s{};
  CHECK(!decode(frame, s));   // wrong checksum
  frame[13] = 0xc3;
  CHECK(decode(frame, s));    // right checksum
}

static void test_decode_reject_bad_marker() {
  uint8_t frame[FRAME_BYTES] = {
      0xc4,0xd3,0x64,0x40,0x00,0x02,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0xa7};
  LuxState s{};
  CHECK(!decode(frame, s));   // Frame A preamble, not a command
}

int main(int argc, char** argv) {
  std::string fx = "tests/fixtures.json";
  if (argc > 1) fx = argv[1];

  test_bit_reverse_involution();
  test_checksum_edges();
  test_encode_pinned();
  test_encode_reject_out_of_range();
  test_decode_reject_bad_checksum();
  test_decode_reject_bad_marker();
  test_frame_a_encode();
  test_frame_a_decode();
  test_frame_a_decode_rejects_invalid();
  test_display_bit();

  // fixtures.json is a flat array of {hex, sources}; every fixture must
  // decode → re-encode byte-exactly.
  auto hexes = read_all_hexes(fx);
  std::fprintf(stderr, "fixtures: %zu\n", hexes.size());
  CHECK(hexes.size() >= 15);

  int ok = 0;
  for (const auto& h : hexes) {
    auto bytes = hex_to_bytes(h);
    CHECK(bytes.size() == FRAME_BYTES);
    if (bytes.size() != FRAME_BYTES) continue;
    CHECK(checksum(bytes.data()) == bytes[13]);
    LuxState s{};
    if (!decode(bytes.data(), s)) { std::fprintf(stderr, "decode failed: %s\n", h.c_str()); failures++; continue; }
    uint8_t out[FRAME_BYTES];
    if (!encode(s, out)) { std::fprintf(stderr, "encode failed: %s\n", h.c_str()); failures++; continue; }
    if (std::memcmp(out, bytes.data(), FRAME_BYTES) != 0) {
      std::fprintf(stderr, "roundtrip mismatch for %s\n  got:  ", h.c_str());
      for (int i = 0; i < FRAME_BYTES; i++) std::fprintf(stderr, "%02x ", out[i]);
      std::fprintf(stderr, "\n");
      failures++;
      continue;
    }
    ok++;
  }

  std::fprintf(stderr, "roundtrip=%d/%zu failures=%d\n", ok, hexes.size(), failures);
  return failures == 0 ? 0 : 1;
}
