# Troubleshooting

## AC does not respond to HA commands

1. Check ESPHome logs for `TX frame: ...` output. Missing → the climate
   entity's state didn't actually change; HA sent no ClimateCall.
2. If the frame is logged but the AC ignores it, try adjusting the IR
   carrier: replace `carrier_duty_percent: 50%` with `carrier_frequency:
   36000` or `40000` in the `remote_transmitter` block. Some
   Chigo variants use non-standard carriers.
3. Verify the IR LED is physically aimed at the AC's IR receiver window.
4. Sanity-check with `examples/lux-ac-ir.example.yaml` first, then
   swap in your customisations one at a time.

## HA does not update when I press the original remote

1. Check ESPHome logs for `remote.raw: Received Raw: ...` — no raw log
   means the receiver isn't picking up the signal (dead battery in
   original remote, wrong GPIO pin, missing pullup).
2. Check for `lux_chigo_ir: RX: ...` decoded log lines. If raw arrives
   but decoded doesn't, the frame's checksum is failing — likely
   bit-noise (see `docs/protocol.md` on capture noise).
3. Ensure `receiver_id: ir_rx` is set on the climate entity.

## HEALTH switch does nothing

The switch template needs `id: lux_ac` on the climate entity — the
lambdas reference `id(lux_ac).set_health(true)`. Without the id, the
lambda fails silently at compile time.

## I-FEEL sensor value looks wrong

The AC and this component both interpret byte 11 as `bit_reverse(temp)`.
Verify the incoming HA sensor value is in °C (integer). The plausibility
check in `gather_state_()` clamps to −20..80 °C — anything outside is
dropped as stale.

## LUX-specific aux entities show up in HA and clutter my dashboard

The example YAML defines a `select.lux_preset`, `select.lux_fan_mode`, and
two swing switches for hands-on control from the ESPHome web UI. HA doesn't
need these — its climate card already exposes fan_mode, swing_mode, and
preset natively on the `climate.lux_ac` entity.

The default pattern in the example is:

- Every `select` / `switch` for a LUX aux feature has `internal: true`.
- The `web_server:` block sets `include_internal: true`.

Together: HA sees only `climate.lux_ac` + `switch.lux_ac_health`; the
ESPHome web UI keeps showing everything. If you want ANY of the internal
entities to appear in HA anyway (e.g. you want the swing switches on your
HA dashboard for a physical remote-style control), just delete the
`internal: true` line from that entity.

## What does I-FEEL actually do?

The AC's built-in temperature sensor sits near the ceiling of the
indoor unit, where hot air pools. Without I-FEEL, the AC uses that
reading as "current room temperature" — so it systematically
over-cools (or under-heats) the actual living space. When I-FEEL is
on, the AC ignores its own ceiling sensor and uses whatever
temperature the remote transmits in byte 11 of every frame.

**Practical setup:** pair the `sensor:` input with a temperature
sensor located at seated head height (the ESP32 board itself, or a
zigbee sensor imported via `platform: homeassistant`). The AC will
then chase your setpoint at that height instead of at the ceiling.

**Turning I-FEEL off but keeping the reading:** the value only reaches
the AC when a frame is sent, so between commands the AC regulates on a
stale reading (see below). To show the sensor as `current_temperature`
in HA without transmitting it, set `ifeel: false` on the climate:

```yaml
climate:
  - platform: lux_chigo_ir
    sensor: room_temp
    ifeel: false   # display only; AC uses its built-in sensor
```

## Feeding I-FEEL from a real Home Assistant sensor

The bundled example YAML uses ESPHome's push-based subscription:

```yaml
sensor:
  - platform: homeassistant
    id: room_temp
    entity_id: sensor.living_room_temperature
    filters:
      - throttle: 5min
```

HA pushes state updates over the API subscription — no polling,
low latency. The last value stays cached on the ESP if HA restarts
or drops off the network; the AC keeps regulating on that last
reading until HA comes back. The `throttle: 5min` filter matches the
original remote's I-FEEL update cadence and keeps IR chatter down.

The value only reaches the AC on the next `transmit_state()` — i.e.
when HA or the physical remote changes something. If you want the
ESP to periodically resend the current state so the AC sees fresh
I-FEEL readings even during idle periods, add:

```yaml
interval:
  - interval: 5min
    then:
      - lambda: |-
          if (id(lux_ac).mode != climate::CLIMATE_MODE_OFF) {
            id(lux_ac).transmit_state();
          }
```

Warning: each transmit makes the AC beep. Extend to `10min` or
`15min` if the beep is intrusive.

## Manual dial as an alternative

If you don't have a real room-temperature sensor (guest room,
calibration testing, forcing the AC to over-cool), replace the HA
sensor with a `number:` slider:

```yaml
number:
  - platform: template
    id: ifeel_input
    name: "I-FEEL room temperature"
    min_value: 16
    max_value: 32
    step: 1
    initial_value: 22
    optimistic: true
    restore_value: true
    unit_of_measurement: "°C"
    mode: box

sensor:
  - platform: template
    id: room_temp
    lambda: 'return id(ifeel_input).state;'
    update_interval: 5s
    unit_of_measurement: "°C"
```

HA shows a draggable value that gets bit-reversed into wire byte
11 on every TX.
