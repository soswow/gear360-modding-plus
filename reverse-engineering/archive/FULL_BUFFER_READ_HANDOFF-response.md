> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Response: full selected buffer, read passively

16 October 2026. The fixed reader copied the selected buffer and the Mac file is saved. It is an unverified memory snapshot, not a triggered capture and not a verified RAW acquisition.

`output/decompile/rea/full-buffer-read-20261016/UNVERIFIED-full-buffer.bin`

2,764,800 bytes. SHA256 `5c7a7c6ce9fb20d467e43806cd85350024311ee288a4dafcdd1a73c2e19f06e9`. Camera `wc`, camera SHA256, and the Mac file all agree. The earlier 4096-byte fragment was left in place and was not overwritten.

## What was done

The existing preview reader’s guards were kept and the copy was widened to the whole selected extent: stride 2880 by height 960, which is 675 pages of 4096 bytes, starting at `0xa77bb000`. Expected values, checked before and after the copy:

| Field | Address | Expected |
|---|---|---|
| Selected-image global | `0x81952e50` | `0x82246000` |
| Selected-image global | `0x81952fa4` | `0x82246000` |
| vptr | `0x82246000` | `0x8067a158` |
| width | `0x82246004` | 1920 |
| height | `0x82246008` | 960 |
| stride | `0x82246060` | 2880 |
| PData | `0x82246084` | `0xa77bb000` |

A mismatch aborts. The length is that constant, not a size read from a new pointer. The bytes are copied into an anonymous Linux mapping, the `/dev/mem` maps are dropped, and only then does stdout receive raw bytes. The one-line diagnostic goes to stderr. Stdout was redirected to a temporary file and retrieved by FTP, because a hex dump of this buffer would exceed the 1 MiB HTTP cap.

No capture command, settings change, MMIO access, camera-memory write, service restart, or firmware change was used. The helper was removed afterward.

The first attempt, at 12:18 UTC, did not reach the camera: the Mac was on `<home-network-ip>/24` and `192.168.43.1:8888` timed out. Nothing was uploaded. That record is in `attempt-unreachable/`.

After the access point was back, a multiline health command was rejected because the CGI drops newlines and glued `fi` to `done`. That response is in `attempt-newline-health/`. The health command was flattened to one line and then succeeded. Host tests were run again before upload. Two ARM rebuilds matched. The ELF was not executed on the Mac.

## Transfer facts

- Helper SHA256 `b521b2f3095bad637c83dcab536b6d20c2bea721d48b39f703ce878c8745bfbc`. Camera `openssl` printed that hash before the binary was run.
- Startup with no mode exited 2 and wrote zero bytes to stdout.
- `full-buffer` exited 0. Diagnostic: `UNVERIFIED_FULL_BUFFER physical-offset 0xa77bb000 bytes 2764800`.
- Remote directory `/tmp/gear360-full-buffer-20261016T122706`. Its five temporary files were deleted and the directory was removed. The following process listing found no `/tmp/gear360-full-buffer-*` path. FTP `NLST` after removal returned an empty list rather than an error; the process listing is the evidence that the directory is gone.
- `di-camera-app` stayed pid 254, start time 1042, 21 threads, every `TracerPid` 0. No new `/mnt/mmc/*.raw`. Battery 100, temperature 360.
- The first 4096 bytes of this snapshot do not match the saved fragment. Prefix SHA256 `9c0ebcda1eef303c4b9feddc924b9212d02bfa6fe9159c60a02f02f71d3b8fce`. Fragment SHA256 `59c5a5d172ed0487a0d2f76d935c175ae57e3a6ce75a4b16f366afebcb1ec9cb`.

The guards matched on this instance. That does not prove a single coherent DMA frame, that the buffer was not reused, or which sensor produced the bytes.

## What the complete buffer shows

Decode used for the pictures:

```text
sample0 = byte0 | (byte1 & 0x0f) << 8
sample1 = (byte1 >> 4) | (byte2 << 4)
```

