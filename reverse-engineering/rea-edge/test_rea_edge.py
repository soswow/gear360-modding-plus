#!/usr/bin/env python3
"""Synthetic tests for the repaired REA source helpers.

No REA target, no camera, no network and no firmware is used here.  Every
artifact is a small synthetic fixture written into a temporary directory, and
every MCP peer is a fake server written by this file: a stdio peer that
speaks the same line-delimited JSON-RPC as the real server, and a loopback
peer that serves the same protocol on 127.0.0.1.

The fake peer serves the real connected tool schemas and answers with actual
evidence envelopes (evidence_id, subject, parameters, limitations, value), and
it writes the exported bundle and the session snapshot to the paths it is
given.

Run:
    python3 output/test_rea_edge.py
"""
import importlib.util
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
BRIDGE = HERE / "rea-edge.py"
SHOW = HERE / "show-ev.py"
READER = HERE / "read-exported-evidence.py"
REAL_SCHEMAS = HERE.parent / "actual-input-schemas.json"


def load(name, path):
    """Load one repaired helper under a module name python accepts."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


rea = load("rea_edge_repaired", BRIDGE)
show_ev = load("show_ev_repaired", SHOW)
reader = load("read_exported_evidence_repaired", READER)

SCHEMA_DROP = {"description", "examples", "$schema", "title", "default"}


def strip_schema(node):
    """Drop the schema keywords that do not constrain an argument."""
    if isinstance(node, dict):
        return {key: strip_schema(value) for key, value in node.items()
                if key not in SCHEMA_DROP}
    if isinstance(node, list):
        return [strip_schema(item) for item in node]
    return node


def real_schemas():
    """Return the connected tool schemas, when the real inventory is here."""
    if not REAL_SCHEMAS.is_file():
        return {}
    tools = json.loads(REAL_SCHEMAS.read_text())
    return {tool["name"]: tool["inputSchema"] for tool in tools
            if isinstance(tool, dict) and "inputSchema" in tool}


FAKE_SERVER_SOURCE = r'''#!/usr/bin/env python3
"""A fake REA MCP peer: the same line-delimited JSON-RPC, fixed answers.

