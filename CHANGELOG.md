# Changelog

All notable changes to this component will be documented in this file.
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]
### Added
- Initial reverse-engineered protocol implementation.
- `LuxChigoIRClimate::get_health()` public getter — lets YAML template
  entities mirror the HEALTH bit back from received IR frames.
- Example YAML: top-level template `select` and `switch` entities for
  preset, fan mode, swing horizontal, swing vertical, and health, each
  performing `climate.make_call().perform()`.
- Example YAML: 500 ms `interval:` block that syncs the template
  entities back from the climate state, so the physical remote's
  presses reflect immediately in HA and the ESPHome web UI.