1920 samples per row, 960 rows. Both even and odd samples are picture samples. Together they are a dual-fisheye image.

The left circle is an indoor room: ceiling, two bright lamps, shelves, a keyboard, cables, and a seated person. The right circle is the same sort of disk, mostly near black, with a bright arc on the upper-left rim and a faint arc on the lower-right rim. `inspection/packed12-le-shift4.png` keeps the top 8 bits of each sample and still shows the room, so the picture is not created by contrast stretch. `inspection/packed12-le-stretched.png` is the viewing copy.

| View | Horizontal correlation | Vertical correlation | Mean | Max |
|---|---|---|---|---|
| Little-endian packed 12-bit | 0.834 | 0.833 | 168.1 | 4095 |
| Even samples only | 0.983 | 0.789 | 161.2 | 4095 |
| Odd samples only | 0.981 | 0.875 | 174.9 | 4095 |
| MSB-first packed 12-bit | -0.178 | 0.694 | 610.0 | 4095 |
| Raw bytes as width 2880 | -0.077 | 0.538 |  |  |

The MSB-first decode still outlines both circles, and the left circle is speckled. Treating each byte as a pixel does not make the picture. Little-endian 16-bit words reach 65535, and only about 479,000 of 1,382,400 words fit mask `0x3ffc`.

Other measurements:

- Left 960 columns mean 298.3. Right 960 columns mean 37.9.
- 6,616 samples equal 4095, all in the left half. The right half has none.
- 46,371 samples set bit 10 or bit 11. Byte 2 of each three-byte group takes every value from 0 through 255. The fragment’s unused top bits and byte-2 range 0–6 belonged to that darker exposure.
- No two consecutive rows are identical. The largest row step is after row 666 and sits inside the left circle: mean absolute change about 1126 and 871 in columns 320–639, against about 14–21 across the right half.
- The fragment’s bright window at first-sample indexes 640:768 is not bright in this snapshot. That window averages 76.2 on row 0 and 47.7 on row 1, and no row of it averages above 100. The brightest rows here are about 616–624.

## Facts and hypotheses

Facts are the file identity, the guard and process checks, the hash match, the changed prefix, and the measurements above, including that the little-endian decode is a recognizable dual-fisheye picture and the other tried layouts are not.

Hypotheses:

1. Stride 2880 is the row pitch, and the little-endian split is the sample packing. Enum 2 is the firmware literal 12, the stride formula yields 2880, both parities are the same picture, and vertical neighbors agree at that pitch. A processed 12-bit image would look the same. The guards do not connect these bytes to a finished sensor DMA write.
2. The buffer holds two side-by-side fisheye views, and the right view is facing something dark or is covered. Two circular fields are visible and the right one keeps a rim. This one snapshot does not name the lens, explain the darkness, or show whether another mode fills that circle.
3. Even and odd samples are successive pixels of one image. Their means are close and each parity is smooth. A mild color-filter difference could still sit in the gap between 161 and 175. No Bayer order is identified.
4. Row 666 is not a full-width tear. The step is inside the left circle. A subtler mix of two moments can still exist. Stable guards do not exclude concurrent DMA, reclamation, or ABA.

## Not established

Sensor name, sensor origin, Bayer order, fresh completion, a pixel lease, full native resolution, or the larger firmware geometry defaults. The observed surface is 1920×960. The main goal, a verified sensor RAW file with provenance and a safe lifetime, is unfinished.

## Where the evidence is

- Snapshot, metadata, commands, responses, cleanup: `output/decompile/rea/full-buffer-read-20261016/`
- Pictures and `analysis.json`: `output/decompile/rea/full-buffer-read-20261016/inspection/`
- Reader sources and host tests: `tools/probes/full-buffer-reader/`
- Prior 4096-byte reader and fragment: unchanged, under `tools/probes/preview-sample-reader/` and `output/decompile/rea/pass-completion-20261016/preview-sample-runtime/`
