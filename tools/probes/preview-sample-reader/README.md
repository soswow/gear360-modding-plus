# Passive, unverified preview sample

This fixed ARM helper reads only4096bytes from ordinary RAM at `a77bb000`, after checking two selected-image globals and five image fields at fixed addresses. It repeats all guards after copying. It opens `/dev/mem` read-only, maps three fixed read-only pages, accepts only `preview-sample`, and has no camera command, MMIO access, device write, cache operation or pointer following. Invalid arguments exit before opening the device. Stable guards do not pin pixels, exclude DMA races, or establish a complete sensor frame.

Build: `sh tools/probes/preview-sample-reader/build.sh`. Parent rebuild matches the worker binary byte-for-byte, SHA256 `0b6cb91f368879dd3d00e6ac91fe0bee7e0c31e67367b23f7b191ebf1aa46e65`. Mocked host tests cover all three pages, seven guards before/after, exact4096-byte output, failures and cleanup. One passive camera read succeeded; helper/directory removed; app identity unchanged. Sources/tests/traces are preserved in the completion-pass evidence bundle.

The Mac file is deliberately named `UNVERIFIED-preview-sample.bin`. It is a fragment of the selected buffer, not a completed RAW acquisition. The sample does not consistently fit the candidate16-bit mask3ffc in either endian or byte phase. **Packed RAW remains possible:** stride2880 for width1920 also equals12bits/pixel. Neither “processed YUV” nor “verified RAW” is established. The first priority on resume is to resolve that packing from original configurator code and actual bytes, then assess a passive full-buffer snapshot without conflating it with a lease.

The full-extent reader lives in `../full-buffer-reader/`. It keeps these same fixed guards and does not replace this 4096-byte helper or its recorded ELF hash.
