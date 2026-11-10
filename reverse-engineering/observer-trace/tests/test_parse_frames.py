#!/usr/bin/env python3
"""Regression tests for the offline ring-observer trace decoder.

Run with::

    python3 -m unittest discover -s tests -v

Standard library only.  The semantics under test are those of
``actual_ring_observer.c``.
"""

import importlib.util
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import parse_frames as pf  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PACKET = os.path.dirname(HERE)
STOCK = os.environ.get("GEAR360_TRACE_FIXTURE", os.path.join(PACKET, "stock-observer.txt"))
LEGACY = os.path.join(PACKET, "legacy_parse_frames.py")


# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------


def body(route, category, attribute, payload=b"", trailer=b"\x00\x00"):
    """A frame body: route, category, attribute, payload length, payload, trailer."""
    return (
        struct.pack("<HHHH", route, category, attribute, len(payload))
        + payload
        + trailer
    )


def frame(route, category, attribute, payload=b"", trailer=b"\x00\x00"):
    """A complete on-wire frame: marker, inner length, body."""
    raw_body = body(route, category, attribute, payload, trailer)
    return pf.OUTER_MARKER + struct.pack("<H", len(raw_body)) + raw_body


def record(time_us, ring, write, read, delta, inner_length, preview):
    return "T %d %s W %d R %d D %d L %d B%s" % (
        time_us,
        ring,
        write,
        read,
        delta,
        inner_length,
        preview.hex(),
    )


def init_record(ring="A", write=100, read=100):
    """The trusted baseline record the source prints once."""
    return "T 0 INIT %s W %d R %d" % (ring, write, read)


def pad(data, length, filler=b"\x00"):
    """Right-pad ``data`` with ``filler`` up to ``length`` bytes."""
    if len(data) >= length:
        return data[:length]
    return data + filler * (length - len(data))


def parse_one(line):
    return pf.parse_trace(line + "\n")


def parse_after_baseline(line, ring="B"):
    """Parse ``line`` as a record whose ring already has a trusted baseline."""
    return pf.parse_trace(init_record(ring) + "\n" + line + "\n")


def codes(trace, line):
    return [d.code for d in trace.diagnostics if d.line == line]


# --------------------------------------------------------------------------
# frame layout and envelope handling
# --------------------------------------------------------------------------


class TestFrameLayout(unittest.TestCase):
    def test_frame_layout_matches_the_observed_bytes(self):
        raw = frame(0x0003, 0x0083, 0x020C, bytes.fromhex("05000000004000aa"))
        self.assertEqual(raw[0:2], b"\xff\x7f")
        inner = struct.unpack("<H", raw[2:4])[0]
        self.assertEqual(inner, 10 + 8)  # 8 header + payload + 2 trailer
        self.assertEqual(len(raw), 4 + inner)

    def test_decode_frame_bytes_round_trip(self):
        raw = frame(0x0300, 0x0082, 0x020A, b"\x01\x02")
        decoded = pf.decode_frame_bytes(raw)
        self.assertEqual(decoded.route, 0x0300)
        self.assertEqual(decoded.category, 0x0082)
        self.assertEqual(decoded.attribute, 0x020A)
        self.assertEqual(decoded.payload, b"\x01\x02")
        self.assertEqual(decoded.trailer, b"\x00\x00")
        self.assertEqual(decoded.declared_inner_length, 12)
        self.assertTrue(decoded.complete)
        self.assertFalse(decoded.uncertain)

    def test_decode_frame_bytes_keeps_exact_route_bytes(self):
        decoded = pf.decode_frame_bytes(frame(0x0300, 0x0082, 0x020A))
        self.assertEqual(decoded.route_bytes, b"\x00\x03")

    def test_decode_frame_bytes_rejects_missing_marker(self):
        with self.assertRaises(pf.FrameDecodeError) as ctx:
            pf.decode_frame_bytes(b"\x03\x00\x83\x00" + b"\x00" * 12)
        self.assertEqual(ctx.exception.reason, "missing outer marker")

    def test_decode_frame_bytes_rejects_truncated_envelope(self):
        with self.assertRaises(pf.FrameDecodeError) as ctx:
            pf.decode_frame_bytes(b"\xff\x7f\x0a")
        self.assertEqual(ctx.exception.reason, "truncated envelope")

    def test_decode_frame_bytes_rejects_malformed_inner_length(self):
        raw = pf.OUTER_MARKER + struct.pack("<H", 4) + b"\x00" * 4
        with self.assertRaises(pf.FrameDecodeError) as ctx:
            pf.decode_frame_bytes(raw)
        self.assertEqual(ctx.exception.reason, "malformed inner length")

    def test_decode_frame_bytes_rejects_truncated_body(self):
        raw = pf.OUTER_MARKER + struct.pack("<H", 20) + b"\x00" * 8
        with self.assertRaises(pf.FrameDecodeError) as ctx:
            pf.decode_frame_bytes(raw)
        self.assertEqual(ctx.exception.reason, "truncated frame body")


