#include "lux_chigo_ir.h"
#include "esphome/core/hal.h"    // for millis()
#include "esphome/core/log.h"
#include <cmath>

namespace esphome::lux_chigo_ir {

static const char *const TAG = "lux_chigo_ir";
constexpr uint32_t RX_LOOP_GUARD_MS = 500;

// Base-class ctor initialises min/max/step, supports_dry, supports_fan_only,
// and the fan/swing/preset masks. Modes cool/heat/off/heat_cool come for free.
// Note: HEAT_COOL is added automatically by climate_ir when both cool and heat
// are supported; no need to add it to a mask.
LuxChigoIRClimate::LuxChigoIRClimate()
    : climate_ir::ClimateIR(
          /*min_temp=*/16.0f, /*max_temp=*/31.0f, /*step=*/1.0f,
          /*supports_dry=*/true, /*supports_fan_only=*/true,
          /*fan_modes=*/{climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
                         climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH},
          /*swing_modes=*/{climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_HORIZONTAL,
                           climate::CLIMATE_SWING_VERTICAL, climate::CLIMATE_SWING_BOTH},
          /*presets=*/{climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_ECO,
                       climate::CLIMATE_PRESET_BOOST, climate::CLIMATE_PRESET_SLEEP}) {}

static Mode ha_mode_to_lux(climate::ClimateMode m) {
  switch (m) {
    case climate::CLIMATE_MODE_HEAT:      return Mode::HEAT;
    case climate::CLIMATE_MODE_DRY:       return Mode::DRY;
    case climate::CLIMATE_MODE_FAN_ONLY:  return Mode::FAN;
    case climate::CLIMATE_MODE_HEAT_COOL: return Mode::AUTO;
    // OFF still uses COOL as underlying frame mode; power bit clears in encode()
    case climate::CLIMATE_MODE_COOL:
    case climate::CLIMATE_MODE_OFF:
    default:                              return Mode::COOL;
  }
}

static climate::ClimateMode lux_mode_to_ha(Mode m) {
  switch (m) {
    case Mode::HEAT: return climate::CLIMATE_MODE_HEAT;
    case Mode::DRY:  return climate::CLIMATE_MODE_DRY;
    case Mode::FAN:  return climate::CLIMATE_MODE_FAN_ONLY;
    case Mode::AUTO: return climate::CLIMATE_MODE_HEAT_COOL;
    case Mode::COOL:
    default:                         return climate::CLIMATE_MODE_COOL;
  }
}

static Fan ha_fan_to_lux(climate::ClimateFanMode f) {
  switch (f) {
    case climate::CLIMATE_FAN_LOW:    return Fan::LOW;
    case climate::CLIMATE_FAN_MEDIUM: return Fan::MED;
    case climate::CLIMATE_FAN_HIGH:   return Fan::HIGH;
    case climate::CLIMATE_FAN_AUTO:
    default:                          return Fan::AUTO;
  }
}

static climate::ClimateFanMode lux_fan_to_ha(Fan f) {
  switch (f) {
    case Fan::LOW:  return climate::CLIMATE_FAN_LOW;
    case Fan::MED:  return climate::CLIMATE_FAN_MEDIUM;
    case Fan::HIGH: return climate::CLIMATE_FAN_HIGH;
    case Fan::AUTO:
    default:                        return climate::CLIMATE_FAN_AUTO;
  }
}

LuxState LuxChigoIRClimate::gather_state_() const {
  LuxState s{};
  s.power_on = (this->mode != climate::CLIMATE_MODE_OFF);
  s.mode     = ha_mode_to_lux(this->mode);
  s.temp_c   = static_cast<uint8_t>(this->target_temperature);
  s.fan      = ha_fan_to_lux(this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO));

  auto swing = this->swing_mode;
  s.swing_h = (swing == climate::CLIMATE_SWING_HORIZONTAL || swing == climate::CLIMATE_SWING_BOTH);
  s.swing_v = (swing == climate::CLIMATE_SWING_VERTICAL   || swing == climate::CLIMATE_SWING_BOTH);

  auto preset = this->preset.value_or(climate::CLIMATE_PRESET_NONE);
  s.eco   = (preset == climate::CLIMATE_PRESET_ECO);
  s.turbo = (preset == climate::CLIMATE_PRESET_BOOST);
  s.sleep = (preset == climate::CLIMATE_PRESET_SLEEP);