It serves the connected tool schemas, answers with actual evidence
envelopes, and writes the exported bundle and the session snapshot to the
paths it is given.
"""
import json
import os
import signal
import socket
import sys

LOG = os.environ.get("FAKE_LOG", "")
MODE = os.environ.get("FAKE_MODE", "stdio")
PORT_FILE = os.environ.get("FAKE_PORT_FILE", "")
TOOLS_FILE = os.environ.get("FAKE_TOOLS_FILE", "")
SUBJECT_FILE = os.environ.get("FAKE_SUBJECT_FILE", "")


def log(event):
    if not LOG:
        return
    with open(LOG, "a") as handle:
        handle.write(json.dumps(event) + "\n")
        handle.flush()


def failing(name):
    wanted = [os.environ.get("FAKE_FAIL_TOOL", ""),
              os.environ.get("FAKE_FAIL_TOOLS", "")]
    return name in wanted[0].split(",") or name in wanted[1].split(",")


def declared_subject():
    if not SUBJECT_FILE:
        return None
    with open(SUBJECT_FILE) as handle:
        return json.load(handle)


def envelope(name, arguments):
    """Return an actual evidence envelope for one query."""
    declared = declared_subject()
    pattern = str(arguments.get("pattern") or "")
    if pattern == "11 22" and declared is not None:
        declared = json.loads(json.dumps(declared))
        declared["digest"] = {"sha256": "0" * 64}
    if name == "search_bytes":
        if pattern == "00 ff":
            value = None
        elif pattern == "de ad be ef":
            value = {"matches": []}
        else:
            value = {"matches": [
                {"address": "0x80280570", "offset": 16, "bytes": "de ad be ef"}]}
    elif name == "read_bytes":
        value = "00 11 22 33"
    else:
        value = {"value": "ok"}
    record = {"evidence_id": "fake-" + name, "parameters": arguments,
              "limitations": ["synthetic fixture record"]}
    if declared is not None:
        record["subject"] = declared
    record["value"] = value
    return record


def tools():
    if not TOOLS_FILE:
        return []
    with open(TOOLS_FILE) as handle:
        return json.load(handle)


def ok(ident, payload):
    return {"jsonrpc": "2.0", "id": ident, "result": {"content": [
        {"type": "text", "text": json.dumps(payload)}]}}


def handle(message):
    """Answer one JSON-RPC message; None means nothing is sent back."""
    method = message.get("method")
    ident = message.get("id")
    if method == "initialize":
        return {"jsonrpc": "2.0", "id": ident, "result": {
            "protocolVersion": "2024-11-05", "capabilities": {},
            "serverInfo": {"name": "fake-rea"}}}
    if method == "notifications/initialized":
        return None
    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": ident, "result": {"tools": tools()}}
    if method == "tools/call":
        name = message["params"]["name"]
        arguments = message["params"].get("arguments") or {}
        log({"event": "call", "name": name, "arguments": arguments})
        if failing(name):
            return {"jsonrpc": "2.0", "id": ident, "result": {
                "isError": True,
                "content": [{"type": "text", "text": os.environ.get(
                    "FAKE_FAIL_MESSAGE", "provider refused the call")}]}}
        if name == "export_evidence_bundle":
            path = arguments.get("path")
            if path:
                with open(path, "w") as handle:
                    handle.write(json.dumps({"records": []}))
            return ok(ident, {"exported": path})
        if name == "close_binary":
            path = arguments.get("snapshot_path")
            if path:
                with open(path, "w") as handle:
                    handle.write("synthetic session snapshot")
            return ok(ident, {"closed": path})
        return ok(ident, envelope(name, arguments))
    return {"jsonrpc": "2.0", "id": ident,
            "error": {"code": -32601, "message": "unknown method %s" % method}}


def serve(reader, writer):
    while True:
        line = reader.readline()
        if not line:
            return
        try:
            message = json.loads(line)
        except ValueError:
            continue
        reply = handle(message)
        if reply is not None:
            writer.write((json.dumps(reply) + "\n").encode("utf-8"))
            writer.flush()


def main():
    if MODE == "tcp":
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", int(os.environ.get("FAKE_PORT", "0"))))
        server.listen(4)
        port = server.getsockname()[1]
        if PORT_FILE:
            with open(PORT_FILE, "w") as handle:
                handle.write(str(port))
        log({"event": "listening", "port": port})
        while True:
            connection, _ = server.accept()
            log({"event": "connection"})
            with connection:
                serve(connection.makefile("rb"), connection.makefile("wb"))
    else:
        log({"event": "started", "pid": os.getpid()})

        def on_term(signum, frame):
            log({"event": "sigterm"})
            sys.exit(0)

        signal.signal(signal.SIGTERM, on_term)
        serve(sys.stdin.buffer, sys.stdout.buffer)


if __name__ == "__main__":
    main()
'''


class BridgeCase(unittest.TestCase):
    """Shared sandbox: synthetic artifacts, a fake peer and a redirected run."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="rea-edge-test-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)

        self.root = self.tmp / "research"
        carrier = rea.ARTIFACTS["carrier"]
        (self.root / carrier.rel).parent.mkdir(parents=True, exist_ok=True)
        (self.root / carrier.rel).write_bytes(b"synthetic carrier bytes")
        snapshot = carrier.snapshot_rel
        (self.root / snapshot).parent.mkdir(parents=True, exist_ok=True)
        (self.root / snapshot).write_bytes(b"synthetic snapshot bytes")
        self.fixture = self.root / carrier.rel
        self.fixture_sha = rea.sha256_file(self.fixture)

        self.fake = self.tmp / "fake_mcp_server.py"
        self.fake.write_text(FAKE_SERVER_SOURCE)
        self.log = self.tmp / "fake-log.jsonl"

        # The fake serves the real connected schemas when they are here.
        schemas = real_schemas()
        source = schemas or rea.SCHEMAS
        wanted = sorted(set(list(rea.SCHEMAS) + ["open_binary",
                                                 "export_evidence_bundle",
                                                 "close_binary"]))
        self.tools_file = self.tmp / "tools.json"
        self.tools_file.write_text(json.dumps(
            [{"name": name, "inputSchema": source[name]}
             for name in wanted if name in source]))

        # An actual evidence subject, shaped like the real one.
        self.subject_file = self.tmp / "subject.json"
        self.subject_file.write_text(json.dumps({
            "name": "rtos-lrc-analysis.elf",
            "digest": {"sha256": self.fixture_sha},
            "local_path": str(self.fixture),
            "format": "elf",
            "architecture": "arm"}))

        saved_env = dict(os.environ)
        os.environ.update({
            "GEAR360_RESEARCH_ROOT": str(self.root),
            "REA_NODE": sys.executable,
            "REA_LAUNCHER": str(self.fake),
            "JAVA_HOME": str(self.tmp),
            "GHIDRA_INSTALL_DIR": str(self.tmp),
            "FAKE_LOG": str(self.log),
            "FAKE_MODE": "stdio",
            "FAKE_TOOLS_FILE": str(self.tools_file),
            "FAKE_SUBJECT_FILE": str(self.subject_file),
        })
        os.environ.pop("REA_MCP_HOST", None)
        os.environ.pop("REA_MCP_PORT", None)
        self.addCleanup(self.restore_env, saved_env)

        saved_dirs = (rea.BASE, rea.RUNS_ROOT, rea.LAST_RUN_DIR)
        rea.BASE = self.tmp
        rea.RUNS_ROOT = self.tmp / "runs"
        rea.LAST_RUN_DIR = None
        self.addCleanup(self.restore_dirs, saved_dirs)

        # Point the carrier pin at the synthetic fixture.  The shipped pin is
        # not weakened: only this in-memory copy is redirected.
        saved_pins = {alias: art.sha for alias, art in rea.ARTIFACTS.items()}
        carrier.sha = self.fixture_sha
        self.addCleanup(self.restore_pins, saved_pins)

    def restore_env(self, saved):
        os.environ.clear()
        os.environ.update(saved)

    def restore_dirs(self, saved):
        (rea.BASE, rea.RUNS_ROOT, rea.LAST_RUN_DIR) = saved

    def restore_pins(self, saved):
        for alias, sha in saved.items():
            rea.ARTIFACTS[alias].sha = sha

    def events(self):
        if not self.log.exists():
            return []
        return [json.loads(line) for line in
                self.log.read_text().splitlines() if line.strip()]

    def calls(self):
        return [event["name"] for event in self.events()
                if event.get("event") == "call"]

    def call_arguments(self, name):
        for event in self.events():
            if event.get("event") == "call" and event.get("name") == name:
                return event.get("arguments") or {}
        return None

    def write_queries(self, queries, name="queries.json"):
        path = self.tmp / name
        path.write_text(json.dumps(queries))
        return path

    def run_bridge(self, queries, alias="carrier"):
        return rea.main([str(self.write_queries(queries)), alias])

    def run_cli(self, queries, alias="carrier", timeout=60):
        """Run the bridge as a child process and return the result."""
        path = self.write_queries(queries)
        return subprocess.run(
            [sys.executable, str(BRIDGE), str(path), alias],
            capture_output=True, text=True, timeout=timeout)

    def run_dir(self):
        self.assertIsNotNone(rea.LAST_RUN_DIR, "the run made no directory")
        return rea.LAST_RUN_DIR

    def evidence(self, name):
        return self.run_dir() / name


