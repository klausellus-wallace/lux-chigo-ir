#!/usr/bin/env python3
"""Tests for analyze_capture.py. Run: python3 tests/test_analyze_capture.py"""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import analyze_capture as ac  # noqa: E402

A_OFF = "c4d36440000200000100000000a7"
A_ON = "c4d36440000600000100000000a0"
B_ON = "c4d364800024c0e00000000001c3"
B_OFF = "c4d364800026c0e00000000001c0"
B_BAD = "c4d364801026c0e00000000001c0"


def raw_line(ts, hex_frame):
    timings = [3140, -1565]
    for byte in bytes.fromhex(hex_frame):
        for bit in range(7, -1, -1):
            timings += [520, -1065 if byte >> bit & 1 else -250]
    timings.append(520)
    body = ", ".join(str(t) for t in timings)
    return f"[{ts}][I][remote.raw:026]: Received Raw: {body}\n"


class FrameFixtures(unittest.TestCase):
    def test_literals_are_14_bytes(self):
        for frame in (A_OFF, A_ON, B_ON, B_OFF, B_BAD):
            self.assertEqual(len(bytes.fromhex(frame)), 14)


class GroupPresses(unittest.TestCase):
    def test_pairs_frame_a_with_following_frame_b(self):
        lines = [raw_line("10:00:00.000", A_ON), raw_line("10:00:00.200", B_OFF)]
        presses = ac.group_presses(ac.parse_log(lines))
        self.assertEqual(len(presses), 1)
        self.assertEqual(presses[0].a[5], 0x06)
        self.assertEqual(presses[0].b[5], 0x26)
        self.assertTrue(presses[0].a_ok)
        self.assertTrue(presses[0].b_ok)

    def test_distant_frames_are_separate_presses(self):
        lines = [raw_line("10:00:00.000", A_OFF), raw_line("10:00:03.000", B_ON)]
        presses = ac.group_presses(ac.parse_log(lines))
        self.assertEqual(len(presses), 2)
        self.assertIsNone(presses[0].b)
        self.assertIsNone(presses[1].a)

    def test_flags_bad_frame_b_checksum(self):
        lines = [raw_line("10:00:00.000", A_OFF), raw_line("10:00:00.200", B_BAD)]
        press = ac.group_presses(ac.parse_log(lines))[0]
        self.assertTrue(press.a_ok)
        self.assertFalse(press.b_ok)

    def test_ignores_non_ir_log_lines(self):
        lines = ["[10:00:00.000][W][component:513]: took a long time\n",
                 raw_line("10:00:01.000", B_ON)]
        self.assertEqual(len(ac.group_presses(ac.parse_log(lines))), 1)


class ChangedBits(unittest.TestCase):
    def test_reports_frame_byte_and_mask(self):
        lines = [raw_line("10:00:00.000", A_OFF), raw_line("10:00:00.200", B_ON),
                 raw_line("10:00:03.000", A_ON), raw_line("10:00:03.200", B_OFF)]
        first, second = ac.group_presses(ac.parse_log(lines))
        self.assertEqual(ac.changed_bits(first, second),
                         [("A", 5, 0x04, 0x02, 0x06), ("A", 13, 0x07, 0xa7, 0xa0),
                          ("B", 5, 0x02, 0x24, 0x26), ("B", 13, 0x03, 0xc3, 0xc0)])

    def test_corrupt_frame_is_not_diffed(self):
        lines = [raw_line("10:00:00.000", B_ON), raw_line("10:00:03.000", B_BAD)]
        first, second = ac.group_presses(ac.parse_log(lines))
        self.assertEqual(ac.changed_bits(first, second), [])


class MatchLabels(unittest.TestCase):
    def setUp(self):
        lines = [raw_line("10:00:01.000", B_ON), raw_line("10:00:11.000", B_OFF),
                 raw_line("10:00:13.000", B_ON)]
        self.presses = ac.group_presses(ac.parse_log(lines))

    def test_classifies_one_none_and_multiple_presses(self):
        labels = [ac.Label("10:00:02.000", "POWER", ""),
                  ac.Label("10:00:05.000", "MUTE", ""),
                  ac.Label("10:00:14.000", "DISPLAY", "")]
        got = [(m.label.button, m.status, len(m.presses))
               for m in ac.match_labels(self.presses, labels)]
        self.assertEqual(got, [("POWER", "OK", 1), ("MUTE", "NO_IR", 0),
                               ("DISPLAY", "MULTI", 2)])

    def test_press_after_last_label_is_not_assigned(self):
        labels = [ac.Label("10:00:02.000", "POWER", "")]
        matches = ac.match_labels(self.presses, labels)
        self.assertEqual(len(matches[0].presses), 1)
        self.assertEqual(len(ac.unlabeled(self.presses, matches)), 2)


class RealCaptures(unittest.TestCase):
    def test_capture2_contains_the_single_mute_candidate(self):
        with (HERE / "captures" / "capture2.log").open() as log:
            presses = ac.group_presses(ac.parse_log(log))
        self.assertEqual(len(presses), 15)
        muted = [p.ts for p in presses if p.a_ok and p.a[5] == 0x06]
        self.assertEqual(len(muted), 1)
        self.assertTrue(muted[0].startswith("20:45:47"))


class Cli(unittest.TestCase):
    def test_reports_no_ir_for_labeled_silent_button(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "session.log"
            log.write_text(raw_line("10:00:01.000", B_ON))
            labels = Path(tmp) / "session.csv"
            labels.write_text("time,button,note\n10:00:02.000,POWER,\n10:00:05.000,MUTE,\n")
            out = subprocess.run(
                [sys.executable, str(HERE / "analyze_capture.py"), str(log),
                 "--labels", str(labels)],
                capture_output=True, text=True, check=True).stdout
        self.assertRegex(out, r"MUTE\s+NO_IR")
        self.assertRegex(out, r"POWER\s+OK")


if __name__ == "__main__":
    unittest.main()
