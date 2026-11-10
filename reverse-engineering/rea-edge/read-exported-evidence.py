#!/usr/bin/env python3
"""Pull exported REA evidence records out of a parent's evidence export.

The export at output/decompile/rea/pass-4d2deb0-review-20261023/rtos-evidence.json
holds 1811 records against the seeded carrier (sha256 8321a211...).  This
prints the records that match an address, so that the reused evidence can be
read without a live REA session.

Usage:
    read-exported-evidence.py 0x80280570              # every record for that address
    read-exported-evidence.py 0x80280570 pseudo       # only the pseudocode record
    read-exported-evidence.py 0x80280570 pseudo /path/to/rtos-evidence.json

The export path is resolved under GEAR360_RESEARCH_ROOT (the repository root
that holds the artifacts) unless a path is given on the command line.

Two record shapes are read.  The actual evidence envelope carries

    evidence_id, subject, parameters, limitations, value

with the subject holding local_path and digest.sha256.  The older export
carries operation, parameters, normalized_result and raw_result.  Both are
supported, and an actual envelope is never quietly reported as unverified.

Every evidence subject that names a file and a digest is verified before its
record is shown.  A record whose subject does not verify is not shown as
evidence; a record whose subject file is simply not present locally is marked
as unverified instead.
"""
import hashlib
import json
import os
import sys
from pathlib import Path

RESEARCH_ROOT_ENV = "GEAR360_RESEARCH_ROOT"
EXPORT_REL = ("output/decompile/rea/pass-4d2deb0-review-20261023/"
              "rtos-evidence.json")

# The pinned artifacts, so that a declared subject digest can be checked
# against the file it names.
PINNED = {
    "8321a21193466694eac6d41d8bd499c4ba3f4061bb780a26e7af885a8632ae45": (
        "carrier",
        "output/decompile/rea/pass-minimax-followup-review-20261023/"
        "rtos-seeded-analysis.elf"),
    "fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7": (
        "library", "dump/extracted/platform/usr/lib/libcapture-fw-prod.so"),
    "5937b3abe7630abc8ac265ce280cc45ee591562968296bc4b368cf7e15742789": (
        "app", "dump/extracted/platform/usr/apps/com.samsung.di-camera-app/"
        "bin/di-camera-app"),
    "c0af5db6f4f26936d20d3625266e8ca3dd38d3db7f26b75de76904695834b17b": (
        "agent", "dump/extracted/platform/usr/bin/mobileap-agent"),
}

SHORT = {
    "procedure_pseudo_code": "pseudo",
    "procedure_callers": "callers",
    "procedure_callees": "callees",
    "read_function_instructions": "instr",
    "read_bytes": "bytes",
    "xrefs": "xrefs",
    "resolve_containing_procedure": "containing",
    "search_procedures": "search_proc",
    "search_strings": "search_str",
    "search_bytes": "search_bytes",
    "procedure_address": "address",
    "inspect_native_instruction": "inspect",
}

NO_RESULT = ("[no raw_result recorded for this record; the searched value may "
             "or may not be present, absence is not established]")
NO_MATCHES = ("[the byte search returned zero matches over the searched range]")


def sha256_file(path):
    """Return the hex sha256 of a file."""
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_root():
    """Return the repository root that holds the exported evidence."""
    configured = os.environ.get(RESEARCH_ROOT_ENV)
    if configured:
        return Path(configured).expanduser().resolve()
    here = Path(__file__).resolve().parent
    for start in (here, Path.cwd()):
        for candidate in [start] + list(start.parents):
            if (candidate / "output" / "decompile" / "rea").is_dir():
                return candidate.resolve()
    return here


def resolve_export_path(given, root):
    """Return the export file to read."""
    if given:
        path = Path(given).expanduser()
        return path if path.is_absolute() else (root / path).resolve()
    return (root / EXPORT_REL).resolve()


def subject_digest(subject):
    """Return the sha256 a subject declares, for either envelope shape."""
    if not isinstance(subject, dict):
        return None
    digest = subject.get("digest")
    if isinstance(digest, dict) and isinstance(digest.get("sha256"), str):
        return digest["sha256"].lower()
    for key in ("sha256", "sha"):
        value = subject.get(key)
        if isinstance(value, str):
            return value.lower()
    return None


def subject_path(subject):
    """Return the file a subject names, for either envelope shape."""
    if not isinstance(subject, dict):
        return None
    for key in ("local_path", "path", "file"):
        value = subject.get(key)
        if isinstance(value, str) and value:
            return value
    return None


def verify_subject(subject, declared_sha=None, root=None):
    """Verify one evidence subject against the declared target hash.

    Both the actual envelope (local_path + digest.sha256) and the flat legacy
    shape are understood.  An actual envelope is never reported as
    undeclared: it comes back verified, mismatched or absent.
    """
    if not isinstance(subject, dict):
        return "not-declared", "the record declares no subject"
    declared = subject_digest(subject)
    named = subject_path(subject)
    if not declared and not named:
        return "not-declared", "the record declares no subject"
    if not declared:
        return "incomplete", "the subject names a file but no digest"
    if declared_sha and declared != str(declared_sha).lower():
        return "mismatch", ("the subject digest %s is not the declared target "
                            "hash %s" % (declared, declared_sha))
    if not named:
        return "incomplete", "the subject declares a digest but no file"
    path = Path(named).expanduser()
    if not path.is_absolute() and root is not None:
        path = root / path
    if not path.is_file():
        return "absent", "the subject file is not present locally: %s" % path
    found = sha256_file(path)
    if found != declared:
        return "mismatch", ("the subject file %s hashes to %s, not the "
                            "declared %s" % (path, found, declared))
    return "verified", "the subject digest matches %s" % path


