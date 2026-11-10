#!/usr/bin/env python3
"""Validate offline REA byte evidence against source-bound manifests.

The validator reads JSON and binary artifacts only. It never launches REA,
firmware, or target binaries. Source and carrier files are hashed in chunks;
manifested ranges are read with bounded seeks.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any, Iterable

PINNED_RTOS_SHA256 = "cfe69efe127b2ae16368e96fb0ae8893570a4e3d94a87fd14ccbc6aeba3e8cd7"
PINNED_RTOS_VA_BASE = 0x80000000
CHUNK_SIZE = 1024 * 1024
SOURCE_OFFSET_KEYS = ("source_offset", "source_file_offset", "source_file_offset_start", "file_offset", "file_offset_start", "offset")
LENGTH_KEYS = ("returned_length", "length_returned", "returned_bytes", "bytes", "length", "size")
WINDOW_SHA_KEYS = ("sha256", "bytes_sha256", "window_sha256")


class ValidationError(Exception):
    pass


def number(value: Any) -> int:
    if isinstance(value, bool):
        raise ValueError("boolean is not an integer coordinate")
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        text = value.strip()
        if text.lower().lstrip("+-").startswith("0x"):
            return int(text, 16)
        if text and (text.isdecimal() or (text[0] in "+-" and text[1:].isdecimal())):
            return int(text, 10)
    raise ValueError(f"coordinate must be an integer or integer string, got {value!r}")


def file_sha256(path: Path) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        while True:
            block = stream.read(CHUNK_SIZE)
            if not block:
                break
            digest.update(block)
            size += len(block)
    return digest.hexdigest(), size


def read_range(path: Path, offset: int, length: int) -> bytes:
    if offset < 0 or length < 0:
        raise ValueError("negative file range")
    with path.open("rb") as stream:
        stream.seek(offset)
        return stream.read(length)


def range_sha256(path: Path, offset: int, length: int) -> str:
    digest = hashlib.sha256()
    remaining = length
    with path.open("rb") as stream:
        stream.seek(offset)
        while remaining:
            block = stream.read(min(CHUNK_SIZE, remaining))
            if not block:
                raise OSError(f"short read at {path}:{offset} for {length} bytes")
            digest.update(block)
            remaining -= len(block)
    return digest.hexdigest()


def ranges_equal(left: Path, left_offset: int, right: Path, right_offset: int, length: int) -> bool:
    with left.open("rb") as left_stream, right.open("rb") as right_stream:
        left_stream.seek(left_offset)
        right_stream.seek(right_offset)
        remaining = length
        while remaining:
            size = min(CHUNK_SIZE, remaining)
            a, b = left_stream.read(size), right_stream.read(size)
            if len(a) != size or len(b) != size or a != b:
                return False
            remaining -= size
    return True


def resolve_path(value: Any, repo: Path, manifest_dir: Path) -> Path:
    raw = Path(str(value)).expanduser()
    if raw.is_absolute():
        return raw.resolve()
    candidates = (repo / raw, manifest_dir / raw)
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    return candidates[0].resolve()


def is_excluded(path: Path, excluded: list[Path]) -> bool:
    resolved = path.resolve()
    return any(resolved == item or item in resolved.parents for item in excluded)


def json_files(root: Path, excluded: list[Path], output_path: Path) -> Iterable[Path]:
    for path in sorted(root.rglob("*.json")):
        if path.resolve() == output_path or is_excluded(path, excluded):
            continue
        rel_parts = path.relative_to(root).parts
        if any(part.startswith("reviewed-") for part in rel_parts):
            continue
        if path.name.startswith("parent-") or path.name == "RUN_CHECKPOINT.json":
            continue
        yield path


def json_object(path: Path) -> tuple[dict[str, Any] | None, str | None]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        return None, str(exc)
    return (value, None) if isinstance(value, dict) else (None, None)


def records_in(obj: dict[str, Any]) -> list[dict[str, Any]]:
    found: list[dict[str, Any]] = []
    content = obj.get("structuredContent")
    if isinstance(content, dict):
        # Tool wrappers occur both as a record directly and as a nested record list.
        if content.get("evidence_id"):
            found.append(content)
        if isinstance(content.get("records"), list):
            found.extend(x for x in content["records"] if isinstance(x, dict))
    if isinstance(obj.get("records"), list):
        found.extend(x for x in obj["records"] if isinstance(x, dict))
    bundle = obj.get("evidence_bundle")
    if isinstance(bundle, dict) and isinstance(bundle.get("records"), list):
        found.extend(x for x in bundle["records"] if isinstance(x, dict))
    return found


def first_present(obj: dict[str, Any], keys: tuple[str, ...]) -> Any:
    for key in keys:
        if key in obj:
            return obj[key]
    return None


def rel(path: Path, repo: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo.resolve()))
    except ValueError:
        return str(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pass_directory", type=Path, help="pass directory containing REA artifacts")
    parser.add_argument("output_json", type=Path, help="where to write the validation report")
    parser.add_argument("--repo-root", type=Path, help="repository root (inferred from output/decompile/rea when omitted)")
    parser.add_argument("--prior-passes-root", type=Path, help="manifest search root for reused carriers (default: output/decompile/rea)")
    parser.add_argument("--exclude", action="append", default=[], metavar="PATH", help="exclude a JSON artifact or directory from both scans; repeatable")
    args = parser.parse_args()

    pass_dir = args.pass_directory.resolve()
    output_path = args.output_json.resolve()
    repo = args.repo_root.resolve() if args.repo_root else None
    if repo is None:
        for ancestor in (pass_dir, *pass_dir.parents):
            if (ancestor / "output" / "decompile" / "rea").is_dir():
                repo = ancestor
                break
    if repo is None:
        repo = Path.cwd().resolve()
    rea_root = (args.prior_passes_root.resolve() if args.prior_passes_root else repo / "output" / "decompile" / "rea")
    if not pass_dir.is_dir():
        parser.error(f"pass directory does not exist: {pass_dir}")
    excluded_paths = [(repo / Path(value)).resolve() if not Path(value).is_absolute() else Path(value).resolve() for value in args.exclude]

    errors: list[dict[str, str]] = []
    prior_metadata_skipped: list[dict[str, str]] = []
    validated_carriers: list[dict[str, Any]] = []
    validated_boot: list[dict[str, Any]] = []
    validated_windows: list[dict[str, Any]] = []
    closed_snapshots: list[dict[str, Any]] = []
    structured_snapshot_files: list[dict[str, Any]] = []
    evidence_ids: dict[str, str] = {}
    source_hash_cache: dict[Path, tuple[str, int]] = {}
    carrier_hash_cache: dict[Path, tuple[str, int]] = {}
    # Mapping metadata from old manifests is loaded lazily for matching; those
    # prior carrier/source archives are not blanket rehashed.
    mappings_by_digest: dict[str, list[dict[str, Any]]] = {}
    read_records: dict[str, dict[str, Any]] = {}
    duplicate_read_ids = 0
    duplicate_evidence_ids = 0

    def cached_hash(path: Path, cache: dict[Path, tuple[str, int]]) -> tuple[str, int]:
        path = path.resolve()
        if path not in cache:
            cache[path] = file_sha256(path)
        return cache[path]

    def add_error(path: Path, message: str) -> None:
        errors.append({"path": rel(path, repo), "error": message})

    manifests: list[tuple[Path, dict[str, Any], bool]] = []
    current_json: dict[Path, dict[str, Any] | None] = {}
    for path in json_files(pass_dir, excluded_paths, output_path):
        obj, parse_error = json_object(path)
        current_json[path] = obj
        if parse_error is not None:
            add_error(path, f"invalid current-pass JSON: {parse_error}")
            continue
        if obj is None:
            continue
        in_pass = True
        if {"source", "carrier", "segments", "carrier_sha256"} <= obj.keys() or {
            "source_path", "carrier_path", "mapping", "carrier_sha256"
        } <= obj.keys():
            manifests.append((path, obj, in_pass))
        for record in records_in(obj):
            eid = record.get("evidence_id")
            if eid:
                if eid in evidence_ids:
                    duplicate_evidence_ids += 1
                else:
                    evidence_ids[str(eid)] = rel(path, repo)
            if record.get("operation") == "read_bytes":
                key = str(eid) if eid else f"missing:{path}:{len(read_records)}"
                if key in read_records:
                    duplicate_read_ids += 1
                else:
                    read_records[key] = {"record": record, "path": path}
        if {"binding", "target", "evidence_bundle"} <= obj.keys():
            identity = {"path": rel(path, repo), "sha256": cached_hash(path, carrier_hash_cache)[0]}
            structured_snapshot_files.append(identity)
            if "closed" in path.name.lower():
                closed_snapshots.append(identity)

    # Load prior mapping metadata only, to recognize read_bytes subjects whose
    # carrier manifest lives in another pass. Current pass manifests are fully
    # checked below before they can validate a read.
    prior_files = sorted(p for p in rea_root.rglob("*.manifest.json") if not is_excluded(p, excluded_paths) and p.resolve() != output_path) if rea_root.is_dir() else []
    pass_manifest_resolved = {p.resolve() for p, _, _ in manifests}
    for path in prior_files:
        if path.resolve() in pass_manifest_resolved:
            continue
        obj, parse_error = json_object(path)
        if parse_error is not None:
            prior_metadata_skipped.append({"path": rel(path, repo), "reason": f"invalid prior manifest JSON: {parse_error}"})
            continue
        if not obj:
            continue
        try:
            mappings = manifest_mappings(path, obj, repo)
        except (KeyError, TypeError, ValueError, OSError):
            continue
        for mapping in mappings:
            mapping["manifest_obj"] = obj
            mappings_by_digest.setdefault(str(obj.get("carrier_sha256", "")), []).append(mapping)

    # Current pass manifest validation. Every listed source and carrier digest,
    # every segment bound and byte equality, plus every segment digest is checked.
    validated_manifest_paths: set[Path] = set()
    for path, obj, _ in manifests:
        try:
            if {"source", "carrier", "segments", "carrier_sha256"} <= obj.keys():
                source = resolve_path(obj["source"], repo, path.parent)
                carrier = resolve_path(obj["carrier"], repo, path.parent)
                source_digest, source_size = cached_hash(source, source_hash_cache)
                carrier_digest, carrier_size = cached_hash(carrier, carrier_hash_cache)
                if source_digest != obj["source_sha256"]:
                    raise ValidationError(f"source_sha256 mismatch: expected {obj['source_sha256']}, got {source_digest}")
                if carrier_digest != obj["carrier_sha256"]:
                    raise ValidationError(f"carrier_sha256 mismatch: expected {obj['carrier_sha256']}, got {carrier_digest}")
                segments = []
                for index, segment in enumerate(obj["segments"]):
                    start = number(segment["source_start"])
                    end = number(segment["source_end_exclusive"])
                    offset = number(segment["carrier_offset"])
                    if not (0 <= start < end <= source_size):
                        raise ValidationError(f"segment {index} source bounds invalid: {start}:{end} of {source_size}")
                    if offset < 0 or offset + end - start > carrier_size:
                        raise ValidationError(f"segment {index} carrier bounds invalid: {offset}+{end-start} of {carrier_size}")
                    if not ranges_equal(source, start, carrier, offset, end-start):
                        raise ValidationError(f"segment {index} bytes differ between source and carrier")
                    segment_digest = range_sha256(source, start, end-start)
                    if segment_digest != segment["sha256"]:
                        raise ValidationError(f"segment {index} sha256 mismatch")
                    segments.append({"va": number(segment["virtual_address"]), "source_start": start, "length": end-start})
                mappings = [{"source": source, "segments": segments, "manifest": path, "validated": True}]
                mappings_by_digest.setdefault(carrier_digest, []).extend(mappings)
                validated_carriers.append({"path": rel(path, repo), "source": rel(source, repo), "carrier": rel(carrier, repo), "bytes": carrier_size, "sha256": carrier_digest, "segments": len(segments)})
            else:
                validate_boot_manifest(path, obj, repo, source_hash_cache, carrier_hash_cache)
                source = resolve_path(obj["source_path"], repo, path.parent)
                carrier = resolve_path(obj["carrier_path"], repo, path.parent)
                mapping_obj = obj["mapping"]
                va = number(mapping_obj["carrier_va_start"])
                source_start = number(mapping_obj["source_file_offset_start"])
                source_end = number(mapping_obj["source_file_offset_end_exclusive"])
                mappings_by_digest.setdefault(str(obj["carrier_sha256"]), []).append({
                    "source": source,
                    "segments": [{"va": va, "source_start": source_start, "length": source_end-source_start}],
                    "manifest": path,
                    "validated": True,
                })
                digest, size = cached_hash(carrier, carrier_hash_cache)
                validated_boot.append({"path": rel(path, repo), "source": rel(source, repo), "carrier": rel(carrier, repo), "bytes": size, "sha256": digest})
            validated_manifest_paths.add(path.resolve())
        except (ValidationError, KeyError, TypeError, ValueError, OSError, struct.error) as exc:
            add_error(path, str(exc))

    # Source windows for the pinned RTOS use several documented offset/length aliases.
    pinned_path = resolve_path("output/decompile/rea/rtos-p6.bin", repo, pass_dir)
    try:
        pinned_digest, pinned_size = cached_hash(pinned_path, source_hash_cache)
        if pinned_digest != PINNED_RTOS_SHA256:
            add_error(pinned_path, f"pinned RTOS digest mismatch: {pinned_digest}")
    except OSError as exc:
        pinned_digest, pinned_size = "", 0
        add_error(pinned_path, f"cannot read pinned RTOS source: {exc}")
    for path in json_files(pass_dir, excluded_paths, output_path):
        obj = current_json.get(path)
        if not obj or obj.get("source_sha256") != PINNED_RTOS_SHA256:
            continue
        windows = obj.get("windows", obj.get("ranges", []))
        if not isinstance(windows, list):
            add_error(path, "windows/ranges must be an array")
            continue
        recipe_va_schema = (
            {"carrier", "carrier_sha256", "mapping_note", "verified_windows"} <= obj.keys()
            and isinstance(obj.get("mapping_note"), str)
            and "Virtual addresses use link-base mapping" in obj["mapping_note"]
        )
        if "source_va_mapping" in obj and obj["source_va_mapping"] != "VA = file_offset + 0x80000000":
            add_error(path, "unsupported source_va_mapping; expected explicit pinned RTOS base mapping")
            continue
        for index, window in enumerate(windows):
            if not isinstance(window, dict):
                add_error(path, f"window {index} is not an object")
                continue
            try:
                offset_value = first_present(window, SOURCE_OFFSET_KEYS)
                length_value = first_present(window, LENGTH_KEYS)
                expected_sha = first_present(window, WINDOW_SHA_KEYS)
                requires_bytes_hex = False
                if offset_value is None and recipe_va_schema and "address" in window:
                    offset_value = number(window["address"]) - PINNED_RTOS_VA_BASE
                    requires_bytes_hex = True
                if offset_value is None or length_value is None or not expected_sha:
                    raise ValidationError("missing source offset, returned length, or sha256")
                offset, length = number(offset_value), number(length_value)
                if length <= 0 or offset < 0 or offset + length > pinned_size:
                    raise ValidationError(f"window bounds invalid: {offset}+{length} of {pinned_size}")
                if "source_file_offset_end_exclusive" in window and number(window["source_file_offset_end_exclusive"]) != offset + length:
                    raise ValidationError("source_file_offset_end_exclusive does not match offset plus length")
                if "source_va_start" in window and number(window["source_va_start"]) != offset + PINNED_RTOS_VA_BASE:
                    raise ValidationError("source_va_start does not match pinned RTOS file offset mapping")
                if "source_va_end_exclusive" in window and number(window["source_va_end_exclusive"]) != offset + length + PINNED_RTOS_VA_BASE:
                    raise ValidationError("source_va_end_exclusive does not match pinned RTOS file offset mapping")
                actual = read_range(pinned_path, offset, length)
                digest = hashlib.sha256(actual).hexdigest()
                if digest != expected_sha:
                    raise ValidationError(f"window sha256 mismatch: expected {expected_sha}, got {digest}")
                if requires_bytes_hex and "bytes_hex" not in window:
                    raise ValidationError("address-based pinned RTOS windows require bytes_hex")
                if "bytes_hex" in window and bytes.fromhex(window["bytes_hex"]) != actual:
                    raise ValidationError("window bytes_hex differ from pinned source")
                validated_windows.append({"manifest": rel(path, repo), "offset": hex(offset), "bytes": length, "sha256": digest})
            except (ValidationError, KeyError, TypeError, ValueError, OSError) as exc:
                add_error(path, f"window {index}: {exc}")

    read_checked: list[dict[str, Any]] = []
    read_excluded: list[dict[str, Any]] = []
    read_failed: list[dict[str, Any]] = []
    empty_reads: list[dict[str, Any]] = []
    for key, item in read_records.items():
        record, path = item["record"], item["path"]
        raw = record.get("raw_result")
        info: dict[str, Any] = {"evidenceId": record.get("evidence_id"), "record": rel(path, repo)}
        if not isinstance(raw, dict) or "bytes_hex" not in raw:
            read_excluded.append({**info, "reason": "missing raw_result.bytes_hex"})
            continue
        try:
            data = bytes.fromhex(raw["bytes_hex"])
            address = number(raw["address"])
            subject = record.get("subject")
            subject_digest = subject.get("digest", {}).get("sha256") if isinstance(subject, dict) else None
            info.update({"address": hex(address), "bytes": len(data), "subjectSha256": subject_digest})
            if not data:
                empty_reads.append({**info, "reason": "empty read_bytes result"})
                continue
            choices = mappings_by_digest.get(str(subject_digest), [])
            matched = None
            for mapping in choices:
                if not mapping.get("validated"):
                    try:
                        validate_prior_mapping(mapping, repo, source_hash_cache, carrier_hash_cache)
                    except (ValidationError, KeyError, TypeError, ValueError, OSError, struct.error):
                        continue
                for segment in mapping["segments"]:
                    if segment["va"] <= address and address + len(data) <= segment["va"] + segment["length"]:
                        matched = (mapping, segment)
                        break
                if matched:
                    break
            if not matched:
                read_excluded.append({**info, "reason": "no validated pass-local or on-demand prior-pass carrier mapping contains the complete read" if choices else "subject digest has no known carrier mapping"})
                continue
            mapping, segment = matched
            source_offset = segment["source_start"] + address - segment["va"]
            expected = read_range(mapping["source"], source_offset, len(data))
            if len(expected) != len(data):
                read_excluded.append({**info, "reason": "mapped source range is short"})
                continue
            result = {**info, "sourceOffset": hex(source_offset), "manifest": rel(mapping["manifest"], repo), "windowSha256": hashlib.sha256(expected).hexdigest()}
            if data == expected:
                read_checked.append(result)
            else:
                read_failed.append(result)
        except (KeyError, TypeError, ValueError, OSError) as exc:
            read_excluded.append({**info, "reason": f"malformed read_bytes record: {exc}"})

    report = {
        "schema": "gear360-offline-evidence-validation-v1",
        "scope": "Offline validation of pass-local manifests, source windows, closed snapshots, retained Evidence IDs and source-mapped REA read_bytes records.",
        "passDirectory": rel(pass_dir, repo),
        "exclusions": [rel(path, repo) for path in excluded_paths],
        "outputJsonExcludedFromScan": rel(output_path, repo),
        "priorMetadataSkipped": prior_metadata_skipped,
        "pinnedRtos": {"path": rel(pinned_path, repo), "sha256": pinned_digest, "bytes": pinned_size, "expectedSha256": PINNED_RTOS_SHA256},
        "validatedCarriers": validated_carriers,
        "validatedBootCarriers": validated_boot,
        "validatedSourceWindows": validated_windows,
        "closedSnapshots": closed_snapshots,
        "structuredSnapshotFiles": structured_snapshot_files,
        "evidenceIds": evidence_ids,
        "readBytes": {"checked": read_checked, "excluded": read_excluded, "empty": empty_reads, "failed": read_failed},
        "counts": {
            "carrierManifests": len(validated_carriers),
            "carrierSegments": sum(x["segments"] for x in validated_carriers),
            "bootManifests": len(validated_boot),
            "sourceWindows": len(validated_windows),
            "closedSnapshots": len(closed_snapshots),
            "structuredSnapshotFiles": len(structured_snapshot_files),
            "evidenceIds": len(evidence_ids),
            "duplicateEvidenceSchemaOccurrences": duplicate_evidence_ids,
            "readBytesUniqueIds": len(read_records),
            "duplicateReadSchemaOccurrences": duplicate_read_ids,
            "readBytesChecked": len(read_checked),
            "readBytesExcluded": len(read_excluded),
            "readBytesEmpty": len(empty_reads),
            "readBytesFailed": len(read_failed),
            "errors": len(errors),
            "priorMetadataSkipped": len(prior_metadata_skipped),
        },
        "errors": errors,
        "limitations": [
            "Prior-pass manifests are loaded as metadata and fully checked only when a read subject needs that mapping; unrelated archived carriers are not blanket rehashed.",
            "Malformed prior-pass manifest JSON is skipped and listed in priorMetadataSkipped; malformed JSON inside the requested pass is an error. Explicit exclusions and the output report itself are omitted from both scans.",
            "A read_bytes record is called checked only if its nonempty bytes fit wholly within an initialized segment of a validated pass-local carrier and equal the mapped source bytes.",
            "This report does not validate REA semantic interpretations or prove that a target binary was executed.",
            "Snapshot counters inspect schema and filename conventions; they do not verify native-session closure or process exit. Audit close/session state separately.",
        ],
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["counts"], sort_keys=True))
    return 1 if errors or read_failed else 0


def manifest_mappings(path: Path, obj: dict[str, Any], repo: Path) -> list[dict[str, Any]]:
    """Extract prior manifest coordinate metadata without reading carrier bytes."""
    mappings: list[dict[str, Any]] = []
    if {"source", "carrier", "segments", "carrier_sha256"} <= obj.keys():
        source = resolve_path(obj["source"], repo, path.parent)
        segments = [{"va": number(s["virtual_address"]), "source_start": number(s["source_start"]),
                     "length": number(s["source_end_exclusive"])-number(s["source_start"])} for s in obj["segments"]]
    elif {"source_path", "carrier_path", "mapping", "carrier_sha256"} <= obj.keys():
        source = resolve_path(obj["source_path"], repo, path.parent)
        m = obj["mapping"]
        start, end = number(m["source_file_offset_start"]), number(m["source_file_offset_end_exclusive"])
        segments = [{"va": number(m["carrier_va_start"]), "source_start": start, "length": end-start}]
    else:
        return []
    carrier_key = "carrier" if "carrier" in obj else "carrier_path"
    mappings.append({"source": source, "carrier": resolve_path(obj[carrier_key], repo, path.parent),
                     "segments": segments, "manifest": path, "validated": False})
    return mappings


def validate_prior_mapping(mapping: dict[str, Any], repo: Path,
                           source_cache: dict[Path, tuple[str, int]],
                           carrier_cache: dict[Path, tuple[str, int]]) -> None:
    """Validate one reused manifest only when a read names its carrier digest."""
    obj = mapping["manifest_obj"]
    source, carrier = mapping["source"], mapping["carrier"]
    if {"source_path", "carrier_path", "mapping", "carrier_sha256"} <= obj.keys():
        validate_boot_manifest(mapping["manifest"], obj, repo, source_cache, carrier_cache)
        mapping["validated"] = True
        return
    if source not in source_cache:
        source_cache[source] = file_sha256(source)
    if carrier not in carrier_cache:
        carrier_cache[carrier] = file_sha256(carrier)
    source_digest, source_size = source_cache[source]
    carrier_digest, carrier_size = carrier_cache[carrier]
    if source_digest != obj["source_sha256"] or carrier_digest != obj["carrier_sha256"]:
        raise ValidationError("reused source or carrier digest mismatch")
    for index, segment in enumerate(obj["segments"]):
        start = number(segment["source_start"])
        end = number(segment["source_end_exclusive"])
        offset = number(segment["carrier_offset"])
        length = end - start
        if not (0 <= start < end <= source_size and 0 <= offset and offset + length <= carrier_size):
            raise ValidationError(f"reused segment {index} bounds invalid")
        if not ranges_equal(source, start, carrier, offset, length) or range_sha256(source, start, length) != segment["sha256"]:
            raise ValidationError(f"reused segment {index} source binding mismatch")
    mapping["validated"] = True


def validate_boot_manifest(path: Path, obj: dict[str, Any], repo: Path,
                           source_cache: dict[Path, tuple[str, int]], carrier_cache: dict[Path, tuple[str, int]]) -> None:
    source = resolve_path(obj["source_path"], repo, path.parent)
    carrier = resolve_path(obj["carrier_path"], repo, path.parent)
    if source not in source_cache:
        source_cache[source] = file_sha256(source)
    if carrier not in carrier_cache:
        carrier_cache[carrier] = file_sha256(carrier)
    source_digest, source_size = source_cache[source]
    carrier_digest, carrier_size = carrier_cache[carrier]
    if source_digest != obj["source_sha256"]:
        raise ValidationError(f"source_sha256 mismatch: expected {obj['source_sha256']}, got {source_digest}")
    if carrier_digest != obj["carrier_sha256"]:
        raise ValidationError(f"carrier_sha256 mismatch: expected {obj['carrier_sha256']}, got {carrier_digest}")
    mapping = obj["mapping"]
    src_start = number(mapping["source_file_offset_start"])
    src_end = number(mapping["source_file_offset_end_exclusive"])
    if not (0 <= src_start < src_end <= source_size):
        raise ValidationError("boot source mapping bounds invalid")
    if src_start != 0 or src_end != source_size:
        raise ValidationError("custom boot mapping must cover the source exactly")
    # The accepted custom schema describes one ELF32 little-endian PT_LOAD whose
    # initialized bytes equal the complete source image.
    header = read_range(carrier, 0, 52)
    if len(header) < 52 or header[:4] != b"\x7fELF" or header[4] != 1 or header[5] != 1:
        raise ValidationError("carrier is not an ELF32 little-endian image")
    phoff = struct.unpack_from("<I", header, 28)[0]
    phentsize, phnum = struct.unpack_from("<HH", header, 42)
    if phentsize < 32 or phoff + phentsize * phnum > carrier_size:
        raise ValidationError("ELF32 program header table is out of bounds")
    matched = False
    for index in range(phnum):
        entry = read_range(carrier, phoff + index * phentsize, 32)
        typ, off, va, _pa, filesz, _memsz, _flags, _align = struct.unpack("<8I", entry)
        if typ == 1 and filesz == source_size and off + filesz <= carrier_size and ranges_equal(carrier, off, source, 0, filesz):
            if number(mapping["carrier_va_start"]) != va or src_start != 0 or src_end != source_size:
                raise ValidationError("boot mapping does not match the source-identical PT_LOAD")
            matched = True
            break
    if not matched:
        raise ValidationError("no ELF32 PT_LOAD contains the complete source-identical image")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyboardInterrupt, BrokenPipeError):
        raise SystemExit(130)
