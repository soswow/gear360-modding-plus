> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Investigation checkpoint — 15 October 2026, 00:40 restart

The subsequent [05:40 restart checkpoint](INVESTIGATION_CHECKPOINT_20261015_0540.md) records the newer candidate-export, driver, event-state and timeout findings. The remainder of this file preserves the 00:40 pass.

This scheduled pass resumed at 0% five-hour usage. The closing measurement reached **91% used / 9% remaining**, crossing the approximately 90% boundary. Six Luna workers traced focused scopes and reviewed the evidence; the parent integrated validated findings. These are account percentages, not exact per-task token counts. No reset credit was consumed.

The [reviewed findings](RAW_ARENA_DMA_AND_CACHE.md) and source/artifact index (not published: `../arena-dma-cache-evidence.json`) preserve 18 selected original-byte windows, 18 cited REA records, 59 distinct native snapshots, 113 retained analysis-artifact hashes and 30 frozen reviewed reports under `output/decompile/rea/continuation-20261015/pass6/`. The first milestone is committed as `82cf94f`; this checkpoint accompanies final local integration.

All workers finished and native targets are closed; connected REA reports `open:false`. One Ghidra close returned a host process-token verification error. An ownership audit confirmed its exact PID/process group had exited; its stale runtime directory and audit were retained. The persistent REA MCP server was preserved.

The main advance is a conditional image-pointer path: material slot zero → six-row table at byte `+0x140` → allocated derived frontend C → CPP_Preview B → CPP_Core A output0 → PP WDMA register programming. Actual derived methods delegate to the traced base implementations. Route A orders configuration before frame start while prior methods succeed; route B does not call that frame-start slot. No live R210 route, numeric geometry, sensor pixel contents or DMA completion was observed.

The concrete 64-MiB arena provider/release and cache service are recovered. Captured non-`0xc09` startup configures an identity map for the arena; actual CPU/MMU state remains unknown. Ipc_End releases and clears an image backing, but its identity with the candidate remains unproved. FileDump suppresses maintenance/wait failures and protects its stack descriptor during the wait; it provides no pixel-buffer lease or proof of file output.

Continue with these exact boundaries:

1. **R210 route and activation:** trace callers/setters of wrapper flags used by `0x8031feac`, wrapper at `*DAT_81952fbc`, flag helpers `0x80568be0`/`0x80568c08`; establish selected six-row descriptor and route. Construction code alone is not a real-shot observation.
2. **Geometry:** role `0x13` defaults rows 25/26 to zero; crop attr `0x191` supplies a zero predicate byte. Derived `VideoSetSensorMode` (`0x8020eda8`) calls device slots `+0xa8/+0xd0/+0xac`; resolve those effects and actual W/H writers.
3. **Sensor input/completion:** connect the chosen output to actual sensor/SSIF input and identify completion after `0x8031c098`. The separate combined RDMA/WDMA wait cannot prove it.
4. **Lifetime/export:** bind this exact candidate to its release owner and prove a completed pixel copy before reuse. FileDump ACK/zero return cannot validate pixels; saved size/content need independent verification when live work is later authorized.
5. **Address/sensor identity:** establish actual MMU branch/later mappings and active imaging sensor. Chip-named attr `0x18e` has a packed-length/wrapper-size mismatch and unresolved semantics; it currently cannot identify the sensor.

The one-time **05:40 Sydney restart today** remains active. Its prompt handles a reset shortly after wake: if usage is at least 90% and reset is within two minutes, wait in pauses no longer than 60 seconds, recheck, and resume only with refreshed allowance. Otherwise save and stop. The reported reset is 05:40:24; the requested wake time is unchanged.

Preserve captured bytes and unrelated workspace work. Continue offline with Luna tracing and parent review: no camera commands, firmware execution, restart, preload or deployment. Commit validated task artifacts locally to master. Sensor RAW capture remains unfinished; no verified RAW frame has been recovered.
