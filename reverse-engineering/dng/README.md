# DNG from the Gear 360's RAW frame

Started 5 November 2026. Goal: turn the full RAW frame the capture library
copies (both lenses, 5824 × 2896) into DNGs with the right black and white
levels, white balance and colour matrix, using what the camera's own firmware
knows.

## Already known about the RAW

From [raw.measured-still-raster](../facts/contracts/raw-buffer.yaml) and
trial F2:
- **Layout:** 5824 × 2896 little-endian 16-bit samples, stride 11,648 bytes
  (33,732,608 bytes). 10 significant bits sit in bits 6–15, so the white
  level is 65472.
- **CFA:** GRBG, measured on the right lens only. In F2 the green pair is
  on the diagonal in both frames.
- **Black:** one October capture measured about 4,100–4,105 (≈ 64 × 64).
- **Wrappers:** [g360_dng_wrap.c](../../src/experimental/capture-engine/tools/g360_dng_wrap.c) and
  the older local-only `wrap-dng.py` (in
  `output/decompile/rea/pass-camera-resume-20261018/stock-dump-trial/raw/`)
  keep the samples unchanged. Their white balance came from one chart, and
  their colour matrix is a placeholder (camera RGB = sRGB).

## What the stock JPEGs record

Every stock JPEG carries a Samsung "Type 2" MakerNote with the camera's
image-processing state. [stock_iq.py](stock_iq.py) decodes it. Decoded
across all 721 stock JPEGs saved locally (714 from this camera, 7 from the
second camera of 29 September), with no failures.

| Record (MakerNote tag) | Writer in the RTOS | Contents used here |
|---|---|---|
| AWB, `0x00a0` (13,000 bytes, XOR 0xC8; `0x00a1` is an identical copy in all 721) | `FUN_803aa714`, called from `IQ_MN_GetMNData_1`; signature "AWB", version 0x105 | gains, lens adjustments, white points, colour matrix |
| Colour processing, `0x00c0` (2,048 bytes) | class `CIpcParamD5`; header "I!?C" from `FUN_803d647c` | tone curve, saturation matrix, RGB→YCbCr, gain triples |
| AE, `0x0060` (6,000 bytes) | `CAE::GetMakerNote1` (`FUN_803b0a7c`), marker `0xaaffaaff` | a copy of the colour matrix |

The record sizes come from `CIQIfMakerNote::GetSize`: IDs 0 and 1 are 13,000
bytes, ID 2 is 6,000, ID 4 is 2,048. The RTOS code is read from the saved
Ghidra decompile of 19 October
(`output/decompile/rea/pass-opus-engine-20261019/rtos/`). No new REA session
was opened for this.

### Fields

**Observed** means the same values or patterns across the 721 JPEGs.
**Inferred** is the reading of them.

- **Colour matrix (CCM).**
  - **Where:** AWB record `0x2e14` (nine int32, ×128; a second copy at
    `0x2e38`), and the AE record `0xd1c` (int16). All three agree in all 721
    JPEGs.
  - **Values:** each row sums to 1.0 (128). In 430 of 721 shots it is:

    | | R | G | B |
    |---|---|---|---|
    | R out | 1.805 | −0.742 | −0.063 |
    | G out | −0.211 | 1.453 | −0.242 |
    | B out | 0.016 | −0.594 | 1.578 |

  - **Variants:** 52 matrices in all. The main one appears across the whole
    white-balance range (R/B gain ratio 0.28–1.87). A weaker one appears only
    under tungsten-like light (R/B 0.27–0.38), and others look like reduced
    saturation.
  - **Inferred:** white-balanced camera RGB → linear sRGB.
- **White-balance gains per shot.**
  - **Where:** AWB record `0x2eb0` and `0x2f2c` (R, G, B; 65536 = 1.0),
    which differ slightly from each other. `0x2f38` is always 1, 1, 1;
    `0x2f44` holds small correction factors (about 0.98–1.01).
  - **P2:** both pairs reproduce the JPEG within about 1% (`0x2eb0` slightly
    better); the gains do reach the RAW.
