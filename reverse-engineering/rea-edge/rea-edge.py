#!/usr/bin/env python3
"""Bounded read-only REA/Ghidra bridge for the next-edge pass.

A process-local REA MCP server (Ghidra 12.1.3, JDK 21) is started, one pinned
artifact is opened against its pinned snapshot, a queue of read-only queries is
served, and the session is closed again.  Nothing here opens the camera, the
device or the network, and evidence is written into a fresh directory under the repository's
ignored output/rea-edge-runs/ directory.

Usage:
    rea-edge.py queries.json [carrier|library|app|agent]

Runtime (must already exist; this script never downloads or installs it):
    REA_NODE            node binary that runs the REA MCP launcher
    REA_LAUNCHER        path to the REA MCP launcher (rea.mjs)
    JAVA_HOME           JDK 21 home used for Ghidra
    GHIDRA_INSTALL_DIR  Ghidra installation directory

Artifacts:
    GEAR360_RESEARCH_ROOT   repository root holding the pinned artifacts.
                            Defaults to the repository root detected above
                            this script; otherwise the run is refused.

Only a server process spawned by this helper is admitted. Existing network
peers are refused because their active target is not owned by this run.
"""
import hashlib
import json
import os
import re
import secrets
import subprocess
import sys
import time
from pathlib import Path

BASE = Path(__file__).resolve().parent
RUNS_ROOT = BASE.parents[1] / "output" / "rea-edge-runs"
LAST_RUN_DIR = None

RESEARCH_ROOT_ENV = "GEAR360_RESEARCH_ROOT"
RUNTIME_VARS = ("REA_NODE", "REA_LAUNCHER", "JAVA_HOME", "GHIDRA_INSTALL_DIR")

# Bounds on the admitted surface.  A dossier tool and an unbounded raw byte
# read are both outside it.
MAX_READ_BYTES = 4096
MAX_SEARCH_LIMIT = 100


class Artifact:
    """One pinned target: relative path, pinned hash and address-space note."""

    def __init__(self, alias, rel, sha, snapshot_rel, note):
        self.alias = alias
        self.rel = rel
        self.sha = sha
        self.snapshot_rel = snapshot_rel
        self.note = note

    def path(self, root):
        return root / self.rel

    def snapshot_path(self, root):
        return (root / self.snapshot_rel) if self.snapshot_rel else None


# The pinned targets, their pinned hashes and their address-space notes.  The
# relative paths are resolved under GEAR360_RESEARCH_ROOT so that a worktree
# can point at the retained main artifacts.
ARTIFACTS = {
    "carrier": Artifact(
        "carrier",
        "output/decompile/rea/pass-minimax-followup-review-20261023/"
        "rtos-seeded-analysis.elf",
        "8321a21193466694eac6d41d8bd499c4ba3f4061bb780a26e7af885a8632ae45",
        "output/decompile/rea/pass-8315d77-review-20261023/rtos.snapshot",
        "code VA is the original offset plus 0x80000000"),
    "agent": Artifact(
        "agent",
        "dump/extracted/platform/usr/bin/mobileap-agent",
        "c0af5db6f4f26936d20d3625266e8ca3dd38d3db7f26b75de76904695834b17b",
        None,
        "Ghidra address is the ELF virtual address plus 0x10000"),
    "library": Artifact(
        "library",
        "dump/extracted/platform/usr/lib/libcapture-fw-prod.so",
        "fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7",
        None,
        "Ghidra address is the ELF virtual address plus 0x10000"),
    "app": Artifact(
        "app",
        "dump/extracted/platform/usr/apps/com.samsung.di-camera-app/bin/"
        "di-camera-app",
        "5937b3abe7630abc8ac265ce280cc45ee591562968296bc4b368cf7e15742789",
        None,
        "Ghidra address is the ELF virtual address; no shared-library bias"),
    # The inter-core bridge the camera's own `st` shell uses for capture
    # commands. mcb_set_ignore_init_checking calls SetDestinationState(true);
    # this does not prove a successful send or acknowledgement. Pinned as
    # linux-bridge-elf in facts/coordinates.yaml.
    "bridge": Artifact(
        "bridge",
        "dump/extracted/platform/usr/lib/libmulticore-bridge.so",
        "9129761c6ee9f6b62968d3a8307a8b5fbf02e1493323658c42d9ebe587673a20",
        None,
        "Ghidra address is the ELF virtual address plus 0x10000"),
    # The library that carries the camera shell's capture-command dispatcher
    # and the refusal text itself.
    "shell": Artifact(
        "shell",
        "dump/extracted/platform/usr/lib/libshell-command.so",
        "0fd079a9ed4cc8287be409dd3427ef9b28e0a3295eaedfddba548b0409d4a1a8",
        None,
        "Ghidra address is the ELF virtual address plus 0x10000"),
}

