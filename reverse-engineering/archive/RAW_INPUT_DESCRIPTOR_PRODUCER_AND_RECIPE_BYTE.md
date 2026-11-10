> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Input-descriptor forwarding and recipe-byte writers

Reviewed 16 October 2026, offline. These findings narrow two unresolved questions; neither establishes an active R210 graph or sensor RAW pixels.

## The input descriptor comes from an enclosing object

Native function `0x80210a88`, whose diagnostic names `CapSetPPCore`, passes its third argument as the input-OTF argument into `0x803217d8`. The callee does not consume its third argument in the recovered body. It conditionally selects helpers `0x803224e8` or `0x80322514` using the enclosing object's byte `+0x44`, and then passes **enclosing object +0x50** as the descriptor argument to `0x803954e4`.

The previously recovered `0x803954e4` conditionally forwards descriptor `+0x14/+0x18` into the Core input setter. Along this particular caller chain those fields are therefore **enclosing object +0x64/+0x68**. This is a pointer-offset relationship, not evidence of their values, allocation ownership or pixel meaning. The writers of those enclosing fields are still unresolved.

The two small helpers are not descriptor copiers: `0x803224e8` obtains the Core and calls `0x8031afdc(Core, 0)`; `0x80322514` does the same with argument 1. Their initial evidence filenames use “copy”, but the function bodies do not support that name. Do not equate these fields with a sensor/RDMA row merely because other objects have the same offsets. Cleared memory-input slots still do not exclude an independent hardware input path.

Full native Evidence IDs:

- `0x80210a88`: `ev_9e17cbc285ef050578001ab7cf712a4553aa5008f66480ca266b067ea7d1ac37`.
- `0x803217d8`: `ev_6a4c05aee89746d8fc84adfbb08801f1a8116c4b4af268333aaeca76cdc411ff`.
- `0x803224e8`: `ev_9703b26e788e471dc24bd2433f9f49bf2f9fa33ba3ae94ecc8d9fff1211ac7de`.
- `0x80322514`: `ev_c21cf3722119ebaf0973d23a9605250a555fe485fcfc93a8634208d7e11cbe03`.

## The byte that selects recipe 1 or 3 has a bounded setter

Retained recipe-selection evidence shows `0x8027510c` obtains a byte through `0x8054e27c`, selecting recipe 3 when that byte is `0x15` and recipe 1 otherwise. Original instructions show that the getter reads global byte **0x81482e54**.

Writer `0x800fea54` accepts values 1 through 0x27 inclusive; on that path it copies the old byte to **0x81482e55** and stores the new value at **0x81482e54**. The inspected call sites include value 0x11 at `0x800ee26c`, value 1 at `0x800ee450` under a second-argument guard, and value 4 at `0x800ee66c`.

This is original-byte/disassembly evidence, not a new native REA dossier. The saved A32 reference scan is bounded: its eight unconditional BL sites exclude conditional, Thumb and indirect calls. A conditional call is separately preserved in the source windows. The semantic domain of this byte remains unknown; **0x15 is not proven to mean R210**. Tracing a named upstream producer is the next useful step, rather than interpreting a numeric value as a camera model.

## Evidence and limits

The [reviewed evidence index](../input-producer-recipe-byte-evidence.json) binds complete native records, snapshots, scans and original source windows. Parent checked all six recipe-byte windows against the original RTOS image and their address mapping, and checked four input-producer windows through the native evidence validator. Schema/hash checks do not certify the semantic interpretation or runtime activation.

RTOS source SHA256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`, base `0x80000000`; carrier SHA256 `3458e4295afe254257f5a533686f451a854eb327b5234a3ad043e4dd952dd6f7`. These initial artifacts were returned before external-only delegation was clarified. Internal workers were stopped; subsequent delegated investigation uses only the requested two external providers. No live camera command was run and no verified sensor RAW file exists.
