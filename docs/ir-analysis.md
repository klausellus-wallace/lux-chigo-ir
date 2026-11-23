# IR analysis handoff

Start here if you are continuing the remote-control analysis on
`feat/display-mute-tcl112`. The goal is to read the original remote's IR
commands and classify each button from **labeled** captures.

## Current state

- **Order of work: capture and classify first.** Continue experimental
  DISPLAY/MUTE work and the TX timing change only after that.
- Do not flash this branch for the capture. Capturing only needs the receiver
  on the firmware already running on the device.
- The DISPLAY/MUTE draft is committed as experimental. It compiles for
  ESP32-C3 but has not been tested on the AC. The TX timing is unchanged.
- The active device config is `~/projects/esphome/lux-ac-ir-7c7754.yaml`. It
  builds from the local copy in `~/projects/esphome/components/`, not from
  this branch, and already logs raw IR with `dump: raw`.
- The three existing logs do not name the pressed buttons. Treat every
  button-to-bit mapping from them as a hypothesis.

## Questions to answer

| # | Question | Current evidence | Settled by |
|---|---|---|---|
| 1 | Is Frame B byte 5 bit `0x02` DISPLAY? | Toggles in `capture2.log` at 20:45:52/55 and `capture3.log` at 20:56:54/57 and 20:57:00. The old notes call the latter ECO | Labeled DISPLAY and ECO presses |
| 2 | Is set = display off? | TCL112AC only | Watching the indoor display |
| 3 | Why does `0x02` clear on MODE? | `capture3.log` 20:56:24 | DISPLAY, then MODE presses |
| 4 | Is Frame A byte 5 bit `0x04` MUTE? | One frame, `capture2.log` 20:45:47. None in `capture3.log` | Labeled MUTE presses |
| 5 | Does MUTE persist across other buttons? | Unknown | MUTE, TEMP_UP, TEMP_DOWN, MUTE |
| 6 | Is ECO byte 5 bit `0x01`? | One `0x27` frame plus a live note in the code | Labeled ECO presses |
| 7 | Does sending these bits change the AC? | Not tested | HA switches on real hardware |

Lower priority, from the maintainer's handoff: the beep bit (byte 5 `0x04`),
timer resolution, swing-V positions, half-degree, and whether SLEEP is only
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
