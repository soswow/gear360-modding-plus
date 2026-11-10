#!/usr/bin/env python3
"""Offline decoder for Gear 360 ring-observer traces.

The trace dialect is the one emitted by ``actual_ring_observer.c`` (the stock
ring observer).  That source is the authority for every semantic in this
module; the notes below quote its behaviour.

Record shape
------------

::

    T <time_us> INIT <ring> W <write_cursor> R <read_cursor>
    T <time_us> <ring> W <write_cursor> R <read_cursor> D <delta> L <inner_len> B<hex>

``INIT`` is printed once, for the first ring of the first iteration
(``have_last`` starts at 0).  Every other record is a dump.

What the observer actually reads
--------------------------------

* ``delta`` is the producer cursor's advance since that ring's previous
  observation: ``(write_cursor + RING_CAPACITY - last_write) % RING_CAPACITY``
  with ``RING_CAPACITY == 395``.
* The packet just written starts at the *old* producer cursor.  Its bytes are
  ``[marker ff 7f][inner_len u16 LE][body...]``; the observer reads
  ``inner_len`` from the packet's bytes 2..3.
* The dumped bytes start at the packet's byte 4 - the body - and are exactly
  ``FRAME_BYTES == 64`` bytes: the marker and the inner length are *not* in
  the dump.

So of the ``delta`` published bytes only ``delta - 4`` are visible in the
dump, and the published extent of a dump is::

    published extent = min(len(preview), delta - 4)

The remaining preview bytes are ring memory at or past the producer cursor:
older ring content, or bytes published after the sampled cursor.  They are never reported as observed frames.

Frame layout
------------

::

    ff 7f | inner_len u16 LE | route u16 LE | category u16 LE | attribute u16 LE |
    payload_len u16 LE | payload | trailer[2]

    inner_len  == 8 + payload_len + 2        on-wire extent == 4 + inner_len

The two trailer bytes close the frame body; ``route`` is kept as raw bytes as
well as a value so that ``03 00`` and ``00 03`` stay distinguishable.

Trust boundaries
----------------

* ``have_last`` is a *single* global flag and ``last_writes[]`` is only
  initialised for the ring that printed the ``INIT`` line.  The first dump
  record of every other ring is therefore computed from an uninitialised
  baseline: its ``delta``, its ``inner_len`` and its byte range are not
  meaningful.  Such records are marked scope ``"initial"`` and are never
  events.
* The observer reads the ring live, one byte at a time, with no copy and no
  marker check.  A dump is not an atomic snapshot, so every current record
  carries snapshot uncertainty regardless of how well its bytes parse.
* ``delta`` is computed modulo the ring capacity, so it is only a faithful
  byte count while the producer advanced less than one full lap between two
  observations of the same ring.

This module never touches the camera, the device or the network, and it
depends on nothing outside the Python standard library.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple

__all__ = [
    "TraceError",
    "TraceFormatError",
    "FrameDecodeError",
    "Diagnostic",
    "Sample",
    "Frame",
    "Trace",
    "parse_trace",
    "parse_trace_file",
    "decode_frame_bytes",
    "route_direction",
    "render_text",
    "render_json",
    "OUTER_MARKER",
    "ENVELOPE_SIZE",
    "BODY_HEADER_SIZE",
    "TRAILER_SIZE",
    "MIN_INNER_LENGTH",
    "FRAME_BYTES",
    "RING_CAPACITY",
    "ROUTE_DIRECTIONS",
]

# --------------------------------------------------------------------------
# constants (values taken from actual_ring_observer.c)
# --------------------------------------------------------------------------

#: Outer frame marker in front of every packet.
OUTER_MARKER = b"\xff\x7f"

#: marker (2) + inner length (2); these four bytes precede the dumped body.
ENVELOPE_SIZE = 4

#: route (2) + category (2) + attribute (2) + payload length (2)
BODY_HEADER_SIZE = 8

#: Two trailer bytes close every frame body.
TRAILER_SIZE = 2

#: Smallest legal inner length: empty payload plus the trailer.
MIN_INNER_LENGTH = BODY_HEADER_SIZE + TRAILER_SIZE

#: The observer always dumps exactly this many bytes ("#define FRAME_BYTES 64u").
FRAME_BYTES = 64

#: Ring capacity used for the cursor arithmetic ("#define RING_CAPACITY 395u").
RING_CAPACITY = 395

#: Direction denoted by a route's raw bytes.  Provenance: the source's ring
#: description ("Ring A is the RTOS-to-Linux control ring the producer fills;
#: ring B is the opposite direction of the same channel pair") together with
#: the packet annotation.  The decoder keeps the raw bytes regardless; this
#: table only names what the fixture already binds.
ROUTE_DIRECTIONS = {
    b"\x03\x00": "linux-to-rtos",  # u16 LE 0x0003, seen in ring B records
    b"\x00\x03": "reverse",  # u16 LE 0x0300, seen in ring A records
}

_HEX_RE = re.compile(r"\A[0-9a-fA-F]*\Z")
_RECORD_RE = re.compile(
    r"\A(?:(?P<init>INIT)\s+)?(?P<ring>[A-Za-z])\s+(?P<rest>.*)\Z",
    re.DOTALL,
)


class TraceError(Exception):
    """Base class for every error raised by this module."""


class TraceFormatError(TraceError):
    """A trace record could not be parsed.

    The offending line number and the raw text are kept as attributes so a
    caller can point at the exact record.
    """

    def __init__(self, message: str, line: Optional[int] = None, text: Optional[str] = None):
        self.line = line
        self.text = text
        if line is not None:
            message = "line %d: %s" % (line, message)
        super().__init__(message)


class FrameDecodeError(TraceError):
    """Frame bytes could not be decoded into a frame.

    ``reason`` carries a stable machine readable cause, ``offset`` the byte
    offset inside the buffer that was handed in.
    """

    def __init__(self, reason: str, offset: int = 0, detail: str = ""):
        self.reason = reason
        self.offset = offset
        message = "%s at offset %d" % (reason, offset)
        if detail:
            message += ": " + detail
        super().__init__(message)


# --------------------------------------------------------------------------
# result model
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class Diagnostic:
    """A non fatal observation about the trace or about a decoded frame."""

    line: int
    level: str  # "info" | "warning" | "error"
    code: str
    message: str

    def __str__(self) -> str:  # pragma: no cover - trivial
        return "L%d %-7s %-24s %s" % (self.line, self.level, self.code, self.message)


@dataclass
class Sample:
    """One trace record.

    ``scope`` is:

    ``"baseline"``
        The single ``INIT`` record.  It establishes a trusted cursor baseline
        for its ring and carries no bytes.
    ``"initial"``
        The first dump record of a ring that had no trusted baseline yet.  The
        source computed it from an uninitialised ``last_writes[]`` entry, so
        its delta, inner length and byte range are not meaningful.  Its
        frames are candidates, never events.
    ``"current"``
        A dump record computed from a trusted baseline.  Its published extent
        is ``min(len(preview), delta - 4)``.

    ``published_bytes`` is the number of preview bytes that are justified as
    published; it is 0 when the extent is unknown.
    """

    line: int
    time_us: int
    ring: str
    kind: str  # "init" | "dump"
    write_cursor: Optional[int] = None
    read_cursor: Optional[int] = None
    delta: Optional[int] = None
    inner_length: Optional[int] = None
    preview: bytes = b""
    scope: str = "current"
    published_bytes: int = 0

    @property
    def time_s(self) -> float:
        return self.time_us / 1_000_000.0


@dataclass
class Frame:
    """A decoded frame.

    ``scope`` is ``"initial"`` (candidate from an untrusted baseline) or
    ``"current"`` (observed during the session).

    ``complete`` means only that the frame's bytes are all present in the
    preview according to the wire format.  It says nothing about the producer
    having finished the packet or about any DMA transfer having completed;
    the observer reads live ring memory.
    """

    line: int
    time_us: int
    ring: str
    scope: str
    offset: int
    envelope_seen: bool
    route_bytes: bytes
    route: Optional[int]
    category: Optional[int]
    attribute: Optional[int]
    declared_payload_length: Optional[int]
    payload: bytes
    trailer: bytes
    declared_inner_length: Optional[int]
    expected_inner_length: Optional[int]
    complete: bool
    truncated: bool
    uncertain: bool
    notes: List[str] = field(default_factory=list)

    @property
    def direction(self) -> Optional[str]:
        """Direction the route denotes, or None if the route is unknown."""
        return route_direction(self.route_bytes)


@dataclass
class Trace:
    """The decoded trace."""

    samples: List[Sample] = field(default_factory=list)
    frames: List[Frame] = field(default_factory=list)
    diagnostics: List[Diagnostic] = field(default_factory=list)

    @property
    def events(self) -> List[Frame]:
        """Newly observed frames: scope ``"current"`` only."""
        return [f for f in self.frames if f.scope == "current"]

    @property
    def initial_candidates(self) -> List[Frame]:
        """Frames from untrusted initial records.  Not observed events."""
        return [f for f in self.frames if f.scope == "initial"]

    def diagnostics_for(self, line: int) -> List[Diagnostic]:
        return [d for d in self.diagnostics if d.line == line]


# --------------------------------------------------------------------------
# low level helpers
# --------------------------------------------------------------------------


def _u16le(raw: bytes) -> int:
    return raw[0] | (raw[1] << 8)


def _hexdump(raw: bytes, limit: int = 12) -> str:
    head = " ".join("%02x" % b for b in raw[:limit])
    if len(raw) > limit:
        head += " ..."
    return head


def route_direction(route_bytes: bytes) -> Optional[str]:
    """Direction a route's raw bytes denote, or None when unknown."""
    return ROUTE_DIRECTIONS.get(route_bytes)


