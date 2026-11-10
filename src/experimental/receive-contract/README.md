# Stock-behaviour producer reference model

A pure C model of the **stock RTOS MCB producer's** ring arithmetic, kept as a
reference for what the real writer does.

**This is not a safe admission check, and it is not a transport.** It reproduces
the writer faithfully, including the behaviour that is unsafe for a receiver.
Use it to reason about the producer. Never use it to decide that a write is
safe, and never use it to receive anything.

## Files

| File | Role |
|---|---|
| `mcb_producer_ring.h` | public API, the transcribed rules, and the limits of each field |
| `mcb_producer_ring.c` | the model; no I/O, no libc dependency, freestanding |
| `tests/test_mcb_producer_ring.c` | host checks (C11) |
| `tests/test_cpp_linkage.cpp` | host check that the C API links and behaves from C++11 |
| `build.sh` | host build and tests; optional ARMv7 compile check with `CHECK_ARM=1` |
| `model-report.md` | what was changed and what was tested |

Build and test on the host:

```sh
sh build.sh                  # system compilers, runs both test binaries
SANITIZE=1 sh build.sh ../../output/receive-contract-sanitize # ASan + UBSan
```

The build writes to `output/receive-contract-build/` in the repository (pass a distinct
output path for each retained run). Nothing is built for ARM unless `CHECK_ARM=1` is set.

## What the model transcribes

Three functions of the pinned RTOS image:

- `FUN_800377dc` — free distance: `(R + N) - W` when `R <= W`, `R - W` when
  `R > W`, and `0` for a cursor outside `[0, N)`.
- `FUN_80037d6c` — the packet writer: admits a message of `M` bytes only when
  `M + 5 < free`, writes a six-byte outer header plus `M` message bytes, and
  publishes the producer cursor as `(W + M + 6) mod N`.
- `FUN_80069d24` — the ordinary bridge sender, which refuses any message
  longer than 395 bytes before the writer is reached.

## The stock behaviour is unsafe on purpose, and stays that way

This is the accepted behaviour of the stock writer, not a defect introduced
here and not something the model corrects:

- **Exact-free admission.** A message that consumes the *exact* free distance
  is admitted. See `session.packet-writer-exact-free-ambiguity`: free-space
  computation returns the capacity for equal cursors, the writer admits
  `frame-length + 6` equal to the free distance, and it publishes a cursor
  equal to the reader, collapsing full and empty.
- **The collapse.** Admitting the largest admissible message always publishes
  the consumer cursor, so the ring reads as empty and the frame just written
  is never handed over.
- **The exact-free wrap short copy.** On the wrap branch whose header fits
  before the boundary, a message consuming the exact free distance copies one
  byte short while the cursor still advances by the full requested extent. See
  `session.packet-writer-exact-free-wrap-short-copy`.

## Limits that the earlier comments overstated

These corrections are the substance of this deliverable; the model code is
otherwise unchanged.

- **Equal cursors are ambiguous.** `producer == consumer` means *empty* to both
  the producer and the consumer, so a ring the producer has just filled is
  indistinguishable from an empty one. Distinguishing them needs an
  **externally maintained no-full invariant**, which this model neither holds
  nor checks.
- **`clobbers_unread` is an overlap calculation, not a detector.** It reports
  whether the written byte intervals overlap `[R, W)`, taken from the two
  cursor values passed in. It does not establish that the consumer cursor was
  truthful and it cannot detect a stale cursor. Within the model's arithmetic
  an admitted write never overlaps that assumed interval, so the flag is `0`
  for every admitted write; it is kept to make the assumption explicit.
- **No live guarantee.** Nothing here establishes cross-CPU visibility, sole
  receiver ownership, freshness, request association, sensor provenance or
  backing lifetime. The model performs no I/O, takes no lock, copies nothing,
  reserves nothing and touches no device.
- **No live ring capacity.** `N` is a model input. The live capacity of the
  shared control ring is not established. `395` is the ordinary bridge
  sender's ceiling on one message; the tests use `N = 395` only as a synthesis
  counterexample input, and the constant is named `SYNTHETIC_RING_SIZE` for
  that reason.
- **The shared-window memory-type question is not answered here.** Constants
  alone cannot establish the active CPU mapping, so this deliverable makes no
  visibility or mapping claim. The device-side checkers
  (`check_descriptor_type.py`, `check_receive_contract.py`) are deliberately
  **not** part of this model.

## The advance overflow fix

`mcb_producer_ring_advance(inner_length)` returned `inner_length + 4`
unchecked. Unsigned addition wraps, so an `inner_length` above `SIZE_MAX - 4`
produced a small, plausible-looking advance instead of an error:

| input | old result | fixed result |
|---|---|---|
| `SIZE_MAX - 4` | `SIZE_MAX` | `SIZE_MAX` |
| `SIZE_MAX - 3` | `0` | `0` (failure marker) |
| `SIZE_MAX` | `3` | `0` (failure marker) |

The old value of `3` is below the minimum valid advance of `4` bytes, so a
caller could have moved a consumer cursor by the wrong amount. The function now
documents and returns `0` when `inner_length > SIZE_MAX - 4`. Every valid
result is at least `4`, so `0` is an unambiguous failure marker and is never a
valid advance.

## Relationship to the production direction

`session.strict-ring-admission-component` — the pure C `mcb_ring_plan_admission`
component — is the **production direction**: it plans our *outgoing* frame
against a caller-supplied ring, rejects `required >= free` including exact
equality, preserves one byte of slack, and leaves its output unchanged on
refusal.

This model is the **opposite direction**: it describes what the *stock RTOS
producer* does to the ring our receiver reads. It is not a replacement for the
strict planner and must not be used as one. The strict planner's admission rule
is deliberately stricter than the stock writer's, and that difference is the
point of keeping both.

## Authority

Accepted facts in `reverse-engineering/facts/contracts/` and the session fact
set: `session.packet-writer-exact-free-ambiguity`,
`session.packet-writer-exact-free-wrap-short-copy`,
`session.strict-ring-admission-component`,
`session.packet-writer-publishes-after-copy-calls`.
