> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Acquisition direction review — 18 October 2026

Reviewed at 2026-10-18T14:46:02+11:00 after reading the complete two-turn chat `01a14d15-1d13-72b2-955e-731085d9aaba` (Summarize latest Gear 360 status) and the retained acquisition/Still/debug reports. This is a strategy correction, not new native or runtime proof.

## Decision

The goal remains a reproducible sensor RAW image export. The convenient preview buffer has acquired too much priority relative to finding and reproducing a camera capture sequence. The kernel mapping and movie-role findings are useful, accepted prerequisites for that particular reader; they do not establish that preview is the preferred RAW acquisition route. Stop expanding allocation internals and kernel mapping for their own sake. No new passive pixel copy is justified just by camera availability or a colour chart.

The next bounded investigation must produce an end-to-end still-capture call/data sequence, or identify its first concrete missing edge and a minimal way to observe it. Its intended implementation deliverable is C/C++ acquisition code using a proved control/export interface, or a precisely scoped RTOS-side copy/export change if the existing interfaces cannot supply RAW. Linux C++ cannot simply invoke RTOS function addresses. Do not implement guessed addresses or an interface-only mock and call that capture.

## Route comparison

| Route | Actual evidence | Missing acquisition-critical edge | Priority |
|---|---|---|---|
| Ordinary still-photo / RAW producer | StillCapture interface `8031f274`; RAW manager and callback chains; candidate configuration/allocation/dump paths | Which path an R210 photo activates, where its sensor RAW backing is completed and still retained, and a working export handoff | Primary investigation: recover the actual sequence, without assuming a Still class name proves a RAW output |
| Stock FrontendNx SSIF debug capture | Explicit parser/start/configure/stop/dump path; separate image/allocation | Wait is VdFalling rather than proved WDMA done; immediate stop has no recovered drain; failure and stale-global reuse hazards; requested geometry is conditional | Bounded alternative/proof of concept; do not execute unmodified merely because it is shorter |
| Passive preview copy | Full packed-Bayer-like bytes; observed geometry; mapping policy; allocation layout | Active source, completed frame identity and stable copy interval | Secondary diagnostic; no more allocation tracing unless it closes a named producer/completion/export edge |

## Prevent repeating old dead ends

- `MAIN_RAW` callback registration alone does not work: the Linux wrapper populates descriptors only for liveview IDs. Reuse SENSOR_RAW_FINDINGS.md rather than retrying enum registration.
- `Ipc_End` releases/clears backing before `SetMainRawImage`. A queued object is not retained pixels. This later finding in RAW_ARENA_DMA_AND_CACHE.md supersedes the early report's tentative retention lead.
- The MID15000 candidate's material completion starts a video stream; it is not itself a DMA wait or evidence that this is the desired one-shot RAW path. Recipe activation is conditional.
- SSIF debug and observed preview geometry/packing differ; do not borrow one descriptor for the other.

## Concrete next question and stopping rule

Reuse saved `StillCapture` (`8031f274`), its direct callers, actual frontend target and existing producer/callback evidence. Construct only: capture request → selected image/backing → submission → completion/publication → export → release. Mark each edge static, runtime or missing, and distinguish the live R210 instance from shared-library support. Follow the first missing edge that decides whether a real still RAW export can be implemented; do not open another broad function census. The resulting deliverable must name the C/C++ integration point and the minimum missing observation/change, rather than another allocator map. If firmware instrumentation is required, prepare the exact bounded design offline before the separately required deployment authorization.

The user's nonstandard colour chart is useful later for distinguishing red/blue Bayer phase. It cannot prove sensor source, completion or ownership and is not the next central task. No chart image has been acquired or sent to an external provider during this review.

## State at review

Persistent goal active/unfinished. Five-hour usage89% used/11% remaining, weekly14%, reset18October18:24:45 Sydney. No reset credit used. No source edits or camera operation during this direction review. Existing Grok kernel follow-up PID78164 was confirmed live; its frozen-packet review is allowed to finish without an arbitrary clock/tool-count cutoff. It is wrap-up of an already launched question, not a reason to expand the kernel branch. Final result still requires parent acceptance.

Sources: RAW_ACQUISITION_DECISION_PLAN.md; RAW_STOCK_SSIF_DEBUG_CAPTURE.md; RAW_CANDIDATE_ALLOCATION_AND_EXPORT_CONTRACT.md; RAW_COMPLETION_IDENTITY_AND_RECIPE_DISPATCH.md; RAW_ARENA_DMA_AND_CACHE.md; SENSOR_RAW_FINDINGS.md; RAW_FULL_BUFFER_AND_BAYER_20261017.md; reviewed18October mapping and movie reports. This note changes priorities, not the proof standard or camera permissions.
