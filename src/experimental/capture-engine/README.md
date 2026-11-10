# Gear 360 capture engine components

C11 components of our own SM-R210 still-capture engine, with C++ linkage. This is **not yet a working RAW capture library**. These are offline analysis and file-format components. Initialization, supported configuration, submission, request-associated completion, RAW ownership, release/shutdown and optional camera JPEG still need an integrated backend and target validation.

| Component | Status |
| --- | --- |
| `pool_monitor` — reads snapshots of the RTOS capture-pool block records and tracks the id-22 RAW reservation (memory slot 28, 64 MiB) from reserve to release | Replayed against two recorded live captures |
| `raw_format` — measured RAW layout, a scan that checks size and 10-bit high alignment, and an uncompressed DNG header writer | Checked on the recorded 18 October dump; DNG read by exiftool and dcraw, payload hash unchanged |
| `samsung3` — from-scratch decoder for the Samsung3 lossless-compressed RAW stream the LRC stage emits (128-bit header, motion predictor, adaptive per-colour difference lengths) | Host round-trip against an independent Python reference encoder; no camera bytes yet |
| `tools/g360_dng_wrap.c` — command-line wrapper: RAW raster → DNG | Host tool |
| Software submission | [`still-request/`](../../lib/still-request/still_request.h) encodes the recovered 48-byte command and calls an already initialized vendor submit export once. Host tests and ARM compilation passed. Context ownership, preparation and completion remain open; no new capture is authorized by this helper |
| **Hold / copy of the RAW** | **Missing.** See below |

## Why the RAW cannot be owned yet

The recorded 19 October timelines show a 64 MiB tag-28 reservation at `0xA6000000`, changing samples, then disappearance/reallocation of its blocks. Polls take roughly 34 ms and sample only a few words per region. They establish observed pool reuse; they do not directly identify the writer, DMA completion, exact RAW-valid interval or lens provenance. The earlier 80–110 ms estimate depends on interpreting the sampled changes as readout. Reading `/dev/mem` without a retained backing contract cannot supply owned RAW.

The [Codex reconciliation](../../../reverse-engineering/archive/INVESTIGATION_CHECKPOINT_20261019_CODEX_RECONCILIATION.md) recovers a different graph for normal 360 capture (type 0) from the type-1/type-2 Naejae recipe studied in the previous pass. The Conti MainRaw ring recycles image objects and does not establish a completed RAW lease. The current accepted lease and missing handoff are recorded in [raw-buffer facts](../../../reverse-engineering/facts/contracts/raw-buffer.yaml); the optional SSIF callback is a diagnostic file dump. A usable hold/copy/result interface remains missing; neither these observations nor an unimplemented interface prove that firmware modification is required for our own engine. Continue offline recovery before asking the user to choose a patch or injection design. The debug-dump backend remains excluded by the project goal.

## RAW layout (measured, one capture)

5824 × 2896 samples, little-endian 16-bit, stride 11648 bytes, 33,732,608 bytes. Every sample is a multiple of 64 (10 significant bits in bits 6..15), so the white level is 65472. CFA phase GRBG, measured on the right lens only. Crop/padding, left-lens red/blue order and sensor precision are not established. The DNG colour matrix is an uncalibrated placeholder.

## Build and check

```sh
sh src/experimental/capture-engine/build.sh                 # host library, tests, tool
CHECK_ARM=1 ARM_CLANG=/opt/homebrew/opt/llvm/bin/clang \
  sh src/experimental/capture-engine/build.sh               # also ARMv7 objects
```

Output goes to `output/capture-engine-build/`. Optional real-data checks use local files that are not in Git:

```sh
G360_TIMELINE_LOG=output/decompile/rea/pass-opus-engine-20261019/camera/timeline1/out \
G360_RAW_FILE=<path to the 33,732,608-byte dump> \
  output/capture-engine-build/test_engine
```

The ARM objects are freestanding compiles of the logic modules only. Nothing here has run on the camera.

## Wrap a RAW file as DNG

```sh
output/capture-engine-build/g360-dng-wrap --input dump.raw --output dump.dng \
  --black 4105 --neutral-red 0.544 --neutral-blue 0.578
```

The tool refuses inputs that are not exactly 33,732,608 bytes, and warns if any sample has its low six bits set.