  s.health = this->health_;

  // I-FEEL: use base-class sensor_ if enabled, configured, valid, and fresh.
  if (this->ifeel_enabled_ && this->sensor_ != nullptr && !std::isnan(this->sensor_->state)) {
    bool fresh = true;
    if (this->ifeel_max_age_ms_ > 0) {
      // ESPHome Sensor doesn't track last-update timestamp directly. As a
      // pragmatic staleness proxy, we require the value to be within a
      // plausible range (-20..80°C) — a stuck-at-NaN or wild value is dropped.
      // TODO(docs/verification.md § Live-test verification (post-Task 10) follow-up): if firmware needs real staleness, wire
      // up a timer that resets on sensor state change.
      float t = this->sensor_->state;
      fresh = (t > -20.0f && t < 80.0f);
    }
    if (fresh) {
      s.ifeel = true;
      float temp = this->sensor_->state;
      if (temp < 0.0f)   temp = 0.0f;
      if (temp > 127.0f) temp = 127.0f;
      s.ifeel_temp_c = static_cast<uint8_t>(temp + 0.5f);
    }
  }
  return s;
}

void LuxChigoIRClimate::set_health(bool on) {
  if (this->health_ == on) return;
  this->health_ = on;
  this->transmit_state();
}

void LuxChigoIRClimate::send_frames_(const uint8_t frame_b[FRAME_BYTES]) {
  auto call = this->transmitter_->transmit();
  auto *data = call.get_data();
  data->set_carrier_frequency(CARRIER_HZ);

  auto emit_frame = [&](const uint8_t frame[FRAME_BYTES]) {
    data->mark(HEADER_MARK_US);
    data->space(HEADER_SPACE_US);
    for (int i = 0; i < FRAME_BYTES; i++) {
      uint8_t b = frame[i];
      // Wire order: MSB first (verified by capture parser).
      for (int bit = 7; bit >= 0; bit--) {
        data->mark(BIT_MARK_US);
        bool one = (b & (1 << bit)) != 0;
        data->space(one ? BIT_SPACE_ONE_US
                        : BIT_SPACE_ZERO_US);
      }
    }
    data->mark(BIT_MARK_US);   // trailing mark
  };

  emit_frame(FRAME_A);
  data->space(FRAME_GAP_US);
  emit_frame(frame_b);

  call.set_send_wait(0);
  call.set_send_times(1);
  call.perform();

  // Set AFTER perform() returns so the guard covers the actual radiation window.
  this->last_transmit_ms_ = millis();
}

void LuxChigoIRClimate::control(const climate::ClimateCall &call) {
  // Let the base apply everything in the incoming call (mode, temp, fan,
  // swing, preset), transmit, and publish.
  climate_ir::ClimateIR::control(call);

  // Turbo mirrors the physical remote's post-press UX: target 16 °C, fan
  // Auto. The wire encoding still forces byte 8 to HIGH under turbo — this
  // is only what HA sees. If nothing effectively changed, skip the extra
  // transmit/publish to avoid a redundant IR blast on repeat Turbo presses.
  if (this->preset == climate::CLIMATE_PRESET_BOOST) {
    bool changed = false;
    if (this->target_temperature != 16.0f) {
      this->target_temperature = 16.0f;
      changed = true;
    }
    if (this->fan_mode != climate::CLIMATE_FAN_AUTO) {
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
      changed = true;
    }
    if (changed) {
      this->transmit_state();
      this->publish_state();
    }
  }
}

void LuxChigoIRClimate::transmit_state() {
  LuxState s = this->gather_state_();
  uint8_t frame_b[FRAME_BYTES];
  if (!encode(s, frame_b)) {
    ESP_LOGE(TAG, "encode() rejected state (temp out of range?)");
    return;
  }
  this->log_state_(s, "TX");
  ESP_LOGD(TAG,
           "TX frame: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
           frame_b[0], frame_b[1], frame_b[2], frame_b[3], frame_b[4], frame_b[5], frame_b[6],
           frame_b[7], frame_b[8], frame_b[9], frame_b[10], frame_b[11], frame_b[12], frame_b[13]);
  this->send_frames_(frame_b);
}

