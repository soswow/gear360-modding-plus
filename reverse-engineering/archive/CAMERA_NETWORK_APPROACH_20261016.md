> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Brief camera access check — 16 October 2026

The user explicitly connected the camera and authorized quick approach tests, while deferring a full investigation. This supersedes offline-only scope for these bounded access/identification checks; it does not authorize capture, configuration, service restart, preload, deployment or live memory/register access.

## Observed

- Supplied host192.168.43.1 is routed through en0. TCP80,22,443 refused connections. TCP23 accepted and presented the Samsung Linux Platform banner and `drime5 login:` after Telnet option negotiation. No Telnet login or application command was sent. These four checks are not a full port inventory.
- README.md already documents the correct web URL, `http://192.168.43.1:8888/`, and Telnet root/no password. Parent should consult these existing instructions before rediscovering access.
- HTTP8888 returned200. The documented `/cgi-bin/execute` endpoint, GET parameter `rawcmd`, executed the exact identification-only command preserved in evidence. Its marked output reported uid0/gid0, Linux drime5 3.5.0 #175, ARMv7l, uptime526.90seconds. `/mnt/mmc` resolved to the SD-card filesystem, with7,790,272KiB available (about7.43GiB).
- `pidof` is unavailable. This does not show that the camera process is absent. Use an existing supported process-list method next time.
- Camera HTTP Date reported3February2017. Use host UTC timestamps for evidence sequencing; do not trust camera wall-clock alignment.

## Approach for the next authorized run

Use the existing web command endpoint for small bounded read-only baselines and record exact command, response, host UTC time and hash. First identify the running camera process and compare relevant on-device build/library hashes with the offline image. Reuse repository access helpers only after reading them: some status/mode helpers attach strace or dispatch camera commands, which are beyond this smoke check.

Then follow RAW_ACQUISITION_DECISION_PLAN.md: establish active input/geometry, fresh DMA completion and same-allocation retention before proposing a single capture trial. Root shell access is now proven; safe sensor capture is not. Do not run the recovered capture command as a connectivity test. No RAW pixels or new image were captured here.

Complete requests, raw responses and hash index (not published: `../evidence/network-smoke-20261016/index.json`). The initial failed HTTP80 request is retained as part of the trail. No agent delegation or native REA session was needed for these simple network checks.

## Clock correction explicitly authorized afterward

The user authorized correcting the date. The Linux clock was set from host UTC, then `/sbin/hwclock --systohc --utc --noadjfile` successfully updated the hardware RTC. Both independently read back16October2026 08:29:21UTC (19:29:21Sydney). The RTC previously matched the old UTC system time. No timezone/configuration or adjtime file was changed. Persistence across reboot or battery removal remains untested; no restart was performed. Requests and responses (not published: `../evidence/clock-sync-20261016/index.json`). Earlier2017 timestamps in the smoke evidence are historical and should remain unchanged.

After the user restarted the camera, a read-only check at host08:30:43UTC found both system and RTC at08:30:42UTC on16October2026, with uptime57.63seconds. The corrected date survived this restart, with clock offset approximately2seconds. Retention across battery removal remains untested. Exact responses are in the same clock evidence index.
