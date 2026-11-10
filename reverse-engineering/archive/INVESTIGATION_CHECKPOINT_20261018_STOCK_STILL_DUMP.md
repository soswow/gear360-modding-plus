> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Full stock still dump captured — 18 October 2026

The camera produced a complete33,732,608-byte Bayer-labelled still dump after one user-operated ordinary shutter press. It was copied to the Mac and its SHA256 matched on the camera before/after transfer and again after a normal power cycle. This is the first full still dump in this pass. **The persistent RAW objective remains unfinished:** exact sensor/producer provenance, fresh completion identity, backing lease/cache proof, plane order and Bayer phase still need verification.

## Artifact and reproducible method

Local file: `<repo>/output/decompile/rea/pass-camera-resume-20261018/stock-dump-trial/raw/0_0_11648_5824x2896_bayer_12-16.raw`

Retained SD file: `/mnt/mmc/0_0_11648_5824x2896_bayer_12-16.raw`.

SHA256: `5af26b9845007a3f410926c006d269d60b6ae760889c3a8eb356a258f346d780`.

With explicit user authorization, the verified stock `/usr/bin/st cap seq dump on ssif` set RTOS819521c4=0x10; the other three flags stayed0. The user pressed the ordinary hardware shutter after readiness. The new root file matched11648×2896bytes. No process had a rootRAW file open when checked. FTP sizes and camera/Mac hashes matched. The helper was removed, the user power-cycled normally, and all four flags were independently verified0 under matching immutable code anchors. The SD file still had the same hash afterward. Exact commands, headers, results, user confirmations and reset/cleanup evidence are in `stock-dump-trial/`.

This method is now demonstrated to create a complete file. File existence, name, ACK and matching hashes do not alone certify all sensor RAW success criteria. No blind SSIF unit-test capture, firmware patch, injection, arbitrary memory write, service restart or agent-issued shutter was used.

## What the actual shutter revealed

Earlier coordinated2400-record metadata window captured transitions at indices774–787. W+7 went1→0→1; Q output became82252800 then8225f000; Core followed those and later returned to new preview822ab800. C+20/+24 held distinct input descriptors82242000 then8225a800. Static source supports the configured routes; sampled equalities are not per-call execution traces.

Guarded post-event input8225a800:5824×2896, stride11648, format1, metadata pixel2/packing4/pack1, layout2/request22/kind1, size33732608. Four plane addresses wereA60016C0/A680C4C0/A70172C0/A78220C0, spaced0x80ae00. This matches the previously traced allocation profile, without proving an exact factory/producer or retention. Its adjusted first plane is not a safe basis for blindly dumping a full logical frame. Post-event output8225f000 had2912×2896, stride5824 and cleared primary planes/size. Old preview82246000/A77BB000 guards are obsolete after capture/reset.

## Measured pixel representation

Interpreting the actual file as little-endian16-bit words gives16,866,304samples. Every word is a multiple of64; bitwise OR is0xffc0; shifted-right6 values span55..1023 with969 distinct values. Thus this file supports **10 varying bits high-aligned in16-bit words**. The filename12-16 reflects firmware metadata, not measured12-bit precision or proof of the ADC format. Plane ordering, logical crop/padding and Bayer phase remain open. Optional PNG diagnostics were not created because the system Python lacks Matplotlib/Pillow; no package was installed.

## Important restoration finding

Do not use `dump off ssif` as an inverse: its whole-word complement would writeFFFFFFEF. `dump off all` also fails to restore the observed state: special setter80570be8 writes819521cc, and offall leaves itFFFFFFFF; consumer802c5650 tests mask1 to install another callback. New source-bound REA records confirm these points. A normal user power cycle restored all four flags to0. Any future enable trial must retain that restoration plan unless a separately reviewed inverse is implemented.

## Review, next work and resources