# The admitted read-only surface.  analyze_function is deliberately absent: a
# per-function dossier is not a bounded read.  The annotating tools are absent
# too: they edit the ephemeral database and no read-only work needs that.
#
# The second group carries typed provider facts the basic calls do not:
# procedure_info names a function and its body, procedure_references reports
# typed DATA/READ/WRITE edges, procedure_assembly gives resolved disassembly,
# and batch_decompile returns several procedures in one call.  All were
# exercised against a pinned library; see
# output/decompile/rea/pass-liveview-contract-20261030/REA-AUDIT.md.
ALLOWED = {
    "procedure_pseudo_code", "read_function_instructions",
    "procedure_callers", "procedure_callees", "xrefs", "read_bytes",
    "search_procedures", "search_strings", "search_bytes",
    "resolve_containing_procedure", "procedure_address",
    "inspect_native_instruction",
    "procedure_references", "procedure_info", "procedure_assembly",
    "batch_decompile", "resolve_native_call_targets",
    "inspect_native_data_type", "trace_native_values",
    "get_call_graph", "build_call_path", "compare_functions",
    "inspect_address_context", "list_procedures", "list_names",
}

# The input schemas of the connected tools, pinned from the real tools/list
# inventory.  Descriptions and examples are dropped; every keyword that
# constrains an argument is kept.  The live inventory is used as well when it
# carries schemas, so a server change is caught rather than guessed at.
SCHEMAS = json.loads(r'''
{
 "close_binary": {
  "additionalProperties": false,
  "properties": {
   "overwrite": {"type": "boolean"},
   "snapshot_path": {"minLength": 1, "pattern": "^[^\\x00]*$", "type": "string"}
  },
  "type": "object"
 },
 "export_evidence_bundle": {
  "additionalProperties": false,
  "properties": {
   "overwrite": {"type": "boolean"},
   "path": {"minLength": 1, "pattern": "^[^\\x00]*$", "type": "string"}
  },
  "required": ["path"],
  "type": "object"
 },
 "inspect_native_instruction": {
  "additionalProperties": false,
  "properties": {
   "address": {"minLength": 1, "type": "string"},
   "document": {"type": "string"}
  },
  "required": ["address"],
  "type": "object"
 },
 "open_binary": {
  "additionalProperties": false,
  "properties": {
   "architecture": {"enum": ["6502", "x86", "x86_64", "arm", "arm64", "m68k"], "type": "string"},
   "entry_point": {"pattern": "^0x[0-9A-Fa-f]{1,16}$", "type": "string"},
   "format": {"enum": ["dos-com", "raw-binary"], "type": "string"},
   "path": {"minLength": 1, "pattern": "^[^\\x00]*$", "type": "string"},
   "provider_id": {"anyOf": [
    {"const": "auto", "type": "string"},
    {"minLength": 1, "pattern": "^[a-z][a-z0-9\\x2d]*$", "type": "string"}
   ]},
   "snapshot_path": {"minLength": 1, "pattern": "^[^\\x00]*$", "type": "string"}
  },
  "required": ["path"],
  "type": "object"
 },
 "procedure_address": {
  "additionalProperties": false,
  "properties": {"document": {"type": "string"}, "procedure": {"type": "string"}},
  "required": ["procedure"],
  "type": "object"
 },
 "procedure_callees": {
  "additionalProperties": false,
  "properties": {"document": {"type": "string"}, "procedure": {"type": "string"}},
  "required": ["procedure"],
  "type": "object"
 },
 "procedure_callers": {
  "additionalProperties": false,
  "properties": {"document": {"type": "string"}, "procedure": {"type": "string"}},
  "required": ["procedure"],
  "type": "object"
 },
 "procedure_pseudo_code": {
  "additionalProperties": false,
  "properties": {"document": {"type": "string"}, "procedure": {"type": "string"}},
  "required": ["procedure"],
  "type": "object"
 },
 "read_bytes": {
  "additionalProperties": false,
  "properties": {
   "address": {"type": "string"},
   "document": {"type": "string"},
   "length": {"maximum": 9007199254740991, "minimum": 1, "type": "integer"}
  },
  "required": ["address"],
  "type": "object"
 },
 "read_function_instructions": {
  "additionalProperties": false,
  "properties": {"document": {"type": "string"}, "procedure": {"type": "string"}},
  "required": ["procedure"],
  "type": "object"
 },
 "resolve_containing_procedure": {
  "additionalProperties": false,
  "properties": {"address": {"type": "string"}, "document": {"type": "string"}},
  "required": ["address"],
  "type": "object"
 },
 "search_bytes": {
  "additionalProperties": false,
  "properties": {
   "cursor": {"minLength": 1, "type": "string"},
   "document": {"type": "string"},
   "end": {"type": "string"},
   "limit": {"maximum": 1000, "minimum": 1, "type": "integer"},
   "mask": {"pattern": "^[0-9a-fA-F]{2}(\\s*[0-9a-fA-F]{2})*$", "type": "string"},
   "pattern": {"pattern": "^([0-9a-fA-F]{2}|\\?\\?)(\\s*([0-9a-fA-F]{2}|\\?\\?))*$", "type": "string"},
   "start": {"type": "string"}
  },
  "required": ["pattern"],
  "type": "object"
 },
 "search_procedures": {
  "additionalProperties": false,
  "properties": {
   "case_sensitive": {"type": "boolean"},
   "document": {"type": "string"},
   "mode": {"enum": ["literal", "regex"], "type": "string"},
   "pattern": {"minLength": 1, "type": "string"}
  },
  "required": ["pattern"],
  "type": "object"
 },
 "search_strings": {
  "additionalProperties": false,
  "properties": {
   "case_sensitive": {"type": "boolean"},
   "document": {"type": "string"},
   "mode": {"enum": ["literal", "regex"], "type": "string"},
   "pattern": {"minLength": 1, "type": "string"}
  },
  "required": ["pattern"],
  "type": "object"
 },
 "xrefs": {
  "additionalProperties": false,
  "properties": {"address": {"type": "string"}, "document": {"type": "string"}},
  "type": "object"
 }
}
''')

