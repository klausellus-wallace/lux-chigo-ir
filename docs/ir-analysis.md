# IR analysis handoff

Start here if you are continuing the remote-control analysis on
`feat/display-mute-tcl112`. The goal is to read the original remote's IR
commands and classify each button from **labeled** captures.

## Status 2026-11-24

- **Beep bit confirmed and implemented.** Frame B byte 5 bit `0x04` cleared =
  the AC applies the command silently. The component clears it by default
  (`set_beep()`). Evidence: `docs/verification.md` § "Live test 2026-11-24".
- **DISPLAY and MUTE removed from the code.** The Frame A bit changes AC state
  but does not mute the beep, and the display bit could not be checked
  remotely. Both stay documented as unknown candidates.
- **Current work: timed I-FEEL** (see "Follow-up" below). Silent commands make
  periodic sends practical.
- The TX timing change stays a separate, later decision.

The capture workflow below still applies to the open questions. Before any
flash, check the HA temperature entity in the device YAML.

## Questions to answer

| # | Question | Current evidence | Settled by |
|---|---|---|---|
| 1 | Is Frame B byte 5 bit `0x02` DISPLAY? | Toggles in `capture2.log` at 20:45:52/55 and `capture3.log` at 20:56:54/57 and 20:57:00. The old notes call the latter ECO | Labeled DISPLAY and ECO presses |
| 2 | Is set = display off? | TCL112AC only | Watching the indoor display |
| 3 | Why does `0x02` clear on MODE? | `capture3.log` 20:56:24 | DISPLAY, then MODE presses |
| 4 | Is Frame A byte 5 bit `0x04` MUTE? | One frame, `capture2.log` 20:45:47. Live 2026-11-24: AC accepts it, it does not mute the beep | Labeled MUTE presses, then an on-site test of what it changes |
| 5 | Does MUTE persist across other buttons? | Unknown | MUTE, TEMP_UP, TEMP_DOWN, MUTE |
| 6 | Is ECO byte 5 bit `0x01`? | One `0x27` frame plus a live note in the code | Labeled ECO presses |
| 7 | Does sending these bits change the AC? | Frame A bit: yes, effect unknown. Display bit: not tested | On-site test |

Lower priority, from the maintainer's handoff: timer resolution, swing-V positions, half-degree, and whether SLEEP is only
fan minimum. TIMER stays out of scope for implementation.

## Workflow

Run the capture and the labeling at the same time, in two terminals. Ask the
maintainer for the device address. Use an ESPHome version that matches the
firmware, because a mismatch can make the log connection time out.

```bash
# Terminal 1: raw log through ESPHome
docker run --rm -v ~/projects/esphome:/config esphome/esphome:<firmware-version> \
  logs /config/lux-ac-ir-7c7754.yaml --device <address> \
  > tests/captures/session-YYYYMMDD.log 2>&1

# Terminal 2: press each prompted button, then press Enter
python3 tests/analyze_capture.py --record-labels tests/sessions/plan-core.txt \
  tests/sessions/session-YYYYMMDD.csv --utc

# Analyze
python3 tests/analyze_capture.py tests/captures/session-YYYYMMDD.log \
  --labels tests/sessions/session-YYYYMMDD.csv
```

**Check the clock first.** Label times must use the same clock as the log.
Containers often run in UTC, so `--utc` is usually right for Docker. Omit it
if the log shows local time.

The report gives each labeled button one status:

- `OK`: exactly one IR press, listed with the bits that changed.
- `NO_IR`: no frame arrived. Repeat once before concluding the button is silent.
- `MULTI`: several presses matched. Usually a bounce or a late Enter, so redo it.

## Classification rules

- Only frames with valid checksums count as evidence.
- Call a mapping confirmed only after at least 3 labeled toggles in both
  directions, with no contradiction in the same session.
- Record what the indoor unit visibly did. A wire bit alone does not prove its
  effect.

## Follow-up: timed I-FEEL and HA controls

The maintainer approved this design. Build it test-first after the capture.

**Timed I-FEEL**

- Add a pure, host-testable send decision. Send only if I-FEEL is on, HA says
  the AC is on, the temperature is valid, and the HA API is connected.
- Add runtime `set_ifeel(bool)` / `get_ifeel()`. On: room temperature in every
  command plus timed sends. Off: send exactly one frame without I-FEEL, so the
  AC returns to its own sensor. The YAML `ifeel:` option stays the start value
  and must not transmit at boot.
- Add `set_ifeel_interval_min()` using ESPHome `set_interval`, plus a YAML
  option `ifeel_interval`. Without the option, timed sending stays off so
  existing configs do not start beeping. Example and live YAML use 10 minutes.
- Add an optional `ifeel_sent_output` sensor for the last transmitted value.
- Replace the `transmit_state()` lambda in `docs/troubleshooting.md`. That
  method is `protected`, so the example likely does not compile.

**HA entities** (example YAML first, live YAML after the maintainer's OK)

- I-FEEL switch and interval number (5 to 60 min, step 5, default 10), both
  restored after reboot, plus a last-sent temperature sensor.
- Keep the Ionizer visible. Remove `internal: true` from preset, fan and both
  swing entities.
- Mirror the new states in the 500 ms `interval:` block.

**Known risks to test live**

- Every timed send transmits the full HA state. With the default `beep` off
  it is silent. If the receiver missed a remote press, the next send
  overwrites the real AC state and can switch the AC back on.
- The manual says I-FEEL ends after 2 hours. The fallback when HA is
  disconnected relies on this and is unverified.

## Repo rules

- Never edit existing capture logs or hand-edit `tests/fixtures.json`.
- `build_fixtures.py` reads a fixed file list. Adding a new session to the
  fixtures changes ground truth, so ask the maintainer first.
- Keep the Frame B checksum unchanged and do not implement TIMER.
- `HANDOFF-lux-chigo-ir.md` is the maintainer's investigation record. Its
  hypotheses are superseded by labeled captures. Do not add keys or Wi-Fi
  credentials to the repo.
- Do not merge or push without the maintainer's OK.

## When the analysis is done

1. Update `docs/protocol.md` and `docs/verification.md` with the results.
2. Replace the "unconfirmed" wording in `CLAUDE.md`, `README.md` and
   `CHANGELOG.md`, or remove the feature if the hypothesis failed.
3. Add unit tests from the labeled frames and run:

```bash
make -C tests run
python3 tests/test_analyze_capture.py
```
