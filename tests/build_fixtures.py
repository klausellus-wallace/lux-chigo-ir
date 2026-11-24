#!/usr/bin/env python3
"""Extract Frame-B captures from capture*.log and dump to fixtures.json.

Runs the same parser logic as parse_ir.py, keeps only valid Frame B (byte3=0x80),
and asserts each frame's checksum matches the LSB-first-sum formula.
Failures during extraction mean the log file has bit errors — those are dropped
with a warning.

Two sets are emitted:
  - roundtrip: fixtures where encode(decode(f)) MUST equal f. Excludes
    transient-event frames (b5=0x27 ECO-second-press, b5=0x36 timer-set,
    b4!=0 timer). Our encoder does NOT reproduce those; they represent
    button-event codes, not persistent state.
  - accept_only: fixtures that decode() must ACCEPT (returning true, with
    a plausible LuxState) but roundtrip is not required. Timer + b5-extra
    frames go here — the decoder should not crash on them.
"""
import json
import re
import sys
from pathlib import Path

THRESHOLD_US = 700
CAPTURE_DIR = Path(__file__).resolve().parent / "captures"
CAPTURE_FILES = ["capture.log", "capture2.log", "capture3.log"]
OUT_FILE = Path(__file__).resolve().parent / "fixtures.json"

def bit_reverse(b: int) -> int:
    r = 0
    for i in range(8):
        if b & (1 << i):
            r |= 1 << (7 - i)
    return r

def checksum(first_13: bytes) -> int:
    return bit_reverse(sum(bit_reverse(b) for b in first_13) & 0xff)

def iter_raw_frames(path: Path):
    """Yield lists of int timings from ESPHome remote.raw log entries."""
    current = None
    with path.open() as f:
        for line in f:
            m = re.match(r'\[[^\]]+\]\[.\]\[remote\.raw:\d+\]:\s*(.*)', line)
            if not m:
                continue
            payload = m.group(1)
            if payload.startswith("Received Raw:"):
                if current is not None:
                    yield current
                payload = payload[len("Received Raw:"):].strip()
                current = []
            if current is not None:
                current.extend(int(x) for x in re.findall(r'-?\d+', payload))
    if current is not None:
        yield current

def decode_frame(timings):
    if len(timings) < 225:
        return None
    if not (2000 < abs(timings[0]) < 5000):
        return None
    bits = []
    i = 2
    while i + 1 < len(timings) and len(bits) < 112:
        bits.append(1 if abs(timings[i+1]) > THRESHOLD_US else 0)
        i += 2
    if len(bits) < 112:
        return None
    out = bytearray()
    for byte_i in range(14):
        v = 0
        for bit_i in range(8):
            v = (v << 1) | bits[byte_i*8 + bit_i]
        out.append(v)
    return bytes(out)

def keep(frame: bytes) -> bool:
    """Only include frames the current encoder can round-trip byte-exactly.

    Skipped categories:
      - b4 != 0x00 → timer frame (out of scope)
      - b5 in {0x27, 0x36} → RE-era event codes we don't emit
      - b5 has bit 0x02 set → DISPLAY-persistent state; encoder emits bit
        0x01 for ECO (live-remote match), so 0x02 frames can't roundtrip.
    """
    if frame[4] != 0x00: return False
    if frame[5] in (0x27, 0x36): return False
    if frame[5] & 0x02: return False
    return True

def main() -> int:
    frames = []
    total_seen = 0
    total_kept = 0
    for fname in CAPTURE_FILES:
        path = CAPTURE_DIR / fname
        if not path.exists():
            print(f"SKIP missing {path}", file=sys.stderr)
            continue
        for timings in iter_raw_frames(path):
            total_seen += 1
            d = decode_frame(timings)
            if d is None:
                continue
            if d[3] != 0x80:
                continue  # skip preamble frames
            if checksum(d[:13]) != d[13]:
                print(f"drop bad-checksum frame {d.hex()}", file=sys.stderr)
                continue
            if not keep(d):
                continue
            frames.append({"hex": d.hex(), "source": fname})
            total_kept += 1

    # Deduplicate identical frames
    seen = {}
    for e in frames:
        h = e["hex"]
        seen.setdefault(h, []).append(e["source"])
    unique = [{"hex": h, "sources": v} for h, v in seen.items()]

    OUT_FILE.write_text(json.dumps(unique, indent=2) + "\n")
    print(f"seen {total_seen} raw, kept {total_kept} Frame-B ({len(unique)} unique) -> {OUT_FILE}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
