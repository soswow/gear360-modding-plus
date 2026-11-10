> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Adjust YCC control route and payload contract

Reviewed 16 October 2026. This extends the recipe-selection work using the captured Linux executable, shared library and RTOS firmware. All analysis is offline; no camera command or target binary was executed. No verified sensor RAW file exists.

## A control route with three identifier domains

The Linux app has `UI_Mm_Camera_Set_Ioctl_Adjust_Ycc(int)` at ELF/REA address `0x1174e8`. It preserves input values 1, 2 and 3 as a four-byte integer payload and maps all other inputs to zero, then calls its internal helper `0x113ef8` with identifier `0x66`, payload address and length 4. A concrete caller, `dfms_app_cmd_scam_capt`, supplies 1 or 2 from two accepted still-image option strings, and 1 on a separate interval branch. The intermediate app ioctl/attribute dispatch is not yet linked to the library setter. A shared label is not a pointer-flow proof.

In `libcapture-fw-prod.so`, `CAttributeHandler::setAdjustYCC` is ELF VMA `0x9ba48`, REA address `0xaba48`. It requires handler word `this+4 == 4`; success calls `SetCaptureDataMCB(0x01090006, *(this+0x10), 4)`, otherwise it returns -2. The payload is a pointer to four bytes, not a pointer to image pixels.

Retained full adapter records establish this translation:

| Domain | Value | Meaning established by code |
|---|---:|---|
| App helper argument | `0x66` | Argument passed to the app's internal ioctl helper |
| Capture-library logical operation | `0x01090006` | Case selected by SetCaptureDataMCB |
| MCB command | `0x81` | First argument of CMulticoreBridge::Send |
| MCB sub-ID | `0x0288` | Translation of logical operation `0x01090006` |
| Payload length | 4 | Original payload forwarded through the adapter |
| Manager tag | `0xb9` | Separate internal manager helper argument in the RTOS setting writer |

`0x66`, `0x01090006`, `0x0288` and `0xb9` are not interchangeable names for one field. These are proved arguments at the bridge Send call; the serialized header layout still needs a source-bound join. The exact RTOS wire table mapping `0x81/0x0288` to the internal logical dispatcher remains open at this checkpoint. The earlier shell `0xb6` route enables a different dump flag.

## RTOS internal contract

Internal dispatcher `0x80051340` takes a logical selector, payload pointer and byte count. Validator `0x80051668` requires exactly four bytes for `0x01090006`: zero length returns -4 and other wrong lengths return -3 before the setter. The valid selector case calls `0x801153cc` at `0x8005152c`, preserving the payload pointer. The handler reads its first word and maps values 1, 2, 3, 4, 5 to stored setting values 0, 1, 2, 4, 0, respectively.

That setting is global `0x81482ea4`. Its low two bits become parameter byte `+0x170`, one of several guards for the recipe-1 candidate factory. This still does not establish that the complete candidate predicate passes or that the active R210 graph uses it. The diagnostic says “Set Adjust Ycc Capture”; it does not identify physical sensor packing or justify calling the setting RAW quality.

Parent review also found an internal compiled caller at `0x800e4a2c` that passes the same logical operation with a one-byte stack payload. The original arguments would take the validator's rejection branch. This is a static contract mismatch if reached with those arguments; runtime reachability and effects are unobserved. It is not evidence that the correctly sized Linux request fails.

## Address and evidence review

The shared library is a zero-based ELF mapped by REA with bias `+0x10000`. The app is ET_EXEC: its mapped code uses ELF VMAs directly, with file offset `VMA-0x8000` in the executable PT_LOAD. An initial app query with the shared-library bias selected the unrelated Camcorder_Cancel method; that probe is preserved and excluded from this route.

Parent independently parsed ELF program headers and checked every source window. This corrected two originally wrong app file offsets: caller range `0x23af64` maps to `0x232f64`, and probe `0x1274c4` maps to `0x11f4c4`. Hashing bytes at an incorrectly calculated offset had not proved the claimed address mapping. Corrected windows and original schemas are retained.

The [evidence index](../adjust-ycc-control-evidence.json) binds the full native records, snapshots, exact bytes, mapping corrections and retained adapter dependencies. Relevant full Evidence IDs:

- Linux setter: `ev_7f4dac17c6a14be6fce5201cd59b977a104d096208c0360a412eba9078d9422f`.
- App helper: `ev_cd541ac8a8fe89db63e4440f6378dbe86af5a9e59caafe42b4913c5eee3d843d`.
- App caller: `ev_18a75431b1fb0f1789410569880cf480087e5b3b40179f674d051f50c40548a4`.
- Logical-to-wire translator: `ev_7cd195980ed3197a6033e7f5594c5a3be9f41cc6e6bd31f69a2ed2b75a368145`.
- Adapter: `ev_338e7cf94d115fed238a527e66fe8e2ee3bb60983e0cbd437c3e643dccdfe284`.
- RTOS dispatcher: `ev_203a09c56c0729fdc35848e994fbd0f84b7a42bedeea25591a7f54ece57e3899`.
- RTOS size validator: `ev_3a49e5b6915c681ce0229b78d122a6375d667988cd7d3486035f888e7a8a9047`.

Linux app SHA256 `5937b3abe7630abc8ac265ce280cc45ee591562968296bc4b368cf7e15742789`; capture library SHA256 `fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7`. RTOS SHA256 `cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7`, base `0x80000000`; analysis carrier SHA256 `3458e4295afe254257f5a533686f451a854eb327b5234a3ad043e4dd952dd6f7`. Ghidra 12.1.3, profile `2efb2839e0e54d98e487132a182b70bf8f5b5989724d969b01494dc44c0182a2`.

The completed native artifacts in this initial part of the run were produced before the user clarified external-only delegation. Luna workers were then stopped; subsequent delegated work uses only Cursor/Grok 4.6 Medium and MiniMax M3.1-Flash-Preview High. Parent reviewed original bytes and owns integration.

The newer [RTOS dispatcher review](RAW_ADJUST_YCC_RTOS_DISPATCH_BOUNDARY.md) identifies the four-byte wrapper0x8012e0e8 and corrects the method-table ROM/RAM coordinates. It preserves the transport join as unresolved.
