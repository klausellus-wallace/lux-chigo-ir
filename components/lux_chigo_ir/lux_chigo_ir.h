#pragma once
#include "esphome/components/climate_ir/climate_ir.h"
#include "esphome/components/sensor/sensor.h"
#include "protocol.h"

namespace esphome::lux_chigo_ir {

class LuxChigoIRClimate : public climate_ir::ClimateIR {
 public:
  LuxChigoIRClimate();

  // Optional sensor to publish I-FEEL room-temp on RX. Not required for TX.
  void set_ifeel_output(sensor::Sensor *sens) { this->ifeel_output_ = sens; }

  // When false, the base-class `sensor:` only drives current_temperature
  // and is never transmitted as I-FEEL; the AC regulates on its built-in
  // sensor. YAML: `ifeel: false`. Default true (legacy behavior).
  void set_ifeel_enabled(bool on) { this->ifeel_enabled_ = on; }

  // Runtime I-FEEL switch (YAML template switch). On: the room temperature
  // goes into every command and into the timed sends, and is sent right
  // away. Off: one frame without I-FEEL is sent so the AC returns to its own
  // sensor. Never transmits before setup is complete, so restoring a switch
  // at boot stays silent on the wire.
  void set_ifeel(bool on);
  bool get_ifeel() const { return this->ifeel_enabled_; }

  // Timed I-FEEL: resend the full state every `min` minutes while
  // ifeel_send_due() holds. 0 (default) disables timed sends. YAML:
  // `ifeel_interval:`; can also be changed at runtime from a number entity.
  void set_ifeel_interval_min(uint32_t min);
  uint32_t get_ifeel_interval_min() const { return this->ifeel_interval_min_; }

  // Optional sensor: last I-FEEL temperature actually transmitted (NaN when
  // the last frame carried no I-FEEL value).
  void set_ifeel_sent_output(sensor::Sensor *sens) { this->ifeel_sent_output_ = sens; }

  // Enable/disable HEALTH bit in the encoded frame. Called from YAML
  // template switches (see Task 6). Triggers a fresh transmit_state().
  void set_health(bool on);

  // Current HEALTH state (kept in sync with the encoded/received frame).
  // Read from a YAML template switch's `lambda:` or `interval:` re-sync
  // block to reflect physical-remote presses back into HA.
  bool get_health() const { return this->health_; }

  // Frame B byte 5 bit 0x04. Off (default) = the AC applies commands from
  // ESPHome without beeping; confirmed live 2026-11-24 (audio + power draw).
  // Does not transmit by itself; applies from the next command on.
  void set_beep(bool on) { this->beep_ = on; }
  bool get_beep() const { return this->beep_; }

  // Guards TX I-FEEL against a stuck / never-updated sensor. Currently
  // implemented as a plausible-range check (accepts temp in (-20, 80) °C).
  // The `ms` argument is preserved as a knob for a future true last-update
  // timestamp check; set to 0 to disable the range guard entirely, non-zero
  // to enable it. NOT a real age gate today — see `gather_state_()` in the
  // .cpp for the current behavior. Do not depend on `ms` values other than
  // 0 or non-zero. Not exposed via YAML config in this release; toggle from
  // a lambda if you need a non-default value.
  void set_ifeel_max_age_ms(uint32_t ms) { this->ifeel_max_age_ms_ = ms; }

 protected:
  // ClimateIR virtual overrides
  void setup() override;
  void control(const climate::ClimateCall &call) override;
  void transmit_state() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;

 private:
  LuxState gather_state_() const;
  void publish_from_state_(const LuxState &s);
  void send_frames_(const uint8_t frame_b[FRAME_BYTES]);
  void log_state_(const LuxState &s, const char *prefix);
  void apply_ifeel_interval_();
  void ifeel_tick_();
  bool api_connected_() const;

  sensor::Sensor *ifeel_output_{nullptr};
  sensor::Sensor *ifeel_sent_output_{nullptr};
  uint32_t ifeel_interval_min_{0};
  bool ready_{false};   // set on the first loop, after every setup() ran
  bool health_{false};
  bool beep_{false};
  bool ifeel_enabled_{true};
  uint32_t ifeel_max_age_ms_{300000};
  uint32_t last_transmit_ms_{0};
};

}  // namespace esphome::lux_chigo_ir
