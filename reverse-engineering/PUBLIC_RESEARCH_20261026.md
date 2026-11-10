# Public research beyond the original mod repositories — 26 October 2026

Follow-up: the five principal leads have now been retrieved and compared
locally in [LOCAL_SOURCE_COMPARISON_20261026.md](LOCAL_SOURCE_COMPARISON_20261026.md).
That source review supersedes this survey where it adds byte comparisons,
report/source qualifications or later manufacturer-manual details.

Searched the public web and GitHub code with recovered library, class and device
names. Luna independently examined related NX work; parent inspected the key
documents, repository trees and actual decoder source. No camera action, native
REA session, third-party executable, firmware download or public posting occurred.

**Result:** useful additional research exists, especially on application restart
failures. This search did not identify a public working SM-R210 app-absent capture
host with fresh sensor RAW and complete initialization/retirement. This is a
bounded search result, not proof that no private or unindexed implementation exists.

## Relevant new leads

### Expanded NX500 research: WApaperplane/metol

[The online-probe report](https://github.com/WApaperplane/metol/blob/cb82f4886c1174261cbba8127000c7b696944ff4/docs/current/CAMERA_ONLINE_PROBE_2026-10-13.md)
reports one whole-camera freeze after restarting di-camera-app, and a startup
hook arriving after the app. It investigates hibernation coupling, with the
causal explanation explicitly tentative. This is new analysis in an expanded
NX mod fork, not merely its inherited feature list. It supplies concrete service,
script and app-lifecycle comparison targets; it does not demonstrate takeover.

Also inspected its [app structure](https://github.com/WApaperplane/metol/blob/cb82f4886c1174261cbba8127000c7b696944ff4/docs/current/CAMERA_APP_STRUCTURE_2026-10-07.md)
and [retained startup-script/unit text](https://github.com/WApaperplane/metol/blob/cb82f4886c1174261cbba8127000c7b696944ff4/raw8/gates/dicam/resume_chain_2026-10-16/scripts_and_units.txt).

### Separate Gear 360 investigation: vcxzvfe/gear360-c200

[Its bench log](https://github.com/vcxzvfe/gear360-c200/blob/7373bf39ac55793b46c0c6cd9ee80805cd5e5876/11-injection-bench-log.md)
reports app crashes during injected calls and whole-system reboots after killing
di-camera-app. Later experiments qualify earlier proposed injection recipes.
Its goal is phone-app-free C200 live streaming while using the onboard app,
not replacing that app or acquiring caller-owned still RAW. These are useful
reported failure cases on the 2016 model, not verified behavior on our SM-R210.

The [firmware teardown](https://github.com/vcxzvfe/gear360-c200/blob/7373bf39ac55793b46c0c6cd9ee80805cd5e5876/08-firmware-teardown.md)
contains proprietary-stack/device-name matches and protocol research. Its own
limits distinguish inferred call chains from independently checked evidence.

### Recent NX500 RTOS work: NR2BJ/nx500ex

[Firmware/LUT notes](https://github.com/NR2BJ/nx500ex/blob/fb7fe9bd3207cad16c1fd18f65d512ce8857e8f9/docs/research/2026-10-23-lut-firmware-analysis.md)
describe RTOS-side image processing, CCapAdjIf filesystem messaging and an IPC
debug RAW-save/load route. These are potential reference relationships, not a
demonstrated sensor-buffer ownership interface. The
[later device report](https://github.com/NR2BJ/nx500ex/blob/fb7fe9bd3207cad16c1fd18f65d512ce8857e8f9/docs/research/2026-10-25-lut-dump-analysis.md)
also records MCB refusal and a freeze during a sequence of mode/stop experiments;
its attribution to particular commands remains the author's interpretation.
The public implementation primarily controls the existing stock camera state.

### Reusable RAW decoder, conditional on format identification

[LibRaw's actual Samsung V3 decoder](https://github.com/LibRaw/LibRaw/blob/2caab57e269cc7fce17c3189e4d4d7e3e5861875/src/decoders/decoders_libraw.cpp#L937)
implements samsung3_load_raw, including bitstream parsing and pixel reconstruction.
This could save decoder work if acquired data is shown to use that format.
Neither the LRC name nor an SRW association proves our selected buffer matches it.
It supplies no acquisition, synchronization or native-retirement contract.

### Official SDK boundary

[Samsung's NX1 announcement](https://api.samsungmobilepress.com/press-releases/samsung-camera-nx1-firmware-upgrade)
describes a Windows PC SDK for USB remote control. The
[manufacturer manual](https://www.bhphotovideo.com/lit_files/107913.pdf)
lists Remote Studio, sample source and an API manual. Searches did not locate
public original SDK headers/source documenting our on-camera libcapture APIs.
This is an external tethering SDK, not evidence of a replacement camera host.

## Small local comparison prompted by the new lead

Read only five known script/unit files in the retained SM-R210 extraction:
hibernation.sh, di-camera-app.service, hibernation.service, hib_post.service and
hib_post.path. All exist. The hibernation unit's SHA256 matches the NX500 unit
hash in the published startup text:

`6d89c8788aca583164d63f055948809901adbf601626f36bbce9a0f9a4d73636`.

The later local comparison identifies partition/LED-path and whitespace
differences in hibernation.sh; HIB/export/launch lines are commented in both
versions. Shared unit text supports a targeted comparison, not a transferred
NX runtime diagnosis. No script was executed or changed, and no restart was
attempted.

## Search coverage and exclusions

Exact terms included libcapture-fw-prod.so, libmulticore-bridge.so,
CMCBAdapter, CCapCmdExtIf, CCapImageSetCb, CCapVirtualAddrIf, CBeautyFaceIf_Linux,
GetStillImageEncData, BeautyCaptureProcessEvent, RTOSPREF_BOOT_TYPE and d5_ipcc,
plus stock-app/restart/RAW/DRIMe5 and NX1 SDK combinations. GitHub code search
found the relevant proprietary-library/device terms in the newer notes; the
specific capture classes and initialization flag produced no useful indexed
implementation matches. Generic OperateCapture hits included unrelated payment
code. Some related fork searches were empty/rate-limited, not exhaustive.

Checked and deprioritized: Open360 is JPEG import/stitch/gallery; ultramango's
gear360reveng is earlier firmware/Bluetooth reconnaissance; Gear360_OSS archives
released source/firmware; NX500Capture/Multi-NX controls stock shutter paths;
ge0rg's NX resources provide firmware/architecture and service documentation.
None of those inspected materials closes our owned-host transaction.

A final repository-name search also checked vitorio/gear360-2017-mods and
oh2mp/gear360hack: their READMEs describe stock-shutter/Street View remote-control
workflows derived from the existing mod route. mikerr/gear360 is video conversion.
These are not evidence of an independent sensor capture host. This was not an
audit of every branch, fork, binary-only package or repository returned by search.

Public document/source receipts and repository heads remain locally in
`output/decompile/rea/pass-public-search-20261026/public-source-records.json`.
No third-party claim is promoted to an accepted SM-R210 hardware fact.

## Most useful next comparison

Use the expanded NX startup/hibernation research to compare our retained app
startup, shutdown and shared-state contracts. Preserve distinctions among an
observed freeze, a proposed hibernation cause and actual remote quiescence.
Continue RAW producer and lifetime work separately. No copied addresses,
restart/injection recipe or debug RAW-file workflow is an approved backend.