bool LuxChigoIRClimate::on_receive(remote_base::RemoteReceiveData data) {
  // Anti-loop: within guard window after our own TX, drop everything.
  if (this->last_transmit_ms_ != 0 &&
      millis() - this->last_transmit_ms_ < RX_LOOP_GUARD_MS) {
    return false;
  }

  // Expected shape: 2 header + 112 bit pairs (mark, space) + optional trailing
  // mark = 226 (or 227) timings. ESPHome stores raw µs as positive marks and
  // negative spaces in a linear buffer.
  const int32_t size = data.size();
  if (size < 226) return false;

  int32_t hdr_mark  = data[0];
  int32_t hdr_space = data[1];
  if (hdr_mark <= 0 || hdr_space >= 0) return false;
  if (hdr_mark < 2000 || hdr_mark > 5000) return false;
  int32_t space_abs = -hdr_space;
  if (space_abs < 1000 || space_abs > 3000) return false;

  uint8_t frame[FRAME_BYTES] = {0};
  int idx = 2;
  for (int bi = 0; bi < FRAME_BYTES; bi++) {
    uint8_t byte = 0;
    for (int bit = 0; bit < 8; bit++) {
      if (idx + 1 >= size) return false;
      int32_t sp = data[idx + 1];
      int32_t sp_abs = sp < 0 ? -sp : sp;
      bool one = sp_abs > 700;
      byte = static_cast<uint8_t>((byte << 1) | (one ? 1u : 0u));
      idx += 2;
    }
    frame[bi] = byte;
  }

  LuxState s{};
  if (!decode(frame, s)) {
    // Includes: Frame A preamble, invalid checksum, unknown byte values,
    // or timer frames. Silently ignore — logging every RX flood-fills DEBUG.
    return false;
  }

  this->log_state_(s, "RX");
  this->publish_from_state_(s);
  return true;
}

void LuxChigoIRClimate::publish_from_state_(const LuxState &s) {
  if (!s.power_on) {
    this->mode = climate::CLIMATE_MODE_OFF;
  } else {
    this->mode = lux_mode_to_ha(s.mode);
  }
  this->target_temperature = s.temp_c;
  // Byte 8 is a bit-field: fan speed and swing-V can be ORed together, so
  // when swing_v is on the frame still carries fan info. Only sleep (0x80)
  // is exclusive — that sentinel does not carry fan bits, so we preserve
  // whatever fan_mode HA had before.
  if (!s.sleep) {
    this->fan_mode = lux_fan_to_ha(s.fan);
  }

  if (s.swing_h && s.swing_v) {
    this->swing_mode = climate::CLIMATE_SWING_BOTH;
  } else if (s.swing_h) {
    this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
  } else if (s.swing_v) {
    this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
  } else {
    this->swing_mode = climate::CLIMATE_SWING_OFF;
  }

  if (s.turbo)      this->preset = climate::CLIMATE_PRESET_BOOST;
  else if (s.eco)   this->preset = climate::CLIMATE_PRESET_ECO;
  else if (s.sleep) this->preset = climate::CLIMATE_PRESET_SLEEP;
  else              this->preset = climate::CLIMATE_PRESET_NONE;

  // Mirror HEALTH state internally so the next TX preserves it.
  this->health_ = s.health;

  // Publish the I-FEEL sensor value from the remote, if configured.
  if (this->ifeel_output_ != nullptr) {
    if (s.ifeel) {
      this->ifeel_output_->publish_state(static_cast<float>(s.ifeel_temp_c));
    } else {
      this->ifeel_output_->publish_state(NAN);
    }
  }

  this->publish_state();
}

void LuxChigoIRClimate::log_state_(const LuxState &s, const char *prefix) {
  ESP_LOGD(TAG,
           "%s: power=%d mode=0x%02x temp=%uC fan=0x%02x swing_h=%d swing_v=%d "
           "sleep=%d eco=%d turbo=%d ifeel=%d ifeel_t=%uC health=%d",
           prefix, s.power_on, static_cast<uint8_t>(s.mode), s.temp_c,
           static_cast<uint8_t>(s.fan), s.swing_h, s.swing_v, s.sleep,
           s.eco, s.turbo, s.ifeel, s.ifeel_temp_c, s.health);
}

}  // namespace esphome::lux_chigo_ir
