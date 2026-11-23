# Changelog

All notable changes to this component will be documented in this file.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]
### Changed
- Device identified as Euromate 482255, protocol as a TCL112AC variant.
- Frame A is now built dynamically, including its `+0x0f` checksum offset.
- Fixtures now include display-off frames (byte 5 bit `0x02`).

### Fixed
- Docs: ECO is byte 5 bit `0x01`.

### Added
- Experimental display and MUTE support through `set_display()` /
  `get_display()` and `set_mute()` / `get_mute()`, with template switches in
  the example YAML. Both bit mappings are inferred and still need a timed
  capture and hardware test.
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
