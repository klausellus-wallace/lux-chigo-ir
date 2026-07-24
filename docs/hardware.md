# Hardware compatibility

## Verified AC unit

| Attribute | Value |
|---|---|
| Sales name | OBI "Split-Klimagerät EEK: A++ SKG 9000-48 QC" |
| Article number | 3432531 (OBI) / 482255 (Euromate) |
| EAN | 4048124822555 |
| Importer | OBI Group Sourcing / Euromate GmbH |
| Refrigerant | R32 |
| Year | ~2020 (mainboard datecode 20200606) |
| Remote part number | 22013-000237, QA stamp "XHY-T" |

Other Chigo OEM units using the same remote family (part 22013-xxx)
may work but are not verified.

## Verified controller

Seeed Studio **XIAO IR Mate** — ESP32-C3-based module with a 3× IR
LED array and IR receiver pre-integrated. Pins (per Seeed docs):

| Function | XIAO pin | GPIO | Direction |
|---|---|---|---|
| IR TX (3× LED emitter array) | D1 | **3** | output |
| IR RX (photodiode) | D2 | **4** | input, inverted, pullup |

Watch out for the touch sensor on D3 (GPIO5) — earlier revisions of
this example used GPIO5 for TX and pulsed the touch input instead of
the IR LEDs. If a rebuild yields no IR emission (front-camera check
shows no LED activity), verify the transmitter pin is GPIO3, not
GPIO5.

## Any ESP32/ESP8266 + IR LED + IR receiver should work

The component uses standard ESPHome `remote_transmitter` and
`remote_receiver` platforms; any board compatible with those can host
it. TSOP4838-style receivers on the RX pin are recommended
(inverted output; enable `pullup: true`).

## Running multiple units in one LAN

Each XIAO IR Mate ships with a unique MAC. Set
`esphome.name_add_mac_suffix: true` (see `examples/lux-ac-ir.example.yaml`)
so the hostname becomes `lux-ac-ir-<last3MACbytes>` (e.g.
`lux-ac-ir-7c7754.local`). ESPHome derives the friendly name suffix
from the same MAC. Each unit then has its own mDNS name, its own web
UI at `http://<hostname>.local/`, and its own HA entity IDs — no
collisions.

If you flash the same YAML to multiple boards without this setting,
they will all try to claim `lux-ac-ir.local` and win a random
race; do not do this.