def _decode_body(raw: bytes) -> Dict[str, object]:
    """Decode a frame *body* (route .. trailer).

    The body may be short.  The returned mapping then carries
    ``truncated`` and only the fields that were actually present.
    """
    result: Dict[str, object] = {
        "route_bytes": b"",
        "route": None,
        "category": None,
        "attribute": None,
        "declared_payload_length": None,
        "payload": b"",
        "trailer": b"",
        "expected_inner_length": None,
        "truncated": False,
        "notes": [],
    }
    notes: List[str] = result["notes"]  # type: ignore[assignment]

    if len(raw) < BODY_HEADER_SIZE:
        result["truncated"] = True
        notes.append(
            "truncated header: %d of %d header bytes" % (len(raw), BODY_HEADER_SIZE)
        )
        return result

    route_bytes = raw[0:2]
    result["route_bytes"] = route_bytes
    result["route"] = _u16le(route_bytes)
    result["category"] = _u16le(raw[2:4])
    result["attribute"] = _u16le(raw[4:6])
    paylen = _u16le(raw[6:8])
    result["declared_payload_length"] = paylen

    available = len(raw) - BODY_HEADER_SIZE
    if available < paylen:
        result["truncated"] = True
        notes.append("truncated payload: %d of %d payload bytes" % (available, paylen))
    result["payload"] = raw[BODY_HEADER_SIZE : BODY_HEADER_SIZE + paylen]

    trailer_end = BODY_HEADER_SIZE + paylen + TRAILER_SIZE
    result["trailer"] = raw[BODY_HEADER_SIZE + paylen : trailer_end]
    if len(raw) < trailer_end:
        result["truncated"] = True
        notes.append(
            "truncated trailer: %d of %d bytes" % (len(result["trailer"]), TRAILER_SIZE)
        )

    result["expected_inner_length"] = BODY_HEADER_SIZE + paylen + TRAILER_SIZE
    return result


