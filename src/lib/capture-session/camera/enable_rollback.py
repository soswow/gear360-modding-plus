#!/usr/bin/env python3
"""Enable the rollback unit and disable the app's boot enablement."""
import sys
sys.path.insert(0, '.')
import run_load_check as rlc

cam = rlc.HttpFtpCamera(host='192.168.43.1')

step = (
    "systemctl daemon-reload ; "
    "systemctl enable gear360-rollback.service 2>&1 ; echo ENABLE_RC=$? ; "
    "systemctl disable di-camera-app.service 2>&1 ; echo DISABLE_RC=$? ; "
    "ls /etc/systemd/system/default.target.wants/ 2>/dev/null ; "
    "ls /usr/lib/systemd/system/default.target.wants/ 2>/dev/null"
)
print(cam.execute(step, timeout=30.0))