# Keys that would select or open a different binary.  "document" is the
# provider's own target-switch argument, so it is refused even though the
# connected schema lists it.
TARGET_SWITCH_KEYS = {
    "path", "snapshot_path", "provider_id", "target", "target_alias",
    "artifact", "binary", "program", "project", "project_path",
    "open_binary", "close_binary", "document",
}
ALLOWED_QUERY_KEYS = {"tool", "arguments", "save", "subject"}


class ConfigError(Exception):
    """The run cannot start: missing runtime, missing artifact, bad hash."""


class DocumentRefused(Exception):
    """The query document breaks a boundary and is refused before execution."""


class SubjectError(Exception):
    """An evidence subject did not verify."""


class Plan:
    """One admitted query, validated and ready to run."""

    def __init__(self, index, name, arguments, save, save_path, subject):
        self.index = index
        self.name = name
        self.arguments = arguments
        self.save = save
        self.save_path = save_path
        self.subject = subject


class Refusal:
    """One query that is refused instead of executed."""

    def __init__(self, index, name, save, save_path, message):
        self.index = index
        self.name = name
        self.save = save
        self.save_path = save_path
        self.message = message


def sha256_file(path):
    """Return the hex sha256 of a file."""
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_research_root():
    """Return the repository root that holds the pinned artifacts."""
    configured = os.environ.get(RESEARCH_ROOT_ENV)
    if configured:
        return Path(configured).expanduser().resolve()
    for start in (BASE, Path.cwd()):
        for candidate in [start] + list(start.parents):
            if (candidate / "dump" / "extracted").is_dir() or \
                    (candidate / "output" / "decompile" / "rea").is_dir():
                return candidate.resolve()
    raise ConfigError(
        "%s is not set and no repository root was detected above %s or %s "
        "(looked for dump/extracted or output/decompile/rea); export %s "
        "=/path/to/gear360_modding" % (
            RESEARCH_ROOT_ENV, BASE, Path.cwd(), RESEARCH_ROOT_ENV))