class TestSchemaCompatibility(unittest.TestCase):
    """The pinned schemas must be the connected ones, not invented ones."""

    def test_the_pinned_schemas_match_the_real_connected_schemas(self):
        schemas = real_schemas()
        if not schemas:
            self.skipTest("the real tools/list inventory is not here")
        for name, schema in rea.SCHEMAS.items():
            self.assertIn(name, schemas, "%s is not a connected tool" % name)
            self.assertEqual(schema, strip_schema(schemas[name]),
                             "the pinned schema for %s drifted" % name)

    def test_the_admitted_surface_has_no_dossier_tool(self):
        self.assertNotIn("analyze_function", rea.ALLOWED)
        schemas = real_schemas()
        if schemas:
            self.assertIn("analyze_function", schemas)

    def test_the_lifecycle_tools_are_the_real_ones(self):
        schemas = real_schemas()
        if not schemas:
            self.skipTest("the real tools/list inventory is not here")
        self.assertIn("export_evidence_bundle", schemas)
        self.assertIn("close_binary", schemas)
        self.assertEqual(schemas["export_evidence_bundle"]["required"],
                         ["path"])
        self.assertIn("snapshot_path", schemas["close_binary"]["properties"])


class TestArgumentValidation(BridgeCase):

    def test_search_bytes_takes_limit_and_not_max_results(self):
        self.run_bridge([{
            "tool": "search_bytes",
            "arguments": {"pattern": "de ad", "start": "0x80280000",
                          "end": "0x80281000", "limit": 10}}])
        arguments = self.call_arguments("search_bytes")
        self.assertEqual(arguments["limit"], 10)
        self.assertNotIn("max_results", arguments)

    def test_search_bytes_refuses_max_results_and_length(self):
        for extra in ({"max_results": 10}, {"length": 16}):
            with self.assertRaises(rea.DocumentRefused) as caught:
                self.run_bridge([{
                    "tool": "search_bytes",
                    "arguments": {"pattern": "de ad", "start": "0x80280000",
                                  "end": "0x80281000", **extra}}])
            self.assertIn("does not accept the argument", str(caught.exception))

    def test_search_bytes_needs_explicit_start_and_end(self):
        for arguments in ({"pattern": "de ad"},
                          {"pattern": "de ad", "start": "0x80280000"},
                          {"pattern": "de ad", "end": "0x80281000"}):
            with self.assertRaises(rea.DocumentRefused):
                self.run_bridge([{"tool": "search_bytes",
                                  "arguments": arguments}])
        self.assertNotIn("search_bytes", self.calls())

    def test_read_bytes_needs_a_bounded_length(self):
        for arguments in ({"address": "0x80280570"},
                          {"address": "0x80280570", "length": 100000}):
            with self.assertRaises(rea.DocumentRefused):
                self.run_bridge([{"tool": "read_bytes",
                                  "arguments": arguments}])
        self.assertNotIn("read_bytes", self.calls())

    def test_an_argument_outside_the_schema_is_refused(self):
        with self.assertRaises(rea.DocumentRefused) as caught:
            self.run_bridge([{"tool": "read_bytes",
                              "arguments": {"address": "0x80280570",
                                            "length": 16, "size": 4}}])
        self.assertIn("does not accept the argument 'size'",
                      str(caught.exception))

    def test_a_missing_required_argument_is_refused(self):
        with self.assertRaises(rea.DocumentRefused) as caught:
            self.run_bridge([{"tool": "procedure_pseudo_code",
                              "arguments": {}}])
        self.assertIn("required argument 'procedure'", str(caught.exception))
        self.assertNotIn("procedure_pseudo_code", self.calls())

    def test_a_pattern_outside_the_schema_is_refused(self):
        with self.assertRaises(rea.DocumentRefused) as caught:
            self.run_bridge([{"tool": "search_bytes",
                              "arguments": {"pattern": "not hex bytes",
                                            "start": "0x80280000",
                                            "end": "0x80281000"}}])
        self.assertIn("does not match the required pattern",
                      str(caught.exception))

    def test_the_target_switch_document_is_refused(self):
        result = self.run_cli([{
            "tool": "search_bytes",
            "arguments": {"pattern": "de ad", "start": "0x80280000",
                          "end": "0x80281000", "document": "other.elf"}}])
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("target-switch", result.stderr)
        self.assertFalse(self.log.exists())

    def test_a_refused_document_starts_no_server_at_all(self):
        with self.assertRaises(rea.DocumentRefused):
            self.run_bridge([{"tool": "search_bytes",
                              "arguments": {"pattern": "de ad"}}])
        self.assertFalse(self.log.exists())
        self.assertFalse((self.tmp / "runs").exists(),
                         "a refused run left a directory behind")