def declared_subject(doc):
    """Return the subject the export declares for itself, if any."""
    subject = doc.get("subject")
    if isinstance(subject, dict):
        return subject
    declared = doc.get("artifact_sha256") or doc.get("sha256")
    if declared:
        alias_rel = PINNED.get(str(declared).lower())
        return {"sha256": declared,
                "path": alias_rel[1] if alias_rel else None}
    return None


def format_matches(matches):
    """Return the recorded matches of a byte search, one per line."""
    lines = []
    for index, match in enumerate(matches):
        if isinstance(match, dict):
            where = ""
            for key in ("address", "virtual_address", "offset", "start", "va"):
                if key in match:
                    where = "%s=%s" % (key, match[key])
                    break
            data = ""
            for key in ("bytes", "hex", "preview", "data", "value"):
                if key in match:
                    value = match[key]
                    if isinstance(value, (bytes, bytearray)):
                        value = " ".join("%02x" % byte for byte in value)
                    elif isinstance(value, list):
                        value = " ".join(
                            "%02x" % byte for byte in value
                            if isinstance(byte, int) and 0 <= byte <= 255)
                    data = str(value)
                    break
            lines.append("%d: %s %s" % (index, where, data))
        else:
            lines.append("%d: %s" % (index, match))
    return "\n".join(lines)


def payload_text(value):
    """Return the readable text of a payload value."""
    if isinstance(value, dict):
        if isinstance(value.get("matches"), list):
            if not value["matches"]:
                return NO_MATCHES
            return format_matches(value["matches"])
        if "value" in value:
            return payload_text(value["value"])
        return json.dumps(value, indent=1)
    if isinstance(value, str):
        return value
    if isinstance(value, list):
        return "\n".join(str(item) for item in value)
    if value is None:
        return ""
    return str(value)


def record_body(record):
    """Return the payload of one evidence record.

    An actual envelope carries the payload in value; the older export carries
    it in raw_result or normalized_result.  A byte search is carried in
    matches; a null raw_result means that no result was recorded, not that
    the searched value is absent.
    """
    if "raw_result" in record:
        raw = record.get("raw_result")
        if raw is None:
            return NO_RESULT
        if isinstance(raw, dict):
            if isinstance(raw.get("matches"), list):
                if not raw["matches"]:
                    return NO_MATCHES
                return format_matches(raw["matches"])
            if "value" in raw:
                return payload_text(raw["value"])
        return payload_text(raw)
    if "value" in record:
        if record["value"] is None:
            return NO_RESULT
        text = payload_text(record["value"])
        return text if text else "[this record holds no readable payload]"
    normalized = record.get("normalized_result")
    if isinstance(normalized, str):
        return normalized
    if normalized is not None:
        return json.dumps(normalized, indent=1)
    return "[this record holds no readable payload]"


def limitations_text(record):
    """Return the limitations the provider recorded for a record."""
    limitations = record.get("limitations")
    if not limitations:
        return []
    if isinstance(limitations, str):
        return [limitations]
    if isinstance(limitations, list):
        return [str(item) for item in limitations]
    return [json.dumps(limitations, indent=1)]


def load_export(path):
    """Return the export document."""
    doc = json.loads(Path(path).read_text())
    if isinstance(doc, list):
        return {"records": doc}
    if not isinstance(doc, dict):
        raise SystemExit("refused: the export is neither a list nor an object")
    return doc


def export_records(doc):
    """Return the records of an export, whatever the container is called."""
    for key in ("records", "evidence", "items"):
        value = doc.get(key)
        if isinstance(value, list):
            return value
    return []


def print_records(export_path, wanted, only=None, root=None, out=None):
    """Print the records that match an address."""
    out = out or sys.stdout
    root = root or resolve_root()
    path = resolve_export_path(export_path, root)
    if not path.is_file():
        raise SystemExit("refused: export not found: %s" % path)
    doc = load_export(path)
    subject = declared_subject(doc)
    declared_sha = subject_digest(subject) if subject else None
    if subject:
        state, message = verify_subject(subject, declared_sha, root)
        print("[export subject: %s - %s]" % (state, message), file=out)
    for record in export_records(doc):
        parameters = json.dumps(record.get("parameters") or {},
                                sort_keys=True).lower()
        if wanted not in parameters:
            continue
        operation = record.get("operation")
        short = SHORT.get(operation, operation) if operation else "record"
        if only and only != short:
            continue
        state, message = verify_subject(record.get("subject"), declared_sha,
                                        root)
        print("=" * 78, file=out)
        print("%s  %s  %s" % (short, record.get("evidence_id"), parameters),
              file=out)
        print("[subject: %s - %s]" % (state, message), file=out)
        if state == "mismatch":
            print("[refused: the subject of this record did not verify]",
                  file=out)
            continue
        for limitation in limitations_text(record):
            print("[limitation: %s]" % limitation, file=out)
        body = record_body(record)
        if only and only != "pseudo" and operation != "procedure_pseudo_code":
            print(body[:4000], file=out)
            continue
        print(body, file=out)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    wanted = sys.argv[1].lower()
    only = sys.argv[2].lower() if len(sys.argv) > 2 else None
    given = sys.argv[3] if len(sys.argv) > 3 else None
    print_records(given, wanted, only)
    return 0


if __name__ == "__main__":
    sys.exit(main())
