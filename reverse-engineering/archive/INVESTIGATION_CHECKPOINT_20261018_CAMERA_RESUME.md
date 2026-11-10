> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Camera-resumed still-route checkpoint — 18 October 2026

User explicitly restored camera access and resumed the investigation. Camera connectivity is no longer the gate. The exact sensor RAW acquisition objective remains unfinished. No RAW pixels were copied in this pass and no completed still frame is certified.

## Reviewed runtime result

The documented command interface is `192.168.43.1:8888/cgi-bin/execute`; FTP21 also responds. Port80 refuses connections. Earlier port80 HEAD probes alone were not a valid test of the command interface; the later prior FTP timeouts were independent availability failures. All commands and results for this pass are under `output/decompile/rea/pass-camera-resume-20261018/`.

Camera app remains PID254/starttime1042 with21 untraced threads. Two64-byte immutable code windows at physical80000000 and80541000 match the retained RTOS image, source SHA256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`. This is a code-anchor check, not a hash of the entire running image. Process maps/uname were retained. Original reader rebuilt byte-identically, SHA256 `2cf81d50e299d4cde2c8c492b183881a607841e524f1ff512f2662cd9b6189a6`.

Fixed singleton reads, followed by fixed-address object reads under matching guards, establish the idle chain:

| Object/field | Current observation | Limit |
| --- | --- | --- |
| Interface I | cell81952fb8→81da0000; head806aead0; I+4→81d78800 | Constructor-compatible current identity; no capture invocation observed. |
| Wrapper W | cell81952fbc→81d78800; W+0→81d7f800; W+7=1 | Configured nonzero still-wrapper branch if invoked; not a RAW-mode or activation proof. |
| Frontend C | cell81952fc0→81d7f800; head806718a0 | Fixed object guard verified. |
| C output routes | enable bytes+8/+38=1; respective images+c/+3c=82246000; other four pairs disabled/null | Idle configured routes. |
| Embedded capture Q=C+50 | head806c0970; Q+10=0 | No selected capture output at these observation times. |
| CPP_Core | **in-place at81952f94**, head806ae1e0; Core+10=82246000 | Getter8031aa88 returns the address, not its first word. Do not follow806ae1e0 as an object pointer. |
| Selected descriptor82246000 | head8067a158; width+4=1920; height+8=960; stride+60=2880; format byte+54=1; PData+84=a77bb000 | Same preview-sized descriptor, not actual still sensor provenance. No pixel copy this pass. |

Existing original instruction checks were reused for I/W constructor links, Core getter/constructor and descriptor offsets. Core constructor8031a9a4 loads head literal at8031aa0c=806ae1e0; I constructor loads8031e3c4=806aead0. Descriptor offsets were checked against the existing full-buffer reader guards. An initial scratch calculation incorrectly assumed stride5c/PData7c; the nonsensical result was rejected and corrected to source-bound60/84 before acceptance.

## Finite observation experiment

A local experimental Linux metadata sampler uses four fixed PROT_READ/MAP_SHARED pages opened via O_RDONLY `/dev/mem`. It never follows loaded pointers or writes target RAM. Guards check I/W/C singleton values, heads and I+4 before and after each record. Two code anchors are checked at startup. Every mapping/guard/output/sleep failure exits through cleanup. Modes are finite once1/test20/sample2400; sample sleeps50ms between records. It writes only metadata to stdout, redirected into an owned temporary Linux file. No firmware code is invoked.

Sampler source SHA256 `51808b69923de01dee9f67aa6e5f854fb76afa365dafd5eafc677a7f7350698c`; ELF SHA256 `865be8624e7ee6a7d62c7da3465eeab2cd165cad1554b1255d5363bddbb15d9c`. Parent reviewed bounds, record layout and source. Luna's bounded local source review found no concrete target-write, page-bound, record-overlap, unbounded-loop or cleanup issue; it did not independently verify camera output. Its report is `sampler-review.md`. Startup rejects missing mode with exit2. Once and20-record timing trials passed camera/host hash, size, sequential-header and zero-padding checks.20-record request took1.179564seconds including transport.

Full observation host envelope: **20:04:26.451981–20:06:26.805874 Sydney**. Camera printed UTC09:04:27–09:06:27 and exit0. Exactly2400 sequential496-byte records /1,190,400bytes transferred with matching camera/Mac SHA256 `1802878165e71bb8e7e80d476deb5bc442743bbb6d340b4390414e326033b2e2`. Record data occupies bytes0..395;100 reserved bytes are zero. Raw file, source, build, exact commands, headers, requests, hashes, parser and analysis remain local.

Across these samples, W+7 remained1, Q+10 remained0, Core+10 and enabled routes stayed82246000; I+1c stayed1; C+268 stayed20400, C+280 stayed1e and C+291 stayed0. Meanwhile C+297 had1024 sampled changes (1/5), and C+314 had1113 changes (0/1/3). This supports the narrow observation that those checker fields change while the observed output remains the preview descriptor and Q output is null. It does not turn either field into fresh completed-still identity.

No ordinary shutter event was confirmed by the user during the window.20Hz polling can miss short transitions; unchanged sampled fields do not prove no capture could have happened between samples. Guard equality does not prove coherent DMA, atomic metadata, cache synchronization, stable backing, lifetime or sensor freshness.

## Next action and exact gate

The sampler is tested and ready to rerun for a **coordinated user-operated ordinary still photo**. User was asked asynchronously whether they can press the shutter; no answer/event confirmation arrived during this pass. Do not ask them to press after a sampling window has ended. Announce a fresh window before the press, retain host/camera times and user confirmation, then compare route/Q/Core/descriptor identities. Stop on a guard change; reconcile the new fixed address before reading it. Do not follow a changed pointer automatically.

This is the first new resumed-epoch event-availability gate, not three consecutive new blocked turns. Goal tool still reports blocked from the earlier connectivity epoch and exposes no active-status setter; exact objective retained without duplication. No new blocked transition was requested. Investigative work resumed at the user's instruction. Camera availability itself is resolved.

Standing boundary: the continuation prompt allows read-only diagnostics and temporary verified helpers. It says an agent-issued capture or firmware instrumentation needs a concrete separately authorized trial. Camera access alone does not select such a trial. The existing still report already names submission/completion trace and retention/export requirements; repeated static class/allocator/status catalogues will not provide this missing runtime event.

## Cleanup, privacy and resources

All seven owned helper directories, including the full sampler's `/tmp/gear360-ram-reader-efe1c0a142`, were removed and absence verified. No camera helper process remains from the finite run. No worker/native REA session owned by this pass remains. No capture command, pixel/media read, settings change, firmware/MMIO write, injection, restart, source commit or push occurred. Master remains clean at2874ae9c01099ced1bbf798c0c5af9cb9c18e7c6. Experimental readers/notes/evidence stay ignored/local; no production reader was changed. Original kickoff archive remains untouched.

Last account reading:66% used /34% remaining in the300-minute window; weekly26%; reset18October23:30:31Sydney. No reset credit/purchase/new automation. Stop is the coordinated still-event dependency, not the reserve boundary. The RAW goal is unfinished.

Final cleanup review: FTP NLST for an absent directory returns an empty successful listing on this camera, rather than550. The initial final-check assertion rejected that API behavior; corrected verification independently checked all seven names absent from the parent listing and each CWD rejected550. `final-health/verification.json` records the accepted checks. No leftover was found. The first archive was frozen before this final verification/wording correction; use the final archive linked by CURRENT_INVESTIGATION.