class TestTrailerHandling(unittest.TestCase):
    def test_trailer_bytes_are_captured(self):
        raw = frame(0x0003, 0x0083, 0x020C, b"\xaa\xbb", trailer=b"\xde\xad")
        decoded = pf.decode_frame_bytes(raw)
        self.assertEqual(decoded.trailer, b"\xde\xad")
        self.assertEqual(decoded.payload, b"\xaa\xbb")
        self.assertEqual(decoded.declared_inner_length, 12)

    def test_inner_length_without_trailer_is_flagged(self):
        # the envelope declares 10 bytes, but the header claims a 2 byte
        # payload, so the trailer does not fit inside the declared length.
        raw = pf.OUTER_MARKER + struct.pack("<H", 10) + struct.pack(
            "<HHHH", 0x0003, 0x0083, 0x020C, 2
        ) + b"\xaa\xbb"
        decoded = pf.decode_frame_bytes(raw)
        self.assertTrue(decoded.uncertain)
        self.assertTrue(any("length mismatch" in note for note in decoded.notes))
        self.assertTrue(any("truncated trailer" in note for note in decoded.notes))

    def test_trailer_is_part_of_the_inner_length(self):
        raw = frame(0x0003, 0x0083, 0x020C)
        self.assertEqual(struct.unpack("<H", raw[2:4])[0], 10)


# --------------------------------------------------------------------------
# record level parsing
# --------------------------------------------------------------------------


class TestRingBRecognition(unittest.TestCase):
    """The ring label 'B' must not be confused with the preview marker 'B'."""

    def test_ring_b_record_is_decoded(self):
        head = body(0x0003, 0x0083, 0x020C, bytes.fromhex("05000000004000aa"))
        preview = pad(head + b"\x00" * 6, 64)
        trace = parse_after_baseline(
            record(4_134_000, "B", 354, 332, 22, len(head), preview)
        )
        self.assertEqual(len(trace.events), 1)
        event = trace.events[0]
        self.assertEqual(event.ring, "B")
        self.assertEqual(event.category, 0x0083)
        self.assertEqual(event.attribute, 0x020C)
        self.assertEqual(event.payload, bytes.fromhex("05000000004000aa"))

    def test_ring_a_and_ring_b_are_both_kept(self):
        head = body(0x0300, 0x0082, 0x020A)
        preview = pad(head, 64)
        text = "\n".join(
            [
                init_record("A"),
                record(2_814_400, "A", 128, 128, 15, len(head), preview),
                record(2_814_800, "B", 206, 206, 19, len(head), preview),
            ]
        )
        trace = pf.parse_trace(text + "\n")
        # ring A owns the INIT line, so it is trusted; ring B's first dump is
        # still based on an uninitialised last_writes[] entry.
        self.assertEqual(sorted(f.ring for f in trace.events), ["A"])
        self.assertEqual([f.ring for f in trace.initial_candidates], ["B"])
        self.assertEqual(
            sorted(s.ring for s in trace.samples), ["A", "A", "B"]
        )

    def test_legacy_parser_drops_ring_b_records(self):
        """The behaviour this decoder replaces, pinned as a regression."""
        if not os.path.exists(LEGACY):
            self.skipTest("legacy_parse_frames.py not present")
        spec = importlib.util.spec_from_file_location("legacy_parse_frames", LEGACY)
        legacy = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(legacy)

        head = body(0x0003, 0x0083, 0x020C, bytes.fromhex("05000000004000aa"))
        preview = pad(head + b"\x00" * 6, 64)
        line = record(4_134_000, "B", 354, 332, 22, len(head), preview)

        # The legacy parser splits on the first " B", which is the ring label.
        _, _, rest = line.partition(" B")
        self.assertEqual(rest.split()[0], "W")  # not a hex payload

        legacy_frames = []
        for item in line.splitlines():
            if " B" not in item:
                continue
            _, _, hex_string = item.partition(" B")
            hex_string = hex_string.strip()
            if len(hex_string) % 2 or not all(c in "0123456789abcdef" for c in hex_string):
                continue
            legacy_frames.extend(legacy.parse_frames(hex_string))
        self.assertEqual(legacy_frames, [])