def require_runtime():
    """Return the runtime settings, or refuse clearly when one is missing."""
    missing = [name for name in RUNTIME_VARS if not os.environ.get(name)]
    if missing:
        raise ConfigError(
            "missing runtime environment: %s. This bridge never downloads or "
            "installs a runtime; export %s to existing local installs." % (
                ", ".join(missing), ", ".join(RUNTIME_VARS)))
    runtime = {name: os.environ[name] for name in RUNTIME_VARS}
    for name in ("REA_NODE", "REA_LAUNCHER"):
        if not Path(runtime[name]).expanduser().is_file():
            raise ConfigError("%s does not point at a file: %s" % (
                name, runtime[name]))
    for name in ("JAVA_HOME", "GHIDRA_INSTALL_DIR"):
        if not Path(runtime[name]).expanduser().is_dir():
            raise ConfigError("%s does not point at a directory: %s" % (
                name, runtime[name]))
    return runtime


def verify_artifact(artifact, root, kind="target artifact"):
    """Verify a pinned artifact by hash and return its path."""
    path = artifact.path(root)
    if not path.is_file():
        raise ConfigError("%s not found: %s (root %s)" % (kind, path, root))
    found = sha256_file(path)
    if found != artifact.sha:
        raise ConfigError(
            "%s sha256 mismatch for %s: pinned %s, found %s; refusing to run"
            % (kind, path, artifact.sha, found))
    return path


def subject_digest(subject):
    """Return the sha256 a subject declares, for either envelope shape.

    The actual envelope carries digest.sha256; the flat legacy shape carries
    sha256 or sha directly.
    """
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
        return "not-declared", "no subject is declared"
    declared = subject_digest(subject)
    named = subject_path(subject)
    if not declared and not named:
        return "not-declared", "no subject is declared"
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
        return "absent", "the subject file is not present: %s" % path
    found = sha256_file(path)
    if found != declared:
        return "mismatch", ("the subject file %s hashes to %s, not the "
                            "declared %s" % (path, found, declared))
    return "verified", "the subject digest matches %s" % path


def value_problems(spec, value, tool, key):
    """Return the schema problems of one argument value."""
    problems = []
    if "anyOf" in spec:
        for option in spec["anyOf"]:
            if not value_problems(option, value, tool, key):
                return []
        return ["%s argument %r does not match any allowed form" % (tool, key)]
    kind = spec.get("type")
    if kind == "string" and not isinstance(value, str):
        return ["%s argument %r must be a string" % (tool, key)]
    if kind == "integer" and (not isinstance(value, int) or
                              isinstance(value, bool)):
        return ["%s argument %r must be an integer" % (tool, key)]
    if kind == "boolean" and not isinstance(value, bool):
        return ["%s argument %r must be a boolean" % (tool, key)]
    if kind == "object" and not isinstance(value, dict):
        return ["%s argument %r must be an object" % (tool, key)]
    if "const" in spec and value != spec["const"]:
        problems.append("%s argument %r must be %r" % (tool, key, spec["const"]))
    if "enum" in spec and value not in spec["enum"]:
        problems.append("%s argument %r must be one of %s" % (
            tool, key, ", ".join(str(item) for item in spec["enum"])))
    if isinstance(value, str):
        if "minLength" in spec and len(value) < spec["minLength"]:
            problems.append("%s argument %r must be at least %d characters" % (
                tool, key, spec["minLength"]))
        if "pattern" in spec and not re.search(spec["pattern"], value):
            problems.append("%s argument %r does not match the required "
                            "pattern" % (tool, key))
    if isinstance(value, int) and not isinstance(value, bool):
        if "minimum" in spec and value < spec["minimum"]:
            problems.append("%s argument %r must be >= %s" % (
                tool, key, spec["minimum"]))
        if "maximum" in spec and value > spec["maximum"]:
            problems.append("%s argument %r must be <= %s" % (
                tool, key, spec["maximum"]))
    return problems


