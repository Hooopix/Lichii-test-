#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests trace_replay.py's PNG reader and frame comparison."""

import math
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import trace_replay  # noqa: E402


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def write_png(path, width, height, pixels, filters):
    """RGBA PNG whose rows use the given filter types (1-4 encoded properly, 0 raw)."""
    stride = width * 4
    raw = b""
    previous = bytes(stride)
    for y in range(height):
        line = pixels[y * stride:(y + 1) * stride]
        kind = filters[y % len(filters)]
        encoded = bytearray(stride)
        for i in range(stride):
            left = line[i - 4] if i >= 4 else 0
            up = previous[i]
            corner = previous[i - 4] if i >= 4 else 0
            if kind == 0:
                predictor = 0
            elif kind == 1:
                predictor = left
            elif kind == 2:
                predictor = up
            elif kind == 3:
                predictor = (left + up) >> 1
            else:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                predictor = left if pa <= pb and pa <= pc else up if pb <= pc else corner
            encoded[i] = (line[i] - predictor) & 0xFF
        raw += bytes([kind]) + bytes(encoded)
        previous = line
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                           chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


class TraceReplayTest(unittest.TestCase):
    def test_png_filters_round_trip(self):
        width, height = 7, 5
        pixels = bytes((x * 37 + y * 11 + c * 5) & 0xFF
                       for y in range(height) for x in range(width) for c in range(4))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "frame.png"
            write_png(path, width, height, pixels, [0, 1, 2, 3, 4])
            self.assertEqual(trace_replay.read_png(path), (width, height, pixels))

    def test_difference(self):
        a = (2, 1, bytes([0, 0, 0, 255, 10, 10, 10, 255]))
        self.assertEqual(trace_replay.difference(a, a)["psnr"], math.inf)
        b = (2, 1, bytes([0, 0, 0, 255, 10, 10, 14, 255]))
        result = trace_replay.difference(a, b)
        self.assertEqual(result["max"], 4)
        self.assertAlmostEqual(result["mean"], 0.5)
        self.assertTrue(trace_replay.difference(a, (1, 1, bytes(4)))["size_differs"])

    def test_device_class(self):
        self.assertEqual(trace_replay.device_class("llvmpipe"), "lavapipe")
        self.assertEqual(trace_replay.device_class("NVIDIA GeForce RTX 4090"), "nvidia")

    def test_compare_runs_marks_differing_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            base, head = Path(directory) / "base", Path(directory) / "head"
            base.mkdir()
            head.mkdir()
            write_png(base / "frame-0000.png", 1, 1, bytes([1, 2, 3, 255]), [0])
            write_png(head / "frame-0000.png", 1, 1, bytes([1, 2, 9, 255]), [0])
            frames = {"frames": [{"index": 0, "file": "frame-0000.png", "hash": "a"}]}
            changed = {"frames": [{"index": 0, "file": "frame-0000.png", "hash": "b"}]}
            rows = trace_replay.compare_runs(base, frames, head, changed)
            self.assertFalse(rows[0]["equal"])
            self.assertEqual(rows[0]["difference"]["max"], 6)
            self.assertTrue(trace_replay.compare_runs(base, frames, base, frames)[0]["equal"])

    def test_write_html_escapes_and_rejects_unsafe_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            rows = [{
                "index": "0<script>alert(1)</script>",
                "base": {"file": "frame '0'.png", "hash": "<b>h</b>"},
                "head": None,
                "equal": False,
                "difference": {"max": 1},
            }]
            trace_replay.write_html(out, rows, "<base>", "<head>")
            page = (out / "index.html").read_text(encoding="utf-8")
            self.assertNotIn("<script>", page)
            self.assertNotIn("<b>", page)
            self.assertIn("&lt;script&gt;", page)
            self.assertIn("&lt;b&gt;h&lt;/b&gt;", page)
            self.assertIn("src='base/frame%20%270%27.png'", page)

            for bad in ("../evil.png", "/abs/evil.png", "C:/evil.png", "sub/../../evil.png"):
                with self.subTest(bad=bad):
                    bad_rows = [{"index": 0, "base": {"file": bad, "hash": "a"}, "head": None,
                                 "equal": False}]
                    with self.assertRaises(ValueError):
                        trace_replay.write_html(out, bad_rows, "base", "head")

    def test_passthrough_flags_forwarded_to_replay_and_compare(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            calls = []

            def fake_replay(cli, trace, out_dir, extra=(), environment=None):
                Path(out_dir).mkdir(parents=True, exist_ok=True)
                calls.append((cli, trace, Path(out_dir), list(extra)))
                return {"frames": [{"index": 0, "file": "frame-0000.png", "hash": "aaaa", "ms": 1.0}]}

            with patch.object(trace_replay, "replay", side_effect=fake_replay):
                rc = trace_replay.main([
                    "replay", "--cli", "hoochii-cli", "t.lxtrace", "--out", str(out / "r"),
                    "--frames", "10", "--device", "1", "--gpu-accuracy", "low",
                    "--render-thread", "off",
                ])
                self.assertEqual(rc, 0)
                self.assertEqual(calls[-1][3], [
                    "--frames", "10", "--device", "1", "--gpu-accuracy", "low",
                    "--render-thread", "off",
                ])

                calls.clear()
                rc = trace_replay.main([
                    "compare", "--base", "base-cli", "--head", "head-cli", "t.lxtrace",
                    "--out", str(out / "c"), "--frames", "5", "--device", "0",
                    "--gpu-accuracy", "high", "--render-thread", "on",
                ])
                self.assertEqual(rc, 0)
                self.assertEqual(len(calls), 2)
                for call in calls:
                    self.assertEqual(call[3], [
                        "--frames", "5", "--device", "0", "--gpu-accuracy", "high",
                        "--render-thread", "on",
                    ])


if __name__ == "__main__":
    unittest.main()