Cursor client-confirmedGrok4.7High completed its frozen-packet branch review (exit0; PID absent). Parent accepted mode/output selection checks, rejected request-value-as-pointer semantics, clarified change-point compression, and scoped packet-absence claims against prior parent evidence. All15 owned camera helper directories are independently absent. All owned workers and REA sessions are closed. No tracked source change/commit/push; master2874ae9 remains clean. Original archives are preserved; this result is a later supplement.

Next: verify the stock callback's active material and original unadjusted allocation through fresh completion/export/release; analyze four-plane ordering and Bayer phase using the retained file and source; reconcile measured10-bit significance with firmware12/16 metadata and validate DMA/cache handling. Avoid another capture until it answers a specific remaining proof gap.

User extended the reserve to2–3%remaining for this run, superseding5%; target stop97%used, hard98%. Last checked96%used/4%remaining; weekly30%; reset23:30:31Sydney. No reset credit consumed. Goal tool still reports blocked from the earlier epoch and exposes no active-status setter; exact objective retained without duplication or false completion. The stop is resource wind-down. User authorization for similar bounded temporary debug trials persists.

## File interpretation after the Codex pass — Cursor Grok 4.7

Discovered on 18 October 2026, after the main Codex research that wrote the sections above. Author: Cursor Grok 4.7. Offline reading of the retained still dump, the firmware string `S5K2M8SX` / `CS5K2M8SXDriver`, and a user-supplied phone photo of the DGK Kustom Balance chart in the same scene. No camera command was sent. This section narrows the Bayer-phase gap left open above. It does not finish sensor provenance, fresh completion, backing lease, or cache proof.

The file is a headerless raster, not a container. Its 33,732,608 bytes equal 5824 × 2896 little-endian 16-bit samples at stride 11648. A full-file scan agrees with the Codex measurement: every word is a multiple of 64, the bitwise OR is `0xffc0`, and values shifted right by 6 span 55..1023. The four plane pointers spaced `0x80ae00` are four 724-row slices of this one buffer (4 × 724 = 2896). Adjacent-row differences at those slice boundaries match differences inside a slice, so the stored file is one continuous raster. A column split at x = 2912, the width of the cleared post-capture output, is not a hard seam.

Public SM-R210 specifications name two 8.4 MP CMOS sensors and do not give a color-filter phase. The firmware driver name matches Samsung S5K2M8SX, a 13 MP ISOCELL (4208 × 3120); 2896 × 2896 is about 8.4 MP, so each lens in this file fits a window of that sensor. The firmware strings `D5_IPC_ARRAY_TYPE_RGGB`, `GRBG`, `GBRG`, and `BGGR` are adjacent log labels, not a record of which pattern this shot used. No datasheet line found for this part states the starting pixel, and a crop or mirror would change it in the stored buffer anyway.

The chart photo identifies the middle row, left to right, as red, yellow, green, cyan, dark blue, magenta. That row sits in the right fisheye. Means of the 10-bit codes in 16 × 16 windows:

| Patch | Approx. origin | Even,even | Even,odd | Odd,even | Odd,odd |
| --- | --- | ---: | ---: | ---: | ---: |
| White | 4060, 1350 | 417 | 227 | 241 | 419 |
| Red | 4020, 1560 | 123 | 152 | 83 | 124 |
| Yellow | 4180, 1560 | 309 | 188 | 123 | 310 |
| Green | 4340, 1560 | 190 | 98 | 105 | 191 |
| Cyan | 4500, 1560 | 190 | 95 | 162 | 190 |

On white and gray, even-even and odd-odd match and are the bright pair, so those positions are the two greens. On red, even-odd rises above green and odd-even falls. On cyan, odd-even rises and even-odd stays low. For this file’s top-left origin, the right-lens pattern is GRBG:

```text
G R
B G
```

Several dim neutral windows on the left fisheye have the same green positions. Red and blue were not measured on the left lens. A 180° relative rotation would swap red and blue there without moving the greens.