def schema_problems(schema, arguments, tool):
    """Return the problems of arguments against a pinned input schema.

    A small hand-written check over the keywords the connected schemas use.
    No schema library is downloaded or imported.
    """
    problems = []
    if not isinstance(schema, dict):
        return problems
    properties = schema.get("properties") or {}
    for key in schema.get("required") or []:
        if key not in arguments:
            problems.append("%s needs the required argument %r" % (tool, key))
    if schema.get("additionalProperties") is False:
        for key in arguments:
            if key not in properties:
                problems.append("%s does not accept the argument %r" % (
                    tool, key))
    for key, value in arguments.items():
        spec = properties.get(key)
        if spec is None:
            continue
        problems.extend(value_problems(spec, value, tool, key))
    return problems


def policy_problems(name, arguments):
    """Return the problems of arguments under this script's own policy."""
    problems = []
    for key in arguments:
        if key in TARGET_SWITCH_KEYS:
            problems.append("%s carries the target-switch argument %r; refused"
                            % (name, key))
    if name == "search_bytes":
        if "start" not in arguments or "end" not in arguments:
            problems.append(
                "search_bytes needs an explicit start and an explicit end; "
                "refusing to run so that a null result is not read as 'no "
                "hits'")
        limit = arguments.get("limit")
        if limit is not None and (not isinstance(limit, int) or
                                  isinstance(limit, bool) or
                                  limit > MAX_SEARCH_LIMIT):
            problems.append("search_bytes limit must be 1..%d" %
                            MAX_SEARCH_LIMIT)
    elif name == "read_bytes":
        if "length" not in arguments:
            problems.append(
                "read_bytes needs an explicit length; an unbounded raw byte "
                "read is not admitted")
        elif (not isinstance(arguments["length"], int) or
              isinstance(arguments["length"], bool) or
              arguments["length"] < 1 or
              arguments["length"] > MAX_READ_BYTES):
            problems.append("read_bytes length must be 1..%d bytes" %
                            MAX_READ_BYTES)
    elif name in ("search_procedures", "search_strings"):
        pattern = arguments.get("pattern")
        if not isinstance(pattern, str) or not pattern.strip():
            problems.append("%s needs a non-empty pattern" % name)
    return problems


def validate_arguments(name, arguments, schemas=None):
    """Enforce the schema and the policy for one admitted tool."""
    if not isinstance(arguments, dict):
        raise DocumentRefused("arguments for %s must be an object" % name)
    schema = (schemas or {}).get(name) or SCHEMAS.get(name)
    problems = schema_problems(schema, arguments, name)
    problems.extend(policy_problems(name, arguments))
    if problems:
        raise DocumentRefused("%s: %s" % (name, "; ".join(problems)))


def resolve_save_path(save, run_dir):
    """Return the path a reply is written to, refusing anything that escapes."""
    if not isinstance(save, str) or not save.strip():
        raise DocumentRefused("save must be a non-empty relative path")
    candidate = Path(save)
    if candidate.is_absolute():
        raise DocumentRefused("save path must be relative: %r" % save)
    if ".." in candidate.parts:
        raise DocumentRefused(
            "save path must not escape the run directory: %r" % save)
    resolved_root = run_dir.resolve()
    resolved = (run_dir / candidate).resolve()
    if resolved == resolved_root or resolved_root in resolved.parents:
        return resolved
    raise DocumentRefused(
        "save path escapes the run directory: %r" % save)


def default_save_name(index, name):
    """Return a safe default reply name for a query."""
    safe = "".join(char if char.isalnum() or char in "._-" else "_"
                   for char in str(name))
    return "%02d-%s.json" % (index, safe)