def decode_frame_bytes(raw: bytes, *, expect_marker: bool = True, offset: int = 0) -> Frame:
    """Decode one complete frame from ``raw``.

    The strict, standalone entry point.  It raises :class:`FrameDecodeError`
    when the bytes cannot be a frame; the trace level decoder never raises,
    it records a diagnostic instead.

    ``raw`` must contain the outer envelope (marker + inner length) unless
    ``expect_marker`` is false, in which case a bare frame body is expected.
    """
    if expect_marker:
        if len(raw) < ENVELOPE_SIZE:
            raise FrameDecodeError(
                "truncated envelope", offset, "need %d bytes" % ENVELOPE_SIZE
            )
        if raw[0:2] != OUTER_MARKER:
            raise FrameDecodeError("missing outer marker", offset, raw[0:2].hex())
        declared: Optional[int] = _u16le(raw[2:4])
        body = raw[ENVELOPE_SIZE:]
    else:
        declared = None
        body = raw

    if declared is not None and declared < MIN_INNER_LENGTH:
        raise FrameDecodeError("malformed inner length", offset, "inner_len=%d" % declared)
    if declared is not None and len(body) < declared:
        raise FrameDecodeError(
            "truncated frame body",
            offset,
            "need %d bytes, have %d" % (declared, len(body)),
        )

    fields = _decode_body(body if declared is None else body[:declared])
    expected = fields["expected_inner_length"]
    notes = list(fields["notes"])  # type: ignore[arg-type]
    if declared is not None and expected is not None and declared != expected:
        notes.append(
            "length mismatch: envelope declares %d, header implies %d" % (declared, expected)
        )

    return Frame(
        line=0,
        time_us=0,
        ring="",
        scope="current",
        offset=offset,
        envelope_seen=expect_marker,
        route_bytes=fields["route_bytes"],  # type: ignore[arg-type]
        route=fields["route"],  # type: ignore[arg-type]
        category=fields["category"],  # type: ignore[arg-type]
        attribute=fields["attribute"],  # type: ignore[arg-type]
        declared_payload_length=fields["declared_payload_length"],  # type: ignore[arg-type]
        payload=fields["payload"],  # type: ignore[arg-type]
        trailer=fields["trailer"],  # type: ignore[arg-type]
        declared_inner_length=declared,
        expected_inner_length=expected,  # type: ignore[arg-type]
        complete=not fields["truncated"],
        truncated=bool(fields["truncated"]),
        uncertain=bool(notes),
        notes=notes,
    )


