# Lens-module EEPROMs (TODO T14)

Worked out 7 November 2026 from the two images read on 6 November
(data (not published: `../../output/peripherals-20261106/EEPROM_AND_REGISTERS.md`)), the
stock firmware and the stock JPEGs. Nothing was read from or run on the
camera for this.

```sh
python3 reverse-engineering/eeprom/lens_eeprom.py --markdown output/peripherals-20261106/EEPROM_AND_REGISTERS.md
python3 reverse-engineering/eeprom/lens_eeprom.py eeprom-3.bin eeprom-4.bin --json
python3 -m pytest -q reverse-engineering/eeprom/tests
```

**All 179 fields with this camera's values: [FIELDS.md](FIELDS.md).**

[lens_eeprom.py](lens_eeprom.py) names every field, checks the checksum,
lists the white points, computes the lens adjustment as the RTOS does, and
prints the optical-axis block.

## Summary

- **Every used byte has a factory name.** The stock factory library
  `libfactory-mode.so` holds the table (179 fields, `EEPA_*`; the camera's
  factory log shows EEPROM B at the same offsets as `EEPB_*`).
- **EEPROM A is bus 3 = front lens = the left half of the frame = lens "a"**
  in the stock JPEG's AWB record. EEPROM B is bus 4 = rear = right = lens "b".
- **White balance: solved.** The RTOS reads four white points per lens from
  the EEPROM. They are exactly the four white points in the stock JPEG's AWB
  record. The per-lens adjustment in that record is a fixed golden value
  divided by the first point; the decoder reproduces both lenses' values
  exactly.
- **Stitching: located, partly read.** The 15 doubles are `OPTICAL_AXIS_02`
  to `_16`. The stock app copies them, untouched, into every JPEG as
  MakerNote tag 0x0012 (the "OPAX" record) for an off-camera stitcher. The
  firmware never parses them, so their meaning comes from the numbers:
  the first six fit two lens centres and two image-circle radii (below).
- **The rest is factory bookkeeping and test results:** dates, step pass
  flags, test-station asset numbers, ISO gain calibration, resolution,
  noise, dust and shading test results, a module ID and a checksum.

## Field map

A grouped summary; [FIELDS.md](FIELDS.md) lists every field. Offsets are
the same in both EEPROMs. "A / B" are this camera's values.

