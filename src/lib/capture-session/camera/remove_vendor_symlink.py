#!/usr/bin/env python3
"""Remove the vendor's wants symlink so the app truly does not start at boot."""
import sys
sys.path.insert(0, '.')
import run_load_check as rlc

cam = rlc.HttpFtpCamera(host='192.168.43.1')

step = (
    "rm /usr/lib/systemd/system/default.target.wants/di-camera-app.service ; "
    "echo RM_RC=$? ; "
    "ls -la /usr/lib/systemd/system/default.target.wants/ | grep camera ; "
    "echo VENDOR-GONE ; "
    "echo APP-STATE=$(systemctl is-enabled di-camera-app.service 2>&1) ; "
    "echo DONE"
)
print(cam.execute(step, timeout=30.0))