# --------------------------------------------------------------------------
# record parsing
# --------------------------------------------------------------------------


def _looks_like_ring_dump(rest: str) -> bool:
    """True for the stock observer's record shape.

    The ring label is followed by the write cursor field, which is what
    distinguishes these records from the unrelated peek observer's records.
    """
    match = _RECORD_RE.match(rest)
    if not match:
        return False
    return match.group("rest").split()[:1] == ["W"]


def _parse_record(line_no: int, text: str, time_us: int, rest: str) -> Sample:
    match = _RECORD_RE.match(rest)
    if not match:
        raise TraceFormatError("malformed ring-dump record", line_no, text)

    is_init = bool(match.group("init"))
    ring = match.group("ring")
    tokens = match.group("rest").split()

    write_cursor: Optional[int] = None
    read_cursor: Optional[int] = None
    delta: Optional[int] = None
    inner_length: Optional[int] = None
    preview = b""
    have_preview = False

    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token in ("W", "R", "D", "L"):
            if index + 1 >= len(tokens):
                raise TraceFormatError("field %r without a value" % token, line_no, text)
            raw_value = tokens[index + 1]
            if not raw_value.isdigit():
                raise TraceFormatError(
                    "field %r has a non numeric value %r" % (token, raw_value), line_no, text
                )
            value = int(raw_value)
            if token == "W":
                write_cursor = value
            elif token == "R":
                read_cursor = value
            elif token == "D":
                delta = value
            else:
                inner_length = value
            index += 2
            continue
        # The preview token is the ring label's sibling: the ring label was
        # already consumed positionally, so a "B"-prefixed token here can only
        # be the payload.  Tokenising on whitespace removes the legacy
        # parser's ambiguity between the ring letter "B" and this marker.
        if token.startswith("B") and len(token) > 1:
            payload_hex = token[1:]
            if len(payload_hex) % 2 or not _HEX_RE.match(payload_hex):
                raise TraceFormatError(
                    "malformed preview payload %r" % token, line_no, text
                )
            preview = bytes.fromhex(payload_hex)
            have_preview = True
            index += 1
            continue
        raise TraceFormatError("unexpected token %r" % token, line_no, text)

    if have_preview and delta is None:
        raise TraceFormatError("preview payload without a D delta", line_no, text)
    if len(preview) > FRAME_BYTES:
        raise TraceFormatError(
            "preview of %d bytes exceeds the observer's %d byte limit"
            % (len(preview), FRAME_BYTES),
            line_no,
            text,
        )

    return Sample(
        line=line_no,
        time_us=time_us,
        ring=ring,
        kind="init" if is_init else "dump",
        write_cursor=write_cursor,
        read_cursor=read_cursor,
        delta=delta,
        inner_length=inner_length,
        preview=preview,
    )


# --------------------------------------------------------------------------
# frame extraction from one record
# --------------------------------------------------------------------------


