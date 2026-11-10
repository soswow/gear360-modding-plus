#!/usr/bin/env python3
"""Rewrite the rollback unit with the remount-rw inside the ExecStart."""
import sys
sys.path.insert(0, '.')
import run_load_check as rlc

cam = rlc.HttpFtpCamera(host='192.168.43.1')

UNIT = (
    "[Unit]\\n"
    "Description=Gear360 automatic app re-enable rollback\\n"
    "[Service]\\n"
    "Type=oneshot\\n"
    "TimeoutStartSec=900\\n"
    "ExecStart=/bin/sh -c \\\"sleep 600; "
    "mount -o remount,rw /; "
    "systemctl enable di-camera-app.service; "
    "systemctl start di-camera-app.service\\\"\\n"
    "[Install]\\n"
    "WantedBy=default.target\\n"
)

step = (
    "mount -o remount,rw / ; "
    "printf '" + UNIT + "' > /etc/systemd/system/gear360-rollback.service ; "
    "systemctl daemon-reload ; "
    "cat /etc/systemd/system/gear360-rollback.service ; "
    "echo VERIFY-OK"
)
print(cam.execute(step, timeout=30.0))
