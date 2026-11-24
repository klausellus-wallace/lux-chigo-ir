# Changelog

All notable changes to this component will be documented in this file.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]
### Changed
- Device identified as Euromate 482255, protocol as a TCL112AC variant.
- Commands from ESPHome no longer make the AC beep by default. Frame B byte 5
  bit `0x04` is now the beep bit and is cleared unless `set_beep(true)` is
  called. Confirmed live by sound and power draw.

### Fixed
- Docs: the periodic-resend example in `troubleshooting.md` called the
  protected `transmit_state()` and could not compile; it now uses
  `ifeel_interval:`.
- Docs: ECO is byte 5 bit `0x01`.

### Added
- Timed I-FEEL: `ifeel_interval:` option and `set_ifeel_interval_min()`
  resend the state with the room temperature while I-FEEL is on, the AC is
  on, the temperature is valid and the HA API is connected
  (`ifeel_send_due()`, host-tested). No timed sends without the option.
- Runtime `set_ifeel()` / `get_ifeel()`. Turning I-FEEL off sends one frame
  without I-FEEL; restoring the switch at boot never transmits.
- Optional `ifeel_sent_output:` sensor with the last transmitted I-FEEL value.
- Example YAML: "I-FEEL" switch, "I-FEEL interval" number (5–60 min) and
  "I-FEEL last sent" sensor. Preset, fan and swing entities are no longer
  internal.
- `set_beep()` / `get_beep()` and a "Beep" template switch in the example
  YAML (`restore_mode: ALWAYS_OFF`).
- Docs: Frame A checksum offset (`+0x0f`) and live-test results for the
  Frame A byte 5 bit `0x04` candidate. It changes AC state but does not mute
  the beep; its effect is unknown, so DISPLAY and MUTE stay unimplemented.
- `ifeel:` climate option (default `true`). With `ifeel: false` the
  `sensor:` only feeds `current_temperature`; TX frames carry no I-FEEL
  value and the AC regulates on its built-in sensor. Useful because the
  I-FEEL value is only refreshed when a command is sent, so a stale
  reading can make the AC over-heat / over-cool for hours.
- Initial reverse-engineered protocol implementation.
- `LuxChigoIRClimate::get_health()` public getter — lets YAML template
  entities mirror the HEALTH bit back from received IR frames.
- Example YAML: top-level template `select` and `switch` entities for
  preset, fan mode, swing horizontal, swing vertical, and health, each
  performing `climate.make_call().perform()`.
- Example YAML: 500 ms `interval:` block that syncs the template
  entities back from the climate state, so the physical remote's
  presses reflect immediately in HA and the ESPHome web UI.