| Offset | Size | Factory name | A / B | Meaning |
|---|---|---|---|---|
| 0x000 | 2 | TABLE_INFO_VER | 3 / 3 | layout version |
| 0x002 | 12 | CREATE_DATE | 20170527 | |
| 0x00e–0x015 | 1 each | NV_DUST, NV_NOISE, NV_ISO, NV_MOT, NV_AWB1–4 | `PNPPNN` | per-step flag, "P" or "N" (AWB3/4 unset) |
| 0x018–0x024 | 2 each | ISO_ASSET_NO, MOT_, DUST_, NOISE_, AWB1_, AWB2_ASSET_NO | "03", "02", "02" / "00"… | test-station numbers |
| 0x022, 0x023 | 1 each | DECENTER_X, DECENTER_Y | 0, 0 / 0, 0 | lens decenter, zero here |
| 0x02a, 0x02d, 0x0b0–0x0b9 | 3 each | AWB1_, AWB2_, ISO_, MOT_, DUST_, NOISE_FW_VER | QC4, QC4 / QD1, QC4; ISO QC4 | test-station software versions |
| 0x030 | 4 | LIVE_ISO_GAIN_X1 | 143 / 141 | ISO line-adjust result |
| 0x034 | 4 | LIVE_ISO_GAIN_X2 | 174 / 169 | ISO line-adjust result |
| 0x038 | 4 | ISO_FLAG | 0xfa / 0xaa | result nibble: 0xa pass, 0x5 fail; A uses the low nibble, B the high one |
| 0x03c | 4 | ISO_VERSION | 0x100 | |
| 0x040 | 2×3 | AWB_LOW_GAIN_R/G/B | (2816, 4096, 1465) / (2864, 4096, 1458) | **white point 1, the adjust point** |
| 0x046 | 2×3 | AWB_HIGH_GAIN_R/G/B | unset | |
| 0x080 | 40 | MOT_PIMA | 10 × u32, mostly 2000 | probably resolution-chart results (PIMA is the ISO 12233 chart); not read by the RTOS |
| 0x0a8 | 4 | HISTOGRAM_PASS_AMOUNT | 1921 / 1570 | test result |
| 0x0c1 | 15 | SET_ID | CFN000805E48254 | the camera's unit number (`memory/private/pmode_uniqueno`); same in both |
| 0x0d0 | 8 | OPTICAL_AXIS_01 | u32 2 | optical-axis format version |
| 0x0d8–0x147 | 8 each | OPTICAL_AXIS_02–16 | 15 doubles, same in both | stitching geometry (below) |
| 0x148–0x20f | 8 each | OPTICAL_AXIS_17–40 | zero | unused slots |
| 0x210 | 1 | OPTICAL_AXIS_FLAG | 2 | |
| 0x211–0x215 | 1, 2, 2 | LENS_GROUP_NO, LENS_OFFSET_R/B | unset | |
| 0x216–0x227 | 2 each | AWB_MODULE_POINT2–4_GAIN_R/G/B | unset | module maker's points (not written) |
| 0x228–0x239 | 2 each | AWB_EEPROM_POINT2–4_GAIN_R/G/B | see below | **white points 2–4** |
| 0x23a | 2×3 | AWB_MODULE_LOW_GAIN_R/G/B | (2797, 4096, 1496) / (2861, 4096, 1458) | module maker's own point-1 measurement |
| 0x240 | 2 | AWB_DELTA_E | unset | |
| 0x260–0x2db | 4 each | MODULE_*: colour noise, dot noise, line noise, shading, exposure, dust 1–3 | unset | module-level test results |
| 0x300, 0x304 | 4 each | BAT_CHARGE_COUNT, TA_INSERT_COUNT | unset | |
| 0x308 | 13 | CHIP_ID | unset | |
| 0x315 | 10 | MODULE_ID | `OVKER` 01 74 00 01 01 / `OVKER` 02 cd 00 01 01 | module ID: maker code and serial |
| 0x320–0x39b | 4 each | SET_*: colour noise, dot noise, line noise, shading diff, exposure avg, dust 1–3 | per module | test results for the assembled camera |
| 0x7fc | 4 | CHECKSUM | 0xaad33f25 / 0x05073e3e | sum of the u32 words 0x000–0x7fb; both match |

The "OVKER" block of the 6 November notes is `MODULE_ID` followed by the
`SET_*` test results; it is not calibration. 0x900–0x907 (zeros) is outside
the factory table.

## White balance

**What the RTOS reads.** In the saved RTOS decompile (Ghidra 12.1.3,
`pass-opus-engine-20261019`; not re-confirmed in a fresh REA session):
- The AWB code gets each white point through three methods of the adjust
  interface (vtable 0x806c1b48, slots +0x2c R, +0x30 G, +0x34 B;
  `FUN_8039fe64/44/24`). Each takes a lens (0 = A, 1 = B) and a point
  (0–3). Point 0 reads 0x040, points 1–3 read 0x228, 0x22e and 0x234
  (24 getters `FUN_803a3558`… `FUN_803a3838`). Region 0x85c20000 is
  EEPROM A, 0x85c40000 EEPROM B.
- `calcBodyAdjustGain` (`FUN_803bb4d8`, "AWB/Impliment/AwbAdjustGain.cpp")
  computes the lens adjustment with `FUN_803c0fe8`: R = golden R / lens R,
  B = golden B / lens B, as a truncated 1/65536 fixed-point value, G = 1.
  The golden values are in the AWB tuning data at 0x806d5450 +0x144 and
  +0x14c: 46160 and 22064 (0.704346 and 0.336670; in the EEPROM's units
  2885 and 1379 over 4096).
