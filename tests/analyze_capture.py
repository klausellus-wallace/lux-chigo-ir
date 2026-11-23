#!/usr/bin/env python3
"""Group captured IR frames into button presses and classify labeled buttons.

Analyze a log:
  python3 tests/analyze_capture.py LOG [--labels LABELS.csv]

Record labels while capturing (press the button, then Enter):
  python3 tests/analyze_capture.py --record-labels PLAN.txt OUT.csv [--utc]

See docs/ir-analysis.md for the full workflow.
"""
import argparse
import csv
import re
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, List, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_fixtures import bit_reverse, decode_frame  # noqa: E402

FRAME_A_MARKER = 0x40
FRAME_B_MARKER = 0x80
FRAME_A_CHECKSUM_OFFSET = 0x0f
MAX_A_TO_B_GAP_S = 1.0
LABEL_GRACE_S = 0.5     # A press can be logged shortly after Enter.
LABEL_MAX_WINDOW_S = 15.0

RAW_LINE = re.compile(r'\[([^\]]+)\]\[.\]\[remote\.raw:\d+\]:\s*(.*)')


@dataclass
class Frame:
    ts: str
    data: bytes


@dataclass
class Press:
    ts: str
    a: Optional[bytes] = None
    b: Optional[bytes] = None

    @property
    def a_ok(self) -> bool:
        return self.a is not None and _checksum(self.a, FRAME_A_CHECKSUM_OFFSET) == self.a[13]

    @property
    def b_ok(self) -> bool:
        return self.b is not None and _checksum(self.b, 0) == self.b[13]


@dataclass
class Label:
    ts: str
    button: str
    note: str


@dataclass
class Match:
    label: Label
    status: str
    presses: List[Press] = field(default_factory=list)


def _checksum(frame: bytes, offset: int) -> int:
    return bit_reverse((sum(bit_reverse(b) for b in frame[:13]) + offset) & 0xff)


def _seconds(ts: str) -> float:
    hours, minutes, seconds = ts.split(":")
    return int(hours) * 3600 + int(minutes) * 60 + float(seconds)


def parse_log(lines: Iterable[str]) -> List[Frame]:
    """Return every decodable IR frame, including frames with bad checksums."""
    entries = []
    for line in lines:
        match = RAW_LINE.match(line)
        if not match:
            continue
        payload = match.group(2)
        if payload.startswith("Received Raw:"):
            entries.append((match.group(1), []))
            payload = payload[len("Received Raw:"):]
        if entries:
            entries[-1][1].extend(int(x) for x in re.findall(r'-?\d+', payload))

    frames = []
    for ts, timings in entries:
        data = decode_frame(timings)
        if data is not None:
            frames.append(Frame(ts, data))
    return frames


def group_presses(frames: Iterable[Frame]) -> List[Press]:
    """Pair each Frame A with the Frame B that follows it."""
    presses: List[Press] = []
    for frame in frames:
        marker = frame.data[3]
        if marker == FRAME_A_MARKER:
            presses.append(Press(frame.ts, a=frame.data))
        elif marker == FRAME_B_MARKER:
            last = presses[-1] if presses else None
            if (last is not None and last.a is not None and last.b is None
                    and _seconds(frame.ts) - _seconds(last.ts) <= MAX_A_TO_B_GAP_S):
                last.b = frame.data
            else:
                presses.append(Press(frame.ts, b=frame.data))
    return presses


def changed_bits(previous: Press, current: Press):
    """Return (frame, byte, mask, old, new) for valid frames present in both."""
    changes = []
    pairs = (("A", previous.a, current.a, previous.a_ok and current.a_ok),
             ("B", previous.b, current.b, previous.b_ok and current.b_ok))
    for name, old, new, valid in pairs:
        if not valid:
            continue
        for index, (old_byte, new_byte) in enumerate(zip(old, new)):
            if old_byte != new_byte:
                changes.append((name, index, old_byte ^ new_byte, old_byte, new_byte))
    return changes