def plan_run_dir():
    """Return a unique run directory path for this run.

    The directory is only created once the run is going to write, so a
    refused run leaves nothing behind.
    """
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    return RUNS_ROOT / ("%s-%d-%s" % (
        stamp, os.getpid(), secrets.token_hex(3)))


def create_run_dir():
    """Create the fresh, unique directory that this run owns.

    The directory is never reused, so retained evidence from an earlier run
    is never overwritten.
    """
    RUNS_ROOT.mkdir(parents=True, exist_ok=True)
    for _ in range(64):
        candidate = plan_run_dir()
        try:
            candidate.mkdir()
        except FileExistsError:
            continue
        except OSError as exc:
            raise ConfigError("could not create the run directory %s: %s" % (
                candidate, exc))
        return candidate
    raise ConfigError("could not create a unique run directory under %s" %
                      RUNS_ROOT)


def build_plan(queries, root, run_dir, schemas=None):
    """Validate the whole document before anything is executed."""
    if not isinstance(queries, list):
        raise DocumentRefused("queries file must hold a JSON list")
    plan = []
    for index, query in enumerate(queries):
        if not isinstance(query, dict):
            raise DocumentRefused("query %d is not an object" % index)
        for key in query:
            if key in TARGET_SWITCH_KEYS:
                raise DocumentRefused(
                    "query %d carries target-switch key %r; the document is "
                    "refused before execution" % (index, key))
            if key not in ALLOWED_QUERY_KEYS:
                raise DocumentRefused(
                    "query %d carries unknown key %r" % (index, key))
        name = query.get("tool")
        arguments = query.get("arguments") or {}
        save = query.get("save") or default_save_name(index, name)
        save_path = resolve_save_path(save, run_dir)
        if name not in ALLOWED:
            plan.append(Refusal(
                index, name, save, save_path,
                "operation outside the admitted surface: %s" % name))
            continue
        try:
            validate_arguments(name, arguments, schemas)
        except DocumentRefused as exc:
            raise DocumentRefused("query %d (%s): %s" % (index, name, exc))
        plan.append(Plan(index, name, arguments, save, save_path,
                         query.get("subject")))
    return plan


def write_json(path, payload):
    """Write one JSON document, creating the parent directory."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2))


def provider_error_text(reply):
    """Return the provider's own error text when the reply carries one."""
    if not isinstance(reply, dict):
        return ""
    error = reply.get("error")
    if error:
        if isinstance(error, dict):
            return str(error.get("message") or error)
        return str(error)
    payload = reply.get("result")
    if isinstance(payload, dict) and payload.get("isError"):
        texts = [item.get("text") for item in (payload.get("content") or [])
                 if isinstance(item, dict) and isinstance(item.get("text"), str)]
        return " ".join(texts) if texts else "provider reported an error"
    return ""


def extract_record(reply):
    """Return the provider's own record from a JSON-RPC reply."""
    if not isinstance(reply, dict):
        return None
    if "subject" in reply:
        return reply
    result = reply.get("result")
    if not isinstance(result, dict):
        return None
    if "subject" in result:
        return result
    structured = result.get("structuredContent")
    if isinstance(structured, dict):
        return structured
    content = result.get("content")
    if isinstance(content, list) and content:
        item = content[0]
        text = item.get("text") if isinstance(item, dict) else None
        if isinstance(text, str):
            try:
                parsed = json.loads(text)
            except json.JSONDecodeError:
                return None
            return parsed if isinstance(parsed, dict) else None
    return result


class StdioTransport:
    """Owns one spawned REA MCP server process and nothing else."""

    owns_process = True

    def __init__(self, command, env, log_path):
        self.log_handle = open(log_path, "w")
        self.process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=self.log_handle, text=True, env=env)

    def send(self, payload):
        self.process.stdin.write(json.dumps(payload) + "\n")
        self.process.stdin.flush()

    def receive(self):
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError("owned REA server exited")
        return json.loads(line)

    def close(self):
        """Terminate the owned process only.  No other process is touched."""
        try:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=10)
        finally:
            for handle in (self.process.stdin, self.process.stdout):
                try:
                    handle.close()
                except Exception:
                    pass
            try:
                self.log_handle.close()
            except Exception:
                pass


