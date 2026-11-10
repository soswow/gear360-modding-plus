#!/usr/bin/env python3
"""Keep one REA MCP server (stdio) alive and serve it on a localhost TCP port.

For agent clients where the REA MCP server is not registered (for example a
Claude Code session): the skill allows manual stdio use of the pinned package.

    REA_BRIDGE=kernel rea_stdio_bridge.py serve      # run in the background
    REA_BRIDGE=kernel rea_stdio_bridge.py call tools/call \
        '{"name":"open_binary","arguments":{...}}' out.json

State (port file, server log, handshake) goes to REA_BRIDGE_DIR (default:
output/rea-bridge/ in the repository, ignored by git). Several named
bridges can run at once (one Ghidra target each). The environment matches
~/.codex/config.toml [mcp_servers.rea.env]; the kernel image needs
REA_GHIDRA_STARTUP_TIMEOUT_MS above 365 s of analysis (default here 40 min).
"""
import json
import os
import socket
import subprocess
import sys
import threading

HERE = os.environ.get("REA_BRIDGE_DIR") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "output", "rea-bridge")
os.makedirs(HERE, exist_ok=True)
NAME = os.environ.get("REA_BRIDGE", "main")
PORTFILE = os.path.join(HERE, f"rea-{NAME}.port")
def _find_pkg(version="6.7.0"):
    import glob, json as _j
    for pj in glob.glob(os.path.expanduser("~/.npm/_npx/*/node_modules/rea-agents/package.json")):
        try:
            if _j.load(open(pj)).get("version") == version:
                return os.path.join(os.path.dirname(pj), "scripts", "rea.mjs")
        except Exception:
            pass
    raise SystemExit(f"rea-agents {version} not in the npm cache; run: npx -y rea-agents@{version} doctor")


PKG = os.environ.get("REA_PKG") or _find_pkg()
JAVA_HOME = "/opt/homebrew/Cellar/openjdk@21/21.0.12.1/libexec/openjdk.jdk/Contents/Home"


def serve() -> None:
    env = dict(os.environ)
    env["GHIDRA_INSTALL_DIR"] = "/opt/homebrew/opt/ghidra/libexec"
    env.setdefault("REA_GHIDRA_STARTUP_TIMEOUT_MS", "2400000")
    env["JAVA_HOME"] = JAVA_HOME
    env["PATH"] = f"{JAVA_HOME}/bin:/opt/homebrew/opt/llvm@17/bin:/opt/homebrew/bin:" + env.get("PATH", "")
    err = open(os.path.join(HERE, f"rea-server-{NAME}.stderr.log"), "ab")
    proc = subprocess.Popen(["node", PKG, "mcp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=err, env=env, cwd=HERE)
    lock = threading.Lock()
    pending = {}
    next_id = [1]

    def reader() -> None:
        for raw in proc.stdout:
            try:
                msg = json.loads(raw)
            except Exception:
                continue
            if "id" in msg and msg["id"] in pending:
                slot = pending[msg["id"]]
                slot["msg"] = msg
                slot["ev"].set()
        for slot in list(pending.values()):
            slot["msg"] = {"error": {"message": "server exited"}}
            slot["ev"].set()

    threading.Thread(target=reader, daemon=True).start()

    def send(method: str, params: dict, notify: bool = False):
        with lock:
            if notify:
                proc.stdin.write((json.dumps({"jsonrpc": "2.0", "method": method, "params": params}) + "\n").encode())
                proc.stdin.flush()
                return None
            rid = next_id[0]
            next_id[0] += 1
            slot = {"ev": threading.Event(), "msg": None}
            pending[rid] = slot
            proc.stdin.write((json.dumps({"jsonrpc": "2.0", "id": rid, "method": method, "params": params}) + "\n").encode())
            proc.stdin.flush()
        slot["ev"].wait()
        pending.pop(rid, None)
        return slot["msg"]

    init = send("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                               "clientInfo": {"name": "claude-code-bridge", "version": "1"}})
    with open(os.path.join(HERE, f"rea-init-{NAME}.json"), "w") as f:
        json.dump(init, f, indent=1)
    send("notifications/initialized", {}, notify=True)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.bind(("127.0.0.1", 0))
    srv.listen(4)
    with open(PORTFILE, "w") as f:
        f.write(str(srv.getsockname()[1]))
    print("READY", flush=True)

    def handle(conn: socket.socket) -> None:
        data = b""
        while not data.endswith(b"\n"):
            chunk = conn.recv(65536)
            if not chunk:
                break
            data += chunk
        req = json.loads(data)
        resp = send(req["method"], req.get("params", {}))
        conn.sendall(json.dumps(resp).encode() + b"\n")
        conn.close()

    while proc.poll() is None:
        conn, _ = srv.accept()
        threading.Thread(target=handle, args=(conn,), daemon=True).start()


def call(method: str, params: str, outfile: str = "") -> None:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(("127.0.0.1", int(open(PORTFILE).read())))
    s.sendall(json.dumps({"method": method, "params": json.loads(params)}).encode() + b"\n")
    data = b""
    while True:
        chunk = s.recv(1 << 20)
        if not chunk:
            break
        data += chunk
    msg = json.loads(data)
    text = json.dumps(msg, indent=1)
    if outfile:
        with open(outfile, "w") as f:
            f.write(text)
    result = msg.get("result", {})
    if isinstance(result, dict) and "content" in result:
        for part in result["content"]:
            if part.get("type") == "text":
                print(part["text"][:int(os.environ.get("REA_PRINT", "6000"))])
        if result.get("isError"):
            print("[isError]")
    else:
        print(text[:int(os.environ.get("REA_PRINT", "6000"))])


if __name__ == "__main__":
    if sys.argv[1] == "serve":
        serve()
    else:
        call(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) > 4 else "")
