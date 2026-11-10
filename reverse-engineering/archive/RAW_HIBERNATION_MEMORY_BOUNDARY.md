> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Historical Linux memory from the captured resume partition

The captured eMMC's p8 partition contains a self-consistent, uncompressed Linux hibernation image under the upstream Linux3.5 format interpretation. Its saved pages can be reconstructed offline. **None of its saved PFNs cover the candidate capture arena at0xa6000000–0xc0000000.** This is a useful boundary for the investigation, not a recovered RAW frame or a current runtime observation.

## Source and format

Primary and backup GPT headers and entry-array checksums agree. Partition8 is named `snapshot`, starts at source byte179,307,008, and contains134,217,728 bytes. The source is the existing `dump/gear360-emmc-full.img`. Its recorded full-image sidecar digest was retained; the7.8GB file was not rehashed. All relevant source windows and every reconstructed page were checked independently against the image.

The first page contains `SWAPSPACE2` at partition offset4076 and `S1SUSPEND` at4086. The header's first map slot is1 and flags word is2. The metadata names Linux3.5.0, ARMv7, `drime5`, with the captured2017 build string. The on-disk fields fit the upstream [v3.5 swap format](https://github.com/torvalds/linux/blob/28a33cbc24e4256c143dce96c7d93bf423229f92/kernel/power/swap.c), [snapshot page ordering](https://github.com/torvalds/linux/blob/28a33cbc24e4256c143dce96c7d93bf423229f92/kernel/power/snapshot.c), and [header/flag definitions](https://github.com/torvalds/linux/blob/28a33cbc24e4256c143dce96c7d93bf423229f92/kernel/power/power.h).

Those sources are references, not proof of Samsung's exact patched implementation. Their tag commit, downloaded source hashes and the format assumptions are retained in the evidence index. Under that reference, flag2 selects uncompressed storage; the saved-image CRC flag is clear. Signature locations, map linkage, size accounting, PFN order and sentinel all fit that interpretation independently.

## Map and page accounting

| Item | Verified value |
|---|---:|
| Partition capacity | 32,768 slots of4096 bytes |
| Linked map pages | 39 |
| Unique mapped data pages | 19,776 |
| Metadata accounting | 1 header +20 PFN pages +19,755 RAM pages |
| Header size field | 81,002,496 bytes =19,776×4096 |
| Reconstructed RAM payload | 80,916,480 bytes |
| Minimum saved PFN | 0x86000 |
| Maximum saved PFN | 0x9ddff |
| Maximum saved page end | 0x9de00000, exclusive |
| Saved PFNs in0xa6000–0xbffff | 0 |

The saved PFNs are unique and ascending, grouped into134 runs. The span from first to last contains97,792 pages, matching the header's `num_physpages`, but78,037 pages in that span are absent from the saved set. Sparse holes mean **not saved**, not observed zero-filled physical memory.

Every map/data slot is within p8, map and data slots do not overlap, the chain terminates at zero, and the last map's unused entries are zero. Twenty PFN pages contain exactly19,755 entries followed by the ARM32 end-of-map sentinel. The parent independently checked60 source windows, the map/PFN-to-payload ordering, and all19,755 source pages against both their hashes and the reconstructed artifact.

## Reconstructed artifact and content limits

`output/decompile/rea/pass0345-20261016/snapshot-parse/reconstructed-ram-pfn-sparse.bin` places each saved page at file offset `PFN×4096`. It is approximately2.65GB logically but consumes about85MB allocated. Do not scan its holes as if they were captured RAM. The per-page index records source slot/offset, PFN and SHA-256; the content hash over saved pages in PFN order is `e3ce6e71fa305e694367ea5757f53541c364f084d2b763bad4d8655aa1c982df`.

A targeted identifier screen found128 matches for the requested camera app, library, model, build and console names, including one `PRD_CAPTURE_RAW_SSIF` string. It found no structured command/routing state. Text can originate from file/page cache or stale memory; it does not prove that a process, capture command, RTOS recipe or callback was executing. Only relevant token locations were retained in the small screen report; unrelated memory contents were not printed.

This saved page set therefore does not supply the known candidate arena. Linux-side metadata may remain useful, but no image-buffer content, sensor provenance, RAW samples or fresh frame identity follows from this reconstruction. The RTOS code/arena and the historical Linux saved-page set remain distinct evidence domains.

## Reproduction and review

The [evidence index](../hibernation-memory-boundary-evidence.json) binds the GPT/header observations, exact source windows, upstream references, parser/reconstructor scripts, map/PFN metadata, per-page index, parent validation and bounded screen. Large reconstructed memory and raw pages remain local under `output/`.

The initial parser's reported245,760-byte image-read counter omitted one direct4096-byte info-page read; the actual initial image reads were249,856 bytes, still below the2MiB phase cap. This accounting correction does not affect page contents or map/PFN validation. No camera command, firmware execution, restart, preload or deployment was used.