class TestScopeModel(unittest.TestCase):
    """baseline -> initial -> current, as the source's have_last implies."""

    def test_init_record_is_the_trusted_baseline(self):
        trace = pf.parse_trace(init_record("A", 113, 113) + "\n")
        self.assertEqual(len(trace.samples), 1)
        sample = trace.samples[0]
        self.assertEqual(sample.kind, "init")
        self.assertEqual(sample.scope, "baseline")
        self.assertEqual(sample.ring, "A")
        self.assertEqual(sample.write_cursor, 113)
        self.assertEqual(sample.read_cursor, 113)
        self.assertIsNone(sample.delta)
        self.assertEqual(trace.frames, [])
        self.assertEqual(trace.events, [])

    def test_first_dump_without_a_baseline_is_initial_scope(self):
        """have_last is global: only the INIT ring gets a trusted baseline."""
        head = body(0x0003, 0x0080, 0x0191)
        preview = pad(head, 64)
        trace = parse_one(record(0, "B", 144, 144, 144, len(head), preview))
        self.assertEqual(trace.samples[0].scope, "initial")
        self.assertEqual(trace.samples[0].published_bytes, 0)
        self.assertEqual(len(trace.initial_candidates), 1)
        self.assertEqual(trace.events, [])
        self.assertTrue(trace.initial_candidates[0].uncertain)
        self.assertIn("uninitialised-baseline", codes(trace, 1))

    def test_initial_scope_is_not_an_observed_event(self):
        head = body(0x0003, 0x0080, 0x0191)
        preview = pad(head, 64)
        trace = parse_one(record(0, "B", 144, 144, 144, len(head), preview))
        for frame in trace.frames:
            self.assertEqual(frame.scope, "initial")
            self.assertNotIn(frame, trace.events)

    def test_a_record_after_a_baseline_is_current_scope(self):
        head = body(0x0003, 0x0083, 0x020C)
        preview = pad(head, 64)
        text = init_record("B") + "\n" + record(1_000, "B", 160, 160, 20, len(head), preview)
        trace = pf.parse_trace(text + "\n")
        self.assertEqual(trace.samples[1].scope, "current")
        self.assertEqual(len(trace.events), 1)
        self.assertEqual(len(trace.initial_candidates), 0)

    def test_the_first_dump_establishes_the_baseline_for_later_records(self):
        head = body(0x0003, 0x0083, 0x020C)
        preview = pad(head, 64)
        text = "\n".join(
            [
                record(0, "B", 144, 144, 144, len(head), preview),
                record(1_000, "B", 160, 160, 20, len(head), preview),
            ]
        )
        trace = pf.parse_trace(text + "\n")
        self.assertEqual(trace.samples[0].scope, "initial")
        self.assertEqual(trace.samples[1].scope, "current")
        self.assertEqual(len(trace.events), 1)