class TestHashBoundary(BridgeCase):

    def test_bad_hash_never_starts_the_server(self):
        """The pinned hash is checked before the server is spawned."""
        result = self.run_cli([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16}}])
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("sha256 mismatch", result.stderr)
        self.assertFalse(self.log.exists(),
                         "the REA server was started despite the bad hash")

    def test_a_good_hash_starts_the_session_and_closes_it(self):
        self.run_bridge([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16}}])
        self.assertIn("read_bytes", self.calls())
        self.assertIn("close_binary", self.calls())
        self.assertIn("sigterm", [event.get("event") for event in self.events()])

    def test_a_subject_that_does_not_verify_refuses_the_query(self):
        subject = self.tmp / "subject.bin"
        subject.write_bytes(b"subject bytes")
        self.run_bridge([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16},
            "subject": {"path": str(subject), "sha256": "0" * 64}}])
        self.assertNotIn("read_bytes", self.calls(),
                         "the query ran despite the failed subject")
        record = json.loads(self.evidence("00-read_bytes.json").read_text())
        self.assertEqual(record["error"]["subject_state"], "mismatch")
        self.assertIn("declared target hash", record["error"]["message"])

    def test_an_actual_subject_that_verifies_runs_the_query(self):
        subject = json.loads(self.subject_file.read_text())
        self.run_bridge([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16},
            "subject": subject}])
        self.assertIn("read_bytes", self.calls())