Contrast previews were written beside the raw file with ImageMagick. The 10-bit codes were stretched from the 1st–99th percentile (63..437) and displayed with gamma 2.2. System Python still has no Pillow or Matplotlib; no package was installed. Files: `contrast-bw.png`, `contrast-bw-half.png`, `chart-crop.png` (crop origin 3400, 1050), and `phase-y0-x0.png`, `phase-y0-x1.png`, `phase-y1-x0.png`, `phase-y1-x1.png`. The reference phone photo is `PXL_20261018_101827264`.

Still open after this reading: exact producer and sensor provenance, fresh completion, backing lease and cache behavior, why the firmware label says 12-bit while these samples use 10, crop or padding inside 5824 × 2896, and red/blue order on the left lens. Sensor colour profiling is deferred; the matrix below is only a stand-in.

## DNG wrap and rawtoaces — Cursor Grok 4.7

Same evening, 18 October 2026, after the Bayer reading above and still after the main Codex research. Author: Cursor Grok 4.7. Offline only. The retained samples were wrapped, not modified. Writer: `output/decompile/rea/pass-camera-resume-20261018/stock-dump-trial/raw/wrap-dng.py`.

DNG: `0_0_11648_5824x2896_bayer_12-16.dng`, beside the raw file. The DNG payload is the original 33,732,608 bytes; its SHA-256 is still `5af26b9845007a3f410926c006d269d60b6ae760889c3a8eb356a258f346d780`. Both lenses stay in one 5824 × 2896 frame. Tags that matter:

| Tag | Value |
| --- | --- |
| PhotometricInterpretation | CFA, 32803 |
| CFA pattern | GRBG, bytes `1 0 2 1` |
| BitsPerSample | 16, samples left as stored |
| WhiteLevel | 65472 (`1023 << 6`) |
| BlackLevel | 4105, the darkest 64 × 64 corner outside the fisheye circles |
| CalibrationIlluminant1 | D65 |
| BaselineExposure | 0 |
| AsShotNeutral | about 0.544, 1, 0.578, green = 1, from the chart white patch |
| ColorMatrix1 | uncalibrated XYZ-to-camera, the XYZ-to-sRGB matrix |

`ColorMatrix1` is not a measured S5K2M8SX profile. The missing rawtoaces spectral database would not supply one either. Profiling stays for later.

The first wrap stored `AsShotNeutral` as the white patch’s raw codes, 14568, 26782, 15483, and stored `ColorMatrix1` in the camera-to-XYZ direction. `dcraw` could still name the file GRBG, but `rawtoaces` 2.2.0 wrote an all-NaN EXR that viewers draw as black. LibRaw 0.22.2 reads those neutrals and sets `cam_mul` to their reciprocals, about 0.000069, 0.000037, 0.000065. In `identify()`, any coefficient ≤ 0.001 fails the multiplier check, which then sets `cam_mul[0]` and `cam_mul[3]` to 0 and leaves the green and blue coefficients tiny. Observed `cam_mul` was `0, 3.734e-05, 6.459e-05, 0`. `rawtoaces` takes white balance from `raw:cam_mul` and sets neutral RGB to `1/multiplier`, so the red neutral is infinity and the metadata IDT is nine NaNs. The EXR header was otherwise a normal ACES container (`lin_ap0_scene`, half float, 5824 × 2896) with `NanCount` equal to every pixel.

Storing the neutral as a ratio puts `cam_mul` at about 1.838, 1, 1.730. Storing `ColorMatrix1` as XYZ-to-camera matches the direction LibRaw and rawtoaces invert. With those tags, the same `rawtoaces` command prints a finite IDT:

```text
0.268081  0.462264  0.142216
0.049683  0.749064  0.073814
0.013613  0.140626  0.718321
```

The replaced file `0_0_11648_5824x2896_bayer_12-16_aces.exr` has no NaNs or infinities. Scene-linear averages are about 0.360, 0.370, 0.323 and the peak is about 5.23, under the default highlight headroom of 6. An ACES view of the chart crop shows the middle row as red, yellow, green, cyan, blue, magenta. That checks the wrap. It is not a calibrated IDT.