class TestPublishedExtent(unittest.TestCase):
    def test_published_extent_excludes_the_head_envelope(self):
        head = body(0x0003, 0x0083, 0x020C, b"\x05\x00\x00\x00")
        preview = pad(head + b"\x00" * 6, 64)
        trace = parse_after_baseline(
            record(1_000, "B", 100, 100, 22, len(head), preview)
        )
        self.assertEqual(trace.samples[1].published_bytes, 18)  # D - 4

    def test_stale_valid_frame_beyond_extent_is_not_an_event(self):
        """A complete older frame inside the preview is not a new event."""
        head = body(0x0003, 0x0083, 0x020C, b"\x05\x00\x00\x00")
        stale = frame(0x0003, 0x0080, 0x0191)
        preview = pad(pad(head, 32) + stale, 64)
        delta = 4 + len(head)
        trace = parse_after_baseline(
            record(1_000, "B", 100, 100, delta, len(head), preview)
        )
        self.assertEqual(len(trace.events), 1)
        self.assertEqual(trace.events[0].category, 0x0083)
        for event in trace.events:
            self.assertNotEqual((event.category, event.attribute), (0x0080, 0x0191))

        self.assertIn("stale-preview-content", codes(trace, 2))
        stale_diag = [d for d in trace.diagnostics if d.code == "stale-preview-content"]
        self.assertIn("cat=0x0080 attr=0x0191", stale_diag[0].message)

    def test_stale_frame_is_never_emitted_as_a_frame(self):
        head = body(0x0003, 0x0083, 0x020C, b"\x05\x00\x00\x00")
        stale = frame(0x0003, 0x0080, 0x0191)
        preview = pad(pad(head, 32) + stale, 64)
        delta = 4 + len(head)
        trace = parse_after_baseline(
            record(1_000, "B", 100, 100, delta, len(head), preview)
        )
        self.assertEqual(
            [(f.category, f.attribute) for f in trace.frames].count((0x0080, 0x0191)), 0
        )

    def test_preview_limit_caps_the_published_extent(self):
        head = body(0x0003, 0x0080, 0x0191)
        following = frame(0x0003, 0x0081, 0x114A, b"\x01\x00\x4a\x00")
        preview = pad(head + following, 64)
        trace = parse_after_baseline(record(0, "B", 144, 144, 144, len(head), preview))
        self.assertEqual(trace.samples[1].published_bytes, 64)
        self.assertEqual(len(trace.events), 2)


class TestDegenerateRecords(unittest.TestCase):
    """Records the source cannot produce, and one it must not be trusted for."""

    def test_delta_zero_yields_no_frame(self):
        """D=0 means the producer cursor did not advance: no publication."""
        head = body(0x0003, 0x0083, 0x020C)
        preview = pad(head, 64)
        trace = parse_after_baseline(record(1_000, "B", 100, 100, 0, len(head), preview))
        self.assertEqual(trace.frames, [])
        self.assertEqual(trace.events, [])
        self.assertEqual(trace.samples[1].published_bytes, 0)
        self.assertIn("no-publication", codes(trace, 2))

    def test_delta_below_the_envelope_yields_no_frame(self):
        head = body(0x0003, 0x0083, 0x020C)
        preview = pad(head, 64)
        trace = parse_after_baseline(record(1_000, "B", 100, 100, 2, len(head), preview))
        self.assertEqual(trace.frames, [])
        self.assertIn("delta-too-small", codes(trace, 2))

    def test_oversized_preview_is_rejected(self):
        """The observer always dumps exactly 64 bytes; more is impossible."""
        head = body(0x0003, 0x0083, 0x020C)
        preview = pad(head, 100)
        line = record(1_000, "B", 100, 100, 22, len(head), preview)
        with self.assertRaises(pf.TraceFormatError) as ctx:
            pf.parse_trace(line + "\n")
        self.assertIn("exceeds", str(ctx.exception))

    def test_oversized_preview_never_yields_frames_when_lenient(self):
        head = body(0x0003, 0x0083, 0x020C)
        preview = pad(head, 100)
        line = record(1_000, "B", 100, 100, 22, len(head), preview)
        trace = pf.parse_trace(line + "\n", strict=False)
        self.assertEqual(trace.frames, [])
        self.assertEqual(trace.events, [])
        self.assertTrue(any(d.code == "malformed-record" for d in trace.diagnostics))

    def test_preview_of_the_source_width_is_accepted(self):
        head = body(0x0003, 0x0083, 0x020C)
        preview = pad(head, pf.FRAME_BYTES)
        trace = parse_after_baseline(
            record(1_000, "B", 100, 100, 22, len(head), preview)
        )
        self.assertEqual(len(trace.events), 1)