- The factory "AWB line adjust" (`+SCAMAWBT`, `CIQ_AdjustIF_NX1::AdjustMain`,
  `AdjGetAwbResult` `FUN_803a1fc8`) writes the slots: point type 1 →
  0x23a, 3/5/7 → 0x216/0x21c/0x222, 11 → 0x040, 13/15/17 →
  0x228/0x22e/0x234 (odd for A, even for B). The camera's factory log
  shows the same writes by name.

**Checked against the stock JPEGs.** In `output/360_2113.JPG`, the AWB
record's white points for lens a and lens b equal the EEPROM A and B points
exactly, in this order: record P1 = 0x040, P2 = 0x228, P3 = 0x22e,
P4 = 0x234. The record's lens adjustments (0x2dac, 0x2db8) equal
golden / point 1 to the last bit: A (67141, 65536, 61688) = R ×1.024490,
B ×0.941284; B (66016, 65536, 61985) = R ×1.007324, B ×0.945816. The same
adjustments are in all 660 JPEGs from this camera.

So: lens a in the AWB record is the front module, which is the left half
of the frame (DNG pair P2 found lens a on the left).

**Second camera** (7 JPEGs of 29 September): its adjustments are
R ×1.04947, B ×0.95366 (a) and R ×0.99757, B ×0.99422 (b), so its point 1
would be about (2749, 4096, 1446) and (2892, 4096, 1387). Not checked
against its EEPROM.

**For our DNGs.** No change is needed. The DNG writer takes white balance
from the shot's IQ MakerNote, which already carries these points and
adjustments. The EEPROM gives the same values without a MakerNote. A
per-lens neutral stays T3's question: the lenses differ by golden/point-1
ratios, +1.7% R and −0.5% B for lens a against b.

## Stitching values (optical axis)

**How the stock app uses them** (fresh REA session, Ghidra 12.1.3,
`libdi-alg-imagestitch.so` sha256 d84fb7a6…):
- `ADE_ImageStitch_get_OPAX` copies EEPROM A 0x0d0–0x16f (160 bytes)
  verbatim; its version is the u32 at 0x0d0 (2). This is MakerNote tag
  0x0012 in every stock JPEG (checked byte for byte).
- `ADE_ImageStitch_get_OPAI` builds MakerNote tag 0x0013 (516 bytes):
  version 3, the u32 at 0x260 of each EEPROM (`MODULE_COLOR_NOISE_R_G_DIFF`,
  unset here, so the version becomes 0xffffffff), and the 15-character
  unit number from `memory/private/pmode_uniqueno` (= `SET_ID`).
- `ADE_ImageStitch_get_STIP` is a fixed 52-byte record: version 1, "0002-0001".
- Nothing in the camera parses the 15 doubles. The consumer is the phone or
  PC software that stitches the JPEGs (not in our firmware dump).

**The camera's own stitcher is a different path.** `libdi-arc-imagestitch.so`
(ArcSoft "FishEyePanorama" 1.0, 2016, GPU shaders with `uvCenter1/2`,
`ufSrcRadius1/2`, `mapParamLeft/Right`, `RotMLeft/Right`, an 89°–91° band)
loads 40 int32 values divided by 10⁶ from adjust region 0x85600000
(+0x179…, written by the factory command `+OPTAXIST`), or a built-in
default with a 195° field of view. It does not read the EEPROM block.

**What the 15 doubles fit** (inferred from the values; not named by any
firmware we have):

