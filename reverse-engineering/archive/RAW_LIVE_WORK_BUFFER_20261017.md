> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Selected Bayer preview: allocation and export boundaries

17 October 2026. Continue from [the full-buffer/Bayer review](RAW_FULL_BUFFER_AND_BAYER_20261017.md). These findings identify the observed allocation more precisely; they do not certify a coherent sensor frame.

## Current image and containing allocation

Three fixed read-only allocator-prefix observations returned identical 28,672-byte files, SHA256 **`1339547e3996de21b811c90074df4ded683fb30aa00daa60d675d6869c98836a`**. They cover manager `M=0x81cbb000`, including allocator records 0..1780 at `M+0xb0`, sixteen bytes per 16-KiB unit. The helper checks the manager cell, arena base, selected image pointers, vptr, geometry, stride and PData before and after copying to Linux-owned memory. Device mappings are read-only and dropped before export. Each transfer passed camera/Mac hash, app identity and cleanup checks.

All observed records have **tag 40 decimal (`0x28`)**, with remaining length `4557-index`. Record zero has sequence0 and timestamp1149; interior sequence/timestamp words are not interpreted as independent allocation identities. Retained allocator writer `0x800afef8` marks each allocated unit with the tag and decreasing remaining length, and writes sequence/timestamp only at the run head. This identifies a containing run beginning at `0xa6000000`, recorded length4557 units, rounded end `0xaa734000`. The selected extent `0xa77bb000..0xa7a5dfff` lies inside it.

Fresh observations through already mapped RAM pages returned these values unchanged before/after the prefix copy:

| Field | Observed value |
|---|---|
| Cached movie-work descriptor at `M+0x6859c` | base `0xa6000000`, size `0x04731000` =74,649,600, multiplicands1 and1 |
| Selected image+`0x54`, low byte | format1, firmware-labelled Bayer |
| Selected image+`0x58`, four bytes | pixel2 / packing2 / packtype0 / knee0 |
| Selected image+`0x5c`, low byte | CFA enum1; phase name unresolved |
| Selected image+`0xa4` | stored request `0xffffffff` |
| Selected image+`0xa8` | backing-byte count2,764,800 |
| Selected image+`0xb0` | kind word0 |

Padding in the surrounding words is retained raw, not interpreted. The older 160-byte image observation also contains the name `movWrk` at +`0x78`. The new read reaches fields beyond that older view. In particular, current request `0xffffffff` and kind0 differ from the separately traced Still candidate's request22/kind1; they must not be substituted for that candidate.

Parent REA analysis resolves **`getMovieWorkBuffer` at `0x800ae348`**, evidence `ev_783a482426ed234ceb806681adac249f59506abd72ebe77576e3fcc5a0ef531c`. When `M+0x6859c` is zero, it initializes a size descriptor from arguments3×4, requests the generic allocator with tag`0x28`, caches the returned base and returns it. Otherwise it returns the cached base. Its active manager-vtable slot is **+`0x70`**. Descriptor initializer `0x8054aa98` writes size at +4 and1 at +8/+0xc; getter `0x8054ac10` multiplies those fields. These original-code facts and the contemporaneous descriptor/run observations strongly connect the selected extent to the cached movie-working allocation.

Let N=2,764,800. The recorded byte capacity is exactly **27×N**, and selected PData is **base+9×N**. This is address/size arithmetic. It does **not** prove a 27-frame ring, temporal slot9, frame rotation or a lease. The cached getter itself contains no rotation logic. Luna found a conditional virtual+`0x70` caller in `0x800d3c5c` when selector byte`0x81482d90` is1, but its receiver and runtime activation remain unbound. A failed query against an older trimmed carrier was discarded; it is not negative source evidence.

## Copy speed and Pause