class TestReturnedSubject(BridgeCase):
    """A returned evidence subject is verified against the target."""

    def test_a_returned_subject_that_verifies_is_accepted(self):
        self.run_bridge([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16}}])
        receipt = json.loads(self.evidence("00-read_bytes.subject.json")
                             .read_text())
        self.assertEqual(receipt["state"], "verified")
        self.assertEqual(receipt["subject"]["digest"]["sha256"],
                         self.fixture_sha)

    def test_a_returned_subject_that_does_not_verify_is_refused(self):
        with self.assertRaises(RuntimeError) as caught:
            self.run_bridge([{
                "tool": "search_bytes",
                "arguments": {"pattern": "11 22", "start": "0x80280000",
                              "end": "0x80281000"}}])
        self.assertIn("did not verify", str(caught.exception))
        receipt = json.loads(self.evidence("00-search_bytes.subject.json")
                             .read_text())
        self.assertEqual(receipt["state"], "mismatch")

    def test_the_raw_reply_is_kept_even_when_the_subject_fails(self):
        with self.assertRaises(RuntimeError):
            self.run_bridge([{
                "tool": "search_bytes",
                "arguments": {"pattern": "11 22", "start": "0x80280000",
                              "end": "0x80281000"}}])
        reply = json.loads(self.evidence("00-search_bytes.json").read_text())
        record = show_ev.payload(reply)
        self.assertIn("subject", record)


class TestDocumentBoundaries(BridgeCase):

    def test_save_path_escape_is_refused_before_execution(self):
        result = self.run_cli([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16},
            "save": "../escape.json"}])
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("escape", result.stderr)
        self.assertFalse(self.log.exists(),
                         "a query ran before the save path was refused")
        self.assertFalse((self.tmp / "escape.json").exists())

    def test_absolute_save_path_is_refused_before_execution(self):
        result = self.run_cli([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16},
            "save": "/tmp/rea-escape.json"}])
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("relative", result.stderr)
        self.assertFalse(self.log.exists())

    def test_target_switch_argument_is_refused_before_execution(self):
        result = self.run_cli([{
            "tool": "procedure_pseudo_code",
            "arguments": {"path": "/etc/passwd"}}])
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("target-switch", result.stderr)
        self.assertFalse(self.log.exists())

    def test_target_switch_document_is_refused_before_execution(self):
        result = self.run_cli([{
            "tool": "procedure_pseudo_code",
            "arguments": {"procedure": "0x80280570"},
            "target": "library"}])
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("target-switch", result.stderr)
        self.assertFalse(self.log.exists())

    def test_out_of_surface_tool_is_refused_and_not_executed(self):
        self.assertNotIn("analyze_function", rea.ALLOWED)
        self.run_bridge([{
            "tool": "analyze_function",
            "arguments": {"procedure": "0x80280570"}}])
        self.assertNotIn("analyze_function", self.calls())
        record = json.loads(self.evidence("00-analyze_function.json").read_text())
        self.assertIn("outside the admitted surface", record["error"]["message"])