class Server:
    """A JSON-RPC peer for one REA MCP session."""

    def __init__(self, transport):
        self.transport = transport
        self.counter = 0

    def rpc(self, method, params):
        self.counter += 1
        self.transport.send({"jsonrpc": "2.0", "id": self.counter,
                             "method": method, "params": params})
        while True:
            message = self.transport.receive()
            if message.get("id") == self.counter:
                return message

    def call(self, name, arguments):
        return self.rpc("tools/call", {"name": name, "arguments": arguments})


def make_env(runtime):
    """Return the environment for the REA MCP server process."""
    env = dict(os.environ)
    env["GHIDRA_INSTALL_DIR"] = runtime["GHIDRA_INSTALL_DIR"]
    env["JAVA_HOME"] = runtime["JAVA_HOME"]
    env.setdefault("REA_GHIDRA_STARTUP_TIMEOUT_MS", "900000")
    env["PATH"] = os.pathsep.join([
        str(Path(runtime["JAVA_HOME"]).expanduser() / "bin"),
        str(Path(runtime["REA_NODE"]).expanduser().parent),
        env.get("PATH", "")])
    return env


def make_transport(runtime):
    """Return the transport for this run: loopback peer, or spawned server."""
    if os.environ.get("REA_MCP_HOST") or os.environ.get("REA_MCP_PORT"):
        raise ConfigError("existing MCP peers are not owned by this run; use the spawned stdio server")
    command = [runtime["REA_NODE"], runtime["REA_LAUNCHER"], "mcp"]
    return StdioTransport(command, make_env(runtime), (LAST_RUN_DIR or BASE) / "rea-server.log")


def run_queries(server, plan, root, declared_sha, live_schemas=None):
    """Serve the admitted queries and write each reply as evidence.

    A reply that carries its own evidence subject is verified against the
    declared target hash and the target file before it counts as evidence.
    """
    for item in plan:
        if isinstance(item, Refusal):
            write_json(item.save_path,
                       {"error": {"message": item.message, "refused": True}})
            print("refused", item.name, flush=True)
            continue
        if item.subject:
            state, detail = verify_subject(item.subject, declared_sha, root)
            if state in ("mismatch", "absent", "incomplete"):
                write_json(item.save_path,
                           {"error": {"message": detail, "refused": True,
                                      "subject_state": state}})
                print("refused subject", item.name, flush=True)
                continue
        try:
            validate_arguments(item.name, item.arguments, live_schemas)
        except DocumentRefused as exc:
            write_json(item.save_path,
                       {"error": {"message": str(exc), "refused": True}})
            print("refused", item.name, flush=True)
            continue
        reply = server.call(item.name, item.arguments)
        write_json(item.save_path, reply)
        print("saved", item.save_path.name, flush=True)
        record = extract_record(reply)
        subject = record.get("subject") if isinstance(record, dict) else None
        if subject is not None:
            state, detail = verify_subject(subject, declared_sha, root)
            write_json(item.save_path.with_suffix(".subject.json"),
                       {"state": state, "detail": detail, "subject": subject})
            if state in ("mismatch", "absent", "incomplete"):
                raise RuntimeError(
                    "%s returned an evidence subject that did not verify: %s"
                    % (item.name, detail))
        error = provider_error_text(reply)
        if error:
            raise RuntimeError("%s failed: %s" % (item.name, error))


def cleanup_session(server, opened, run_dir):
    """Export the evidence bundle, close the session, and report problems.

    The provider's reply to each lifecycle call is kept as a receipt in its
    own file.  The receipt is never written over the exported bundle, and the
    bundle is never written over a retained file: both destinations live in
    the fresh run directory and are written with overwrite off.
    """
    problems = []
    if server is None or not opened:
        return problems
    receipts = run_dir / "receipts"
    steps = (
        ("export_evidence_bundle",
         {"path": str(run_dir / "evidence-bundle.json"), "overwrite": False},
         "export-evidence-bundle.json"),
        ("close_binary",
         {"snapshot_path": str(run_dir / "session.snapshot"),
          "overwrite": False},
         "close-binary.json"),
    )
    for tool, arguments, receipt in steps:
        try:
            reply = server.call(tool, arguments)
        except Exception as exc:
            problems.append("%s: %s" % (tool, exc))
            continue
        write_json(receipts / receipt, reply)
        error = provider_error_text(reply)
        if error:
            problems.append("%s reported an error: %s" % (tool, error))
    return problems