class TestAggregatedDeltas(unittest.TestCase):
    def test_multiple_complete_packets_inside_one_delta(self):
        head = body(0x0003, 0x0083, 0x020C, b"\x07\x00\x00\x00")
        second = frame(0x0003, 0x0083, 0x020C, b"\x08\x00\x00\x00")
        preview = pad(head + second, 64)
        delta = 4 + len(head) + len(second)
        trace = parse_after_baseline(
            record(4_134_800, "B", 3, 376, delta, len(head), preview)
        )
        self.assertEqual(len(trace.events), 2)
        self.assertEqual([e.offset for e in trace.events], [0, len(head)])
        self.assertEqual(trace.events[1].payload, b"\x08\x00\x00\x00")
        self.assertTrue(trace.events[1].envelope_seen)

    def test_only_captured_packets_are_parsed(self):
        """A packet whose body was not published must not be decoded."""
        head = body(0x0003, 0x0083, 0x020C, b"\x07\x00\x00\x00")
        second = frame(0x0003, 0x0083, 0x020C, b"\x08\x00\x00\x00")
        preview = pad(head + second, 64)
        delta = 4 + len(head) + 4  # the second packet's envelope only
        trace = parse_after_baseline(
            record(4_134_800, "B", 3, 376, delta, len(head), preview)
        )
        whole = [f for f in trace.events if f.complete and not f.truncated]
        self.assertEqual(len(whole), 1)
        self.assertEqual(whole[0].payload, b"\x07\x00\x00\x00")
        self.assertIn("truncated-frame", codes(trace, 2))
        self.assertTrue(trace.frames[-1].truncated)
        self.assertFalse(trace.frames[-1].complete)