def _published_extent(sample: Sample) -> int:
    """Number of preview bytes justified as published.

    The packet's four byte envelope is published just before the dump
    starts, so of the ``delta`` published bytes only ``delta - 4`` are
    visible in the 64 byte window.
    """
    if sample.delta is None:
        return 0
    if sample.delta < ENVELOPE_SIZE:
        return 0
    return min(len(sample.preview), sample.delta - ENVELOPE_SIZE)


def _frame_from_body(
    sample: Sample,
    scope: str,
    offset: int,
    body: bytes,
    declared_inner_length: Optional[int],
    envelope_seen: bool,
) -> Frame:
    fields = _decode_body(body)
    notes = list(fields["notes"])  # type: ignore[arg-type]
    expected = fields["expected_inner_length"]
    # Every frame reconstructed from the live observer inherits its sampling
    # uncertainty, including format-complete frames and aggregated packets.
    uncertain = True

    if declared_inner_length is not None and expected is not None:
        if declared_inner_length != expected:
            notes.append(
                "length mismatch: recorded inner length %d, header implies %d"
                % (declared_inner_length, expected)
            )
            uncertain = True

    return Frame(
        line=sample.line,
        time_us=sample.time_us,
        ring=sample.ring,
        scope=scope,
        offset=offset,
        envelope_seen=envelope_seen,
        route_bytes=fields["route_bytes"],  # type: ignore[arg-type]
        route=fields["route"],  # type: ignore[arg-type]
        category=fields["category"],  # type: ignore[arg-type]
        attribute=fields["attribute"],  # type: ignore[arg-type]
        declared_payload_length=fields["declared_payload_length"],  # type: ignore[arg-type]
        payload=fields["payload"],  # type: ignore[arg-type]
        trailer=fields["trailer"],  # type: ignore[arg-type]
        declared_inner_length=declared_inner_length,
        expected_inner_length=expected,  # type: ignore[arg-type]
        complete=not fields["truncated"] and len(body) == (expected or len(body)),
        truncated=bool(fields["truncated"]),
        uncertain=uncertain,
        notes=notes,
    )


def _scan_frames(sample: Sample, scope: str, limit: int) -> Tuple[List[Frame], List[Diagnostic]]:
    """Decode the frames of one record inside ``limit`` preview bytes."""
    diagnostics: List[Diagnostic] = []
    frames: List[Frame] = []
    preview = sample.preview
    if not preview or limit <= 0:
        return frames, diagnostics

    # ---- the head frame: its envelope is just before the window ----------
    head_length = sample.inner_length
    if head_length is None:
        diagnostics.append(
            Diagnostic(
                sample.line,
                "warning",
                "no-inner-length",
                "record carries no L field; the head frame is decoded from its own "
                "header and marked uncertain",
            )
        )
        head_length = 0
    elif head_length < MIN_INNER_LENGTH:
        diagnostics.append(
            Diagnostic(
                sample.line,
                "error",
                "malformed-inner-length",
                "L=%d is below the minimum legal inner length %d"
                % (head_length, MIN_INNER_LENGTH),
            )
        )
        head_length = 0

    if head_length:
        raw_body = preview[: min(head_length, limit)]
        frame = _frame_from_body(sample, scope, 0, raw_body, head_length, False)
        if len(raw_body) < head_length:
            frame.truncated = True
            frame.complete = False
            frame.notes.append(
                "head frame body truncated: %d of %d bytes inside the decoded region"
                % (len(raw_body), head_length)
            )
        frames.append(frame)
        for note in frame.notes:
            diagnostics.append(Diagnostic(sample.line, "warning", "head-frame", note))
        offset = head_length
    else:
        # No usable L: derive the head body length from its own header.
        if limit < BODY_HEADER_SIZE:
            diagnostics.append(
                Diagnostic(
                    sample.line,
                    "warning",
                    "short-preview",
                    "only %d decoded bytes; the frame header is incomplete" % limit,
                )
            )
            return frames, diagnostics
        paylen = _u16le(preview[6:8])
        derived = BODY_HEADER_SIZE + paylen + TRAILER_SIZE
        frame = _frame_from_body(sample, scope, 0, preview[: min(derived, limit)], None, False)
        frame.uncertain = True
        frames.append(frame)
        offset = derived

    # ---- subsequent complete frames inside the decoded region ------------
    while offset < limit:
        if preview[offset : offset + 2] != OUTER_MARKER:
            diagnostics.append(
                Diagnostic(
                    sample.line,
                    "warning",
                    "missing-marker",
                    "no outer marker at preview offset %d; scan stopped" % offset,
                )
            )
            break
        if offset + ENVELOPE_SIZE > limit:
            diagnostics.append(
                Diagnostic(
                    sample.line,
                    "warning",
                    "truncated-envelope",
                    "outer envelope at preview offset %d is incomplete" % offset,
                )
            )
            break
        inner = _u16le(preview[offset + 2 : offset + 4])
        if inner < MIN_INNER_LENGTH:
            diagnostics.append(
                Diagnostic(
                    sample.line,
                    "error",
                    "malformed-inner-length",
                    "frame at preview offset %d declares inner length %d (minimum %d)"
                    % (offset, inner, MIN_INNER_LENGTH),
                )
            )
            break
        body_end = offset + ENVELOPE_SIZE + inner
        if body_end > limit:
            body = preview[offset + ENVELOPE_SIZE : limit]
            frame = _frame_from_body(sample, scope, offset, body, inner, True)
            frame.truncated = True
            frame.complete = False
            frame.notes.append(
                "frame body truncated: %d of %d bytes inside the decoded region"
                % (len(body), inner)
            )
            frames.append(frame)
            diagnostics.append(
                Diagnostic(
                    sample.line,
                    "warning",
                    "truncated-frame",
                    "frame at preview offset %d declares inner length %d but only %d "
                    "bytes are decoded" % (offset, inner, len(body)),
                )
            )
            break
        frame = _frame_from_body(sample, scope, offset, preview[offset + ENVELOPE_SIZE : body_end], inner, True)
        frames.append(frame)
        for note in frame.notes:
            diagnostics.append(Diagnostic(sample.line, "warning", "frame", note))
        offset = body_end

    return frames, diagnostics