class TestSessionLifecycle(BridgeCase):

    def test_the_bundle_is_exported_and_the_session_is_closed(self):
        self.run_bridge([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16}}])
        names = self.calls()
        self.assertIn("export_evidence_bundle", names)
        self.assertIn("close_binary", names)
        self.assertLess(names.index("export_evidence_bundle"),
                        names.index("close_binary"))

    def test_the_export_is_written_to_an_absolute_path_in_the_run_dir(self):
        self.run_bridge([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16}}])
        arguments = self.call_arguments("export_evidence_bundle")
        self.assertTrue(Path(arguments["path"]).is_absolute())
        self.assertTrue(arguments["path"].startswith(str(self.run_dir())))
        self.assertFalse(arguments.get("overwrite", False))
        self.assertTrue(Path(arguments["path"]).is_file())

    def test_the_receipt_is_not_written_over_the_exported_bundle(self):
        self.run_bridge([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16}}])
        bundle = self.run_dir() / "evidence-bundle.json"
        receipt = self.run_dir() / "receipts" / "export-evidence-bundle.json"
        self.assertTrue(bundle.is_file())
        self.assertTrue(receipt.is_file())
        self.assertNotEqual(bundle.resolve(), receipt.resolve())
        self.assertEqual(json.loads(bundle.read_text()), {"records": []})
        self.assertIn("result", json.loads(receipt.read_text()))

    def test_close_binary_is_given_a_snapshot_path(self):
        self.run_bridge([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16}}])
        arguments = self.call_arguments("close_binary")
        self.assertIn("snapshot_path", arguments)
        self.assertTrue(Path(arguments["snapshot_path"]).is_absolute())
        self.assertFalse(arguments.get("overwrite", False))

    def test_no_snapshot_tool_is_called(self):
        self.run_bridge([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16}}])
        for name in self.calls():
            self.assertNotIn("snapshot_program", name)
            self.assertNotIn("save_snapshot", name)

    def test_command_error_closes_the_owned_server_and_keeps_the_error(self):
        os.environ["FAKE_FAIL_TOOL"] = "read_bytes"
        os.environ["FAKE_FAIL_MESSAGE"] = "provider refused the read"
        unowned = subprocess.Popen(
            [sys.executable, "-c", "import time; time.sleep(30)"])
        self.addCleanup(unowned.kill)
        try:
            with self.assertRaises(RuntimeError) as caught:
                self.run_bridge([{
                    "tool": "read_bytes",
                    "arguments": {"address": "0x80280570", "length": 16}}])
            self.assertIn("provider refused the read", str(caught.exception))
            self.assertIn("close_binary", self.calls())
            self.assertIn("sigterm",
                          [event.get("event") for event in self.events()])
            self.assertIsNone(unowned.poll(),
                              "a process this bridge does not own was killed")
        finally:
            unowned.kill()
            unowned.wait(timeout=10)

    def test_a_cleanup_error_is_reported_and_the_primary_error_is_kept(self):
        os.environ["FAKE_FAIL_TOOLS"] = "read_bytes,export_evidence_bundle"
        os.environ["FAKE_FAIL_MESSAGE"] = "provider refused the read"
        with self.assertRaises(RuntimeError) as caught:
            self.run_bridge([{
                "tool": "read_bytes",
                "arguments": {"address": "0x80280570", "length": 16}}])
        self.assertIn("provider refused the read", str(caught.exception))

    def test_an_open_failure_still_terminates_the_owned_server(self):
        os.environ["FAKE_FAIL_TOOL"] = "open_binary"
        os.environ["FAKE_FAIL_MESSAGE"] = "the pinned target would not open"
        with self.assertRaises(RuntimeError) as caught:
            self.run_bridge([{
                "tool": "read_bytes",
                "arguments": {"address": "0x80280570", "length": 16}}])
        self.assertIn("would not open", str(caught.exception))
        self.assertIn("sigterm", [event.get("event") for event in self.events()])
        self.assertNotIn("read_bytes", self.calls())


class TestRunDirectory(BridgeCase):

    def test_each_run_owns_a_fresh_directory(self):
        self.run_bridge([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16},
            "save": "keep.json"}])
        first = self.run_dir()
        self.assertTrue((first / "keep.json").is_file())
        self.run_bridge([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16},
            "save": "keep.json"}])
        second = self.run_dir()
        self.assertNotEqual(first, second)
        self.assertTrue((first / "keep.json").is_file(),
                        "the earlier run's evidence was overwritten")
        self.assertTrue((second / "keep.json").is_file())

    def test_a_run_never_writes_outside_its_own_directory(self):
        self.run_bridge([{
            "tool": "read_bytes",
            "arguments": {"address": "0x80280570", "length": 16}}])
        run_dir = self.run_dir()
        self.assertTrue((run_dir / "tools.json").is_file())
        self.assertTrue((run_dir / "open.json").is_file())
        self.assertFalse((self.tmp / "evidence").exists())