class TestTruncationAndWrongLengths(unittest.TestCase):
    def test_truncated_header(self):
        preview = pad(b"\x03\x00\x83", 64)
        trace = parse_after_baseline(record(1_000, "B", 10, 10, 9, 10, preview))
        frame_obj = trace.frames[0]
        self.assertTrue(frame_obj.truncated)
        self.assertIsNone(frame_obj.category)
        self.assertIsNone(frame_obj.attribute)
        self.assertTrue(any("truncated header" in note for note in frame_obj.notes))

    def test_truncated_body(self):
        head = body(0x0003, 0x0083, 0x020C, b"\x05\x00\x00\x00\x40\x00\xaa\x00")
        preview = pad(head[:10], 64)
        trace = parse_after_baseline(record(1_000, "B", 10, 10, 14, len(head), preview))
        frame_obj = trace.frames[0]
        self.assertTrue(frame_obj.truncated)
        self.assertEqual(frame_obj.declared_payload_length, 8)
        self.assertEqual(len(frame_obj.payload), 2)
        self.assertTrue(any("truncated payload" in note for note in frame_obj.notes))

    def test_truncated_envelope(self):
        head = body(0x0003, 0x0083, 0x020C)
        preview = pad(head + b"\xff\x7f\x0a", 64)
        delta = 4 + len(head) + 3
        trace = parse_after_baseline(record(1_000, "B", 10, 10, delta, len(head), preview))
        self.assertIn("truncated-envelope", codes(trace, 2))

    def test_wrong_inner_length_is_uncertain(self):
        head = body(0x0003, 0x0083, 0x020C, b"\x05\x00")
        preview = pad(head, 64)
        # the record claims L=14 while the header implies 12
        trace = parse_after_baseline(record(1_000, "B", 10, 10, 16, 14, preview))
        frame_obj = trace.frames[0]
        self.assertTrue(frame_obj.uncertain)
        self.assertEqual(frame_obj.declared_inner_length, 14)
        self.assertEqual(frame_obj.expected_inner_length, 12)
        self.assertTrue(any("length mismatch" in note for note in frame_obj.notes))

    def test_malformed_inner_length_in_a_following_frame(self):
        head = body(0x0003, 0x0083, 0x020C)
        broken = pf.OUTER_MARKER + struct.pack("<H", 3) + b"\x00\x00\x00"
        preview = pad(head + broken, 64)
        delta = 4 + len(head) + len(broken)
        trace = parse_after_baseline(record(1_000, "B", 10, 10, delta, len(head), preview))
        self.assertIn("malformed-inner-length", codes(trace, 2))

    def test_missing_marker_stops_the_scan(self):
        head = body(0x0003, 0x0083, 0x020C)
        preview = pad(head + b"\x00\x00\x00\x00", 64)
        delta = 4 + len(head) + 4
        trace = parse_after_baseline(record(1_000, "B", 10, 10, delta, len(head), preview))
        self.assertIn("missing-marker", codes(trace, 2))


class TestErrorHandling(unittest.TestCase):
    def test_malformed_record_raises(self):
        with self.assertRaises(pf.TraceFormatError) as ctx:
            pf.parse_trace("T 100 B W 1 R 1 D 4 L zz B00\n")
        self.assertEqual(ctx.exception.line, 1)

    def test_malformed_record_is_collected_when_lenient(self):
        trace = pf.parse_trace("T 100 B W 1 R 1 D 4 L zz B00\n", strict=False)
        self.assertEqual(len(trace.samples), 0)
        self.assertTrue(any(d.code == "malformed-record" for d in trace.diagnostics))

    def test_odd_hex_payload_raises(self):
        with self.assertRaises(pf.TraceFormatError):
            pf.parse_trace("T 100 B W 1 R 1 D 4 L 10 B000\n")

    def test_preview_without_delta_raises(self):
        with self.assertRaises(pf.TraceFormatError):
            pf.parse_trace("T 100 B W 1 R 1 L 10 B00\n")

    def test_unknown_record_is_collected_when_lenient(self):
        trace = pf.parse_trace("nonsense\n", strict=False)
        self.assertTrue(any(d.code == "metadata" for d in trace.diagnostics))


class TestMetadataHandling(unittest.TestCase):
    """The observer's headers and the unrelated peek dialect are metadata."""

    def test_map_and_summary_lines_are_metadata(self):
        trace = pf.parse_trace(
            "MAP base=33619968\n"
            + init_record("A")
            + "\nSUMMARY frames=24 dropped=0\n"
        )
        self.assertEqual(len(trace.samples), 1)
        self.assertTrue(any(d.code == "metadata" for d in trace.diagnostics))

    def test_peek_dialect_lines_are_ignored_not_raised(self):
        trace = pf.parse_trace("T 0 D 1 L 58\nT 1000 E has=-1 peek=-1\n")
        self.assertEqual(trace.samples, [])
        self.assertEqual(trace.frames, [])
        self.assertTrue(any(d.code == "metadata" for d in trace.diagnostics))


