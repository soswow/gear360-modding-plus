# Offline SSIF dump descriptor and row splitter

This C++17 prototype defines the Linux dump descriptor as a 0x4c-byte ABI: `filename[64]` at +0x00, physical-buffer `u32` at +0x40, byte-count `u32` at +0x44, and suffix-flag `u32` at +0x48. Compile-time assertions pin the size and every field offset. The descriptor inspector reads its three integer fields as little-endian values.

The splitter implements the saved RTOS SSIF writer's row behavior. Given a saved local byte buffer, positive even `stride`, and `height`, it copies the first `stride/2` bytes of each row into `.front.raw` and the second `stride/2` bytes into `.rear.raw`, preserving byte order. It rejects odd/zero stride, zero height, multiplication overflow on hosts where `size_t` can overflow, and a source shorter than `stride * height`. Trailing source bytes are allowed and ignored.

Pixel bit depth and memory packing are separate metadata fields. The current split operation does not use either to reinterpret bytes and performs no packed-bit unpacking or 14-bit decode. `inspect` only displays a saved descriptor; the physical buffer value is an address field, not a local pointer the program dereferences.

The later [FrontendNx SSIF debug capture candidate](../archive/RAW_STOCK_SSIF_DEBUG_CAPTURE.md) uses the common whole-buffer FileDump descriptor but a different capture route. Its pixels have not been obtained. The half-row front/rear split described here is **not established for that route**; preserve an eventual original file unsplit and confirm its layout before applying the splitter. Neither descriptor parsing nor the `.raw` extension verifies sensor RAW.

Usage:

Build on the host with the existing C++ compiler:

```sh
/usr/bin/clang++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -O2 \
  reverse-engineering/raw-buffer-prototype/ssif_dump.cpp \
  -o output/decompile/rea/continuation-20261014/ssif-dump
```

```sh
./ssif_dump inspect saved-descriptor.bin
./ssif_dump inspect-image saved-stCAPTURE_IMAGE.bin
./ssif_dump split saved-buffer.bin 4096 2048 extracted
./ssif_dump split saved-buffer.bin 4096 2048 extracted 10 16
./ssif_dump --self-check
```

This prototype does not capture the camera, call a live device, fetch token 0x15000, establish Linux attachment/control, or make `/dev/mem` calls. The future Linux attachment edge and source-buffer ownership/lifetime remain unknown. It only inspects caller-supplied saved files.

Evidence for the splitter contract is in the parent checkpoint's `sensor-dma/rea-row-writer.json` and `sensor-dma/raw-dump-original.txt`: writer `0x801e516c` uses full stride as row step and half-stride as row write length; caller `0x80282760` supplies base buffer then buffer + stride/2. Descriptor field offsets are encoded here as the requested ABI contract and are compile-time enforced.

The descriptor offsets are also independently recovered from Linux `DumpFileData` (`0x627d4`) and RTOS `FileDump` (`0x8010ccd8`), which both use this layout. See [the dump protocol](../archive/RAW_DUMP_PROTOCOL.md) for the reconstructed request and ACK contracts and the unresolved live capture boundary. An ACK alone does not confirm file creation or complete pixel writes. The example metadata values are caller-supplied labels; they do not identify the camera's actual frame format.

`inspect-image` reads a saved, exactly `0x88`-byte shared `stCAPTURE_IMAGE` descriptor and prints the four geometry words at `+0x00/+0x04/+0x08/+0x0c`, stride `+0x20`, format `+0x24`, pack type `+0x28`, pixel enum `+0x2c`, memory-packing enum `+0x30`, address type `+0x40`, physical plane 0 `+0x44`, virtual plane 0 `+0x54`, and byte count `+0x74`. All words are decoded as little-endian 32-bit values and printed numerically. The address values are never dereferenced and no device or memory mapping is opened.

The address labels here belong to `stCAPTURE_IMAGE`: `+0x40` is its address-type word and `+0x44` is its physical-plane-0 word. They are distinct from the Linux `FileDump` descriptor, whose separate `+0x40` field is the physical-buffer value consumed as a `/dev/mem` mapping offset. `inspect-image` does not parse that `0x4c`-byte FileDump descriptor.

The reported `libmm_photo_packtype_guard` only states whether the saved `+0x28` value equals 3, matching the observed generic `libmm-photo.so` `convertRaw` guard. A pass does not establish image validity, successful capture, or that this particular descriptor reaches that consumer. A fail does not prove that no other route can consume the descriptor. The meanings of the four geometry words are intentionally left unspecified here. These field names and offsets come from [SENSOR_RAW_FINDINGS.md](../archive/SENSOR_RAW_FINDINGS.md) and the pass-6 Linux RAW consumer report; actual R210 descriptor values and payload validity remain unobserved.

The optional `split` arguments are caller-supplied bit-count labels such as `10 16`; they are separate from the numeric enums printed by `inspect-image`. The splitter does not use them to decode samples.