- **Per-lens factory calibration.**
  - **Lens adjustments:** `0x2dac` (lens a) and `0x2db8` (lens b), constant
    for a camera:

    | Camera | Lens a | Lens b |
    |---|---|---|
    | This camera | R ×1.0245, B ×0.9413 | R ×1.0073, B ×0.9458 |
    | Second camera | R ×1.0495, B ×0.9537 | R ×0.9976, B ×0.9942 |

  - **White points:** four per lens (`0x2e60`… `0x2fb4`, G = 4096), each the
    raw R/G and B/G of a neutral. White point × lens adjustment:

    | Point (record order) | R/G | B/G | Agreement across both cameras and lenses | Probable light |
    |---|---|---|---|---|
    | P1 (`0x2e60`) | 0.7043 | 0.3367 | exact | the calibration point |
    | P2 (`0x2ec8`) | 0.422–0.426 | 0.526–0.532 | within about 1% | daylight (bluest) |
    | P3 (`0x2efc`) | 0.520–0.531 | 0.377–0.382 | within about 2% | |
    | P4 (`0x2fa8`) | 0.855–0.859 | 0.268–0.270 | within about 0.5% | tungsten (reddest) |

  - **From the lens EEPROMs (7 November):** the four points are the lens
    module's EEPROM values, P1 = `AWB_LOW_GAIN` (0x040) and P2–P4 =
    `AWB_EEPROM_POINT2–4` (0x228–0x239); lens a is EEPROM A (front). The
    adjustment is the RTOS golden P1 (46160, 22064 over 65536 = 2885, 1379
    over 4096) divided by the lens's P1, truncated to 1/65536.
    [eeprom/README.md](../eeprom/README.md).
  - **P2:** lens a is the frame's left half, b the right.
  - **Not known yet:** the illuminant of
    each point.
- **Tone curve.**
  - **Where:** colour record, 19 knots: inputs at `0x174` (0–16383), outputs
    at `0x1c0` (0–1023).
  - **Varies:** 86 different curves in 721 shots, so it adapts per scene.
  - **Most common:** 0→0, 16→8, 37→21, 70→45, 116→80, 182→128, 275→194,
    405→269, 584→355, 832→452, 1182→556, 1694→657, 2524→748, 3966→828,
    6218→895, 9241→953, 12545→998, 15872→1022, 16383→1023.
  - **P2:** the input is (RAW − black, 10-bit) × about 20.3.
- **Saturation matrix.**
  - **Where:** colour record `0x218` (×128, copy at `0x22a`); the
    RGB→YCbCr matrix is at `0x23c` (BT.601, ×256).
  - **Inferred:** a YCC-domain saturation/hue stage after the CCM. It
    changes together with the CCM's variants.
- **Gain triples.**
  - **Where:** colour record `0xae`, three 16-bit triples, for example
    (418, 555, 256), (425, 552, 256), (418, 555, 256).
  - **Observed:** the first two differ by exactly the ratio of the two lens
    adjustments.
  - **Not known yet:** their stage and channel order.

## Verified on a real pair (P2, 5 November 2026)

Result (not published: `../../output/dng-P2/RESULT.md`). One still of a colour checker:
- **What was saved:** the RAW frame, our JPEG, and the camera's MakerNote
  buffer. g360cap now copies that buffer; our JPEGs have no Exif.
- **The buffer:** it starts with the IQ block at the RTOS table's offsets
  (AWB 0x14, AE 0x32dc, colour 0x4a4c), so `stock_iq.py` reads it directly.

Results:
- **The JPEG geometry:** the same two fisheye images. Each JPEG half is the
  RAW half minus 8 px on each side.
- **Black level:** 64.1–64.5 (10-bit), ≈ 4106 in the 16-bit file, the same
  everywhere.
- **White level:** 65472.
- **CFA:** GRBG on both lenses.
- **Lens shading:** already corrected in the frame (flat across the radius).
- **The recorded gains and matrix:** they reproduce the JPEG's 18 checker
  patches to about 6 levels RMS (8-bit), against 23 with no matrix.
- **The tone curve:** it applies to (RAW − black) × about 20.3.
- **Lens adjustment "a"** is the left half of the frame, "b" the right.

## DNG tags (from P2; the white point illuminant is still assumed)

