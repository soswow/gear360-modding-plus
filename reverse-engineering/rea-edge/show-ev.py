#!/usr/bin/env python3
"""Print the readable payload of one REA evidence record.

Usage: show-ev.py <evidence.json> [grep-string]
       show-ev.py --raw <evidence.json>

The REA bridge writes each reply as a JSON-RPC envelope whose result holds a
content list with one text item; that item is itself JSON with the provider's
own record.  This prints the provider's payload: the pseudo-code or byte
listing, the caller list, or - for a byte search - the matches the provider
recorded.

Two record shapes are read.  The compact parent evidence envelope carries

    evidence_id, subject, parameters, limitations, value

with the subject holding local_path and digest.sha256.  The full REA Evidence record
carries raw_result and normalized_result.  Both are supported.

Two absences are kept apart on purpose:

  * matches = []       the provider searched a range and found nothing;
  * a null raw_result  no result was recorded at all, so the value may or
                       may not be there.  Absence is not established.

The evidence file is never rewritten: --raw prints the original record as the
provider wrote it.
"""
import json
import sys

NO_RESULT = ("[no raw_result recorded in this evidence file; the searched "
             "value may or may not be present, absence is not established]")
NO_MATCHES = ("[the byte search returned zero matches over the searched range]")


def payload(doc):
    """Return the provider's record dict from a REA reply envelope."""
    if "result" in doc:
        doc = doc["result"]
    if isinstance(doc.get("structuredContent"), dict):
        return doc["structuredContent"]
    content = doc.get("content")
    if isinstance(content, list) and content:
        text = content[0].get("text", "")
        try:
            doc = json.loads(text)
        except json.JSONDecodeError:
            return {"raw_result": text}
    return doc


def format_bytes(value):
    """Return one byte string in a readable hex form."""
    if isinstance(value, str):
        return value
    if isinstance(value, (bytes, bytearray)):
        return " ".join("%02x" % byte for byte in value)
    if isinstance(value, list):
        return " ".join("%02x" % byte for byte in value
                        if isinstance(byte, int) and 0 <= byte <= 255)
    return str(value)


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
                    data = format_bytes(match[key])
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


def body_text(doc):
    """Return the record's own payload.

    The provider puts the answer in raw_result.value: a string for pseudo-code
    and byte listings, a list of addresses for a caller query, and a matches
    list for a byte search.  An actual envelope puts it in value.  An error
    text elsewhere in the record can be longer than the payload, so the
    payload is taken by position rather than by size.
    """
    if not isinstance(doc, dict):
        return str(doc)
    if "raw_result" in doc:
        raw = doc["raw_result"]
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
    if "value" in doc:
        if doc["value"] is None:
            return NO_RESULT
        text = payload_text(doc["value"])
        return text if text else "[this record holds no readable payload]"
    if "normalized_result" in doc:
        return payload_text(doc["normalized_result"])
    return biggest_text(doc)


def limitations_text(doc):
    """Return the limitations the provider recorded for a record."""
    if not isinstance(doc, dict):
        return []
    limitations = doc.get("limitations")
    if not limitations:
        return []
    if isinstance(limitations, str):
        return [limitations]
    if isinstance(limitations, list):
        return [str(item) for item in limitations]
    return [json.dumps(limitations, indent=1)]


def subject_text(doc):
    """Return one line describing the subject of a record, if it has one."""
    if not isinstance(doc, dict):
        return ""
    subject = doc.get("subject")
    if not isinstance(subject, dict):
        return ""
    digest = subject.get("digest")
    sha = digest.get("sha256") if isinstance(digest, dict) else \
        subject.get("sha256")
    name = subject.get("name") or subject.get("local_path") or ""
    if sha and name:
        return "[subject: %s sha256 %s]" % (name, sha)
    if name:
        return "[subject: %s]" % name
    return ""


def biggest_text(doc):
    """Return the largest string in the record, which carries the body."""
    best = ""
    stack = [doc]
    while stack:
        node = stack.pop()
        if isinstance(node, dict):
            stack.extend(node.values())
        elif isinstance(node, list):
            stack.extend(node)
        elif isinstance(node, str) and len(node) > len(best):
            best = node
    return best


def main():
    argv = sys.argv[1:]
    raw_mode = False
    if argv and argv[0] == "--raw":
        raw_mode = True
        argv = argv[1:]
    if not argv:
        print(__doc__)
        return 2
    with open(argv[0]) as handle:
        doc = json.load(handle)
    record = payload(doc)
    if raw_mode:
        print(json.dumps(record, indent=2))
        return 0
    body = body_text(record)
    subject = subject_text(record)
    if subject:
        print(subject)
    for limitation in limitations_text(record):
        print("[limitation: %s]" % limitation)
    if len(argv) > 1:
        needle = argv[1]
        lines = body.splitlines()
        for number, line in enumerate(lines, 1):
            if needle.lower() in line.lower():
                start = max(0, number - 6)
                end = min(len(lines), number + 25)
                print(f"--- lines {start + 1}..{end} ---")
                for index in range(start, end):
                    mark = ">" if index + 1 == number else " "
                    print(f"{mark} {index + 1:4d} {lines[index]}")
    else:
        print(body)
    return 0


if __name__ == "__main__":
    sys.exit(main())