class TestRouteDirection(unittest.TestCase):
    def test_raw_route_bytes_map_to_the_fixture_directions(self):
        self.assertEqual(pf.route_direction(b"\x03\x00"), "linux-to-rtos")
        self.assertEqual(pf.route_direction(b"\x00\x03"), "reverse")
        self.assertIsNone(pf.route_direction(b"\x01\x02"))

    def test_frame_exposes_its_direction(self):
        decoded = pf.decode_frame_bytes(frame(0x0003, 0x0083, 0x020C))
        self.assertEqual(decoded.route_bytes, b"\x03\x00")
        self.assertEqual(decoded.route, 0x0003)
        self.assertEqual(decoded.direction, "linux-to-rtos")

        decoded = pf.decode_frame_bytes(frame(0x0300, 0x0082, 0x020A))
        self.assertEqual(decoded.route_bytes, b"\x00\x03")
        self.assertEqual(decoded.route, 0x0300)
        self.assertEqual(decoded.direction, "reverse")


# --------------------------------------------------------------------------
# the stock fixture
# --------------------------------------------------------------------------


@unittest.skipUnless(os.path.exists(STOCK), "stock-observer.txt not present")
class TestStockFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.trace = pf.parse_trace_file(STOCK)

    def test_all_records_are_recognised(self):
        dumps = [s for s in self.trace.samples if s.preview]
        self.assertEqual(len(self.trace.samples), 25)  # 24 payload records + 1 INIT
        self.assertEqual(len(dumps), 24)

    def test_both_rings_are_decoded(self):
        self.assertEqual({s.ring for s in self.trace.samples}, {"A", "B"})
        self.assertEqual({f.ring for f in self.trace.events}, {"A", "B"})

    def test_delta_matches_the_write_cursor_advance(self):
        """D is the producer cursor delta, modulo the ring capacity."""
        last = {}
        for sample in self.trace.samples:
            if sample.delta is None:
                continue
            if sample.ring in last and sample.scope == "current":
                delta = (sample.write_cursor - last[sample.ring]) % pf.RING_CAPACITY
                self.assertEqual(delta, sample.delta, "line %d" % sample.line)
            # the source stores the cursor it just reported, so the next
            # record of that ring is measured against it
            last[sample.ring] = sample.write_cursor

    def test_the_initial_record_breaks_the_delta_invariant(self):
        """The T0 ring B record is computed from an uninitialised baseline."""
        initial = [s for s in self.trace.samples if s.scope == "initial"]
        self.assertEqual(len(initial), 1)
        self.assertEqual(initial[0].ring, "B")
        self.assertEqual(initial[0].published_bytes, 0)
        self.assertIn("uninitialised-baseline", codes(self.trace, initial[0].line))

    def test_ring_capacity_is_the_source_value(self):
        self.assertEqual(pf.RING_CAPACITY, 395)

    def test_inner_length_matches_the_head_frame_of_every_record(self):
        """L is read from the packet's bytes 2..3, so it always agrees."""
        for sample in self.trace.samples:
            if not sample.preview:
                continue
            paylen = struct.unpack("<H", sample.preview[6:8])[0]
            self.assertEqual(sample.inner_length, 10 + paylen, "line %d" % sample.line)

    def test_preview_is_the_source_width(self):
        for sample in self.trace.samples:
            if sample.preview:
                self.assertEqual(len(sample.preview), pf.FRAME_BYTES, "line %d" % sample.line)

    def test_event_at_4_134_000_us(self):
        """The parent's expected event, verified against the bytes."""
        events = [f for f in self.trace.events if f.time_us == 4_134_000]
        self.assertEqual(len(events), 1)
        event = events[0]
        self.assertEqual(event.ring, "B")
        self.assertEqual(event.scope, "current")
        self.assertEqual(event.route_bytes, b"\x03\x00")
        self.assertEqual(event.route, 0x0003)
        self.assertEqual(event.direction, "linux-to-rtos")
        self.assertEqual(event.category, 0x0083)
        self.assertEqual(event.attribute, 0x020C)
        self.assertEqual(event.payload, bytes.fromhex("05000000004000aa"))
        self.assertEqual(event.trailer, b"\x00\x00")
        self.assertEqual(event.declared_inner_length, 18)
        self.assertTrue(event.complete)
        self.assertFalse(event.truncated)

    def test_the_published_run_is_exactly_that_frame(self):
        sample = [s for s in self.trace.samples if s.time_us == 4_134_000][0]
        self.assertEqual(sample.delta, 22)
        self.assertEqual(sample.published_bytes, 18)  # D - 4
        self.assertEqual(sample.published_bytes, sample.inner_length)

    def test_purported_request_is_not_an_event(self):
        """cat=0x0080 attr=0x0191 must not be reported as a 4.134 s event."""
        for event in self.trace.events:
            self.assertNotEqual(
                (event.category, event.attribute),
                (0x0080, 0x0191),
                "stale frame leaked into the event list",
            )
        candidates = [
            f
            for f in self.trace.initial_candidates
            if (f.category, f.attribute) == (0x0080, 0x0191)
        ]
        self.assertEqual(len(candidates), 1)
        self.assertTrue(candidates[0].uncertain)

    def test_stale_frame_in_the_4_134_800_preview_is_quarantined(self):
        sample = [s for s in self.trace.samples if s.time_us == 4_134_800][0]
        self.assertEqual(sample.published_bytes, 40)
        stale = [
            d
            for d in self.trace.diagnostics
            if d.code == "stale-preview-content" and d.line == sample.line
        ]
        self.assertEqual(len(stale), 1)
        self.assertIn("cat=0x0080 attr=0x0191", stale[0].message)

    def test_scope_split(self):
        self.assertEqual(len(self.trace.initial_candidates), 5)
        self.assertEqual(len(self.trace.events), 25)
        self.assertTrue(all(f.scope == "initial" for f in self.trace.initial_candidates))
        self.assertTrue(all(f.scope == "current" for f in self.trace.events))
        self.assertTrue(all(f.uncertain for f in self.trace.initial_candidates))

    def test_truncated_frame_in_the_initial_record(self):
        truncated = [f for f in self.trace.initial_candidates if f.truncated]
        self.assertEqual(len(truncated), 1)
        self.assertEqual(truncated[0].declared_inner_length, 30)

    def test_route_bytes_are_kept_exactly(self):
        routes = {f.route_bytes for f in self.trace.frames if f.route_bytes}
        self.assertEqual(routes, {b"\x03\x00", b"\x00\x03"})
        directions = {f.route_bytes: f.direction for f in self.trace.frames if f.route_bytes}
        self.assertEqual(directions[b"\x03\x00"], "linux-to-rtos")
        self.assertEqual(directions[b"\x00\x03"], "reverse")

    def test_ring_a_frames_are_the_reverse_direction(self):
        ring_a = {f.route_bytes for f in self.trace.events if f.ring == "A"}
        self.assertEqual(ring_a, {b"\x00\x03"})

    def test_renderers_do_not_crash(self):
        self.assertIn("events:", pf.render_text(self.trace))
        self.assertIn("\"frames\"", pf.render_json(self.trace))


class TestSnapshotUncertainty(unittest.TestCase):
    def test_complete_head_and_following_frame_remain_uncertain(self):
        head = body(0x0003, 0x0083, 0x020C)
        following = frame(0x0003, 0x0083, 0x020C)
        preview = pad(head + following, 64)
        trace = parse_after_baseline(
            record(1000, "B", 100, 100, 4 + len(head) + len(following), len(head), preview)
        )
        self.assertEqual(len(trace.events), 2)
        self.assertTrue(all(event.complete for event in trace.events))
        self.assertTrue(all(event.uncertain for event in trace.events))


if __name__ == "__main__":
    unittest.main()
