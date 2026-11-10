> **Frozen 20 October 2026.** Not the task list. Claims live in `reverse-engineering/facts/`. The open phase is `reverse-engineering/CURRENT_INVESTIGATION.md`.

# Offline raw evidence validator

Run `python3 reverse-engineering/validate_raw_evidence.py PASS_DIRECTORY REPORT.json` to validate pass-local carrier manifests and segments, pinned RTOS byte windows, structured snapshots, retained Evidence IDs, and source-mapped `read_bytes` records. By default, the repository root is inferred from the pass path and prior manifests are discovered below `output/decompile/rea`; use `--repo-root` and `--prior-passes-root` to override those locations. Repeat `--exclude PATH` to omit an artifact or directory from current-pass scanning and prior-manifest discovery. The report records explicit exclusions and omits its own output file from scanning.

Only nonempty reads whose subject digest maps to a validated carrier and whose full address range fits inside a manifested initialized segment can appear under `readBytes.checked`. Empty, malformed, and unmapped reads are separately reported as `empty` or `excluded`; source mismatches and malformed current-pass JSON appear in the report and cause a nonzero exit. Malformed prior manifest JSON is skipped and listed in `priorMetadataSkipped`. Coordinates accept integers and integer strings only. The validator reads files as data and does not run REA, firmware, or target binaries.

Snapshot counts recognize the stored schema and, separately, filenames containing `closed`. They do not verify process exit. Native-session closure needs its own REA close/session check and any required exact-PID audit.

Pinned RTOS window manifests accept explicit file-offset fields, including `source_file_offset_start`, and numeric `bytes` lengths. The recognized recipe-window schema may derive the file offset from `address - 0x80000000` only with the pinned source digest and required exact `bytes_hex`. Optional file/VA end coordinates must agree with the computed range. Fractional, boolean, invalid-string and out-of-range addresses are rejected.
