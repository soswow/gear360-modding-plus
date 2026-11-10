> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Linux physical mapping branch — reviewed 18 October 2026

The vendor kernel has now been extracted reproducibly and its relevant code joined to the running camera. For the preview pixel physical offset `0xa77bb000`, the verified `phys_mem_access_prot` branch clears memory-type bits `0x3c` and returns without testing O_SYNC. This is the Linux ARM 3.5 noncached software-PTE encoding. It removes an important uncertainty about the reader's Linux mapping policy; it does not establish a coherent sensor frame, an RTOS cache flush, or a retained backing buffer.

## Original extraction and parent review

All working evidence is under `output/decompile/rea/pass-resume-20261018/`. Luna's `luna-kernel/reproduce_extract.py` validates primary GPT/header-array CRCs, partition bounds, uImage header and payload CRCs, ARM zImage magic/length and bounded gzip expansion. Its fresh `reproduce/` output exactly matches the original four extracted source windows and inflated bytes. The parent read the script and checked the extracted hashes and focused-carrier segment equality. No complete 7.8-GB dump hash/read was needed.

Partition 5, named `uImage`, begins at byte 84,935,168. The 4,077,608-byte payload has SHA256 `8964cf531db11b3017e47e860d0526c5a1f4f8b8f817b4abe6c195d0a1c7d9fb`. Its uImage compression field 0 does not mean the nested kernel is uncompressed: gzip begins inside the ARM zImage at payload+`0x5580`. The 8,068,284 inflated bytes have SHA256 `ebfd2c32ea58c6b3e247294e11cec371272d8ee4c9c093c8382c434ed53d5cf4`. The banner is at inflated+`0x55f010`; the worker's initial manifest typo `0x55eaf0` is corrected in a separate preserved note.

Parent REA MCP analysis used an exact 40,960-byte partial ELF carrier, SHA256 `c8d920f001ce37dace5ef3fc8027ee01cf5a535382aa627166eae101307bd6a8`, with original inflated offsets mapped at `0xc0008000+offset`. Its manifest and all source-window bytes/hashes are retained. The initial full-kernel MCP query failed before returning usable evidence; it is not a negative code finding. The focused view completed successfully. All parent sessions were exported and closed.

## Live joins and RAM classification

Fresh read-only diagnostics confirm Linux 3.5.0/#175, the known symbol VAs, app PID254/starttime1042, 21 threads with TracerPid0, and the arena `/dev/mem` mapping at physical offset `a6000000`. `/proc/iomem` gives kernel code starting `86008000` and System RAM `[86000000,9de00000)`.

Two temporary fixed-scope Linux readers used O_RDONLY `/dev/mem` and PROT_READ|MAP_SHARED mappings only. Host behavior checks covered fixed bounds/read-only flags, invalid inputs, mapping and cleanup failures, partial/zero/failed writes; independent ARM builds were identical. v2 SHA256 is `f6a0972e152d02f1865de105db34e68bf61ae7162359f525e9ee7020e852a6cb`. FTP upload/download bytes and camera-side SHA256 matched before execution. No-argument startup returned2 without memory access.

The six v2 code windows total412 bytes and all exactly match the extracted kernel: `_text`64, `pfn_valid`8, `phys_mem_access_prot`68, `mmap_mem`160, `memblock_is_memory`32 and `cpu_v7_set_pte_ext`80. See `kernel-reader/runtime-v2/code-comparison.json` for each physical/virtual address and digest.

The first metadata observation at physical `86794fd0` found memory count1, maximum128, total size`17e00000`, regions pointer`c0840730`. A new fixed read was designed for the corresponding physical address `86840730`; its source checked those exact metadata fields before reading that address. It returned one region: base`86000000`, size`17e00000`, end-exclusive`9de00000`. The pointer was never followed as an arbitrary address. Health checks passed and both temporary directories were removed and verified absent.

## Code-level conclusion