def _scan_stale(preview: bytes, start: int) -> List[Dict[str, object]]:
    """Identify frames beyond the published extent (diagnostics only).

    These are never emitted as frames: they are older ring content, and a
    naive parser that scans the whole window would present them as fresh
    events.
    """
    stale: List[Dict[str, object]] = []
    offset = start
    while offset + ENVELOPE_SIZE <= len(preview):
        if preview[offset : offset + 2] != OUTER_MARKER:
            offset += 1
            continue
        inner = _u16le(preview[offset + 2 : offset + 4])
        if inner < MIN_INNER_LENGTH:
            offset += 1
            continue
        end = offset + ENVELOPE_SIZE + inner
        body = preview[offset + ENVELOPE_SIZE : min(end, len(preview))]
        fields = _decode_body(body)
        stale.append(
            {
                "offset": offset,
                "inner_length": inner,
                "category": fields["category"],
                "attribute": fields["attribute"],
                "truncated": end > len(preview),
            }
        )
        if end > len(preview):
            break
        offset = end
    return stale


def _decode_sample(sample: Sample) -> Tuple[List[Frame], List[Diagnostic]]:
    """Decode the frames of one record."""
    diagnostics: List[Diagnostic] = []
    preview = sample.preview
    if not preview:
        return [], diagnostics

    if sample.scope == "initial":
        # The baseline was uninitialised in the source: delta, inner length
        # and byte range are not meaningful.  Decode candidates over the
        # window and mark them; they are never observed events.
        frames, more = _scan_frames(sample, "initial", len(preview))
        diagnostics.extend(more)
        for frame in frames:
            frame.uncertain = True
            frame.notes.append(
                "record computed from an uninitialised cursor baseline; the "
                "publication time of these bytes is unknown"
            )
        diagnostics.append(
            Diagnostic(
                sample.line,
                "warning",
                "uninitialised-baseline",
                "first record of ring %s has no trusted baseline: the source "
                "compared the producer cursor against an uninitialised "
                "last_writes[] entry, so D and L are not meaningful here"
                % sample.ring,
            )
        )
        return frames, diagnostics

    justified = _published_extent(sample)
    sample.published_bytes = justified

    if sample.delta == 0:
        diagnostics.append(
            Diagnostic(
                sample.line,
                "warning",
                "no-publication",
                "D=0: the producer cursor did not advance, so nothing was "
                "published; no frame is derived from this record",
            )
        )
        return [], diagnostics

    if sample.delta < ENVELOPE_SIZE:
        diagnostics.append(
            Diagnostic(
                sample.line,
                "warning",
                "delta-too-small",
                "D=%d is smaller than the %d byte packet envelope; none of the "
                "published bytes is inside the dump"
                % (sample.delta, ENVELOPE_SIZE),
            )
        )
        return [], diagnostics

    frames, more = _scan_frames(sample, "current", justified)
    diagnostics.extend(more)

    stale = _scan_stale(preview, justified)
    if stale:
        def _field(value: Optional[int]) -> str:
            return "------" if value is None else "0x%04x" % value

        described = ", ".join(
            "cat=%s attr=%s%s"
            % (_field(item["category"]), _field(item["attribute"]), " (cut)" if item["truncated"] else "")
            for item in stale
        )
        diagnostics.append(
            Diagnostic(
                sample.line,
                "info",
                "stale-preview-content",
                "%d byte(s) beyond the published extent contain an older frame (%s); "
                "not reported as an observed event" % (len(preview) - justified, described),
            )
        )

    return frames, diagnostics


