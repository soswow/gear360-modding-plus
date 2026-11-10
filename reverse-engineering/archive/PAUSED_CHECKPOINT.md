> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Paused reverse-engineering branch

Historical pause checkpoint. The user resumed this work on 2026-10-14; see [the subsequent image/cache findings](RAW_IMAGE_AND_CACHE_FINDINGS.md) and local `pass4/` traces for current state.

Paused at the user's request on 2026-10-14. Investigation workers have finished, shared and private REA targets are closed, and no further analysis is scheduled. No live camera command, restart, preload, deployment, or firmware execution occurred.

Verified protocol work is committed on local `master` as `8c5e12a3dd21b5a70547c7daf30fa4c33e56150c`. [RAW_DUMP_PROTOCOL.md](RAW_DUMP_PROTOCOL.md) contains the current synthesis. The older unrelated pending edits in `REVERSE_ENGINEERING_LEDGER.md` remain unstaged.

Latest saved results beyond that protocol checkpoint:

- **Diagnostic registration:** captured Linux `st cap` sends text through MCB family `0xb6`. RTOS tokenization and command-map indexing select original token `back`, registered to `CBackendDebugProcess`, then its `unitest ssifdump on|off` parser. The setter and dump guard use the same singleton byte. Full checkpoint and closed snapshot: `output/decompile/rea/continuation-20261014/pass3/rtos-shell-registration/checkpoint.md` and `rtos-shell-final.snapshot.json`.
- **Material construction:** MID `0x15000` is assigned through `0x802ba7b4` → `SetMaterial`. Seven provider-confirmed caller functions are distinct from nine raw branch-target candidates. Recipe error paths identify its result as an SSIF material. It inserts a `0xc8`-byte image from selector `0x28` into slot zero. `0x80141444` is an interior allocator case, not a constructor entry; actual configurator is `0x80141c78`. Evidence: `pass3/material-factory/report.md`, `checkpoint.md`, and closed private snapshots.
- **Indirect memory operation:** FileDump uses receiver vtable `+0x78` for descriptor and pixel-buffer ranges. Receiver comes from holder `0x81c0d7bc` field `+0x24`; its initialization clears that field. The assignment and concrete target remain unresolved. Destructor registration was corrected to avoid treating `0x805836e0` as construction. Evidence: `pass3/dump-cache/report.md` and snapshots.

All relative `pass3/` paths above are beneath `output/decompile/rea/continuation-20261014/`. Full source-byte excerpts, hashes, manifests, scan scripts, and REA evidence records are retained there. The independent protocol audit is `pass3/protocol-audit.md`.

Resume priorities: concrete MID factory entry and active R210 graph; selector-0x28 image configuration and exact DMA-buffer identity; receiver assignment/cache/address semantics; then a narrowly scoped live validation preserving the healthy camera app. No actual sensor RAW frame has been recovered.