def match_labels(presses: List[Press], labels: List[Label]) -> List[Match]:
    """Assign presses to the label whose time window contains them."""
    matches = []
    lower = float("-inf")
    for label in labels:
        at = _seconds(label.ts)
        upper = at + LABEL_GRACE_S
        start = max(lower, at - LABEL_MAX_WINDOW_S)
        found = [p for p in presses if start < _seconds(p.ts) <= upper]
        status = "NO_IR" if not found else "OK" if len(found) == 1 else "MULTI"
        matches.append(Match(label, status, found))
        lower = upper
    return matches


def unlabeled(presses: List[Press], matches: List[Match]) -> List[Press]:
    assigned = {id(p) for m in matches for p in m.presses}
    return [p for p in presses if id(p) not in assigned]


def read_labels(path: Path) -> List[Label]:
    with path.open(newline="") as handle:
        return [Label(row["time"], row["button"], row.get("note") or "")
                for row in csv.DictReader(handle)]


def _diffs_from_last_valid(presses: List[Press]):
    """Map each press to its bit changes against the last valid A and B."""
    diffs = {}
    base = Press("")
    for press in presses:
        diffs[id(press)] = changed_bits(base, press)
        if press.a_ok:
            base.a = press.a
        if press.b_ok:
            base.b = press.b
    return diffs


def _format_changes(changes) -> str:
    shown = [f"{name}.b{index} {old:02x}->{new:02x} (mask {mask:02x})"
             for name, index, mask, old, new in changes if index != 13]
    return "; ".join(shown) or "no payload change"


def _format_press(press: Press, changes) -> str:
    flags = []
    if press.a is None:
        flags.append("A missing")
    elif not press.a_ok:
        flags.append("A BADCSUM")
    if press.b is None:
        flags.append("B missing")
    elif not press.b_ok:
        flags.append("B BADCSUM")
    suffix = f" [{', '.join(flags)}]" if flags else ""
    return f"{press.ts}  {_format_changes(changes)}{suffix}"


def report(presses: List[Press], labels: Optional[List[Label]]) -> None:
    diffs = _diffs_from_last_valid(presses)
    if labels is None:
        for press in presses:
            print(_format_press(press, diffs[id(press)]))
        print(f"\npresses: {len(presses)}")
        return

    matches = match_labels(presses, labels)
    for match in matches:
        note = f"  note: {match.label.note}" if match.label.note else ""
        if not match.presses:
            print(f"{match.label.button:<10} {match.status:<6} at {match.label.ts}{note}")
        for press in match.presses:
            print(f"{match.label.button:<10} {match.status:<6} "
                  f"{_format_press(press, diffs[id(press)])}{note}")

    extra = unlabeled(presses, matches)
    counts = {s: sum(m.status == s for m in matches) for s in ("OK", "NO_IR", "MULTI")}
    print(f"\nlabels: {len(matches)} {counts} unlabeled presses: {len(extra)}")
    for press in extra:
        print(f"UNLABELED  {_format_press(press, diffs[id(press)])}")


def record_labels(plan: Path, out: Path, use_utc: bool) -> None:
    buttons = [line.strip() for line in plan.read_text().splitlines()
               if line.strip() and not line.lstrip().startswith("#")]
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("x", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["time", "button", "note"])
        handle.flush()
        for number, button in enumerate(buttons, 1):
            note = input(f"[{number}/{len(buttons)}] Press {button}, "
                         "then Enter (optional: type what the AC did): ")
            now = datetime.now(timezone.utc) if use_utc else datetime.now()
            writer.writerow([now.strftime("%H:%M:%S.%f")[:-3], button, note.strip()])
            handle.flush()
    print(f"saved {out}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", nargs="?", type=Path)
    parser.add_argument("--labels", type=Path)
    parser.add_argument("--record-labels", nargs=2, type=Path, metavar=("PLAN", "OUT"))
    parser.add_argument("--utc", action="store_true", help="record label times in UTC")
    args = parser.parse_args()

    if args.record_labels:
        record_labels(args.record_labels[0], args.record_labels[1], args.utc)
        return 0
    if args.log is None:
        parser.error("LOG is required unless --record-labels is used")

    with args.log.open() as log:
        presses = group_presses(parse_log(log))
    report(presses, read_labels(args.labels) if args.labels else None)
    return 0


if __name__ == "__main__":
    sys.exit(main())