# --------------------------------------------------------------------------
# trace level parser
# --------------------------------------------------------------------------


def parse_trace(text: str, *, strict: bool = True) -> Trace:
    """Parse a whole ring-observer trace.

    ``strict`` raises :class:`TraceFormatError` on a malformed record; with
    ``strict=False`` the record is skipped and a diagnostic is recorded
    instead.  Lines that are not ring-dump records (the observer's ``MAP``,
    ``SUMMARY`` headers, or records of the unrelated peek observer) are
    ignored as metadata.
    """
    trace = Trace()
    trusted_baseline: Dict[str, bool] = {}

    for line_no, raw_line in enumerate(text.splitlines(), 1):
        line = raw_line.strip()
        if not line:
            continue
        if not line.startswith("T "):
            trace.diagnostics.append(
                Diagnostic(line_no, "info", "metadata", line[:120])
            )
            continue

        parts = line.split(None, 2)
        if len(parts) < 3 or not parts[1].isdigit():
            message = "malformed time record"
            if strict:
                raise TraceFormatError(message, line_no, raw_line)
            trace.diagnostics.append(
                Diagnostic(line_no, "error", "malformed-record", message)
            )
            continue

        time_us = int(parts[1])
        rest = parts[2]
        if not _looks_like_ring_dump(rest):
            # Not a ring-dump record: the observer's MAP/SUMMARY headers or a
            # record of the unrelated peek observer.  Metadata only.
            trace.diagnostics.append(
                Diagnostic(line_no, "info", "metadata", line[:120])
            )
            continue
        try:
            sample = _parse_record(line_no, raw_line, time_us, rest)
        except TraceFormatError as error:
            if strict:
                raise
            trace.diagnostics.append(
                Diagnostic(line_no, "error", "malformed-record", str(error))
            )
            continue

        # Scope: the source initialises last_writes[] only for the ring that
        # printed the INIT line.  The first dump of any other ring is
        # computed from an uninitialised baseline and is not trustworthy.
        if sample.kind == "init":
            sample.scope = "baseline"
            trusted_baseline[sample.ring] = True
        elif trusted_baseline.get(sample.ring):
            sample.scope = "current"
        else:
            sample.scope = "initial"
            trusted_baseline[sample.ring] = True

        trace.samples.append(sample)
        frames, diagnostics = _decode_sample(sample)
        trace.frames.extend(frames)
        trace.diagnostics.extend(diagnostics)

    return trace


def parse_trace_file(path: str, *, strict: bool = True) -> Trace:
    """Parse a ring-observer trace file."""
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return parse_trace(handle.read(), strict=strict)


# --------------------------------------------------------------------------
# rendering
# --------------------------------------------------------------------------


def _frame_row(frame: Frame) -> str:
    def hx(value: Optional[int]) -> str:
        return "------" if value is None else "0x%04x" % value

    flags = ""
    if frame.truncated:
        flags += "T"
    if frame.uncertain:
        flags += "?"
    return "%-4d %-9d %-3s %-8s %-8s %-8s %-8s %-7s %-4s %s" % (
        frame.line,
        frame.time_us,
        frame.ring,
        frame.scope,
        hx(frame.route),
        hx(frame.category),
        hx(frame.attribute),
        "-" if frame.declared_payload_length is None else frame.declared_payload_length,
        flags or "-",
        _hexdump(frame.payload),
    )