- `pfn_valid` at `c00153e8` shifts PFN left12 and tail-branches to `memblock_is_memory`. Ghidra merges the noncontiguous tail into its dossier; the gap is not function code. Evidence `ev_534d2a281a396a8b6df2bb4a7cfaad22412db47d296dc09bf9a40c8cc267ff1b`.
- `memblock_is_memory` uses the memory descriptor at `c0794fd4`, calls binary-search helper`c00abf14`, then returns whether its result differs from -1. Helper evidence `ev_672bb85c6406d578778d6c20dbcb31d76195e936e2894ccb0821432a175f1e27` establishes count at+0, regions pointer at+c, and base/size records of8 bytes.
- Thus PFN`a77bb` resolves to physical`a77bb000`, outside the observed sole region. This is derived from code and current metadata, not merely from a generic Linux assumption.
- `phys_mem_access_prot`, `c00168ec..c001692f`, does `BICEQ r5,r5,#0x3c`, then branches to return. O_SYNC is tested only on the valid-RAM arm. Evidence `ev_9d032522321594d16deddb845b24293b48f17217d46d108cc5ce0894a0448dca`; all68 bytes matched live.
- `mmap_mem` passes this result into `remap_pfn_range` and stores it in VMA+14; evidence `ev_23f48fbcf1bb20baa42bbb36d79bc83d53b4dd15ed349ae72476b974e0149e6b`. The protection branch is not a flush or a frame-completion operation.

The [Linux 3.5 ARM definitions](https://raw.githubusercontent.com/torvalds/linux/v3.5/arch/arm/include/asm/pgtable-2level.h) name mask`0x3c` as `L_PTE_MT_MASK` and zero as `L_PTE_MT_UNCACHED`; [pgtable.h](https://raw.githubusercontent.com/torvalds/linux/v3.5/arch/arm/include/asm/pgtable.h) defines pgprot_noncached with those values. These names corroborate the observed vendor instructions; the vendor bytes and live metadata establish this result. CPU-PTE encoder evidence `ev_27d5a082677e5bf4beda9a37aaeb9afa48258d4d95549c7a95f1f0c35050dcee` is retained separately. Actual selected-process hardware PTEs and current CP15 remap registers were not read.

## Important boundary for subsequent code signatures

`remap_pfn_range` in the original image contains a physical/virtual translation fixup site at`c009eefc`, listed in `__pv_table_begin..end` (397 entries). The captured instruction is `e2415481`, a SUB using the build-time immediate. The boot fixup explicitly replaces the low immediate byte at listed sites, using the runtime displacement. Therefore do not require this instruction to match the unbooted image or use its build-time arithmetic to walk live page tables. `__pv_phys_offset/__pv_offset` are known symbols but have not been observed. This caveat does not affect the six accepted identical windows.

The movie-work getter caches an allocation pointer; the word “cached” in that ownership discussion does not itself classify CPU memory attributes. RTOS-side mapping/cache state, completion and retention are still open. No new pixel copy, imaging command, register write, firmware execution, restart, persistent patch or push occurred.


## Follow-up: boot fixup observed, not assumed

A third fixed read-only reader pass joined the missing arithmetic to the running kernel. `__pv_phys_offset` at VA`c0771788`, physical`86771788`, is `86000000`; the next32-bit word stores the patch byte **`c6`**, not the complete displacement. Thus `(physical_base - virtual_base) mod2^32 = c6000000`; the inverse displacement is`3a000000`.

The listed site VA`c009eefc`, physical`8609eefc`, now contains **`e24154c6`**, exactly the captured `e2415481` with its low immediate byte changed to the observed`c6`. It executes `SUB r5,r1,#c6000000`, equivalent modulo32 to adding`3a000000`. This is consistent with the exact boot fixup loop, which clears only the instruction's low8 bits and ORs the patch byte. It does not identify the unique historical writer of that word or constitute a live PTE walk.

The same pass revalidated the six prior identical code windows and added all88 bytes of `memblock_search`; these500 unmodified code bytes match the extracted source. The patched4-byte instruction and8-byte runtime-variable pair bring the total read to512 bytes in nine fixed windows. Helper SHA256`90212961741150a5b6a7f1782b8b0efad6e3b640c20780e2932cfd1459649f82`; host contracts and two deterministic builds passed. Upload/download and camera hash matched, app identity stayed pinned, and the helper directory was removed and verified absent. Exact values, source/live comparisons and cleanup are in `kernel-reader/runtime-v3/pv-validation.json` and adjacent original responses.

This supersedes the earlier statement that the PV runtime words were unobserved. Actual selected hardware PTE/CP15 state and RTOS cache/producer handling remain unobserved. The new observation supports Linux address conversion for the reviewed code path; it adds no RAW-frame completion or retention proof.
