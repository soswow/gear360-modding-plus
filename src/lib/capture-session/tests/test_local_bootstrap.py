#!/usr/bin/env python3
"""Focused host-only CLI checks against synthetic shared libraries."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
cc = os.environ.get("CC", "cc")
with tempfile.TemporaryDirectory(prefix="capture-bootstrap-test-") as tmp:
    out = Path(tmp)
    inc = [f"-I{root}", f"-I{root / '../still-request'}", f"-I{root / '../encoded-set'}"]
    sources = [root / "capture-shot.c", root / "capture_session.c",
               root / "capture_session_library.c", root / "../still-request/still_request.c",
               root / "../encoded-set/encoded_set.c"]
    exe = out / "capture-shot"
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O1", *inc,
                    *(str(p) for p in sources), "-ldl", "-pthread", "-o", str(exe)], check=True)
    libs = {}
    for label, define in [("full", None), ("instance", "OMIT_INSTANCE"),
                          ("init", "OMIT_INIT"), ("beauty", "OMIT_BEAUTY")]:
        lib = out / (label + ".so")
        cmd = [cc, "-shared", "-fPIC", str(root / "tests/stub_local_bootstrap.c"), "-o", str(lib)]
        if define: cmd.insert(1, "-D" + define)
        subprocess.run(cmd, check=True)
        libs[label] = lib

    def run(args, lib="full", failure=None):
        trace = out / "trace"
        trace.unlink(missing_ok=True)
        env = dict(os.environ, BOOTSTRAP_TRACE=str(trace))
        if failure: env["BOOTSTRAP_FAIL"] = failure
        else: env.pop("BOOTSTRAP_FAIL", None)
        log = out / "log"
        log.unlink(missing_ok=True)
        p = subprocess.run([str(exe), *args, "--library", str(libs[lib]), "--log", str(log)],
                           env=env, capture_output=True, text=True)
        return p.returncode, trace.read_text().splitlines() if trace.exists() else [], log.read_text() if log.exists() else ""

    for args in (["--probe-local-bootstrap"], ["--load-only", "--probe-local-bootstrap"],
                 ["--probe", "--load-only", "--probe-local-bootstrap"]):
        rc, trace, log = run(args)
        assert rc == 2 and not trace and not log, (rc, trace, log)
    for missing in ("instance", "init", "beauty"):
        rc, trace, _ = run(["--probe", "--probe-local-bootstrap"], lib=missing)
        assert rc == 1 and trace == ["load"], (missing, rc, trace)
    for failure, expected in [("instance", ["load", "instance"]),
                              ("init", ["load", "instance", "init:correct"]),
                              ("beauty", ["load", "instance", "init:correct", "beauty"])]:
        rc, trace, log = run(["--probe", "--probe-local-bootstrap"], failure=failure)
        assert rc == 1 and trace == expected and "remote cleanup" in log, (failure, rc, trace, log)
        if failure == "init": assert "Initialize returned -7" in log
    rc, trace, log = run(["--probe", "--probe-local-bootstrap"])
    assert rc == 0 and trace == ["load", "instance", "init:correct", "beauty", "create", "register", "destroy"], (rc, trace)
    assert "readiness is unknown" in log and "Initialize returned 0" in log
    rc, trace, _ = run(["--probe"])
    assert rc == 0 and trace == ["load", "create", "register", "destroy"], (rc, trace)
    print("local bootstrap synthetic host checks passed")
