# How the protocol was reverse-engineered

## Hardware setup

Seeed Studio XIAO IR Mate (ESP32-C3) with ESPHome. IR RX on GPIO4
(inverted, pullup), IR TX on GPIO5.

## Method

1. Configure `remote_receiver: dump: raw` in the ESPHome YAML.
2. Press buttons on the original remote one at a time with ≥3 s gaps.
3. Read the remote's own display (temperature + mode symbol) after
   every press — this is the ground truth against which bytes are
   interpreted.
4. Parse the raw µs timings using `tests/build_fixtures.py` (Python
   reference implementation).
5. Classify bit spaces: `> 700 µs → 1`, `< 700 µs → 0`. Mark length is
   constant and carries no information.
6. Differential analysis — vary one parameter at a time (e.g. only
   temperature) to isolate which byte / bits change.

### Pitfalls avoided

- **`dump: all` misclassifies noise.** The Bang & Olufsen `beo4`
  decoder repeatedly matched IR-room background noise. Always use
  `dump: raw`.
- **Every keypress sends two frames.** Frame A (byte 3 = 0x40) carries
  the MUTE state; Frame B (byte 3 = 0x80) carries the command. Analyse
  both.
- **Bit-noise on the RX side flips ~1 bit per few frames.** When two
  captures of the same press differ, compare multiple captures
  (majority vote) before assuming a protocol variant.
- **Display readout resolved every "anomaly".** Multiple apparent
  protocol quirks turned out to be normal AC state changes we hadn't
  been tracking.

## Key discoveries

### The temperature nibble = `bit_reverse(31 - temp)`

This was the first hint that the AC works LSB-first internally, even
though bytes are transmitted MSB-first. The temperature table matches
what public sources document for `ZH/JT-03` (a related Chigo protocol)
— but the frame structure and timings are otherwise unrelated.

### Checksum

Simple 8-bit sum over the bit-reversed payload, then bit-reversed
back. Formula matches 33/33 organic Frame-B captures. See `docs/protocol.md`
§ Byte 13 for the formula.

### Byte-8 is a single-slot enum, not bit-field

Sleep, swing-vertical, and non-AUTO fan values all occupy byte 8 with
distinct hex codes; they can't be combined because they share the
byte. This drives our TX priority: sleep > swing-V > TURBO/FAN forces
HIGH > user fan.

### DISPLAY and MUTE: likely IR, not yet proven

The original notes concluded that these buttons emit no IR because a
`capture3.log` session produced fewer frames than expected. Those notes also
say the result was not technically proven. No log names the pressed buttons,
so every mapping below is inferred from press order.

- **DISPLAY candidate (Frame B byte 5 bit `0x02`):** `capture2.log` changes
  `0x26 → 0x24` at 20:45:52 and returns at 20:45:55. The return frame has a
  flipped byte 4 bit and fails its checksum, so that bit is RX noise.
- **Conflicting evidence:** `capture3.log` toggles the same bit at 20:56:54,
  20:56:57 and 20:57:00, where the original notes list three ECO presses.
  Those notes treated `0x02` as ECO, which a later live test corrected to
  `0x01`, so the label may be wrong. The bit also clears at 20:56:24 on a
  MODE press, and it changes at 20:55:11 with no other byte changing.
- **MUTE candidate (Frame A byte 5 bit `0x04`):** `capture2.log` changes
  `0x02 → 0x06` at 20:45:47 with a valid checksum (`0xa0`), and returns at
  20:45:50. This is the only MUTE-on frame. `capture3.log` has none, although
  the original notes describe three targeted MUTE presses in that session.

A timed capture and a hardware test must confirm both functions.

## Live-test verification (post-Task 10)

*(Fill in after live testing per Task 10 Step 4.)*

- Checksum accepted by AC: ...
- 38 kHz carrier confirmed: ...
- Frame A required: ...
- Frame gap 200 ms: ...
- RX anti-loop guard 500 ms: ...
- Notable surprises: ...