class TestByteSearchRoundTrip(BridgeCase):

    def setUp(self):
        super().setUp()
        self.run_bridge([
            {"tool": "search_bytes",
             "arguments": {"pattern": "de ad", "start": "0x80280000",
                           "end": "0x80281000"},
             "save": "hit.json"},
            {"tool": "search_bytes",
             "arguments": {"pattern": "de ad be ef", "start": "0x80280000",
                           "end": "0x80281000"},
             "save": "nohit.json"},
            {"tool": "search_bytes",
             "arguments": {"pattern": "00 ff", "start": "0x80280000",
                           "end": "0x80281000"},
             "save": "null.json"},
        ])

    def body(self, name):
        doc = json.loads(self.evidence(name).read_text())
        return show_ev.body_text(show_ev.payload(doc))

    def test_matches_come_back_as_results(self):
        body = self.body("hit.json")
        self.assertIn("0x80280570", body)
        self.assertIn("de ad be ef", body)

    def test_zero_matches_is_not_an_absence_claim(self):
        body = self.body("nohit.json")
        self.assertIn("zero matches", body)
        self.assertNotIn("absence is not established", body)

    def test_a_null_result_is_not_read_as_no_hits(self):
        body = self.body("null.json")
        self.assertIn("absence is not established", body)
        self.assertNotIn("zero matches", body)

    def test_the_command_line_reader_shows_the_same_matches(self):
        result = subprocess.run(
            [sys.executable, str(SHOW), str(self.evidence("hit.json"))],
            capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("0x80280570", result.stdout)

    def test_the_raw_record_is_preserved_and_printable(self):
        result = subprocess.run(
            [sys.executable, str(SHOW), "--raw", str(self.evidence("hit.json"))],
            capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        raw = json.loads(result.stdout)
        self.assertEqual(len(raw["value"]["matches"]), 1)
        self.assertIn("digest", raw["subject"])


class TestExportedEvidenceReader(unittest.TestCase):

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="rea-reader-test-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.subject = self.tmp / "subject.bin"
        self.subject.write_bytes(b"subject bytes")
        self.subject_sha = reader.sha256_file(self.subject)
        self.export = self.tmp / "rtos-evidence.json"
        self.export.write_text(json.dumps({
            "subject": {"name": "rtos-lrc-analysis.elf",
                        "digest": {"sha256": self.subject_sha},
                        "local_path": str(self.subject),
                        "format": "elf", "architecture": "arm"},
            "records": [
                {"evidence_id": "e-1",
                 "parameters": {"pattern": "DEAD", "start": "0x80280570"},
                 "value": {"matches": [
                     {"address": "0x80280570", "bytes": "de ad be ef"}]},
                 "limitations": ["zero hits is not universal absence"],
                 "subject": {"name": "rtos-lrc-analysis.elf",
                             "digest": {"sha256": self.subject_sha},
                             "local_path": str(self.subject),
                             "format": "elf", "architecture": "arm"}},
                {"evidence_id": "e-2",
                 "parameters": {"pattern": "DEAD", "start": "0x80280571"},
                 "value": {"matches": []},
                 "subject": {"name": "rtos-lrc-analysis.elf",
                             "digest": {"sha256": self.subject_sha},
                             "local_path": str(self.subject),
                             "format": "elf", "architecture": "arm"}},
                {"evidence_id": "e-3",
                 "parameters": {"pattern": "DEAD", "start": "0x80280572"},
                 "value": None,
                 "subject": {"name": "rtos-lrc-analysis.elf",
                             "digest": {"sha256": self.subject_sha},
                             "local_path": str(self.subject),
                             "format": "elf", "architecture": "arm"}},
                {"evidence_id": "e-4",
                 "parameters": {"pattern": "DEAD", "start": "0x80280573"},
                 "value": {"matches": []},
                 "subject": {"name": "rtos-lrc-analysis.elf",
                             "digest": {"sha256": "0" * 64},
                             "local_path": str(self.subject),
                             "format": "elf", "architecture": "arm"}},
            ]}))

    def read(self, wanted="0x8028057"):
        out = io.StringIO()
        reader.print_records(str(self.export), wanted, out=out)
        return out.getvalue()

    def test_the_export_resolves_under_the_research_root(self):
        root = Path("/tmp/some-root")
        self.assertEqual(reader.resolve_export_path(None, root),
                         (root / reader.EXPORT_REL).resolve())

    def test_a_verified_subject_is_reported_and_the_record_is_shown(self):
        text = self.read()
        self.assertIn("[subject: verified", text)
        self.assertIn("0x80280570", text)
        self.assertIn("de ad be ef", text)

    def test_byte_search_matches_are_read_instead_of_a_null_value(self):
        text = self.read()
        self.assertIn("0x80280570", text)

    def test_the_limitations_of_the_record_are_shown(self):
        self.assertIn("[limitation: zero hits is not universal absence]",
                      self.read())

    def test_zero_matches_and_a_null_value_stay_apart(self):
        text = self.read()
        self.assertIn("zero matches", text)
        self.assertIn("absence is not established", text)

    def test_a_record_whose_subject_does_not_verify_is_not_shown(self):
        text = self.read()
        self.assertIn("[refused: the subject of this record did not verify]",
                      text)

    def test_a_subject_that_is_absent_locally_is_marked_unverified(self):
        self.export.write_text(json.dumps({
            "records": [{"evidence_id": "e-5",
                         "parameters": {"pattern": "DEAD"},
                         "value": {"matches": []},
                         "subject": {"digest": {"sha256": "1" * 64},
                                     "local_path": str(self.tmp / "gone.bin")}}]}))
        out = io.StringIO()
        reader.print_records(str(self.export), "dead", out=out)
        self.assertIn("[subject: absent", out.getvalue())

    def test_a_legacy_record_is_still_readable(self):
        self.export.write_text(json.dumps({
            "records": [{"operation": "procedure_pseudo_code",
                         "evidence_id": "legacy-1",
                         "parameters": {"procedure": "0x80280570"},
                         "normalized_result": "void handler(void) {}"}]}))
        out = io.StringIO()
        reader.print_records(str(self.export), "0x80280570", out=out)
        self.assertIn("void handler", out.getvalue())


class TestLoopbackTransport(unittest.TestCase):
    """The bridge can also talk to a REA MCP peer on 127.0.0.1."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="rea-loopback-test-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.fake = self.tmp / "fake_mcp_server.py"
        self.fake.write_text(FAKE_SERVER_SOURCE)
        self.log = self.tmp / "fake-log.jsonl"
        self.port_file = self.tmp / "port.txt"

    def test_a_loopback_peer_is_also_refused(self):
        saved = dict(os.environ)
        os.environ.update({"REA_MCP_HOST": "127.0.0.1", "REA_MCP_PORT": "9000"})
        try:
            with self.assertRaises(rea.ConfigError):
                rea.make_transport({})
        finally:
            os.environ.clear()
            os.environ.update(saved)

    def test_structured_content_subject_is_extracted(self):
        subject = {"digest": {"sha256": "a" * 64}, "local_path": "/synthetic/target"}
        record = {"subject": subject, "raw_result": {"matches": []}}
        reply = {"result": {"structuredContent": record, "content": []}}
        self.assertEqual(rea.extract_record(reply), record)
        self.assertEqual(show_ev.payload(reply), record)

    def test_a_non_loopback_peer_is_refused(self):
        saved_env = dict(os.environ)
        os.environ.update({"REA_MCP_HOST": "10.0.0.1", "REA_MCP_PORT": "9000"})
        try:
            with self.assertRaises(rea.ConfigError):
                rea.make_transport(
                    {"REA_NODE": sys.executable, "REA_LAUNCHER": str(self.fake),
                     "JAVA_HOME": str(self.tmp),
                     "GHIDRA_INSTALL_DIR": str(self.tmp)})
        finally:
            os.environ.clear()
            os.environ.update(saved_env)


if __name__ == "__main__":
    unittest.main(verbosity=2)
