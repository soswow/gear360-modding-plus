# Capture timeline observer

A freestanding ARM helper that watches the RTOS capture pool while the camera takes a still, without writing anything. It maps RTOS memory through `/dev/mem` with `PROT_READ` only and records:

- every change to the `CCaptureMemoryManager` per-block records (`manager + 0xb0 + index * 16`, one 16-byte record per 16 KiB block of the 416 MiB pool at `0xA6000000`)
- a checksum of eight words every 256 KiB across the pool, to see where and when pixel data is written
- the VD-falling callback counter at `0x81952d38`

It never maps MMIO and never writes device memory. It only dereferences the manager pointer passed on the command line, and only after checking that the live manager cell still holds it, that its vtable is `0x8062b790`, and that its pool base field is `0xA6000000`. Output is written only after all device mappings are released and `/dev/mem` is closed.

## Build

```sh
sh tools/probes/capture-timeline-observer/build.sh
```

Output: `output/capture-timeline-observer-build/capture-timeline.elf`. The script prints its SHA-256. It does not execute the ELF.

## Run on the camera

The camera must be reachable at `192.168.43.1` with the mod web endpoint on port 8888 and FTP enabled. `run_on_camera.py` uploads the helper to a unique `/tmp` directory, verifies the camera-side hash, records the camera app's PID and start tick before and after, runs one mode, downloads the output, removes the directory, and verifies that it is gone.

```sh
# Print the live manager pointer and guards.
python3 tools/probes/capture-timeline-observer/run_on_camera.py probe --out-dir <new-dir>

# Record 20 s, issuing one stock software shutter (st key click s2) two seconds in.
python3 tools/probes/capture-timeline-observer/run_on_camera.py timeline \
  --manager 0x81cbb000 --duration-ms 20000 --period-us 0 --shutter --out-dir <new-dir>
```

Take `--manager` from a fresh `probe` on the same boot. The runner rejects `--trigger capt` before file or network operations: its former request-level command (`st cap capt single start`) was followed by the camera becoming unreachable. See `output/decompile/rea/pass-opus-engine-20261019/camera/timeline3-capt/INCIDENT.md`. The cause remains unknown; a matching RTOS submission entry does not establish a safe trigger outside the app's preparation and event handling.

## Analyze

```sh
python3 tools/probes/capture-timeline-observer/analyze_timeline.py <dir>/out --json <dir>/summary.json
```

This prints the initial allocation runs, every run added or removed with its time, address, tag and size, and the pixel-sample write activity grouped by region.

## Interpreting the output

Observed record layout: word0 is the memory slot (tag), word1 is the number of blocks remaining in the run from that block (so a run head holds the run length), and word2/word3 are non-zero only on the run head. Free blocks are all zero. One poll of the whole table takes about 34 ms, so event times are only accurate to about ±35 ms. The checksums detect writes, not content. A release seen mid-way shows up as a shorter run with zero word2/word3; that is the RTOS clearing blocks while the observer reads.