The earlier reader was built without an optimization flag. The parent had mistakenly described it as O2 in a text-only MiniMax review prompt; that premise was corrected by inspecting the actual build script. The current build uses **O2 with vectorization disabled**. Parent inspected its exact scalar32-bit loop (0xa8c00 words), retained volatile guard/cadence loads, read-only mappings and cleanup. Host behavior tests and two identical ARM builds passed; no source-text/register-name tests were retained.

The optimized helper SHA256 is **`8c97cdf9a5627fb9640aac6c55a4ab638e041fa8913a59265cd5fc35b221e9ad`**. One comparative passive snapshot, SHA256 **`9355243f3e966bff473b288a66daa7138de6d6526ed953030cae1f5d223eb230`**, still crossed **seven VD-falling increments (1670→1677)**. This does not establish that CPU loop overhead was the bottleneck, that the copy is frame-atomic, or that a particular WDMA wrote during it. Further speed-only repetitions were stopped.

Luna completed the remaining normal Pause leaves. `0x804ff5f0` writes mode7, calls known stop`0x80511ba8`, calls `0x8050d368(0)` and `(1)`, then writes mode2. `0x8050d368` performs fixed register writes, with one unused read on its second arm. `0x80511470` dispatches the relevant modes to setters changing bits19/31 of`0x20800000`. The traced sequence contains no source-visible completion poll or drain. This does not exclude asynchronous hardware semantics; it means a software Pause return is not presently sufficient proof of a completed, retained image.

## Generic exporter callers are Still-adjust callbacks

A source-wide aligned ARM BL scan and three new REA dossiers resolve two direct callers of `CAdjustAdapter::dumpImage`: **`0x8032fac4`** uses mode`0x100`; **`0x8032fbcc`** uses mode`0x200`. Dispatcher **`0x8032f838`** gets the image from `0x80251c58(material,0)` and chooses a handler using the material tag. Both handlers continue processing that same image after the synchronous dump call.

Parent original-string review identifies their source path as **`product/Still/Callback/CB/CCallback_Still_Adjust.cpp`**, rather than establishing an active live-preview exporter. No observed argument links these callbacks to image`0x82246000`. Their synchronous descriptor lifetime is established; pixel retention and a finished-frame handoff are not. Do not enable flags or assume these handlers pause the current movie-working producer merely because they call the generic exporter.

## Kernel mapping lead, unfinished

One read-only `/proc/kallsyms` query established running-kernel symbol addresses: `_text=c0008000`, `_stext=c0008200`, `pfn_valid=c00153e8`, `phys_mem_access_prot=c00168ec`, `mmap_mem=c0203590`. The running banner matches the captured Linux3.5.0/#175 build; documented Linux System RAM is`86000000..9ddfffff`, outside the selected pixel address. This is a precise way to investigate Linux mapping attributes using the captured p5 kernel, without guessing upstream/vendor behavior.

The delegated MiniMax extraction task hit its step limit after writing an **unexecuted, unvalidated script**. It produced no extracted kernel or cache-attribute conclusion. Its script needs proper CRC/bounds enforcement and recognition that an uncompressed uImage payload can itself be a compressed ARM zImage. Preserve this as unfinished work, not evidence of noncached mapping or DMA coherence. No additional kernel-memory read was performed.

## Finite next work

1. Bind the current movie-working allocation's consumer/producer and publication rule. Determine whether 27×N describes working regions, frame slots or another layout before designing further sampling.
2. Establish fresh completion and a stable interval for the selected bytes. Equal hashes, allocation containment and normal Pause state are insufficient individually.
3. Resolve Linux mapping/cache behavior from the captured vendor kernel and a source-bound runtime identity check; do not replace that with generic ARM/Linux assumptions.
4. Follow the Still-adjust exporter only if its active image/owner can be joined to the intended acquisition. Keep its scope separate from the current preview and the older MID0x15000 candidate.

The main RAW acquisition goal remains unfinished. No imaging command, register write, service restart, persistent patch or remote push occurred during this pass.
