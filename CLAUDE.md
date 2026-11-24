# CLAUDE.md — context for AI coding assistants

This file is read by Claude Code at session start and gives future
sessions the ground truth for this repo so we don't have to re-derive
it every time.

## What this repo is

An ESPHome external component + Home Assistant climate integration for
the OBI/Euromate "LUX SKG 9000-48 QC" split AC (Euromate 482255). The
AC uses a device-specific TCL112AC variant, which ESPHome's `tcl112`
platform only partly covers. The protocol was reverse-engineered
against raw capture logs; it is specified in `docs/protocol.md`.

## Ground truth — do not second-guess

- The wire-protocol spec in `docs/protocol.md` was validated against
  33+ organic Frame-B captures (100 % checksum match, 100 % round-trip
  encode/decode). Treat it as authoritative. If code disagrees with
  the doc, the code is wrong.
- `tests/captures/*.log` are the raw ground truth. If a change looks
  right in isolation but breaks `make -C tests run`, the change is
  wrong.
- The Frame B checksum formula is `bit_reverse(sum(bit_reverse(b) for
  b in payload[0:13]) & 0xff)`. Frame A adds `0x0f` to the sum before
  the mask. Do NOT propose alternatives unless you can point to a
  captured frame where they fail.
- Frame B byte 5 bit `0x04` is the beep bit, confirmed live: cleared =
  the AC applies the command silently. The component clears it by default.
- DISPLAY (Frame B byte 5 bit `0x02`) and MUTE (Frame A byte 5 bit
  `0x04`) are unknown. The Frame A bit changes AC state but does not mute
  the beep. Do not implement either without a labeled capture and a
  hardware test showing the effect. See `docs/verification.md`.
- TURBO is byte 6 bit `0x02` on this unit, unlike stock TCL112AC.
- TIMER is intentionally out of scope. Don't add it.

## Current work

On `feat/display-mute-tcl112`, start with `docs/ir-analysis.md`. It holds the
next steps, open questions and the approved follow-up.

## Repo layout

See `docs/development.md` § "Repo layout" for the file map. Two rules
of thumb:
- `components/lux_chigo_ir/protocol.{h,cpp}` is pure C++17 with zero
  ESPHome/Arduino deps. Host-testable via `tests/Makefile`. All
  encoding/decoding logic lives here.
- `components/lux_chigo_ir/lux_chigo_ir.{h,cpp}` is the ESPHome wrapper
  that translates HA climate state to/from `LuxState` and drives
  `remote_transmitter` + `remote_receiver`.

## Running tests

Every meaningful protocol change must pass:

```bash
cd tests
python3 build_fixtures.py   # regenerate ground truth from captures/
make run                    # compile + run test_protocol
```

Expected: `rt_ok=N/N ao_ok=M/M failures=0`.

## Coding conventions

- C++17. Namespaces: `lux_chigo_ir` for the pure protocol layer,
  `esphome::lux_chigo_ir` for the ESPHome-integrated class.
- Wire order: bytes are transmitted MSB-first. The internal AC
  interpretation is LSB-first; that's why the checksum works on
  bit-reversed bytes. See `docs/protocol.md`.
- No exceptions in the C++ layer (ESPHome compiles with -fno-exceptions
  on some targets).
- Prefer explicit `uint8_t` over `int` for byte-level code.
- Python (climate.py, build_fixtures.py): follow ESPHome codegen
  conventions — look at `esphome/components/coolix/climate.py` for
  the reference pattern.

## When adding a new AC feature

Follow `docs/development.md` § "Adding a new AC feature". Short version:
protocol.md → LuxState → encode/decode → tests → HA-facing wiring →
CHANGELOG bump.

## What NOT to do without human confirmation

- Change the checksum formula (see "Ground truth").
- Add HTTP endpoints, MQTT publishing, or non-ESPHome integrations —
  scope is deliberately narrow.
- Move `handoff.md` (the RE working notes) into this repo — it lives
  in `~/projects/lux-ir/` on the maintainer's machine
  and is intentionally excluded.
- Rewrite the fixtures — they're ground truth from a physical remote.

## Human maintainer

[@klausellus-wallace](https://github.com/klausellus-wallace)

## AI-usage disclosure

See `AI_DISCLOSURE.md`. This repo was co-authored with Claude Code.
All commits attribute the AI collaboration via `Co-Authored-By` lines.
