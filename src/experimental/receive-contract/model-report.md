# Model report — stock-behaviour producer reference model

## Produced

The pure C model in `output/`; `README.md` lists the files. Model files are copies of the reviewed `receive-contract` component, changed only as below. Excluded: `check_descriptor_type.py`, `check_receive_contract.py`, the device helpers. `build.sh` now defaults to `output/build/`; `CHECK_ARM=1` is kept but unrun.

## One code change

`mcb_producer_ring_advance` returned `inner_length + 4` unchecked. Unsigned addition wraps, so an `inner_length` above `SIZE_MAX - 4` produced a plausible-looking advance: `SIZE_MAX - 3` wrapped to `0`, and `SIZE_MAX` wrapped to `3` — below the minimum valid advance of 4, so a caller could have moved a consumer cursor by the wrong amount.

It now documents and returns `0` above `SIZE_MAX - 4`. Every valid result is at least 4, so `0` is an unambiguous failure marker.

No other arithmetic changed. The existing `message_length > SIZE_MAX - 5` refusal still returns `MCB_PRODUCER_REFUSED`. The free-distance expression uses unsigned modular arithmetic; its intermediate wrap alone is not an arithmetic defect.

## Claim corrections (no behaviour changed)

1. "With a truthful consumer cursor the producer never writes into the unread region" — equal cursors are ambiguous between empty and full without an external no-full invariant, which the model neither holds nor checks.
2. "`clobbers_unread` ... so that a caller can detect a stale cursor" — it computes overlap with the assumed `[R, W)` interval only; it cannot detect a stale cursor, and is `0` for every admitted write in this model.
3. Capacity — `395` is the ordinary bridge sender's message ceiling, not an established live ring capacity. The constant is renamed `SYNTHETIC_RING_SIZE`; `N = 395` is documented as a synthesis counterexample input.
4. The header now opens by stating this is a reference model of stock behaviour, never a safe admission check and never a transport; test comments and one test name (`check_producer_never_clobbers_unread` → `check_write_avoids_the_assumed_unread_interval`) were corrected.

The unsafe stock behaviour is reproduced deliberately: exact-free admission, the collapse onto the consumer cursor, and the exact-free wrap short copy.

## What was tested

Host, system compilers (Apple clang 21.0.0), assertions live (`NDEBUG` undefined):

- `sh build.sh` and `SANITIZE=1 sh build.sh` (ASan + UBSan): both build the library, link and run both test binaries, including the C++11 linkage test with the overflow boundary. **PASS**.

New checks:

- `check_ring_advance_overflow`: `SIZE_MAX - 4` → `SIZE_MAX`; `SIZE_MAX - 5` → `SIZE_MAX - 1`; `SIZE_MAX - 3` and above → `0`; no valid advance is `0`.
- `check_exact_free_wrap_short_copy`: at `N=395, W=100, R=50` (free 345), `M=338` copies its full 344-byte extent, `M=339` copies 344 of 345 while publishing the consumer cursor, `M=340` is refused.
- `check_equal_cursors_are_ambiguous`: a collapsed ring is described identically to a fresh empty one.

Existing sweeps retained: free distance, collapse-is-largest, no-overlap, wrap branches, argument validation, unchanged output on refusal.

## Not done

No ARM build (the parent builds ARM); no camera, network, Git, config, delegation or instruction/native proof changes. The shared-window memory-type and CPU-mapping question is not answered here: constants alone cannot establish the active mapping, so no visibility proof is claimed.
