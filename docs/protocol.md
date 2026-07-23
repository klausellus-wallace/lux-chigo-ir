# LUX/Chigo IR Protocol — Wire Specification

## Overview

Every button press transmits **two frames**:
- **Frame A** — fixed 14-byte preamble, identical for every press.
- **Frame B** — 14-byte command frame carrying mode/temp/fan/aux state.

Gap between A and B: ~200 ms (150–400 ms observed).

Carrier: 38 kHz, duty 50 %.

Bit encoding (µs):
- Header: 3145 mark, 1560 space
- Bit mark (constant): 243
- Bit space 0: 243
- Bit space 1: 1063
- Trailing mark after last bit: one bit mark

Bytes are transmitted MSB-first over the wire, but the AC internally
interprets each byte as LSB-first. This matters for the checksum (see
below).

## Frame A (preamble)

```
c4 d3 64 40 00 02 00 00 01 00 00 00 00 a7
```

Fixed constant. Byte 3 = 0x40 identifies it. Byte 13 = 0xa7 is a fixed
value, not computed by the same formula as Frame B — treat it as part
of the constant.

## Frame B (command)

```
byte:  0  1  2  3  4  5  6  7  8  9 10 11 12 13
       c4 d3 64 80 XX YY ZZ TT AA 00 00 II CC KK
                     |  |  |  |  |        |  |  └─ checksum (see below)
                     |  |  |  |  |        |  └──── mode base + swing_h flag
                     |  |  |  |  |        └─────── I-FEEL room temp (LSB-first) or 0
                     |  |  |  |  └──────────────── aux slot (fan/swing_v/sleep)
                     |  |  |  └─────────────────── temperature nibble
                     |  |  └────────────────────── mode + health/turbo/ifeel flags
                     |  └───────────────────────── power + ECO + reserved bit
                     └──────────────────────────── timer flag (0x00 non-timer)
```

Byte 3 = 0x80 identifies a command frame (0x40 = preamble).

### Byte 4 — timer flag
- `0x00`: not a timer frame.
- `0x10`: timer active. **Not implemented in this component.**

### Byte 5 — power + ECO
| Bit | Meaning |
|---|---|
| 0x20 | Power ON (0 = OFF) |
| 0x04 | Constant 1 in every observed frame (reserved / protocol-version bit) |
| 0x02 | ECO active |

Common wire values: `0x24` (on, no ECO), `0x26` (on, ECO), `0x04` (off).

### Byte 6 — mode + feature flags
Upper 4 bits = mode:
| Mode | Value |
|---|---|
| COOL | 0xc0 |
| HEAT | 0x80 |
| DRY | 0x40 |
| FAN | 0xe0 |
| AUTO | 0x10 |

Lower 4 bits = flags:
| Bit | Meaning |
|---|---|
| 0x08 | HEALTH active |
| 0x02 | TURBO active (AC also forces byte 7 = 0xf0 and byte 8 = 0xa0) |
| 0x01 | I-FEEL active (byte 11 carries room temperature) |
| 0x04, 0x10, 0x20 | Reserved; never seen set |

### Byte 7 — temperature (upper nibble)
Temperature 16..31 °C maps to a 4-bit code stored in the upper nibble:

| °C | Nibble | °C | Nibble |
|---|---|---|---|
| 16 | f | 24 | e |
| 17 | 7 | 25 | 6 |
| 18 | b | 26 | a |
| 19 | 3 | 27 | 2 |
| 20 | d | 28 | c |
| 21 | 5 | 29 | 4 |
| 22 | 9 | 30 | 8 |
| 23 | 1 | 31 | 0 |

Equivalent formula: `nibble = bit_reverse_4(31 - temp)`. The lower nibble
is always 0.

### Byte 8 — auxiliary single-slot
Byte 8 holds one of six mutually exclusive values:

| Value | Meaning |
|---|---|
| 0x00 | fan AUTO, no swing-V, no sleep |
| 0x40 | fan LOW |
| 0xc0 | fan MED |
| 0xa0 | fan HIGH (also auto-set by AC when mode=FAN or TURBO) |
| 0x1c | swing-V active |
| 0x80 | sleep active |

Sleep, swing-V, and non-AUTO fan cannot combine. The most recently
selected feature wins.

### Bytes 9, 10 — reserved
Always `0x00` in non-timer frames.

### Byte 11 — I-FEEL sensor value
- `0x00` when I-FEEL is off.
- Bit-reversed room-temperature integer (°C) when I-FEEL is on. Example: 27 °C → `bit_reverse(0x1b) = 0xd8`.

### Byte 12 — swing-H + mode base
Upper nibble bit `0x10` = swing-H active.
Lower nibble = mode-dependent constant: `0x01` for COOL/DRY/FAN, `0x00` for HEAT/AUTO.

### Byte 13 — checksum

```python
def bit_reverse(b: int) -> int:
    r = 0
    for i in range(8):
        if b & (1 << i):
            r |= 1 << (7 - i)
    return r

def checksum(payload_13_bytes: bytes) -> int:
    total = sum(bit_reverse(b) for b in payload_13_bytes) & 0xff
    return bit_reverse(total)
```

Verified against 33+ organic captures — 100 % match.

## References

- Raw capture logs: `tests/captures/capture{,2,3}.log`
- Fixtures: `tests/fixtures.json`
- Encoder/decoder: `components/lux_chigo_ir/protocol.{h,cpp}`