| DNG tag | Value / source |
|---|---|
| `CFAPattern` | GRBG (both lenses) |
| `BlackLevel` | 4106 (16-bit file) |
| `WhiteLevel` | 65472 |
| `AsShotNeutral` | 1 / the AWB gains, `0x2eb0` (P2: 0.4547 1 0.5927). Per lens, scale R and B by that lens' adjustment relative to the other (≈1.7% R, 0.5% B) |
| `ForwardMatrix1` | Bradford(D65→D50) × sRGB→XYZ × CCM. For the main CCM: 0.7080 0.1509 0.1053 / 0.2513 0.8406 −0.0919 / 0.0158 −0.2933 1.1027 |
| `ColorMatrix1` + `CalibrationIlluminant1` = D65 | diag(neutral) × CCM⁻¹ × XYZ→sRGB, with the daylight white point P2 as the neutral (assumed D65). Lens a: 0.6606 −0.1242 −0.0836 / −0.4602 1.3083 0.1186 / −0.0868 0.2066 0.4045 |
| `BaselineExposure` | +0.34 to match the stock brightness (the ×1.27 before the curve) |
| `ProfileToneCurve` | Optional: the shot's recorded curve, for a stock-like look |

Lens shading needs no opcode: the frame is already corrected.

## Decisions and requests (user, 5 November 2026)

- **One DNG for the whole frame,** both lenses side by side, since the RAW
  already arrives as one file ("whatever makes it less code").
- **Our JPEGs should carry the camera's Exif and MakerNote,** like the stock
  app's.
  - **How the stock app does it:** `libsif`'s
    `MpfEncoder::putApp1(sImgExifInfo*, sImgMakerNoteInfo*, …)` builds the
    Exif from the buffer g360cap now saves.
  - **The simplest version for us:** insert an APP1 segment after SOI. It
    holds a big-endian TIFF with IFD0 (Make SAMSUNG, Model SM-R210), an
    Exif IFD and a Samsung Type 2 MakerNote. That MakerNote carries the
    buffer's records under the stock tags:
    - `0x00a0` and `0x00a1`: AWB, already XOR-masked in the buffer;
    - `0x0060`: AE;
    - `0x00c0`: colour.

    Its value offsets are relative to the MakerNote's own start, and tag
    `0x0001` = "0100". [stock_iq.py](stock_iq.py) reads this format, and its
    test already builds one (`tests/test_stock_iq.py`, `jpeg()`).
  - **Open:** exposure time, ISO and the other Exif fields need their place
    in the AE record or in the capture settings g360-shot sent.

## The DNG writer (5 November 2026)

```sh
python3 reverse-engineering/dng/make_dng.py shot-001-frame.bin shot-001-makernote.bin shot-001.dng
```

[make_dng.py](make_dng.py) writes one uncompressed CFA DNG for the whole
frame, with the samples unchanged and the tags in the table above.
- **AsShotNeutral:** one value for both lenses, from `0x2eb0`.
- **ColorMatrix1:** uses the mean of both lenses' P2 white points.

P2's DNG passes `exiftool -validate`, and macOS's own RAW engine (`sips`)
renders it with correct colours on both lenses; the checker greys come out
neutral. Not yet opened in Lightroom, darktable or RawTherapee.
`tests/test_make_dng.py` checks the layout and tags.

## DNG with the shot's metadata (6 November 2026)

`make_dng.build_full()` and the C `g360_dng_header_full()` /
`g360_save_dng_full()` write the same DNG with the shot's metadata, byte
for byte the same:
- **IFD0 adds** Orientation 1, LensInfo (1.2 mm f/2.2), an Exif IFD, and
  when given DateTime and ImageDescription (at most 63 printable ASCII).
- **The Exif IFD:** ExposureTime, ExposureProgram, ISO, DateTimeOriginal and
  DateTimeDigitized, WhiteBalance when known (the native per-shot record,
  as for our JPEGs); FNumber 2.2, FocalLength 1.2 mm, FocalLengthIn35mmFilm
  6 and ExifVersion 0230 always.
- **The MakerNote:** Samsung Type 2, little-endian, offsets relative to its
  own start, with the same four records as our JPEGs (0x0001 "0100",
  0x0060 AE, 0x00a0 and 0x00a1 AWB, 0x00c0 colour).