| | This camera | Second camera | Reading |
|---|---|---|---|
| d0, d1 | 1446.106, 1445.184 | 1439.545, 1445.840 | lens 1 (front, left half) centre x, y |
| d2, d3 | 1453.034, 1435.182 | 1444.400, 1448.720 | lens 2 (rear, right half) centre x, y |
| d4 | 1442.373 | 1439.545 | lens 1 image-circle radius |
| d5 | 1435.182 | 1438.081 | lens 2 image-circle radius |
| d6–d11 | 1.001065, 5.2e-5, 5.2e-5, 1.006810, −7.4e-4, −7.4e-4 | 1.000618, 1.8e-5, 1.8e-5, 1.000328, −2.1e-4, −2.1e-4 | near-identity, pattern (a, b, b, c, d, d); unknown |
| d12–d14 | 32.326, 3.920, 4.364 | 7.958, 5.334, 1.681 | unknown (angles or offsets?) |

Why this reading:
- **Pixels of a 2896 × 2896 half** (the stock JPEG half; the RAW half minus
  8 px per side). The first six are float32 values stored as doubles.
- **Our own ChArUco calibration** of this camera
  (`gear360-calibration/output/super-flat-through-3463/intrinsics.json`,
  JPEG-half pixels) puts the principal points at (1442.87, 1445.88) left
  and (1453.08, 1437.05) right: within 0.05–3.2 px of d0–d3. The other
  pairing is 7–9 px off.
- **The radius is clipped to the frame.** In each camera one radius equals
  a centre coordinate exactly: this camera's d5 = d3 (lens 2 sits 1435.18
  px from the top edge), the second camera's d4 = d0 (1439.55 px from the
  left edge). In all four cases the radius is the smaller of a measured value
  and the distance to the nearest edge of the half.

Not known: d6–d14, the axis conventions, and whether "centre" means the
image-circle centre or the optical axis (the factory name). The stitcher
that reads them would answer it.

## Other values

- **ISO:** the factory "ISO line adjust" (`+SCAMISOT`, RTOS `FUN_803a1674`)
  writes `LIVE_ISO_GAIN_X1`/`X2` per module, sets the pass/fail nibble in
  `ISO_FLAG` and writes 0x100 to `ISO_VERSION`. Its run-time use is not
  traced.
- **Not read by the RTOS by fixed offset:** MOT_PIMA, HISTOGRAM_PASS_AMOUNT,
  MODULE_ID and the SET_* results. They look like quality-test records.

## Evidence

- Field table: `libfactory-mode.so` sha256 927c9e04…, entries of 0x60
  bytes at file offset 0xf7e08 (`lens_eeprom.py --extract-table`).
- Factory log: `dump/extracted/opt/share/dfms/dfms.log` (local).
- REA evidence (Ghidra 12.1.3): `get_OPAX`
  ev_52f7c0a531ed969fb9f5fd8910cf4c7d72c83e5fb2c1ca76159567656903c97b,
  `get_OPAI` ev_e104b3c61d91d371d8f4b0175f272c72c8393c3904369519248438df8edbd4c4,
  `getversion_OPAX` ev_231fbbe5c420463f5310da2cbc2f2d37520b9bd89541f2dac53fad0116132414,
  ArcSoft `WriteMeta_OpticalAxis`
  ev_e899f5f9d1f74d613f348fb29b98d3c020f10fbc805664cf2d23f9312ee2aa62,
  `InitializeEngine` ev_0402966e7efe1c600b4d62a567edcfcb0987a0c5be1fec24abcaa6197d68aae2,
  calibration parser ev_e7409771975bc9bc9cfae474178876e38ca75bdf14c125ddea675d65e7b99e94.
- RTOS: saved decompile `output/decompile/rea/pass-opus-engine-20261019/rtos/`
  (functions named above) and the golden values from its `rtos-full.elf`.
- JPEGs: 666 unique decoded. 660 from this camera (640 stock JPEGs, all
  with the same tag 0x0012, and 20 of ours, which have no tag 0x0012) and
  6 stock JPEGs from the second camera.
