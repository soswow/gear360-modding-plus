> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Full preview buffer and Bayer evidence

17 October 2026, Australia/Sydney. The complete selected **1920×960 preview surface** is now on the Mac. This is a substantial improvement over the original 4096-byte fragment, but remains an **unverified passive memory snapshot**, rather than a synchronized sensor RAW acquisition. Full preview size is not full native sensor resolution.

## What is established

The other agent's 16 October transfer was reviewed using its recorded commands, camera/Mac hashes, process identity and cleanup records, rather than repeating its acquisition. The parent reran its host contracts and two deterministic ARM builds. The original helper hash was `b521b2f3095bad637c83dcab536b6d20c2bea721d48b39f703ce878c8745bfbc`. Its fixed read-only extent is `0xa77bb000`, length **2,764,800 = 2880×960 bytes**. Width, height, stride, selected-object pointers, vptr and PData were checked before and after the copy. The source was copied into Linux-owned memory before export.

Original snapshot SHA256: **`5c7a7c6ce9fb20d467e43806cd85350024311ee288a4dafcdd1a73c2e19f06e9`**. The complete transfer evidence and sample are versioned in the verified archive index (not published: `../evidence/full-buffer-read-20261016.index.json`), alongside its archive. All 56 archived file contents were checked against their recorded hashes. The [other agent's response](FULL_BUFFER_READ_HANDOFF-response.md) retains the original experiment details.

The following unpacking produces 1920 samples per 2880-byte row:

```text
p0 = b0 | ((b1 & 15) << 8)
p1 = (b1 >> 4) | (b2 << 4)
```

The first full sample forms two side-by-side fisheye views. All twelve sample bits are used in the illuminated view. The user confirmed nighttime conditions and a face-down lens; the dark view is not evidence of a failed sensor. Subsequent snapshots are darker still.

## Firmware label and numerical interpretation

Original firmware SHA256 is `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`, loaded at `0x80000000`. In `CAdjustAdapter::dumpImage` (`0x8004d558`, retained REA evidence `ev_64a1b0cba182dd6a891dbf3655e31c296bed888f5c3a9d7266c206263a5b9a3f`), instructions `0x8004d640..0x8004d690` call getter `0x80541a08`, compare with **1**, and select the literal **`bayer`** at `0x805e8a5c` on that branch. The getter's instruction at `0x80541a1c` reads byte **image+0x54**. Parent independently decoded these instructions and checked the original string bytes. Current selected-image metadata has format byte **1**, so its descriptor has the firmware's Bayer classification.

Do not conflate adjacent bytes: getter `0x80541a80` reads **+0x5a** and selects pixel-bit versus packing-bit metadata for the dump name; getter `0x80541aa8` reads **+0x5b** and selects the filename suffix **`knee`** when nonzero. The latter is a separate flag. The dump-name branch does not by itself define packtype 0/1 as packed/unpacked, or establish actual compression or processing of current pixels.

Cursor, reporting **Grok 4.6 Medium**, independently analyzed saved bytes using NumPy. The parent extracted its three numerical scripts and reproduced the two refined analyses. On unsaturated left-circle cells, phases A/B/C/D at even-even/even-odd/odd-even/odd-odd have means approximately **481.8/321.2/241.6/484.0**. A and D match closely, while B and C differ. On the lowest-texture 174 tiles, plane-removed median absolute A−D is **2.74**, versus **11.36** for B−C; nearest-neighbor residuals exceed two-pixel residuals by about threefold. These observations strongly support a Bayer mosaic with greens on A/D: **GRBG or GBRG**. They are inconsistent with the simple grayscale, two-level checkerboard and row/column stripe alternatives tested.

This is evidence for a Bayer-like mosaic, not a universal exclusion of processed or synthetic mosaics. Noise statistics and plausible diagnostic demosaics do not prove untouched sensor data. **Red/blue order remains unresolved.** Four uncalibrated diagnostic renders are retained; none is a colour calibration or an asserted final RAW interpretation.

## Copy duration and freshness

The reader now offers `full-buffer-observed`, preserving the earlier mode and adding two reads of the already mapped RAM counter at `0x81952d38`, immediately around the pixel copy. Host contracts, parser rejection cases, counter wrap handling and deterministic builds passed. The new helper SHA256 is **`afa30203acf58e2f4974d97f7e0534a198e3985b0928e4aee748c91ea6e22c8c`**. Retrieval also uses unique evidence directories, refuses nonempty destinations before camera access, and records the planned staging path before upload.

| Passive sample, UTC 16 October | SHA256 | VD-falling counter around copy |
|---|---|---|
| 14:15, snapshot-01 | `ba13947d93cb89a4a81fcc7fbb4afd2e15bf4d2378756ad3307842c75930dd13` | 9102 → 9111, difference 9 modulo 10000 |
| snapshot-02 | `e6f9be67c2ced6fb4d60def73cdc5674badb4aaf01211217d3f83653a17291eb` | 74 → 81, difference 7 modulo 10000 |

This counter wraps at **10000**, and earlier observations were consistent with about 30 events/second. It is **cadence, not WDMA completion**. Its advancement shows the copy is not instantaneous; it does not prove that a particular DMA write overlapped the selected surface. Between these two snapshots, **1,621,416 of 1,843,200 samples changed** (87.97%), with changes present in all rows and columns. This establishes changing observed contents between copies, not coherent frame identity within either copy.

Both camera and Mac hashes matched. PID254/starttime1042, 21 untraced app threads and empty owned staging paths were preserved. No imaging command, device/register write, service restart or persistent change was used. Full commands, responses, helpers, metadata and cleanup are under `output/decompile/rea/pass0105-20261017/` and will be archived at this pass's reviewed checkpoint.

MiniMax reported **MiniMax-M3.1-Flash-Preview**, thinking variant; launch requested `--effort high`. Its small text-only review correctly separated copy throughput from frame ownership. Its statement that both copies crossed nine events is wrong (the second crossed seven), and its suggestion to repeat until a zero-event copy appears is not an experiment plan we adopted. Wider loads, barriers or two matching hashes cannot supply a missing lease or DMA-completion protocol.

## What remains for the goal

1. Bind this selected surface to its hardware producer and physical sensor path, including any upstream processing or resizing.
2. Identify a finished frame and exclude concurrent reuse/writes for the complete copy. The known stock stop helper has no established drain, and software Pause discards an inner return value; neither is sufficient evidence of a safe freeze.
3. Prove backing retention and memory visibility through export. Stable pointers and pre/post metadata checks cannot exclude ABA, reclamation or cache issues.
4. Resolve red/blue CFA order and reproduce acquisition. A larger native surface is a later geometry question; the present file contains the complete observed preview extent.

The next work should target an existing safe publication/pause/export contract and the selected buffer's allocation identity. Repeating passive screenshots or broad recipe mapping will not close these boundaries by itself. **The overarching verified sensor RAW acquisition goal is unfinished.**
