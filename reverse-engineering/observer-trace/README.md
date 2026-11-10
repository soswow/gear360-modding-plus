# Offline ring-observer trace decoder

`parse_frames.py` decodes Gear 360 ring-observer traces **offline**. It never
opens the camera, the device or the network, and it depends on nothing outside
the Python standard library.

```
python3 parse_frames.py stock-observer.txt            # text report
python3 parse_frames.py stock-observer.txt --json     # machine readable
python3 -m unittest discover -s tests -v
```

The trace dialect is the one emitted by **the ring observer at `4d2deb0:reverse-engineering/ipcc-ring-observer/ipcc_ring.c`** (the stock
ring observer); that file is the authority for every semantic below.
`observer.c` is a different tool — a peek-only observer whose records carry no
frame bytes — so its dialect is ignored here as metadata.

## Record shape

```
T <time_us> INIT <ring> W <write_cursor> R <read_cursor>
T <time_us> <ring> W <write_cursor> R <read_cursor> D <delta> L <inner_len> B<hex>
```

`INIT` is printed once, for the first ring of the first iteration. Every other
record is a dump of exactly `FRAME_BYTES == 64` bytes.

## What the observer actually reads

* `delta` is the producer cursor's advance since that ring's previous
  observation: `(write_cursor + RING_CAPACITY - last_write) % RING_CAPACITY`,
  with `RING_CAPACITY == 395`.
* The packet just written starts at the **old** producer cursor and is laid out
  `[ff 7f][inner_len u16 LE][body...]`. The observer reads `inner_len` from
  the packet's bytes 2..3.
* The dumped bytes start at the packet's **byte 4** — the body — so the marker
  and the inner length are *not* in the dump.

Therefore the published extent of a dump is:

```
published extent = min(len(preview), delta - 4)
```

Bytes past that extent are ring memory at or past the producer cursor: older
content, or bytes published after the sampled cursor. They are never reported as observed frames; they are named in a
`stale-preview-content` diagnostic instead.

## Frame layout

```
ff 7f | inner_len u16 LE | route u16 LE | category u16 LE | attribute u16 LE |
payload_len u16 LE | payload | trailer[2]

inner_len  == 8 + payload_len + 2        on-wire extent == 4 + inner_len
```

The two trailer bytes close the frame body. `route` is kept as raw bytes as
well as a value, so `03 00` and `00 03` stay distinguishable.

## Scope: baseline, initial, current

`have_last` is a **single global** flag, and `last_writes[]` is only
initialised for the ring that printed the `INIT` line. The first dump record
of every *other* ring is therefore computed from an uninitialised baseline: its
delta, its inner length and its byte range are not meaningful. The decoder
scopes records accordingly:

| scope | meaning | frames |
|---|---|---|
| `baseline` | the `INIT` record; trusted cursor baseline | none |
| `initial` | first dump of a ring with no trusted baseline yet | candidates only, all flagged uncertain |
| `current` | dump computed from a trusted baseline | observed events |

`Trace.events` holds the `current` frames; `Trace.initial_candidates` holds the
`initial` ones. The `initial` scope is deliberately *not* called history: the
source does not know when those bytes were published.

## Degenerate input

* `D == 0` — no cursor advance is visible; a full lap remains possible. No
  frame is derived, and a `no-publication` diagnostic is recorded.
* `D < 4` — the published bytes all precede the dump window. No frame is
  derived (`delta-too-small`).
* A preview longer than 64 bytes is impossible for this observer. It is
  rejected as a malformed record, and in lenient mode the record is skipped
  without producing any frame.

## Route bytes and direction

| raw route bytes | u16 LE | direction |
|---|---|---|
| `03 00` | `0x0003` | Linux → RTOS (ring B records) |
| `00 03` | `0x0300` | reverse, i.e. RTOS → Linux (ring A records) |

The direction names come from the source's ring description ("Ring A is the
RTOS-to-Linux control ring the producer fills; ring B is the opposite
direction of the same channel pair") together with the packet annotation. The
decoder applies no direction of its own, and keeps the raw bytes regardless.

## What the decoder reports

Per frame: source line, time, ring, scope, preview offset, route bytes and
value, direction, category, attribute, declared payload length, payload,
trailer, the declared and the implied inner length, and flags for truncation
and uncertainty. Per record: the recorded `W`, `R`, `D`, `L`, the scope and
the published extent.

`Frame.complete` means only that the frame's bytes are all present in the
preview according to the wire format. It does **not** mean the producer
finished the packet or that any DMA transfer completed.

Malformed input raises `TraceFormatError` (with the line number) in strict
mode, or is collected as a diagnostic with `strict=False`. The strict
single-frame entry point `decode_frame_bytes` raises `FrameDecodeError` with a
stable `reason`.

## Limits

* **No record is an atomic snapshot.** The observer reads live ring memory one
  byte at a time, with no copy and no marker check, while the producer may be
  writing. Every current record therefore carries snapshot uncertainty,
  however well its bytes parse.
* `delta` is computed modulo the ring capacity, so it is a faithful byte count
  only while the producer advanced less than one full lap between two
  observations of the same ring.
* The published extent is exact only when the head frame is the first packet
  of the run. Otherwise it is an upper bound.
* `W` and `R` are reported as recorded. The fixture does not pin their exact
  semantics, so they are not used to bound anything.

## Files

| file | role |
|---|---|
| `parse_frames.py` | the decoder and its CLI |
| `tests/test_parse_frames.py` | regression suite (stdlib `unittest`) |
| `REPORT.md` | findings on the stock fixture |

The historical capture stays in ignored local evidence. Set
`GEAR360_TRACE_FIXTURE` to its absolute path to run the optional historical
fixture cases. Ordinary regression cases use synthetic protocol vectors.
The source contract and review are recorded in
the parent review (not published: `../../output/decompile/rea/pass-4d2deb0-review-20261023/parent-review.md`).
