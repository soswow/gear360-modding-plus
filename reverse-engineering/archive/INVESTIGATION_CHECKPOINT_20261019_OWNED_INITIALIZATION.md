> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Owned capture initialization — reviewed checkpoint

Saved 2026-10-19T09:25:15.984116+11:00. Own software-triggered C/C++ capture library remains unfinished. Persistent goal created from PROJECT_GOAL.md after get_goal returned null; no budget invented. Intake300-minute usage86% used/14% remaining, latest92%/8%, weekly59%, reset2026-10-19T09:45:23+11:00. No reset credit or automation.

## Accepted advance

Parent review: `output/decompile/rea/pass-owned-init-20261019/parent-review.md`. Full REA evidence29 records and saved/closed snapshot, index, original-byte windows and arithmetic in same pass. Library SHA256 fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7. ELF addresses below; Ghidra adds10000hex for this library.

- Handle registry nodes are8 bytes, payload20 bytes holds shared CCapCmdExtIf singleton and separate0x120-byte storage. Membership validation does not establish acquisition readiness or device event isolation.
- create2a6e4 saves allocator result, calls realize at2a7d8, reloads old result at2a7dc. Initialization failure is lost. OperateCapture4920c creates virtual-address singleton, zero pref1268 branch sets1 before adapter Initialize; nonzero branch returns0 skipping that initialization. No permission to reset pref or execute competing contexts is implied.
- destroy2a800 removes/frees handle and global last-instance bookkeeping; payload cleanup32528 can unregister process-global notification ids/client and clear shared table. This does not isolate callbacks per handle. Separate unrealize2a91c issues01070002, whose dispatcher waits a power-off event; no complete shutdown/recovery contract is proven.
- CCapVirtualAddrIf::Start64c28 only logs. Actual constructor649ec calls Initialize650dc with a6000000,bfffffff; GetVirtualAddr65c88 uses /dev/mem raw openflags101002, shared writable mmap prot3/flags1. Argument length19ffffff rounds to1a000000=436207616bytes=416MiB. Failure aborts through assertion; Uninitialize65478 munmaps base/extent and asserts on failure. Mapping gives no RAW ownership, fresh-completion or cache proof. No mapping executed.

## Worker and review state

Cursor research worker requested grok-4.7-high; init metadata confirms Grok4.7 256K High, sessioncc952f1a-8566-417d-8592-18a38f05b4ca. Sanitized one-ELF packet, exact instruction read allowlist, no camera/network authority. It independently traces adapter initialization; stream/launch/routing/tool-scope records retained under pass/cursor-init. Pending final review; do not promote its unfinished findings. Parent native session exported and closed. Final worker disposition and usage will be appended before end.

## Next contract

Review Adapter Initialize setup/return/error propagation, then determine bridge/driver receiver ownership and real RTOS acquisition setup. Prior libudd5 review still needs kernel open/release and ioctl c0107406/80087409/8018740a semantics. Only then choose and validate a callable initializer with explicit rollback. Still need sensor/RAW/exposure configuration, request-to-producer association, fresh RAW completion, byte format/provenance, cache and owned-release API, repeat target captures. Do not substitute handle creation/mmap/request acceptance for capture readiness or frame ownership.

No source edits/commit/merge/push. Source master5b31dbf907fb65ac5a812e7e0b5756cda1a70ac9 remains previous reviewed milestone. This pass is static research; no camera/helpers/settings/files or service changes. Notes/evidence stay ignored/private.


## Final handoff 2026-10-19T09:26:36.439408+11:00

Owned Grok worker stopped with SIGINT at parent94% usage boundary; PID absent, launcher exit recorded. Partial report is UNREVIEWED; no worker claims promoted. Parent29-record bundle exported,28 primitive bindings/snapshot saved and REA session closed. No camera/helper cleanup needed; no source edits, master remains5b31dbf907fb65ac5a812e7e0b5756cda1a70ac9 clean/ahead88. Next: resume/review adapter initialization packet/session, then kernel queue ownership and actual producer/completion/RAW ownership. Goal active/unfinished; no completion or pause asserted. No automation/resetcredit/push.

Final actual usage:300-minute94%used/6%remaining; weekly60%used; reset19October2026 09:45:23Sydney. Resource boundary ends this pass; persistent goal remains active/unfinished.
