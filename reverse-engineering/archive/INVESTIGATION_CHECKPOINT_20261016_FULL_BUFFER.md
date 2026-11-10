> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Unverified 1920×960 snapshot saved

16 October 2026, about 12:27 UTC. `output/decompile/rea/full-buffer-read-20261016/UNVERIFIED-full-buffer.bin` is 2,764,800 bytes, SHA256 `5c7a7c6ce9fb20d467e43806cd85350024311ee288a4dafcdd1a73c2e19f06e9`. Camera and Mac hashes match. The helper reading the buffer at `0xa77bb000` exited 0 after the fixed guards matched. `di-camera-app` stayed pid 254 / start time 1042, with 21 untraced threads. Temporary files were removed. No capture command, MMIO access, or camera-memory write was used.

Parent review and subsequent passive timing observations are in [the 17 October review](RAW_FULL_BUFFER_AND_BAYER_20261017.md). The original sample and transfer evidence are preserved in the versioned archive (not published: `../evidence/full-buffer-read-20261016.index.json`).

Little-endian packed 12-bit samples at stride 2880 show two fisheye circles. The left circle is a room; the right circle is mostly dark with a visible rim. MSB-first packing and raw bytes do not produce that clean picture. The first 4096 bytes differ from the earlier fragment. Report and pictures: `output/decompile/rea/full-buffer-read-20261016/report.md`.

This is still not verified sensor RAW. Guards do not prove frame coherence, sensor origin, or a lease. The 4096-byte fragment remains at its original path and hash.
