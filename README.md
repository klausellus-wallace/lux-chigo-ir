# lux_chigo_ir — ESPHome external component for LUX/Chigo split AC

Home Assistant integration for the OBI-branded LUX split AC ("Split-Klimagerät
EEK: A++ SKG 9000-48 QC", Art.-Nr. 3432531) via IR, with **bidirectional
state sync**: HA commands are transmitted to the AC, and manual presses on
the original remote are received and reflected back to HA.

The protocol is a Chigo-OEM variant that is NOT ZH/JT-03 (despite superficial
button-layout similarity) and NOT covered by any existing ESPHome climate
platform. See [docs/protocol.md](docs/protocol.md) for the wire spec and
[docs/verification.md](docs/verification.md) for how it was reverse-engineered.

## Features

| HA capability | Notes |
|---|---|
| `hvac_mode` | OFF, COOL, HEAT, DRY, FAN_ONLY, HEAT_COOL (=AUTO) |
| `target_temperature` | 16–31 °C, 1 °C step |
| `fan_mode` | AUTO, LOW, MEDIUM, HIGH |
| `swing_mode` | OFF, HORIZONTAL, VERTICAL, BOTH |
| `preset` | NONE, ECO, BOOST (=TURBO), SLEEP |
| HEALTH toggle | separate `switch:` entity (template pattern) |
| I-FEEL | sensor input from HA is transmitted as room-temp reference; RX value from remote can be mirrored back via optional `ifeel_output:` sensor. Set `ifeel: false` to use the sensor for `current_temperature` only |

The DISPLAY and MUTE buttons on the original remote don't transmit any IR
signal and are not exposed. TIMER is intentionally out of scope.

## Hardware

Tested on Seeed Studio **XIAO IR Mate** (ESP32-C3). Any ESP32/ESP8266 with an
IR transmitter and receiver should work — see [docs/hardware.md](docs/hardware.md).

## Installation

Add to your ESPHome device YAML:

```yaml
external_components:
  - source: github://klausellus-wallace/lux-chigo-ir
    components: [lux_chigo_ir]

remote_transmitter:
  id: ir_tx
  pin: GPIO5
  carrier_duty_percent: 50%

remote_receiver:
  id: ir_rx
  pin:
    number: GPIO4
    inverted: true
    mode:
      input: true
      pullup: true
  tolerance: 25%

sensor:
  - platform: homeassistant
    id: room_temp
    entity_id: sensor.living_room_temperature   # replace with your sensor

climate:
  - platform: lux_chigo_ir
    id: lux_ac
    name: "LUX AC"
    receiver_id: ir_rx
    transmitter_id: ir_tx
    sensor: room_temp

switch:
  - platform: template
    name: "LUX HEALTH"
    optimistic: true
    turn_on_action:
      - lambda: 'id(lux_ac).set_health(true);'
    turn_off_action:
      - lambda: 'id(lux_ac).set_health(false);'
```

A complete example lives in [`examples/lux-ac-ir.example.yaml`](examples/lux-ac-ir.example.yaml).

## Bidirectional sync

The AC does not itself echo state changes back over IR. The sync happens by
listening for the original remote's IR transmissions with the same
`remote_receiver`. Every valid Frame B is decoded and the climate entity's
state is republished. See [docs/protocol.md](docs/protocol.md) for how
frames are structured.

## Development

See [docs/development.md](docs/development.md) for how to run the C++ tests
(`make -C tests run`), regenerate fixtures from the raw captures in
`tests/captures/`, and rebuild the firmware in the ESPHome Docker container.

## Verification data

Raw log captures from the original remote live in `tests/captures/`:

- `capture.log` (temperature sweep, swing, sleep, power, mode cycle)
- `capture2.log` (special buttons: ECO/TURBO/I-FEEL/HEALTH/MUTE/DISPLAY/TIMER)
- `capture3.log` (systematic per-button toggle sequences)

These are the ground truth. `tests/build_fixtures.py` extracts Frame B
samples into `tests/fixtures.json`, and `tests/test_protocol.cpp` runs
differential tests against them.

## AI usage disclosure

See [AI_DISCLOSURE.md](AI_DISCLOSURE.md). The reverse-engineering was
performed collaboratively with an AI assistant; the code and docs are
released under MIT (see [LICENSE](LICENSE)).

## Author

[@klausellus-wallace](https://github.com/klausellus-wallace)

## License

MIT — see [LICENSE](LICENSE).