def main(argv=None):
    """Run one bounded pass over a query document."""
    global LAST_RUN_DIR
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    queries_path = Path(argv[0]).expanduser()
    alias = argv[1] if len(argv) > 1 else "carrier"
    if alias not in ARTIFACTS:
        raise ConfigError("unknown target %r; known targets: %s" % (
            alias, ", ".join(sorted(ARTIFACTS))))
    runtime = require_runtime()
    root = resolve_research_root()
    artifact = ARTIFACTS[alias]
    print("target", alias, artifact.rel, artifact.sha, artifact.note, flush=True)

    if not queries_path.is_file():
        raise ConfigError("queries file not found: %s" % queries_path)
    try:
        queries = json.loads(queries_path.read_text())
    except json.JSONDecodeError as exc:
        raise ConfigError("queries file is not valid JSON: %s" % exc)

    # The whole document is validated first, so that a broken document is
    # refused before the artifact is even opened, and before anything is
    # written.
    plan = build_plan(queries, root, plan_run_dir())

    # The hash is checked before the server is started, so a wrong artifact
    # never reaches a REA session.
    target_path = verify_artifact(artifact, root)
    snapshot_path = artifact.snapshot_path(root)
    if snapshot_path is not None and not snapshot_path.is_file():
        raise ConfigError("pinned snapshot not found: %s" % snapshot_path)

    # Each run owns a fresh directory, so retained evidence is never
    # overwritten.
    run_dir = create_run_dir()
    LAST_RUN_DIR = run_dir
    print("run dir:", run_dir, flush=True)
    for item in plan:
        item.save_path = resolve_save_path(item.save, run_dir)

    transport = None
    server = None
    live_schemas = {}
    opened = False
    first_error = None
    try:
        transport = make_transport(runtime)
        server = Server(transport)
        server.rpc("initialize", {"protocolVersion": "2024-11-05",
                                   "capabilities": {},
                                   "clientInfo": {"name": "gear360-next-edge",
                                                  "version": "1"}})
        transport.send({"jsonrpc": "2.0",
                        "method": "notifications/initialized"})
        inventory = server.rpc("tools/list", {})
        tools = inventory.get("result", {}).get("tools", [])
        live_schemas = {tool["name"]: tool["inputSchema"] for tool in tools
                        if isinstance(tool, dict) and "inputSchema" in tool}
        write_json(run_dir / "tools.json", inventory)
        print("tools:", len(tools), flush=True)

        open_arguments = {"path": str(target_path), "provider_id": "ghidra"}
        if snapshot_path is not None:
            open_arguments["snapshot_path"] = str(snapshot_path)
        opened_reply = server.call("open_binary", open_arguments)
        write_json(run_dir / "open.json", opened_reply)
        error = provider_error_text(opened_reply)
        if error:
            raise RuntimeError("pinned target open failed: %s" % error)
        opened = True
        print("READY", flush=True)

        run_queries(server, plan, root, artifact.sha, live_schemas)
    except BaseException as exc:  # the first error is kept, not replaced
        first_error = exc
    finally:
        problems = cleanup_session(server, opened, run_dir)
        if transport is not None:
            try:
                transport.close()
            except Exception as exc:
                problems.append("terminate: %s" % exc)

    # Cleanup problems are reported whether or not the run itself failed, and
    # the first error is then raised unchanged.
    for problem in problems:
        print("cleanup warning:", problem, file=sys.stderr)
    if first_error is not None:
        raise first_error
    print("CLOSED", flush=True)
    return 0


def cli(argv=None):
    """Command line entry point."""
    try:
        return main(argv)
    except (ConfigError, DocumentRefused, SubjectError) as exc:
        print("refused: %s" % exc, file=sys.stderr)
        return 2
    except RuntimeError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(cli())
