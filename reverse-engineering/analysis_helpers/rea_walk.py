"""Walk raw instructions with REA inspect_native_instruction through a bridge.

For code Ghidra decoded but never grouped into a function (for example the
d5_ipcc ioctl handler reached only through a file_operations table).

    python3 rea_walk.py PORTFILE START_HEX END_HEX OUT.txt
"""
import json, socket, sys
port = int(open(sys.argv[1]).read())
start = int(sys.argv[2], 16); end = int(sys.argv[3], 16)
out = open(sys.argv[4], 'w')
def call(args):
    s = socket.socket(); s.connect(("127.0.0.1", port))
    s.sendall(json.dumps({"method": "tools/call", "params": {"name": "inspect_native_instruction", "arguments": args}}).encode() + b"\n")
    data = b""
    while True:
        c = s.recv(1 << 20)
        if not c: break
        data += c
    m = json.loads(data)
    return json.loads(m["result"]["content"][0]["text"])
a = start
while a < end:
    e = call({"address": hex(a)})
    r = e.get("raw_result", e)
    dis = r.get("raw_disassembly") or r.get("status")
    refs = [x.get("to") or x.get("address") for x in (r.get("outgoing_references") or r.get("references") or []) if isinstance(x, dict)]
    line = f"{a:08x}  {r.get('bytes','')}  {dis}" + (f"   -> {refs}" if refs else "")
    out.write(line + "\n"); out.flush()
    a += r.get("length") or 4