- **Layout:** the samples start at a fixed 40,960 bytes, so every such DNG
  is 33,773,568 bytes.

LibRaw reads it: OIIO shows ExposureTime, FNumber, ISO, FocalLength, the
dates and ImageDescription, and rawtoaces copies ExposureTime, ISO,
ExposureProgram, WhiteBalance and FocalLength into its EXR header.
`verify-capture.py` rebuilds both formats. Camera check: pair `DNG3`
(6 November, `g360-shot` `479e6bae…`): byte parity with the reference,
1/125 s ISO 100 Program AE as in the JPEGs, ExifTool OK.

```sh
rawtoaces --wb-method metadata --mat-method metadata --adjust-maximum-threshold 0 \
  --output-dir OUT --create-dirs FILE.dng
```

`--adjust-maximum-threshold 0` keeps the white level fixed, so the frames of
a sweep scale alike.

## On-camera C writers (T1/T2, 5 November 2026)

[g360cap](../../src/lib/g360cap/README.md#jpeg-exif-and-camera-dng-t1t2) now includes
`g360_dng_header` / `g360_save_dng` and `g360_exif_app1` /
`g360_save_jpeg_exif`. `g360-shot --frame --frame-output dng` writes the
whole-frame DNG on the card, including that still's gains and matrices.
The header is 588 bytes; the samples are streamed unchanged from the owned
capture buffer. The Python and C writers intentionally retain identical
Software strings for exact output comparison.

JPEGs get the minimum Exif/Samsung IQ profile by default (`--exif off`
keeps bare JPEGs). Actual exposure and ISO come from the native per-shot
Exif record at image-set +8, **not** the AE record or the requested settings.
The four IQ tags round-trip unchanged through `stock_iq.py`. MPF, the IFD1
thumbnail, Samsung angle/UTC tags and CLI date fields remain outside this
minimum profile.

Camera and host evidence: [T1/T2 validation](T1_T2_VALIDATION.md).
For a repeatable same-boot capture and reference check:

```sh
python3 reverse-engineering/dng/capture-pair.py --tag DNG1 --count 2 --frame-output dng
python3 reverse-engineering/dng/verify-capture.py output/dng-DNG1 --expect-exif --render
```

The verifier compares each complete DNG with the Python reference rebuilt
from its unchanged sample strip and the same-shot MakerNote, compares every
JPEG's IQ records with its sidecar, runs ExifTool when available, and can
render the first DNG with Apple's RAW engine. `capture-pair.py` also accepts
`--exposure TV,ISO` and `--exif on|off`.

## Next steps

1. **Check the DNG in a raw developer** against the JPEG. A per-lens
   AsShotNeutral would need two DNGs or a per-lens gain on the samples.
2. **Exif + MakerNote in our JPEGs:** the minimum C profile is implemented
   in g360-shot; the remaining stock fields/MPF are compatibility follow-ups
   if a viewer or stitching workflow needs them.
3. **REA confirmation of the field writers**, for the native-analysis rule:
   - `FUN_803aa714` (AWB record) and its source struct (`FUN_803ba09c`);
   - the `CIpcParamD5` field writers;
   - the source of the CCM at `0x2e14`, which the AWB writer's visible copies
     don't cover (it agrees with the AE copy every time).
4. **More light sources.** A daylight-only and a tungsten-only pair would
   pin down the white points' illuminants and test the tungsten matrix
   variant.

## Capture a pair

```sh
python3 reverse-engineering/dng/capture-pair.py --tag P3
```

This runs the experiment wrapper with `g360-shot --frame --count 1`. It
uses reenter mode when the stock app is stopped and never restarts the
stock app. Files arrive in `output/dng-P3/`, each checked against the
camera's SHA-256.

## Tests

```sh
python3 reverse-engineering/dng/tests/test_stock_iq.py /tmp
python3 reverse-engineering/dng/stock_iq.py output/360_2110.JPG output/dng-P2/shot-001-makernote.bin
```

The test builds two synthetic inputs and checks every decoded field, the
CLI and the refusals:
- a stock JPEG (big-endian Exif, MakerNote offsets relative to the
  MakerNote, masked AWB record);
- a camera MakerNote buffer.
