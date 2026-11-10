> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Capture library architecture correction

The stable goal and acceptance criteria now live in PROJECT_GOAL.md (not published: `../../PROJECT_GOAL.md`). This note preserves the specific user correction; use the current runbook and index to resume.

Saved 2026-10-19T08:43:18.597470+11:00.

## Governing architecture correction — 19 October 2026

The user wants **our own callable C/C++ capture library that reconstructs/reuses the capture mechanisms reverse engineered from the stock .so libraries, with our code controlling initialization, configuration, programmatic trigger, acquisition, completion and output ownership, including keeping RAW.** Our implementation replaces/bypasses the stock app's capture/processing orchestration as needed. Stock processing is optional and must not define the product architecture.

The product is not a RAW tap attached to stock-app captures, a copy-before-stock-release hook, a debug dump backend, or a physical-shutter workflow. A CLI/network/service can call our library; the physical shutter must not be required. Replacing the entire main app is not the deliverable. Physical-shutter observations already collected remain diagnostic evidence only.

Reuse stock capture code/contracts, sensor drivers and RTOS services where useful; determine their actual responsibilities before choosing reuse versus reconstruction. Do not assume every component must be rewritten, or that lack of a RAW arm in the stock Linux image callback proves our library needs an RTOS patch. First determine what lower-level capture controls/data paths our own orchestration can use and what the minimum missing interface is. Firmware changes remain unapproved.

**Next investigation:** reconstruct the minimal programmatically callable capture engine: Linux .so entry points and dependencies -> initialization/context ownership -> sensor/RAW-mode/exposure configuration -> software trigger/submission -> lower-level producer/completion -> RAW result owned by our caller -> explicit release/shutdown. Separate required acquisition operations from optional image processing, stock-app policy, callbacks and UI. The existing48-byte still-request adapter covers only one submission component. Read current source-bound capture-ABI/app/bridge evidence first; extend missing initialization, control and RAW acquisition contracts rather than returning to generic allocator/lifetime surveys.

Memory ownership, completion and cache handling still matter as implementation contracts of **our** capture pipeline. They are not a reason to centre the investigation on preserving stock processing or racing the stock app. Prior release-hook findings are supporting evidence about stock internals, not the selected backend. Do not resume the old producer-post/pre-release-copy decision as the default next task.

Preserve original evidence and existing authorization/privacy/resource boundaries. Do not create a duplicate persistent goal or schedule another run. In unattended sessions do not depend on manual shutter, power cycle, reconnection or scene changes.

## Parent correction

The earlier answers framed the task as retaining pixels from a stock-app still and exporting them before stock release. That was an incorrect architectural assumption. The user is asking for the acquisition engine itself to be under our control. Existing release and allocation traces can inform reconstruction, but they must not dictate an app-interception product or a physical-button dependency. No new reverse-engineering or camera proof is claimed by this clarification.
