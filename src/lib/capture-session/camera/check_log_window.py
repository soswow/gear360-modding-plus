#!/usr/bin/env python3
"""Check /dev/mem read errors explicitly."""
import sys
sys.path.insert(0, '.')
import run_load_check as rlc

cam = rlc.HttpFtpCamera(host='192.168.43.1')

step = (
    "id ; "
    "dd if=/dev/mem bs=4 count=4 skip=$((0x85401268 / 4)) 2>&1 | od -A x -t x4 ; "
    "echo RC=$?"
)
print(cam.execute(step, timeout=20.0))
