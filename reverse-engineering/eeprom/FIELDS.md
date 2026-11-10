# Lens-module EEPROM fields, with this camera's values

All 179 fields of the stock factory table (`libfactory-mode.so`, sha256
927c9e04…), in address order, with the values read from this camera on
6 November 2026. Front is EEPROM A (I2C bus 3, front lens, left half of the
frame); rear is EEPROM B (bus 4, rear lens, right half). Both use the same
layout.

- **·** means never written (all bytes 0xff).
- Numbers are little-endian unsigned integers unless shown otherwise;
  `OPTICAL_AXIS_02`–`_40` are doubles. Text fields are in backticks.
- White points (`AWB_*_GAIN_R/G/B`) are in 1/4096 with G = 4096.
- Notes are short; the evidence is in [README.md](README.md). A "?" marks an
  inferred meaning.
- Bytes 0x800–0x1fff are outside the table. They are 0xff, except
  0x900–0x907, which are 0x00 in both.

Regenerate with:

```sh
python3 reverse-engineering/eeprom/lens_eeprom.py --fields-md output/peripherals-20261106/EEPROM_AND_REGISTERS.md
```

| Offset | Size | Factory name | Front (A, bus 3) | Rear (B, bus 4) | Note |
|---|---|---|---|---|---|
| 0x000 | 2 | TABLE_INFO_VER | 3 | 3 | layout version |
| 0x002 | 12 | CREATE_DATE | `20170527` | `20170527` |  |
| 0x00e | 1 | NV_DUST | `P` | `P` | factory step result, P or N |
| 0x00f | 1 | NV_NOISE | `N` | `N` |  |
| 0x010 | 1 | NV_ISO | `P` | `P` |  |
| 0x011 | 1 | NV_MOT | `P` | `P` |  |
| 0x012 | 1 | NV_AWB1 | `N` | `N` |  |
| 0x013 | 1 | NV_AWB2 | `N` | `N` |  |
| 0x014 | 1 | NV_AWB3 | · | · |  |
| 0x015 | 1 | NV_AWB4 | · | · |  |
| 0x016 | 2 | RESERVED1 | · | · |  |
| 0x018 | 2 | ISO_ASSET_NO | `03` | `00` |  |
| 0x01a | 2 | MOT_ASSET_NO | · | · |  |
| 0x01c | 2 | DUST_ASSET_NO | · | · |  |
| 0x01e | 2 | NOISE_ASSET_NO | · | · |  |
| 0x020 | 2 | AWB1_ASSET_NO | `02` | `00` |  |
| 0x022 | 1 | DECENTER_X | 0 | 0 | lens decenter |
| 0x023 | 1 | DECENTER_Y | 0 | 0 |  |
| 0x024 | 2 | AWB2_ASSET_NO | `02` | `00` |  |
| 0x026 | 4 | RESERVED2 | · | · |  |
| 0x02a | 3 | AWB1_FW_VER | `QC4` | `QD1` |  |
| 0x02d | 3 | AWB2_FW_VER | `QC4` | `QC4` |  |
| 0x030 | 4 | LIVE_ISO_GAIN_X1 | 143 | 141 | ISO line-adjust result |
| 0x034 | 4 | LIVE_ISO_GAIN_X2 | 174 | 169 | ISO line-adjust result |
| 0x038 | 4 | ISO_FLAG | 0xfa | 0xaa | ISO adjust result nibble: 0xa pass, 0x5 fail (A low, B high) |
| 0x03c | 4 | ISO_VERSION | 256 | 256 |  |
| 0x040 | 2 | AWB_LOW_GAIN_R | 2816 | 2864 | white point 1 (the adjust point), JPEG AWB record P1 |
| 0x042 | 2 | AWB_LOW_GAIN_G | 4096 | 4096 |  |
| 0x044 | 2 | AWB_LOW_GAIN_B | 1465 | 1458 |  |
| 0x046 | 2 | AWB_HIGH_GAIN_R | · | · |  |
| 0x048 | 2 | AWB_HIGH_GAIN_G | · | · |  |
| 0x04a | 2 | AWB_HIGH_GAIN_B | · | · |  |
| 0x04c | 52 | RESERVED3 | · | · |  |
| 0x080 | 40 | MOT_PIMA | 2004 2154 2000 2000 1821 2000 2000 2000 2000 2000 | 1971 1931 2000 2000 2000 2000 2000 2000 2000 2000 | 10 x u32; probably resolution-chart results |
| 0x0a8 | 4 | HISTOGRAM_PASS_AMOUNT | 1921 | 1570 |  |
| 0x0ac | 4 | RESERVED4 | · | · |  |
| 0x0b0 | 3 | ISO_FW_VER | `QC4` | `QC4` |  |
| 0x0b3 | 3 | MOT_FW_VER | · | · |  |
| 0x0b6 | 3 | DUST_FW_VER | · | · |  |
| 0x0b9 | 3 | NOISE_FW_VER | · | · |  |
| 0x0bc | 5 | RESERVED5 | · | · |  |
| 0x0c1 | 15 | SET_ID | `CFN000805E48254` | `CFN000805E48254` | unit number, also memory/private/pmode_uniqueno |
| 0x0d0 | 8 | OPTICAL_AXIS_01 | 2 (u32) | 2 (u32) | u32 format version (2); 0x0d0-0x16f is JPEG MakerNote tag 0x0012 |
| 0x0d8 | 8 | OPTICAL_AXIS_02 | 1446.10583 | 1446.10583 | lens 1 (front) centre x? (inferred) |
| 0x0e0 | 8 | OPTICAL_AXIS_03 | 1445.18433 | 1445.18433 | lens 1 centre y? (inferred) |
| 0x0e8 | 8 | OPTICAL_AXIS_04 | 1453.03406 | 1453.03406 | lens 2 (rear) centre x? (inferred) |
| 0x0f0 | 8 | OPTICAL_AXIS_05 | 1435.18176 | 1435.18176 | lens 2 centre y? (inferred) |
| 0x0f8 | 8 | OPTICAL_AXIS_06 | 1442.37268 | 1442.37268 | lens 1 image-circle radius? (inferred) |
| 0x100 | 8 | OPTICAL_AXIS_07 | 1435.18176 | 1435.18176 | lens 2 image-circle radius, clipped to the frame? (inferred) |
| 0x108 | 8 | OPTICAL_AXIS_08 | 1.00106458 | 1.00106458 |  |
| 0x110 | 8 | OPTICAL_AXIS_09 | 5.21426009e-05 | 5.21426009e-05 |  |
| 0x118 | 8 | OPTICAL_AXIS_10 | 5.21426009e-05 | 5.21426009e-05 |  |
| 0x120 | 8 | OPTICAL_AXIS_11 | 1.00681037 | 1.00681037 |  |
| 0x128 | 8 | OPTICAL_AXIS_12 | -0.000739024988 | -0.000739024988 |  |
| 0x130 | 8 | OPTICAL_AXIS_13 | -0.000739024988 | -0.000739024988 |  |
| 0x138 | 8 | OPTICAL_AXIS_14 | 32.3264023 | 32.3264023 |  |
| 0x140 | 8 | OPTICAL_AXIS_15 | 3.9198688 | 3.9198688 |  |
| 0x148 | 8 | OPTICAL_AXIS_16 | 4.36352527 | 4.36352527 |  |
| 0x150 | 8 | OPTICAL_AXIS_17 | 0 | 0 |  |
| 0x158 | 8 | OPTICAL_AXIS_18 | 0 | 0 |  |
| 0x160 | 8 | OPTICAL_AXIS_19 | 0 | 0 |  |
| 0x168 | 8 | OPTICAL_AXIS_20 | 0 | 0 |  |
| 0x170 | 8 | OPTICAL_AXIS_21 | 0 | 0 |  |
| 0x178 | 8 | OPTICAL_AXIS_22 | 0 | 0 |  |
| 0x180 | 8 | OPTICAL_AXIS_23 | 0 | 0 |  |
| 0x188 | 8 | OPTICAL_AXIS_24 | 0 | 0 |  |
| 0x190 | 8 | OPTICAL_AXIS_25 | 0 | 0 |  |
| 0x198 | 8 | OPTICAL_AXIS_26 | 0 | 0 |  |
| 0x1a0 | 8 | OPTICAL_AXIS_27 | 0 | 0 |  |
| 0x1a8 | 8 | OPTICAL_AXIS_28 | 0 | 0 |  |
| 0x1b0 | 8 | OPTICAL_AXIS_29 | 0 | 0 |  |
| 0x1b8 | 8 | OPTICAL_AXIS_30 | 0 | 0 |  |
| 0x1c0 | 8 | OPTICAL_AXIS_31 | 0 | 0 |  |
| 0x1c8 | 8 | OPTICAL_AXIS_32 | 0 | 0 |  |
| 0x1d0 | 8 | OPTICAL_AXIS_33 | 0 | 0 |  |
| 0x1d8 | 8 | OPTICAL_AXIS_34 | 0 | 0 |  |
| 0x1e0 | 8 | OPTICAL_AXIS_35 | 0 | 0 |  |
| 0x1e8 | 8 | OPTICAL_AXIS_36 | 0 | 0 |  |
| 0x1f0 | 8 | OPTICAL_AXIS_37 | 0 | 0 |  |
| 0x1f8 | 8 | OPTICAL_AXIS_38 | 0 | 0 |  |
| 0x200 | 8 | OPTICAL_AXIS_39 | 0 | 0 |  |
| 0x208 | 8 | OPTICAL_AXIS_40 | 0 | 0 |  |
| 0x210 | 1 | OPTICAL_AXIS_FLAG | 2 | 2 |  |
| 0x211 | 1 | LENS_GROUP_NO | · | · |  |
| 0x212 | 2 | LENS_OFFSET_R | · | · |  |
| 0x214 | 2 | LENS_OFFSET_B | · | · |  |
| 0x216 | 2 | AWB_MODULE_POINT2_GAIN_R | · | · |  |
| 0x218 | 2 | AWB_MODULE_POINT2_GAIN_G | · | · |  |
| 0x21a | 2 | AWB_MODULE_POINT2_GAIN_B | · | · |  |
| 0x21c | 2 | AWB_MODULE_POINT3_GAIN_R | · | · |  |
| 0x21e | 2 | AWB_MODULE_POINT3_GAIN_G | · | · |  |
| 0x220 | 2 | AWB_MODULE_POINT3_GAIN_B | · | · |  |
| 0x222 | 2 | AWB_MODULE_POINT4_GAIN_R | · | · |  |
| 0x224 | 2 | AWB_MODULE_POINT4_GAIN_G | · | · |  |
| 0x226 | 2 | AWB_MODULE_POINT4_GAIN_B | · | · |  |
| 0x228 | 2 | AWB_EEPROM_POINT2_GAIN_R | 1690 | 1722 | white point 2, JPEG AWB record P2 |
| 0x22a | 2 | AWB_EEPROM_POINT2_GAIN_G | 4096 | 4096 |  |
| 0x22c | 2 | AWB_EEPROM_POINT2_GAIN_B | 2312 | 2302 |  |
| 0x22e | 2 | AWB_EEPROM_POINT3_GAIN_R | 2081 | 2117 | white point 3, JPEG AWB record P3 |
| 0x230 | 2 | AWB_EEPROM_POINT3_GAIN_G | 4096 | 4096 |  |
| 0x232 | 2 | AWB_EEPROM_POINT3_GAIN_B | 1662 | 1651 |  |
| 0x234 | 2 | AWB_EEPROM_POINT4_GAIN_R | 3417 | 3483 | white point 4, JPEG AWB record P4 |
| 0x236 | 2 | AWB_EEPROM_POINT4_GAIN_G | 4096 | 4096 |  |
| 0x238 | 2 | AWB_EEPROM_POINT4_GAIN_B | 1168 | 1164 |  |
| 0x23a | 2 | AWB_MODULE_LOW_GAIN_R | 2797 | 2861 | module maker's point 1; read only in factory mode |
| 0x23c | 2 | AWB_MODULE_LOW_GAIN_G | 4096 | 4096 |  |
| 0x23e | 2 | AWB_MODULE_LOW_GAIN_B | 1496 | 1458 |  |
| 0x240 | 2 | AWB_DELTA_E | · | · |  |
| 0x242 | 30 | RESERVED6 | · | · |  |
| 0x260 | 4 | MODULE_COLOR_NOISE_R_G_DIFF | · | · | also read for JPEG MakerNote tag 0x0013 (LSC info) |
| 0x264 | 4 | MODULE_COLOR_NOISE_B_G_DIFF | · | · |  |
| 0x268 | 4 | MODULE_DOT_NOISE_YMAX | · | · |  |
| 0x26c | 4 | MODULE_DOT_NOISE_YMIN | · | · |  |
| 0x270 | 4 | MODULE_DOT_NOISE_YDIFF | · | · |  |
| 0x274 | 4 | MODULE_DOT_NOISE_YNMAX | · | · |  |
| 0x278 | 4 | MODULE_DOT_NOISE_YNMIN | · | · |  |
| 0x27c | 4 | MODULE_DOT_NOISE_YNDIFF | · | · |  |
| 0x280 | 4 | MODULE_COLOR_NOISE_RMAX | · | · |  |
| 0x284 | 4 | MODULE_COLOR_NOISE_RMIN | · | · |  |
| 0x288 | 4 | MODULE_COLOR_NOISE_GMAX | · | · |  |
| 0x28c | 4 | MODULE_COLOR_NOISE_GMIN | · | · |  |
| 0x290 | 4 | MODULE_COLOR_NOISE_BMAX | · | · |  |
| 0x294 | 4 | MODULE_COLOR_NOISE_BMIN | · | · |  |
| 0x298 | 4 | MODULE_LINE_NOISE1X | · | · |  |
| 0x29c | 4 | MODULE_LINE_NOISE2X | · | · |  |
| 0x2a0 | 4 | MODULE_LINE_NOISE3X | · | · |  |
| 0x2a4 | 4 | MODULE_SHADING_DIFF | · | · |  |
| 0x2a8 | 4 | MODULE_EXPOSURE_AVG | · | · |  |
| 0x2ac | 4 | MODULE_DUST_1X | · | · |  |
| 0x2b0 | 4 | MODULE_DUST_1Y | · | · |  |
| 0x2b4 | 4 | MODULE_DUST_1MIN | · | · |  |
| 0x2b8 | 4 | MODULE_DUST_1MAX | · | · |  |
| 0x2bc | 4 | MODULE_DUST_2X | · | · |  |
| 0x2c0 | 4 | MODULE_DUST_2Y | · | · |  |
| 0x2c4 | 4 | MODULE_DUST_2MIN | · | · |  |
| 0x2c8 | 4 | MODULE_DUST_2MAX | · | · |  |
| 0x2cc | 4 | MODULE_DUST_3X | · | · |  |
| 0x2d0 | 4 | MODULE_DUST_3Y | · | · |  |
| 0x2d4 | 4 | MODULE_DUST_3MIN | · | · |  |
| 0x2d8 | 4 | MODULE_DUST_3MAX | · | · |  |
| 0x2dc | 36 | RESERVED7 | · | · |  |
| 0x300 | 4 | BAT_CHARGE_COUNT | · | · |  |
| 0x304 | 4 | TA_INSERT_COUNT | · | · |  |
| 0x308 | 13 | CHIP_ID | · | · |  |
| 0x315 | 10 | MODULE_ID | `OVKER 0174000101` | `OVKER 02cd000101` | maker code + serial |
| 0x31f | 1 | RESERVED8 | · | · |  |
| 0x320 | 4 | SET_COLOR_NOISE_R_G_DIFF | 1443 | 1877 |  |
| 0x324 | 4 | SET_COLOR_NOISE_B_G_DIFF | 531 | 431 |  |
| 0x328 | 4 | SET_DOT_NOISE_YMAX | 20150 | 20228 |  |
| 0x32c | 4 | SET_DOT_NOISE_YMIN | 12838 | 13252 |  |
| 0x330 | 4 | SET_DOT_NOISE_YDIFF | 7312 | 6975 |  |
| 0x334 | 4 | SET_DOT_NOISE_YNMAX | 11937 | 11856 |  |
| 0x338 | 4 | SET_DOT_NOISE_YNMIN | 7605 | 7767 |  |
| 0x33c | 4 | SET_DOT_NOISE_YNDIFF | 4331 | 4088 |  |
| 0x340 | 4 | SET_COLOR_NOISE_RMAX | 21599 | 21759 |  |
| 0x344 | 4 | SET_COLOR_NOISE_RMIN | 12015 | 12234 |  |
| 0x348 | 4 | SET_COLOR_NOISE_GMAX | 21667 | 21705 |  |
| 0x34c | 4 | SET_COLOR_NOISE_GMIN | 13458 | 14086 |  |
| 0x350 | 4 | SET_COLOR_NOISE_BMAX | 21134 | 21331 |  |
| 0x354 | 4 | SET_COLOR_NOISE_BMIN | 13752 | 14034 |  |
| 0x358 | 4 | SET_LINE_NOISE1X | 0 | 0 |  |
| 0x35c | 4 | SET_LINE_NOISE2X | 0 | 0 |  |
| 0x360 | 4 | SET_LINE_NOISE3X | 0 | 0 |  |
| 0x364 | 4 | SET_SHADING_DIFF | 839 | 836 |  |
| 0x368 | 4 | SET_EXPOSURE_AVG | 16719 | 16825 |  |
| 0x36c | 4 | SET_DUST_1X | 0 | 0 |  |
| 0x370 | 4 | SET_DUST_1Y | 0 | 0 |  |
| 0x374 | 4 | SET_DUST_1MIN | 0 | 0 |  |
| 0x378 | 4 | SET_DUST_1MAX | 0 | 0 |  |
| 0x37c | 4 | SET_DUST_2X | 0 | 0 |  |
| 0x380 | 4 | SET_DUST_2Y | 0 | 0 |  |
| 0x384 | 4 | SET_DUST_2MIN | 0 | 0 |  |
| 0x388 | 4 | SET_DUST_2MAX | 0 | 0 |  |
| 0x38c | 4 | SET_DUST_3X | 0 | 0 |  |
| 0x390 | 4 | SET_DUST_3Y | 0 | 0 |  |
| 0x394 | 4 | SET_DUST_3MIN | 0 | 0 |  |
| 0x398 | 4 | SET_DUST_3MAX | 0 | 0 |  |
| 0x39c | 1120 | RESERVED9 | · | · |  |
| 0x7fc | 4 | CHECKSUM | 0xaad33f25 | 0x5073e3e | sum of u32 words 0x000-0x7fb |