def render_text(trace: Trace) -> str:
    """Render a human readable report."""
    out: List[str] = []
    out.append(
        "samples: %d   frames: %d   events: %d   initial-candidates: %d"
        % (
            len(trace.samples),
            len(trace.frames),
            len(trace.events),
            len(trace.initial_candidates),
        )
    )
    out.append(
        "note: the observer reads live ring memory, so no record is an atomic "
        "snapshot"
    )
    out.append("")
    out.append(
        "%-4s %-9s %-3s %-8s %-8s %-8s %-8s %-7s %-4s %s"
        % ("line", "time_us", "ring", "scope", "route", "cat", "attr", "paylen", "fl", "payload")
    )
    for frame in trace.frames:
        out.append(_frame_row(frame))

    dumps = [s for s in trace.samples if s.preview]
    if dumps:
        out.append("")
        out.append("records:")
        out.append(
            "%-4s %-9s %-3s %-8s %-6s %-6s %-6s %-6s %-9s %s"
            % ("line", "time_us", "ring", "scope", "W", "R", "D", "L", "published", "preview")
        )
        for sample in dumps:
            out.append(
                "%-4d %-9d %-3s %-8s %-6s %-6s %-6s %-6s %-9s %d"
                % (
                    sample.line,
                    sample.time_us,
                    sample.ring,
                    sample.scope,
                    "-" if sample.write_cursor is None else sample.write_cursor,
                    "-" if sample.read_cursor is None else sample.read_cursor,
                    "-" if sample.delta is None else sample.delta,
                    "-" if sample.inner_length is None else sample.inner_length,
                    sample.published_bytes,
                    len(sample.preview),
                )
            )

    if trace.diagnostics:
        out.append("")
        out.append("diagnostics:")
        for diagnostic in trace.diagnostics:
            out.append("  " + str(diagnostic))
    return "\n".join(out)


def render_json(trace: Trace) -> str:
    """Render the trace as JSON."""

    def frame_dict(frame: Frame) -> Dict[str, object]:
        return {
            "line": frame.line,
            "time_us": frame.time_us,
            "ring": frame.ring,
            "scope": frame.scope,
            "offset": frame.offset,
            "envelope_seen": frame.envelope_seen,
            "route_bytes": frame.route_bytes.hex(),
            "route": frame.route,
            "direction": frame.direction,
            "category": frame.category,
            "attribute": frame.attribute,
            "declared_payload_length": frame.declared_payload_length,
            "payload": frame.payload.hex(),
            "trailer": frame.trailer.hex(),
            "declared_inner_length": frame.declared_inner_length,
            "expected_inner_length": frame.expected_inner_length,
            "complete": frame.complete,
            "truncated": frame.truncated,
            "uncertain": frame.uncertain,
            "notes": list(frame.notes),
        }

    document = {
        "samples": [
            {
                "line": s.line,
                "time_us": s.time_us,
                "ring": s.ring,
                "kind": s.kind,
                "scope": s.scope,
                "write_cursor": s.write_cursor,
                "read_cursor": s.read_cursor,
                "delta": s.delta,
                "inner_length": s.inner_length,
                "preview_len": len(s.preview),
                "published_bytes": s.published_bytes,
            }
            for s in trace.samples
        ],
        "frames": [frame_dict(f) for f in trace.frames],
        "diagnostics": [
            {"line": d.line, "level": d.level, "code": d.code, "message": d.message}
            for d in trace.diagnostics
        ],
    }
    return json.dumps(document, indent=2, sort_keys=False)


# --------------------------------------------------------------------------
# command line
# --------------------------------------------------------------------------


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Decode a Gear 360 ring-observer trace offline (no camera access)."
    )
    parser.add_argument("trace", help="ring-observer trace file")
    parser.add_argument("--json", action="store_true", help="emit JSON instead of text")
    parser.add_argument(
        "--lenient",
        action="store_true",
        help="skip malformed records instead of raising",
    )
    args = parser.parse_args(argv)

    try:
        trace = parse_trace_file(args.trace, strict=not args.lenient)
    except (TraceFormatError, FrameDecodeError) as error:
        print("error: %s" % error, file=sys.stderr)
        return 2
    except OSError as error:
        print("error: %s" % error, file=sys.stderr)
        return 2

    print(render_json(trace) if args.json else render_text(trace))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
