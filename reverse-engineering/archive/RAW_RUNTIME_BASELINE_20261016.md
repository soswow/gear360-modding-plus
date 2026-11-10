> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# First live RAW-investigation baseline — 16 October 2026

The user connected the camera, authorized quick access checks and then gave a go-ahead to continue. Parent performed bounded runtime observations using the existing root web shell. No image capture, mode selection, firmware patch, service restart, preload, camera-memory write or new-helper deployment occurred in this baseline. The five-hour allowance refreshed before it began; the current cutoff remains95%used/reserve5%.

## Build and process identity

Five camera-side SHA256 results exactly match the offline sources:

| Live artifact | SHA256 |
|---|---|
| `/usr/apps/com.samsung.di-camera-app/bin/di-camera-app` |5937b3abe7630abc8ac265ce280cc45ee591562968296bc4b368cf7e15742789 |
| `/usr/lib/libcapture-fw-prod.so` |fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7 |
| `/usr/lib/libmulticore-bridge.so` |9129761c6ee9f6b62968d3a8307a8b5fbf02e1493323658c42d9ebe587673a20 |
| `/usr/lib/libmm-photo.so` |22d7d37f3d8e28234fbc450d0be959a2655500242a5986e43dc5ee593bfa9c46 |
| `/dev/mmcblk0p6` |cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7 |

The app is PID254,21threads, with the expected executable and imaging libraries named in `/proc/254/maps`; its stdout/stderr point to `/dev/console`. FD18 is `/dev/d5_ipcc`; FD15/16 are `/dev/mem`. These observations connect the on-disk artifacts to the running process. They do not establish that every live instruction, selected recipe or MMU table matches our offline assumptions.

The existing `hdr/camera-mode.py` get-only script was read and reused. A bounded strace of the shell thread captured fresh `st app uscap 428` output: camera-mode type1, photo, matching its DB value. No mode setter was called. A later process/status check found unchanged process start time and TracerPid0 on all21threads, with no owned trace directories/locks left.

Linux CPU reports ARMv7 Cortex-A9 part0xc09. This identifies the Linux CPU, not the core executing RTOS, so it does not resolve the RTOS startup's conditional MIDR branch. The root kernel command line names Linux RAM as382MiB; `/proc/iomem` reports System RAM86000000–9ddfffff.

## Actual mapped memory and access limits

The camera process already has shared `/dev/mem` mappings at the arena physical offset **a6000000** (length1a000000), plus several shared control regions around85400000–85c61000 and other buffer regions. This proves live Linux mappings with those offsets exist. It does not prove a particular capture owns an arena run, cache visibility, or RTOS virtual identity.

A small ordinary-RAM signature attempt used `/dev/mem` read via `dd`, offset80000000,64bytes. It returned no bytes. A diagnostic repeat retained stderr and showed **Bad address/EFAULT**. No source signature was validated. This result does not prove mmap would fail, nor that the RTOS firmware is absent. No imaging MMIO address was accessed.

The Linux system has neither sha256sum nor busybox in the CGI PATH; absolute `/usr/bin/openssl dgst -sha256` worked. `pidof` is unavailable. `/sbin/hwclock` exists outside that PATH and was used in the separately authorized earlier clock correction. FTP root login/working-directory query succeeds; no FTP file was transferred in this baseline.

## Stock diagnostics and evidence boundaries

Parent reviewed the compiled `fenx` dispatch and its fewer-than-two-arguments usage branch before invoking exactly `st cap fenx`. Its helper makes15logging calls; parent does not confuse logger object writes with capture-control actions. The short all-thread Linux write trace contained housekeeping pipe writes but **no fenx help text**. Therefore runtime registry membership and delivery of RTOS debug logs remain unconfirmed. Silence does not establish absence, nor that the command ran in RTOS. No capture subcommand was sent.

Cursor's bounded review extracted useful diagnostic arguments but left readily available literal text undecoded. Parent decoded it directly from pinned p6 bytes. The capture handler's geometry log is exactly `[DBG] stride = %d, gwidth = %d, gheight = %d, width = %d, height = %d, addr = 0x%08x\n`. Those arguments are software image fields after configuration, not independent hardware/sensor measurements. Its wait error format does not print the returned status. The `dram` command dereferences the allocation pointer, so it must not be used as a harmless null-state query.

Raw root-file glob produced no `.raw` entries at that moment; this is not a full-card search or a claim that no RAW-named files exist elsewhere. No verified sensor RAW data has been obtained.

## Next bounded observation

The [fixed RAM reader](../../tools/probes/ram-reader/README.md) is built and reviewed offline. It opens `/dev/mem` O_RDONLY and maps only fixed ordinary-RAM candidates with PROT_READ, accepts no arbitrary addresses, follows no pointers and has no MMIO/capture/control/write functionality. Its5588byte static ARM binary has SHA256 `713b39ee23a1c797612d44b6a009345a0c941633c27737a8a31bee4e5933bc5c`; the parent rebuild is byte-identical. Host-only mocked contract tests pass, but cannot prove target syscall execution or actual mapping.

A separate deployment clarification was presented because the earlier investigation explicitly prohibited deployment. Proposed action: stage the hash-verified reader in a unique `/tmp`directory, test invalid/no-argument startup without memory access, then inspect fixed immutable signatures. Stop on error/mismatch and remove the staging directory afterward. No deployment or target execution is claimed by the offline build. Until signatures establish backing, mutable global snapshots must not be interpreted as live objects.

The [finite acquisition plan](RAW_ACQUISITION_DECISION_PLAN.md) remains the goal guide. A first sensor file still needs actual input/geometry, fresh DMA completion, backing ownership and byte-level pixel validation. No broad re-mapping pass or repeat of the existing stale-pointer capture route substitutes for those proofs.

## Evidence and review method

Raw marked HTTP requests/responses, digests, full Linux traces, failed reads, agent reports, source-bound literal windows and the helper build/tests are under `output/decompile/rea/pass-runtime-20261016/`. Versioned baseline bundle (not published: `../evidence/pass-runtime-20261016-baseline.tar.gz`) and member/hash index (not published: `../evidence/pass-runtime-20261016-baseline.index.json`) preserve this milestone. The parent reviews original bytes and actual returned data; agent prose is not accepted as runtime evidence. Earlier2017 timestamps remain historical; current system/RTC time survived the user's restart.

## Subsequent authorized RAM observation

The user explicitly approved temporary read-only helper upload and execution after this baseline. [Live RAM observations](RAW_LIVE_RAM_OBSERVATIONS_20261016.md) record the successful mmap access, two exact source anchors, mutable copied-data mismatch, guarded selected-image geometry and activity counter. This supersedes the pending-deployment status above while preserving the baseline timeline. No capture, MMIO access or camera-memory write occurred.
